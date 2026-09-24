// Project64 - A Nintendo 64 emulator
// The launcher: one window listing a ROM folder's games. A click starts one as a child
// Project64, the window hides until it ends, then comes back. Pointer and left button only:
// there is no keyboard handling, and the cursor is never captured or moved.
// Design: Docs/superpowers/specs/2026-09-24-launcher-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "LauncherModel.h"
#include "Screens.h"

#include <SDL3/SDL.h>

#include <limits.h>
#include <mach-o/dyld.h>
#include <mutex>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char ** environ;

namespace
{
std::string ExecutablePath()
{
    char Buf[PATH_MAX];
    uint32_t Size = sizeof(Buf);
    if (_NSGetExecutablePath(Buf, &Size) != 0) return "";
    char Resolved[PATH_MAX];
    return realpath(Buf, Resolved) != nullptr ? std::string(Resolved) : std::string();
}

// $PJ64_LAUNCHER_HOME/launcher.yaml, else ~/Library/Application Support/Project64/launcher.yaml.
std::string SettingsPath()
{
    const std::string Home = LauncherHomeSettingsPath();
    if (!Home.empty()) return Home;
    char * Pref = SDL_GetPrefPath("", "Project64");   // ends in a slash; creates the folder
    if (Pref == nullptr) return "";
    const std::string Path = std::string(Pref) + "launcher.yaml";
    SDL_free(Pref);
    return Path;
}

// The folder picker's answer. SDL may call back on another thread, so the loop takes it
// from here under the lock.
struct FolderPick
{
    enum class Phase { Idle, Waiting, Answered };
    std::mutex Lock;
    Phase State = Phase::Idle;
    std::string Path;   // once Answered; empty: cancelled or failed
};

void SDLCALL OnFolderPicked(void * User, const char * const * Files, int)
{
    FolderPick * Pick = (FolderPick *)User;
    std::lock_guard<std::mutex> Guard(Pick->Lock);
    Pick->State = FolderPick::Phase::Answered;
    Pick->Path = (Files != nullptr && Files[0] != nullptr) ? Files[0] : "";
    if (Files == nullptr) fprintf(stderr, "launcher: folder picker failed: %s\n", SDL_GetError());
}

struct Launcher
{
    SDL_Window * Window = nullptr;
    std::string EmulatorDir;
    std::string SettingsFile;
    LauncherSettings Settings;
    LauncherState State;
    std::string Status;           // the last game's error, or ""
    pid_t Child = 0;              // the running game, or 0
    LauncherGame ChildGame;
    Uint64 KillAt = 0;            // PJ64_LAUNCHER_SELFTEST: when to stop the game
    int GamesEnded = 0;
    FolderPick Pick;
};

void Save(Launcher & L)
{
    if (!L.SettingsFile.empty() && !LauncherSaveSettings(L.SettingsFile.c_str(), L.Settings))
    {
        fprintf(stderr, "launcher: cannot write %s\n", L.SettingsFile.c_str());
    }
}

void RebuildRecent(Launcher & L)
{
    L.State.Recent.clear();
    for (const std::string & P : L.Settings.Recent) L.State.Recent.push_back(LauncherGameFor(P, L.EmulatorDir.c_str()));
}

void Rescan(Launcher & L)
{
    L.State.Games.clear();
    const char * Folder = L.Settings.Folder.c_str();
    if (Folder[0] != '\0')
    {
        if (LauncherScan(Folder, L.EmulatorDir.c_str(), &L.State.Games))
        {
            fprintf(stderr, "launcher: folder %s (%d games)\n", Folder, (int)L.State.Games.size());
        }
        else
        {
            fprintf(stderr, "launcher: cannot read folder %s\n", Folder);
        }
    }
    if (L.State.View != LAUNCHER_VIEW_RECENT && L.State.View >= LauncherPageCount((int)L.State.Games.size()))
    {
        L.State.View = 0;
    }
}

void OpenPicker(Launcher & L)
{
    {
        std::lock_guard<std::mutex> Guard(L.Pick.Lock);
        if (L.Pick.State != FolderPick::Phase::Idle) return;
        L.Pick.State = FolderPick::Phase::Waiting;
    }
    const char * Start = L.Settings.Folder.empty() ? nullptr : L.Settings.Folder.c_str();
    SDL_ShowOpenFolderDialog(OnFolderPicked, &L.Pick, L.Window, Start, false);
}

// Applies the picker's answer, if one came. True when it changed the folder.
bool TakePick(Launcher & L)
{
    std::string Path;
    {
        std::lock_guard<std::mutex> Guard(L.Pick.Lock);
        if (L.Pick.State != FolderPick::Phase::Answered) return false;
        L.Pick.State = FolderPick::Phase::Idle;
        Path = L.Pick.Path;
    }
    if (Path.empty()) return false;   // cancelled: the current folder stays
    L.Settings.Folder = Path;
    Save(L);
    L.State.View = 0;
    Rescan(L);
    return true;
}

void StartGame(Launcher & L, const LauncherGame & Game)
{
    const LauncherEnv Child = LauncherChildEnv(environ, L.EmulatorDir, Game, L.Settings.Face);
    std::vector<char *> Envp;
    for (const std::string & E : Child.Vars) Envp.push_back(const_cast<char *>(E.c_str()));
    Envp.push_back(nullptr);
    const std::string Exe = L.EmulatorDir + "/Project64";
    char * Argv[] = { const_cast<char *>(Exe.c_str()), const_cast<char *>(Game.Path.c_str()), nullptr };

    // posix_spawn, not fork then setenv: SDL runs threads by now, and only async-signal-safe
    // calls are allowed between fork and exec in a threaded process.
    posix_spawn_file_actions_t Actions;
    posix_spawn_file_actions_init(&Actions);
    // The _np name exists in every SDK since macOS 10.15; its replacement,
    // posix_spawn_file_actions_addchdir, exists only from macOS 26, whose SDK marks the _np
    // name deprecated. Keeping the _np call lets the launcher build against either SDK, so the
    // deprecation (which fires because the Makefile sets no -mmacosx-version-min and the
    // deployment target is the host's) is silenced here rather than the call changed.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    posix_spawn_file_actions_addchdir_np(&Actions, L.EmulatorDir.c_str());
#pragma clang diagnostic pop
    pid_t Pid = 0;
    const int Err = posix_spawn(&Pid, Exe.c_str(), &Actions, nullptr, Argv, Envp.data());
    posix_spawn_file_actions_destroy(&Actions);
    if (Err != 0)
    {
        L.Status = Game.Title + " could not start: " + strerror(Err);
        fprintf(stderr, "launcher: cannot start %s: %s\n", Exe.c_str(), strerror(Err));
        return;
    }
    // A PJ64_INPUT_YAML LauncherChildEnv kept from the launcher's own environment wins for
    // every game, so say that instead of "the generic layout", whether or not this particular
    // ROM has its own layout.
    if (Child.InheritedLayout)
    {
        fprintf(stderr, "launcher: started %s with $PJ64_INPUT_YAML\n", Game.Path.c_str());
    }
    else
    {
        fprintf(stderr, "launcher: started %s%s\n", Game.Path.c_str(), Game.Generic ? " with the generic layout" : "");
    }
    L.Child = Pid;
    L.ChildGame = Game;
    L.Status.clear();
    const char * Selftest = getenv("PJ64_LAUNCHER_SELFTEST");
    const int Seconds = Selftest != nullptr ? atoi(Selftest) : 0;
    L.KillAt = Seconds > 0 ? SDL_GetTicks() + (Uint64)Seconds * 1000 : 0;
    SDL_HideWindow(L.Window);
}

// Reaps the game once it ends and brings the window back. True when it did.
bool PollChild(Launcher & L)
{
    if (L.Child == 0) return false;
    if (L.KillAt != 0 && SDL_GetTicks() >= L.KillAt)
    {
        kill(L.Child, SIGTERM);
        L.KillAt = 0;
    }
    int Status = 0;
    const pid_t Done = waitpid(L.Child, &Status, WNOHANG);
    if (Done == 0) return false;
    L.Child = 0;
    L.GamesEnded++;
    char How[32];
    if (Done < 0) snprintf(How, sizeof(How), "exit -1");
    else if (WIFSIGNALED(Status)) snprintf(How, sizeof(How), "signal %d", WTERMSIG(Status));
    else snprintf(How, sizeof(How), "exit %d", WEXITSTATUS(Status));
    fprintf(stderr, "launcher: game ended (%s)\n", How);
    const bool Clean = Done > 0 && WIFEXITED(Status) && WEXITSTATUS(Status) == 0;
    L.Status = Clean ? std::string() : L.ChildGame.Title + " ended with an error (" + How + ")";
    LauncherPushRecent(&L.Settings.Recent, L.ChildGame.Path);
    RebuildRecent(L);
    Save(L);
    SDL_ShowWindow(L.Window);
    SDL_RaiseWindow(L.Window);
    fprintf(stderr, "launcher: window back\n");
    return true;
}

// Runs what a released click on T does. False: quit.
bool ActOnClick(Launcher & L, LauncherTarget T)
{
    const LauncherGame * Game = nullptr;
    switch (LauncherAct(&L.State, T, &Game))
    {
    case LauncherCommand::ToggleFace:
        L.Settings.Face = !L.Settings.Face;
        Save(L);
        break;
    case LauncherCommand::PickFolder: OpenPicker(L); break;
    case LauncherCommand::Quit: return false;
    case LauncherCommand::Start:
        if (Game != nullptr)
        {
            const LauncherGame Copy = *Game;   // State may change while the game runs
            StartGame(L, Copy);
        }
        break;
    case LauncherCommand::None: break;
    }
    return true;
}

// A left click on the centre of T, pushed as real SDL events so it takes a player's path.
void PushClick(LauncherTarget T)
{
    const LauncherRect R = LauncherTargetRect(T);
    SDL_Event E;
    SDL_zero(E);
    E.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    E.button.button = SDL_BUTTON_LEFT;
    E.button.down = true;
    E.button.x = R.X + R.W / 2;
    E.button.y = R.Y + R.H / 2;
    SDL_PushEvent(&E);
    E.type = SDL_EVENT_MOUSE_BUTTON_UP;
    E.button.down = false;
    SDL_PushEvent(&E);
}

// --selftest (Scripts/launcher_selftest.sh): click the first row, wait for the window to
// come back, click the second, wait again, then check the recent games. Returns -1 while
// running, else the exit code.
int SelftestStep(Launcher & L, int * Step)
{
    if (L.Child != 0) return -1;
    if (*Step == 0)
    {
        if (L.State.Games.size() < 2)
        {
            fprintf(stderr, "launcher: selftest failed: the folder needs two games, has %d\n", (int)L.State.Games.size());
            return 1;
        }
        PushClick(LauncherTarget{ LauncherTargetKind::Row, 0 });
        *Step = 1;
    }
    else if (*Step == 1 && L.GamesEnded == 1)
    {
        PushClick(LauncherTarget{ LauncherTargetKind::Row, 1 });
        *Step = 2;
    }
    else if (*Step == 2 && L.GamesEnded == 2)
    {
        const std::vector<std::string> & R = L.Settings.Recent;
        if (R.size() >= 2 && R[0] == L.State.Games[1].Path && R[1] == L.State.Games[0].Path)
        {
            fprintf(stderr, "launcher: selftest ok\n");
            return 0;
        }
        fprintf(stderr, "launcher: selftest failed: the recent games are not the two played, newest first\n");
        return 1;
    }
    return -1;
}
}

int main(int argc, char ** argv)
{
    if (argc >= 2 && strcmp(argv[1], "--version") == 0)
    {
        printf("Project64 launcher\n");
        return 0;
    }
    const bool Selftest = argc >= 2 && strcmp(argv[1], "--selftest") == 0;

    // The click that brings the window to the front counts: a player with one button should
    // not have to click twice.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    Launcher L;
    L.State.EmulatorFound = LauncherFindEmulator(ExecutablePath().c_str(), &L.EmulatorDir);
    if (L.State.EmulatorFound) fprintf(stderr, "launcher: emulator %s\n", L.EmulatorDir.c_str());
    else fprintf(stderr, "launcher: Project64 not found beside the app\n");
    L.SettingsFile = SettingsPath();
    if (LauncherLoadSettings(L.SettingsFile.c_str(), &L.Settings) == LauncherLoad::Malformed)
    {
        fprintf(stderr, "launcher: settings unreadable, using defaults: %s\n", L.SettingsFile.c_str());
    }
    RebuildRecent(L);
    Rescan(L);
    L.State.View = LauncherInitialView(L.State);

    // Fixed size: every rectangle in LauncherModel is laid out for 800x640.
    L.Window = SDL_CreateWindow("Project64", LAUNCHER_WIDTH, LAUNCHER_HEIGHT, 0);
    SDL_Renderer * Renderer = L.Window != nullptr ? SDL_CreateRenderer(L.Window, nullptr) : nullptr;
    if (Renderer == nullptr)
    {
        fprintf(stderr, "launcher: cannot open a window: %s\n", SDL_GetError());
        if (L.Window != nullptr) SDL_DestroyWindow(L.Window);
        SDL_Quit();
        return 1;
    }

    if (L.State.EmulatorFound && !Selftest && L.Settings.Folder.empty()) OpenPicker(L);

    LauncherTarget Hover, Pressed;
    int Step = 0;
    int ExitCode = 0;
    bool Running = true;
    // The screen changes only on an event, a folder choice or a game's end; an idle launcher
    // does not redraw.
    bool Redraw = true;
    while (Running)
    {
        SDL_Event E;
        while (SDL_PollEvent(&E))
        {
            Redraw = true;
            if (E.type == SDL_EVENT_QUIT || E.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                Running = false;
            }
            // No input while a game runs: the window is hidden, and one game at a time.
            if (L.Child != 0) continue;
            if (E.type == SDL_EVENT_MOUSE_MOTION)
            {
                Hover = LauncherHit(L.State, E.motion.x, E.motion.y);
            }
            else if (E.type == SDL_EVENT_MOUSE_BUTTON_DOWN && E.button.button == SDL_BUTTON_LEFT)
            {
                Pressed = LauncherHit(L.State, E.button.x, E.button.y);
            }
            else if (E.type == SDL_EVENT_MOUSE_BUTTON_UP && E.button.button == SDL_BUTTON_LEFT)
            {
                const LauncherTarget Released = LauncherHit(L.State, E.button.x, E.button.y);
                if (Released.Kind != LauncherTargetKind::None && Released == Pressed && !ActOnClick(L, Released))
                {
                    Running = false;
                }
                Pressed = LauncherTarget();
            }
        }
        if (TakePick(L)) Redraw = true;
        if (PollChild(L)) Redraw = true;
        if (Selftest && Running)
        {
            const int Result = SelftestStep(L, &Step);
            if (Result >= 0)
            {
                ExitCode = Result;
                Running = false;
            }
        }
        if (L.Child == 0 && Redraw)
        {
            Redraw = false;
            std::string Status = L.Status;
            if (Status.empty() && !L.Settings.Face) Status = "Face is off: gesture controls do nothing";
            const LauncherLabels Labels = { L.Settings.Face, L.Settings.Folder.c_str(), Status.c_str() };
            LauncherDraw(Renderer, L.State, Hover, Labels);
            SDL_RenderPresent(Renderer);
        }
        SDL_Delay(L.Child != 0 ? 50 : 16);
    }

    // Quit while a game runs (Cmd-Q from the Dock): stop the game first, but do not wait on
    // it forever — up to 5 seconds for SIGTERM to work, then SIGKILL.
    if (L.Child != 0)
    {
        kill(L.Child, SIGTERM);
        bool Exited = false;
        for (int i = 0; i < 100; i++)
        {
            if (waitpid(L.Child, nullptr, WNOHANG) != 0) { Exited = true; break; }
            SDL_Delay(50);
        }
        if (!Exited)
        {
            kill(L.Child, SIGKILL);
            waitpid(L.Child, nullptr, 0);
            fprintf(stderr, "launcher: game did not stop, killed it\n");
        }
    }
    SDL_DestroyRenderer(Renderer);
    SDL_DestroyWindow(L.Window);
    SDL_Quit();
    return ExitCode;
}
