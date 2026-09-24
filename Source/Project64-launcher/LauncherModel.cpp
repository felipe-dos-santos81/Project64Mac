// Project64 - A Nintendo 64 emulator
// See LauncherModel.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "LauncherModel.h"
#include <Project64-sdl/GameConfig.h>

#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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

std::vector<std::string> LauncherChildEnv(const char * const * Environ, const std::string & EmulatorDir,
                                          bool Generic, bool FaceOn)
{
    static const char kLayout[] = "PJ64_INPUT_YAML=";
    std::vector<std::string> Out;
    bool Inherited = false;
    for (const char * const * E = Environ; E != nullptr && *E != nullptr; E++)
    {
        if (HasPrefix(*E, "PJ64_MENU_AUTO=") || HasPrefix(*E, "PJ64_FACE=")) continue;
        if (HasPrefix(*E, kLayout))
        {
            if ((*E)[sizeof(kLayout) - 1] == '\0') continue;
            Inherited = true;
        }
        Out.push_back(*E);
    }
    if (Generic && !Inherited) Out.push_back(std::string(kLayout) + EmulatorDir + "/Config/mouse/default.yaml");
    Out.push_back("PJ64_MENU_AUTO=1");
    if (!FaceOn) Out.push_back("PJ64_FACE=0");
    return Out;
}
