// Project64 - A Nintendo 64 emulator
// Per-game input layout lookup. See GameConfig.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "GameConfig.h"

#include <stdio.h>
#include <string>
#include <unistd.h>

// RomPath's directory ("." for a bare name) and its file name without the last extension.
static bool SplitRom(const char * RomPath, std::string * Dir, std::string * Base)
{
    const std::string Rom = RomPath;
    const size_t Slash = Rom.find_last_of('/');
    *Dir = Slash == std::string::npos ? "." : Rom.substr(0, Slash);
    *Base = Slash == std::string::npos ? Rom : Rom.substr(Slash + 1);
    const size_t Dot = Base->find_last_of('.');
    if (Dot != std::string::npos && Dot > 0)   // ".hidden" keeps its name whole
    {
        Base->erase(Dot);
    }
    return !Base->empty();
}

bool GameConfigBesideRom(const char * RomPath, char * Out, size_t Size)
{
    std::string Dir, Base;
    if (!SplitRom(RomPath, &Dir, &Base)) return false;
    snprintf(Out, Size, "%s/%s.yaml", Dir.c_str(), Base.c_str());
    return true;
}

bool GameConfigPath(const char * RomPath, const char * ExeDir, char * Out, size_t Size)
{
    std::string Dir, Base;
    if (!SplitRom(RomPath, &Dir, &Base)) return false;
    const std::string Candidates[2] = {
        Dir + "/" + Base + ".yaml",
        std::string(ExeDir) + "/Config/mouse/" + Base + ".yaml",
    };
    for (const std::string & Path : Candidates)
    {
        if (access(Path.c_str(), R_OK) == 0)
        {
            snprintf(Out, Size, "%s", Path.c_str());
            return true;
        }
    }
    return false;
}
