// Project64 - A Nintendo 64 emulator
// Per-game input layout lookup. See GameConfig.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "GameConfig.h"

#include <stdio.h>
#include <string>
#include <unistd.h>

bool GameConfigPath(const char * RomPath, const char * ExeDir, char * Out, size_t Size)
{
    const std::string Rom = RomPath;
    const size_t Slash = Rom.find_last_of('/');
    const std::string Dir = Slash == std::string::npos ? "." : Rom.substr(0, Slash);
    std::string Base = Slash == std::string::npos ? Rom : Rom.substr(Slash + 1);
    const size_t Dot = Base.find_last_of('.');
    if (Dot != std::string::npos && Dot > 0)   // ".hidden" keeps its name whole
    {
        Base.erase(Dot);
    }
    if (Base.empty())
    {
        return false;
    }
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
