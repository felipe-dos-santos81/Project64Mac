// Project64 - A Nintendo 64 emulator
// See LauncherModel.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "LauncherModel.h"
#include <Project64-sdl/GameConfig.h>

#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

static const char * BaseName(const char * Path)
{
    const char * Slash = strrchr(Path, '/');
    return Slash != nullptr ? Slash + 1 : Path;
}

bool LauncherIsRom(const char * FileName)
{
    if (FileName[0] == '.') return false;
    const char * Dot = strrchr(FileName, '.');
    if (Dot == nullptr || Dot == FileName) return false;
    return strcasecmp(Dot, ".z64") == 0 || strcasecmp(Dot, ".n64") == 0 || strcasecmp(Dot, ".v64") == 0;
}

std::string LauncherTitle(const char * FileName)
{
    std::string Title(FileName);
    const size_t Dot = Title.rfind('.');
    if (Dot != std::string::npos && Dot > 0) Title.erase(Dot);
    bool WordStart = true;
    for (char & C : Title)
    {
        if (C == '_') C = ' ';
        if (C == ' ')
        {
            WordStart = true;
            continue;
        }
        if (WordStart) C = (char)toupper((unsigned char)C);
        WordStart = false;
    }
    return Title;
}

LauncherGame LauncherGameFor(const std::string & Path, const char * EmulatorDir)
{
    LauncherGame G;
    G.Path = Path;
    G.Title = LauncherTitle(BaseName(Path.c_str()));
    char Layout[PATH_MAX];
    G.Generic = !GameConfigPath(Path.c_str(), EmulatorDir, Layout, sizeof(Layout));
    return G;
}

void LauncherSort(std::vector<LauncherGame> * Games)
{
    std::sort(Games->begin(), Games->end(), [](const LauncherGame & A, const LauncherGame & B) {
        const int C = strcasecmp(A.Title.c_str(), B.Title.c_str());
        return C != 0 ? C < 0 : A.Path < B.Path;
    });
}

bool LauncherScan(const char * Folder, const char * EmulatorDir, std::vector<LauncherGame> * Out)
{
    Out->clear();
    DIR * Dir = opendir(Folder);
    if (Dir == nullptr) return false;
    while (const struct dirent * Entry = readdir(Dir))
    {
        if (!LauncherIsRom(Entry->d_name)) continue;
        const std::string Path = std::string(Folder) + "/" + Entry->d_name;
        struct stat St;
        if (stat(Path.c_str(), &St) != 0 || !S_ISREG(St.st_mode)) continue;   // follows links; skips folders
        Out->push_back(LauncherGameFor(Path, EmulatorDir));
    }
    closedir(Dir);
    LauncherSort(Out);
    return true;
}

int LauncherPageCount(int GameCount)
{
    return GameCount <= 0 ? 1 : (GameCount + LAUNCHER_ROWS - 1) / LAUNCHER_ROWS;
}

int LauncherLetterPage(const std::vector<LauncherGame> & Games, int Letter)
{
    for (size_t i = 0; i < Games.size(); i++)
    {
        if (Games[i].Title.empty()) continue;
        const int C = toupper((unsigned char)Games[i].Title[0]);
        if (C == 'A' + Letter || (Letter == 0 && !isalpha(C))) return (int)i / LAUNCHER_ROWS;
    }
    return -1;
}

void LauncherPushRecent(std::vector<std::string> * Recent, const std::string & Path)
{
    Recent->erase(std::remove(Recent->begin(), Recent->end(), Path), Recent->end());
    Recent->insert(Recent->begin(), Path);
    if (Recent->size() > LAUNCHER_RECENT_MAX) Recent->resize(LAUNCHER_RECENT_MAX);
}

static bool HoldsEmulator(const std::string & Dir)
{
    const std::string Exe = Dir + "/Project64";
    struct stat St;
    return stat(Exe.c_str(), &St) == 0 && S_ISREG(St.st_mode) && access(Exe.c_str(), X_OK) == 0;
}

bool LauncherFindEmulator(const char * LauncherPath, std::string * Dir)
{
    const std::string Path(LauncherPath);
    const size_t Slash = Path.rfind('/');
    const std::string Own = Slash == std::string::npos ? "." : Path.substr(0, Slash);
    const std::string Candidates[] = { Own, Own + "/../../.." };
    for (const std::string & C : Candidates)
    {
        char Resolved[PATH_MAX];
        if (HoldsEmulator(C) && realpath(C.c_str(), Resolved) != nullptr)
        {
            *Dir = Resolved;
            return true;
        }
    }
    return false;
}

static bool HasPrefix(const char * S, const char * Prefix)
{
    return strncmp(S, Prefix, strlen(Prefix)) == 0;
}

LauncherEnv LauncherChildEnv(const char * const * Environ, const std::string & EmulatorDir,
                             const LauncherGame & Game, bool FaceOn)
{
    static const char kLayout[] = "PJ64_INPUT_YAML=";
    LauncherEnv Out;
    for (const char * const * E = Environ; E != nullptr && *E != nullptr; E++)
    {
        if (HasPrefix(*E, "PJ64_MENU_AUTO=") || HasPrefix(*E, "PJ64_FACE=")) continue;
        if (HasPrefix(*E, kLayout))
        {
            if ((*E)[sizeof(kLayout) - 1] == '\0') continue;
            Out.InheritedLayout = true;
        }
        Out.Vars.push_back(*E);
    }
    if (Game.Generic && !Out.InheritedLayout) Out.Vars.push_back(std::string(kLayout) + EmulatorDir + "/Config/mouse/default.yaml");
    Out.Vars.push_back("PJ64_MENU_AUTO=1");
    if (!FaceOn) Out.Vars.push_back("PJ64_FACE=0");
    return Out;
}

std::string LauncherHomeSettingsPath()
{
    const char * Home = getenv("PJ64_LAUNCHER_HOME");
    return (Home != nullptr && Home[0] != '\0') ? std::string(Home) + "/launcher.yaml" : std::string();
}

LauncherLoad LauncherLoadSettings(const char * Path, LauncherSettings * Out)
{
    *Out = LauncherSettings();
    if (access(Path, F_OK) != 0) return LauncherLoad::Missing;
    LauncherSettings S;
    try
    {
        const YAML::Node Root = YAML::LoadFile(Path);
        if (!Root.IsMap()) return LauncherLoad::Malformed;
        if (Root["folder"]) S.Folder = Root["folder"].as<std::string>();
        if (Root["face"])
        {
            const YAML::Node Face = Root["face"];
            if (!Face.IsScalar()) return LauncherLoad::Malformed;
            const std::string Text = Face.as<std::string>();
            if (Text == "true") S.Face = true;
            else if (Text == "false") S.Face = false;
            else return LauncherLoad::Malformed;
        }
        const YAML::Node Recent = Root["recent"];
        if (Recent)
        {
            if (!Recent.IsSequence()) return LauncherLoad::Malformed;
            for (const YAML::Node & Item : Recent)
            {
                const std::string P = Item.as<std::string>();
                if (access(P.c_str(), F_OK) == 0 && S.Recent.size() < LAUNCHER_RECENT_MAX) S.Recent.push_back(P);
            }
        }
    }
    catch (const YAML::Exception &)
    {
        return LauncherLoad::Malformed;
    }
    *Out = S;
    return LauncherLoad::Ok;
}

bool LauncherSaveSettings(const char * Path, const LauncherSettings & In)
{
    YAML::Emitter E;
    E << YAML::BeginMap;
    E << YAML::Key << "folder" << YAML::Value << In.Folder;
    E << YAML::Key << "face" << YAML::Value << In.Face;
    E << YAML::Key << "recent" << YAML::Value << YAML::BeginSeq;
    for (const std::string & P : In.Recent) E << P;
    E << YAML::EndSeq << YAML::EndMap;

    const std::string Temp = std::string(Path) + ".tmp";
    FILE * F = fopen(Temp.c_str(), "w");
    if (F == nullptr) return false;
    const bool Wrote = fprintf(F, "%s\n", E.c_str()) > 0;
    const bool Closed = fclose(F) == 0;
    if (!Wrote || !Closed || rename(Temp.c_str(), Path) != 0)
    {
        unlink(Temp.c_str());
        return false;
    }
    return true;
}

// The screen, in window points (800x640, fixed). Two letter rows of fourteen 50-point cells
// on a 54-point pitch: Recent spans the first two cells of the top row, A-L the other twelve,
// M-Z the bottom row. Ten 40-point game rows below them, then < and >.
static const float kLetterLeft = 24.0f, kLetterPitch = 54.0f, kLetterWidth = 50.0f;
static const float kLetterTop = 56.0f, kLetterHeight = 40.0f, kLetterGap = 4.0f;
static const float kRowTop = 148.0f, kRowHeight = 40.0f;

const std::vector<LauncherTarget> & LauncherTargets()
{
    static const std::vector<LauncherTarget> All = [] {
        std::vector<LauncherTarget> Targets;
        const LauncherTargetKind Fixed[] = { LauncherTargetKind::Face, LauncherTargetKind::Folder, LauncherTargetKind::Quit,
                                             LauncherTargetKind::Recent };
        for (LauncherTargetKind K : Fixed) Targets.push_back(LauncherTarget{ K, 0 });
        for (int i = 0; i < 26; i++) Targets.push_back(LauncherTarget{ LauncherTargetKind::Letter, i });
        for (int i = 0; i < LAUNCHER_ROWS; i++) Targets.push_back(LauncherTarget{ LauncherTargetKind::Row, i });
        Targets.push_back(LauncherTarget{ LauncherTargetKind::Prev, 0 });
        Targets.push_back(LauncherTarget{ LauncherTargetKind::Next, 0 });
        Targets.push_back(LauncherTarget{ LauncherTargetKind::Choose, 0 });
        return Targets;
    }();
    return All;
}

LauncherRect LauncherTargetRect(LauncherTarget T)
{
    switch (T.Kind)
    {
    case LauncherTargetKind::Face: return LauncherRect{ 360, 8, 160, 36 };
    case LauncherTargetKind::Folder: return LauncherRect{ 536, 8, 120, 36 };
    case LauncherTargetKind::Quit: return LauncherRect{ 672, 8, 112, 36 };
    case LauncherTargetKind::Recent: return LauncherRect{ kLetterLeft, kLetterTop, kLetterPitch + kLetterWidth, kLetterHeight };
    case LauncherTargetKind::Letter:
    {
        const bool Top = T.Index < 12;
        const int Column = Top ? T.Index + 2 : T.Index - 12;
        const float Y = Top ? kLetterTop : kLetterTop + kLetterHeight + kLetterGap;
        return LauncherRect{ kLetterLeft + Column * kLetterPitch, Y, kLetterWidth, kLetterHeight };
    }
    case LauncherTargetKind::Row: return LauncherRect{ 16, kRowTop + T.Index * kRowHeight, 768, kRowHeight - 2 };
    case LauncherTargetKind::Prev: return LauncherRect{ 16, 556, 160, 48 };
    case LauncherTargetKind::Next: return LauncherRect{ 624, 556, 160, 48 };
    case LauncherTargetKind::Choose: return LauncherRect{ 250, 320, 300, 56 };
    case LauncherTargetKind::None: break;
    }
    return LauncherRect{ 0, 0, 0, 0 };
}

int LauncherRowCount(const LauncherState & S)
{
    if (S.View == LAUNCHER_VIEW_RECENT) return (int)S.Recent.size();
    const int Left = (int)S.Games.size() - S.View * LAUNCHER_ROWS;
    return Left < 0 ? 0 : (Left > LAUNCHER_ROWS ? LAUNCHER_ROWS : Left);
}

const LauncherGame * LauncherRowGame(const LauncherState & S, int Row)
{
    if (Row < 0 || Row >= LauncherRowCount(S)) return nullptr;
    return S.View == LAUNCHER_VIEW_RECENT ? &S.Recent[Row] : &S.Games[S.View * LAUNCHER_ROWS + Row];
}

bool LauncherEmpty(const LauncherState & S)
{
    return S.View != LAUNCHER_VIEW_RECENT && S.Games.empty();
}

bool LauncherEnabled(const LauncherState & S, LauncherTarget T)
{
    if (!S.EmulatorFound) return T.Kind == LauncherTargetKind::Quit;
    switch (T.Kind)
    {
    case LauncherTargetKind::Face:
    case LauncherTargetKind::Folder:
    case LauncherTargetKind::Quit: return true;
    case LauncherTargetKind::Recent: return !S.Recent.empty();
    case LauncherTargetKind::Letter: return T.Index >= 0 && T.Index < 26 && LauncherLetterPage(S.Games, T.Index) >= 0;
    case LauncherTargetKind::Row: return T.Index >= 0 && T.Index < LauncherRowCount(S);
    case LauncherTargetKind::Prev: return S.View == 0 ? !S.Recent.empty() : S.View > 0;
    case LauncherTargetKind::Next:
        return S.View == LAUNCHER_VIEW_RECENT ? !S.Games.empty() : S.View + 1 < LauncherPageCount((int)S.Games.size());
    case LauncherTargetKind::Choose: return LauncherEmpty(S);
    case LauncherTargetKind::None: break;
    }
    return false;
}

LauncherTarget LauncherHit(const LauncherState & S, float X, float Y)
{
    for (const LauncherTarget & T : LauncherTargets())
    {
        const LauncherRect R = LauncherTargetRect(T);
        if (X >= R.X && X < R.X + R.W && Y >= R.Y && Y < R.Y + R.H && LauncherEnabled(S, T)) return T;
    }
    return LauncherTarget();
}

int LauncherInitialView(const LauncherState & S)
{
    return S.Recent.empty() ? 0 : LAUNCHER_VIEW_RECENT;
}

LauncherCommand LauncherAct(LauncherState * S, LauncherTarget T, const LauncherGame ** Game)
{
    if (!LauncherEnabled(*S, T)) return LauncherCommand::None;
    switch (T.Kind)
    {
    case LauncherTargetKind::Face: return LauncherCommand::ToggleFace;
    case LauncherTargetKind::Folder:
    case LauncherTargetKind::Choose: return LauncherCommand::PickFolder;
    case LauncherTargetKind::Quit: return LauncherCommand::Quit;
    case LauncherTargetKind::Recent: S->View = LAUNCHER_VIEW_RECENT; break;
    case LauncherTargetKind::Letter: S->View = LauncherLetterPage(S->Games, T.Index); break;
    case LauncherTargetKind::Prev: S->View = S->View == 0 ? LAUNCHER_VIEW_RECENT : S->View - 1; break;
    case LauncherTargetKind::Next: S->View = S->View == LAUNCHER_VIEW_RECENT ? 0 : S->View + 1; break;
    case LauncherTargetKind::Row:
        *Game = LauncherRowGame(*S, T.Index);
        return LauncherCommand::Start;
    case LauncherTargetKind::None: break;
    }
    return LauncherCommand::None;
}
