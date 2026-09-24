// Project64 - A Nintendo 64 emulator
// This process's executable and its folder, resolved through links. Header-only, so the
// launcher and the wizard share it without a link dependency. (The frontend's main.cpp and
// GridHost.cpp keep their own older copies.)
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>

// The executable's full path, or "" when macOS cannot say.
inline std::string ExecutablePath()
{
    char Buf[PATH_MAX];
    uint32_t Size = sizeof(Buf);
    if (_NSGetExecutablePath(Buf, &Size) != 0) return "";
    char Resolved[PATH_MAX];
    return realpath(Buf, Resolved) != nullptr ? std::string(Resolved) : std::string();
}

// The folder holding it, or "." when unknown.
inline std::string ExecutableDirectory()
{
    const std::string Path = ExecutablePath();
    const size_t Slash = Path.rfind('/');
    return Slash == std::string::npos ? std::string(".") : Path.substr(0, Slash);
}
