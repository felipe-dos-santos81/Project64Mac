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

// A game's environment, and whether it kept the launcher's own PJ64_INPUT_YAML, which then
// wins over the game's layout.
struct LauncherEnv
{
    std::vector<std::string> Vars;   // NAME=value, as posix_spawn takes them
    bool InheritedLayout = false;
};

// Environ without PJ64_MENU_AUTO, PJ64_FACE or an empty PJ64_INPUT_YAML; then the generic
// layout for a Generic game unless Environ names a layout; PJ64_MENU_AUTO=1; and PJ64_FACE=0
// when Face is off. The launcher's Face button alone decides the camera: an inherited
// PJ64_FACE never reaches the game.
LauncherEnv LauncherChildEnv(const char * const * Environ, const std::string & EmulatorDir,
                             const LauncherGame & Game, bool FaceOn);

// $PJ64_LAUNCHER_HOME/launcher.yaml when that variable is set and non-empty, else "" (the
// caller then uses the per-user preferences folder).
std::string LauncherHomeSettingsPath();

#define LAUNCHER_VIEW_RECENT -1   // LauncherState::View of the recent games; pages are 0 … N-1
#define LAUNCHER_WIDTH 800
#define LAUNCHER_HEIGHT 640

// launcher.yaml: the folder, Face, and the recent games' paths, newest first.
struct LauncherSettings
{
    std::string Folder;
    bool Face = false;   // off until someone turns it on: the camera never opens unasked
    std::vector<std::string> Recent;
};

enum class LauncherLoad { Ok, Missing, Malformed };

// Out gets the file's settings, dropping recent paths whose file is gone; on Missing or
// Malformed it gets the defaults.
LauncherLoad LauncherLoadSettings(const char * Path, LauncherSettings * Out);

// Written to Path.tmp and renamed over Path, so a crash never leaves half a file.
bool LauncherSaveSettings(const char * Path, const LauncherSettings & In);

// What the screen shows.
struct LauncherState
{
    std::vector<LauncherGame> Games;    // the folder's, sorted
    std::vector<LauncherGame> Recent;   // newest first
    int View = 0;                       // LAUNCHER_VIEW_RECENT or a page
    bool EmulatorFound = true;          // false: only Quit
};

enum class LauncherTargetKind { None, Face, Folder, Quit, Recent, Letter, Row, Prev, Next, Choose };

struct LauncherTarget
{
    LauncherTargetKind Kind = LauncherTargetKind::None;
    int Index = 0;   // Letter: 0 = A … 25 = Z; Row: 0 … LAUNCHER_ROWS - 1
};

inline bool operator==(LauncherTarget A, LauncherTarget B)
{
    return A.Kind == B.Kind && A.Index == B.Index;
}

struct LauncherRect
{
    float X, Y, W, H;
};

// Every target, in drawing order: the top bar, Recent, the letters, the rows, < and >, Choose.
const std::vector<LauncherTarget> & LauncherTargets();

LauncherRect LauncherTargetRect(LauncherTarget T);

// The rows the current view shows, and the game on one (null past the last).
int LauncherRowCount(const LauncherState & S);
const LauncherGame * LauncherRowGame(const LauncherState & S, int Row);

// A page view with no games at all: Choose a folder is shown in place of the rows.
bool LauncherEmpty(const LauncherState & S);

// Whether T does anything now; a disabled target is drawn dimmed and never hit.
bool LauncherEnabled(const LauncherState & S, LauncherTarget T);

// The enabled target under a point in window coordinates, or None.
LauncherTarget LauncherHit(const LauncherState & S, float X, float Y);

// Recent when there are recent games, else page 1.
int LauncherInitialView(const LauncherState & S);

enum class LauncherCommand { None, ToggleFace, PickFolder, Quit, Start };

// What a click on T does. Navigation changes S->View here and returns None; the rest is the
// caller's. Start sets *Game to the row's game, which points into S and is valid until S changes.
LauncherCommand LauncherAct(LauncherState * S, LauncherTarget T, const LauncherGame ** Game);
