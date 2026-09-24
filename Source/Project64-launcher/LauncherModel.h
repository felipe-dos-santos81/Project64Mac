// Project64 - A Nintendo 64 emulator
// The launcher's rules, with no SDL: which files are games, their titles and order, the
// pages and the letter strip, the recent games, where the emulator is, and the environment
// a game starts with. Design: Docs/superpowers/specs/2026-09-24-launcher-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <string>
#include <vector>

#define LAUNCHER_ROWS 10         // games per page
#define LAUNCHER_RECENT_MAX 5    // recent games kept

struct LauncherGame
{
    std::string Path;       // the folder joined with the file name
    std::string Title;      // LauncherTitle of the file name
    bool Generic = false;   // no layout of its own: starts with Config/mouse/default.yaml
};

// A .z64, .n64 or .v64 file name, in any case, not starting with a dot.
bool LauncherIsRom(const char * FileName);

// The file name without its extension, underscores as spaces, each word capitalised:
// banjo_kazooie_u.z64 is "Banjo Kazooie U".
std::string LauncherTitle(const char * FileName);

// One game; Generic when GameConfigPath finds no layout for it.
LauncherGame LauncherGameFor(const std::string & Path, const char * EmulatorDir);

// Titles without regard to case, the path breaking a tie.
void LauncherSort(std::vector<LauncherGame> * Games);

// Every game directly in Folder, sorted. False, with Out empty, when the folder cannot be read.
bool LauncherScan(const char * Folder, const char * EmulatorDir, std::vector<LauncherGame> * Out);

// Pages of LAUNCHER_ROWS; never fewer than one.
int LauncherPageCount(int GameCount);

// The page of the first title whose first letter is Letter (0 = A … 25 = Z); A also takes a
// title that starts with anything but a letter, which sorts before it. -1 when none.
int LauncherLetterPage(const std::vector<LauncherGame> & Games, int Letter);

// Path to the front of Recent, without repeating it, keeping LAUNCHER_RECENT_MAX.
void LauncherPushRecent(std::vector<std::string> * Recent, const std::string & Path);

// The directory holding an executable Project64: the launcher's own directory (the bare
// Bin/macOS/Project64-launcher), else three levels up (from Project64.app/Contents/MacOS).
// Dir gets the resolved path.
bool LauncherFindEmulator(const char * LauncherPath, std::string * Dir);

// The game's environment: Environ without PJ64_MENU_AUTO, PJ64_FACE or an empty
// PJ64_INPUT_YAML; then the generic layout for a Generic game unless Environ names a layout;
// PJ64_MENU_AUTO=1; and PJ64_FACE=0 when Face is off.
std::vector<std::string> LauncherChildEnv(const char * const * Environ, const std::string & EmulatorDir,
                                          bool Generic, bool FaceOn);
