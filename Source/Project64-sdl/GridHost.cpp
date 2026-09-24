// Project64 - A Nintendo 64 emulator
// Grid orchestrator: one child process per ROM, laid out near-square, with a control
// strip that owns the keyboard. See Docs/superpowers/specs/2026-09-14-multi-rom-grid-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "GridHost.h"
#include "ExecutablePath.h"
#include <Common/GridKeys.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <SDL3/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>

static const int MAX_TILES = 16;
static const int STRIP_HEIGHT = 28;
static const int MIN_TILE_W = 320;
static const int MIN_TILE_H = 240;

static volatile sig_atomic_t g_StopRequested = 0;

static void HandleStopSignal(int /*sig*/)
{
    g_StopRequested = 1;
}

// Drops one reaped pid so later kill/wait loops never touch a pid the OS may reuse.
static void RemoveChild(std::vector<pid_t> & Children, pid_t Child)
{
    for (size_t i = 0; i < Children.size(); i++)
    {
        if (Children[i] == Child)
        {
            Children[i] = Children.back();
            Children.pop_back();
            return;
        }
    }
}

int GridHostRun(int argc, char ** argv)
{
    const int RomCount = argc - 2;
    char ** Roms = argv + 2;
    if (RomCount < 1 || RomCount > MAX_TILES)
    {
        fprintf(stderr, "usage: %s --grid rom1 .. rom%d (got %d)\n", argv[0], MAX_TILES, RomCount);
        return 2;
    }
    for (int i = 0; i < RomCount; i++)
    {
        if (access(Roms[i], R_OK) != 0)
        {
            fprintf(stderr, "cannot read %s\n", Roms[i]);
            return 2;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Rect Area;
    if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &Area))
    {
        fprintf(stderr, "SDL_GetDisplayUsableBounds failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Near-square grid. Integer sqrt so no floating point is involved.
    int Cols = 1;
    while (Cols * Cols < RomCount)
    {
        Cols += 1;
    }
    const int Rows = (RomCount + Cols - 1) / Cols;
    const int CellW = Area.w / Cols;
    const int CellH = (Area.h - STRIP_HEIGHT) / Rows;
    int TileH = CellH < CellW * 3 / 4 ? CellH : CellW * 3 / 4;
    if (TileH < MIN_TILE_H)
    {
        TileH = MIN_TILE_H;
    }
    int TileW = TileH * 4 / 3;
    if (TileW < MIN_TILE_W)
    {
        TileW = MIN_TILE_W;
    }
    const int GridW = Cols * TileW;
    const int GridH = Rows * TileH;
    const int OriginX = Area.x + (Area.w - GridW) / 2;
    const int OriginY = Area.y + (Area.h - STRIP_HEIGHT - GridH) / 2;

    char Title[64];
    snprintf(Title, sizeof(Title), "Project64 grid - %d game%s - Esc to quit", RomCount,
        RomCount == 1 ? "" : "s");
    SDL_Window * Strip = SDL_CreateWindow(Title, Area.w, STRIP_HEIGHT, SDL_WINDOW_ALWAYS_ON_TOP);
    if (Strip == nullptr)
    {
        fprintf(stderr, "SDL_CreateWindow (strip) failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowPosition(Strip, Area.x, Area.y + Area.h - STRIP_HEIGHT);
    SDL_RaiseWindow(Strip);

    const std::string Exe = ExecutablePath();
    if (Exe.empty())
    {
        fprintf(stderr, "could not resolve the executable path\n");
        SDL_DestroyWindow(Strip);
        SDL_Quit();
        return 1;
    }

    // One mapped snapshot, one writer (this process), many readers (the tiles). The name is
    // unlinked at once: children inherit the descriptor, never the name, so a crash here
    // leaves nothing behind.
    char ShmName[64];
    snprintf(ShmName, sizeof(ShmName), "/pj64grid-%d", (int)getpid());
    const int KeyFd = shm_open(ShmName, O_CREAT | O_RDWR, 0600);
    GridKeys * Keys = nullptr;
    if (KeyFd < 0 || ftruncate(KeyFd, (off_t)sizeof(GridKeys)) != 0)
    {
        fprintf(stderr, "could not create the key snapshot\n");
        shm_unlink(ShmName);
        close(KeyFd);
        SDL_DestroyWindow(Strip);
        SDL_Quit();
        return 1;
    }
    void * MappedKeys = mmap(nullptr, sizeof(GridKeys), PROT_READ | PROT_WRITE, MAP_SHARED, KeyFd, 0);
    if (MappedKeys == MAP_FAILED)
    {
        fprintf(stderr, "could not map the key snapshot\n");
        shm_unlink(ShmName);
        close(KeyFd);
        SDL_DestroyWindow(Strip);
        SDL_Quit();
        return 1;
    }
    Keys = (GridKeys *)MappedKeys;
    memset(Keys, 0, sizeof(GridKeys));
    shm_unlink(ShmName);
    fcntl(KeyFd, F_SETFD, 0); // shm_open sets FD_CLOEXEC; the children need it open

    const bool Selftest = getenv("PJ64_GRID_SELFTEST") != nullptr;
    bool Pattern[SDL_SCANCODE_COUNT];
    memset(Pattern, 0, sizeof(Pattern));
    Pattern[SDL_SCANCODE_X] = true;
    Pattern[SDL_SCANCODE_RETURN] = true;
    // In self-test mode, publish before any tile starts so a tile can never read the
    // zero-filled mapping first and report a false negative. A normal run starts with
    // nothing pressed, and the mapping is already zero.
    if (Selftest)
    {
        GridKeysPublish(Keys, Pattern);
    }

    std::vector<pid_t> Children;
    for (int i = 0; i < RomCount; i++)
    {
        const int Row = i / Cols;
        const int Col = i % Cols;
        const int X = OriginX + Col * TileW + (CellW - TileW) / 2;
        const int Y = OriginY + Row * TileH + (CellH - TileH) / 2;
        const pid_t Pid = fork();
        if (Pid == 0)
        {
            char Rect[64];
            snprintf(Rect, sizeof(Rect), "%d,%d,%d,%d", X, Y, TileW, TileH);
            setenv("PJ64_AUDIO_MUTE", "1", 1);
            char FdText[16];
            snprintf(FdText, sizeof(FdText), "%d", KeyFd);
            setenv(PJ64_GRID_KEYS_ENV, FdText, 1);
            char * ChildArgv[] = {
                const_cast<char *>(Exe.c_str()),
                const_cast<char *>("--tile"),
                Roms[i],
                const_cast<char *>("--tile-rect"),
                Rect,
                nullptr,
            };
            execv(Exe.c_str(), ChildArgv);
            _exit(127);
        }
        if (Pid < 0)
        {
            fprintf(stderr, "fork failed for %s\n", Roms[i]);
            continue;
        }
        Children.push_back(Pid);
    }
    if (Children.empty())
    {
        fprintf(stderr, "no tiles started\n");
        SDL_DestroyWindow(Strip);
        SDL_Quit();
        return 1;
    }

    signal(SIGINT, HandleStopSignal);
    signal(SIGTERM, HandleStopSignal);

    int Status = 0;
    pid_t Done = 0;
    const Uint64 LoopStart = SDL_GetTicks();
    while (!g_StopRequested)
    {
        SDL_Event Ev;
        while (SDL_PollEvent(&Ev))
        {
            if (Ev.type == SDL_EVENT_QUIT || Ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                g_StopRequested = 1;
            }
            if (Ev.type == SDL_EVENT_KEY_DOWN && Ev.key.key == SDLK_ESCAPE)
            {
                g_StopRequested = 1;
            }
        }
        while ((Done = waitpid(-1, &Status, WNOHANG)) > 0)
        {
            fprintf(stderr, "tile pid %d exited (status %d)\n", (int)Done, Status);
            RemoveChild(Children, Done);
        }
        // If the app was not frontmost at launch, macOS may never give the strip keyboard
        // focus, so every tile sees an all-zero key state and ignores the keyboard. Say so
        // once, after SDL has had time to deliver any focus it was going to grant.
        static bool Warned = false;
        if (!Warned && SDL_GetTicks() - LoopStart >= 1000)
        {
            Warned = true;
            if (SDL_GetKeyboardFocus() != Strip)
            {
                fprintf(stderr, "grid has no keyboard focus; click the control strip to give it focus\n");
            }
        }
        const bool * State = SDL_GetKeyboardState(nullptr);
        if (Selftest)
        {
            GridKeysPublish(Keys, Pattern);
        }
        else if (State != nullptr)
        {
            GridKeysPublish(Keys, State);
        }
        SDL_Delay(10);
    }

    for (size_t i = 0; i < Children.size(); i++)
    {
        kill(Children[i], SIGTERM);
    }
    for (int Pass = 0; Pass < 10 && !Children.empty(); Pass++)
    {
        while ((Done = waitpid(-1, nullptr, WNOHANG)) > 0)
        {
            RemoveChild(Children, Done);
        }
        if (Children.empty())
        {
            break;
        }
        SDL_Delay(100);
    }
    for (size_t i = 0; i < Children.size(); i++)
    {
        kill(Children[i], SIGKILL);
    }
    for (size_t i = 0; i < Children.size(); i++)
    {
        waitpid(Children[i], nullptr, 0);
    }

    munmap(Keys, sizeof(GridKeys));
    close(KeyFd);

    SDL_DestroyWindow(Strip);
    SDL_Quit();
    return 0;
}
