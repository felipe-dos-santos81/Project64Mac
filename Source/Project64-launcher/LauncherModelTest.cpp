// Project64 - A Nintendo 64 emulator
// Tests for the launcher's model (LauncherModel.h). Builds temp trees; no window, no SDL.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "LauncherModel.h"
#include <Project64-sdl/UnitTest.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static std::vector<LauncherGame> Titled(const std::vector<const char *> & Titles)
{
    std::vector<LauncherGame> Games;
    for (const char * T : Titles)
    {
        LauncherGame G;
        G.Path = std::string("/roms/") + T;
        G.Title = T;
        Games.push_back(G);
    }
    return Games;
}

static void GamesAndPages()
{
    // Which files are games.
    CHECK(LauncherIsRom("a.z64") && LauncherIsRom("b.N64") && LauncherIsRom("c.V64"));
    CHECK(!LauncherIsRom("a.yaml") && !LauncherIsRom("z64") && !LauncherIsRom(".hidden.z64"));
    CHECK(!LauncherIsRom("a.z64.zip"));

    // Titles, as the pack's README writes them.
    CHECK(LauncherTitle("banjo_kazooie_u.z64") == "Banjo Kazooie U");
    CHECK(LauncherTitle("007_goldeneye.v64") == "007 Goldeneye");
    CHECK(LauncherTitle("a_bug_s_life_u.z64") == "A Bug S Life U");
    CHECK(LauncherTitle("noext") == "Noext");

    // Sorted without regard to case, the path breaking a tie.
    std::vector<LauncherGame> Games = Titled({ "zelda", "Aero", "007 Goldeneye", "aero" });
    Games[3].Path = "/roms/0";   // the tie: "aero" sorts before "Aero" by path
    LauncherSort(&Games);
    CHECK(Games[0].Title == "007 Goldeneye");
    CHECK(Games[1].Path == "/roms/0" && Games[2].Title == "Aero");
    CHECK(Games[3].Title == "zelda");

    // Pages of ten; an empty folder still has one (empty) page.
    CHECK(LauncherPageCount(0) == 1 && LauncherPageCount(1) == 1 && LauncherPageCount(10) == 1);
    CHECK(LauncherPageCount(11) == 2 && LauncherPageCount(296) == 30);

    // Letters: the page of the first title starting with it; A also takes digit-led titles;
    // a letter nothing starts with is -1.
    std::vector<LauncherGame> Many;
    for (int i = 0; i < 12; i++) Many.push_back(Titled({ "Banjo" })[0]);
    Many.insert(Many.begin(), Titled({ "007 Goldeneye" })[0]);
    Many.push_back(Titled({ "Zelda" })[0]);
    CHECK(LauncherLetterPage(Many, 0) == 0);          // A: the digit-led title on page 0
    CHECK(LauncherLetterPage(Many, 1) == 0);          // B starts on page 0
    CHECK(LauncherLetterPage(Many, 25) == 1);         // Z is the 14th title: page 1
    CHECK(LauncherLetterPage(Many, 2) == -1);         // no C
    CHECK(LauncherLetterPage(Titled({ "Banjo" }), 0) == -1);   // no A and no digit

    // Recent games: newest first, no repeats, at most five.
    std::vector<std::string> Recent;
    LauncherPushRecent(&Recent, "/a");
    LauncherPushRecent(&Recent, "/b");
    LauncherPushRecent(&Recent, "/a");
    CHECK(Recent.size() == 2 && Recent[0] == "/a" && Recent[1] == "/b");
    for (const char * P : { "/c", "/d", "/e", "/f" }) LauncherPushRecent(&Recent, P);
    CHECK(Recent.size() == 5 && Recent[0] == "/f" && Recent[4] == "/a");
}

static void ScanAndEmulator()
{
    const std::string Root = TestMakeTempDir("pj64-launcher");
    const std::string Emu = Root + "/bin";
    const std::string Roms = Root + "/roms";
    TestMakeDir(Emu);
    TestMakeDir(Emu + "/Config");
    TestMakeDir(Emu + "/Config/mouse");
    TestMakeDir(Roms);
    TestTouch(Roms + "/super_mario_64_usa.z64");
    TestTouch(Roms + "/banjo_kazooie_u.N64");
    TestTouch(Roms + "/banjo_kazooie_u.yaml");        // its own layout, beside it
    TestTouch(Roms + "/notes.txt");
    TestTouch(Roms + "/.hidden.z64");
    TestMakeDir(Roms + "/folder.z64");                // a folder is never a game
    TestTouch(Emu + "/Config/mouse/super_mario_64_usa.yaml");

    std::vector<LauncherGame> Games;
    CHECK(LauncherScan(Roms.c_str(), Emu.c_str(), &Games));
    CHECK(Games.size() == 2);
    if (Games.size() == 2)
    {
        CHECK(Games[0].Title == "Banjo Kazooie U" && !Games[0].Generic);
        CHECK(Games[0].Path == Roms + "/banjo_kazooie_u.N64");
        CHECK(Games[1].Title == "Super Mario 64 Usa" && !Games[1].Generic);   // found in Config/mouse
    }
    TestTouch(Roms + "/aerogauge_u.z64");
    CHECK(LauncherScan(Roms.c_str(), Emu.c_str(), &Games) && Games.size() == 3);
    CHECK(Games[0].Title == "Aerogauge U" && Games[0].Generic);
    CHECK(!LauncherScan((Root + "/gone").c_str(), Emu.c_str(), &Games) && Games.empty());

    // The emulator: beside the launcher, or three levels up from inside the app bundle.
    std::string Dir;
    TestMakeDir(Emu + "/Project64.app");
    TestMakeDir(Emu + "/Project64.app/Contents");
    TestMakeDir(Emu + "/Project64.app/Contents/MacOS");
    const std::string Bundled = Emu + "/Project64.app/Contents/MacOS/Project64-launcher";
    CHECK(!LauncherFindEmulator(Bundled.c_str(), &Dir));            // no Project64 yet
    TestTouch(Emu + "/Project64", 0644);
    CHECK(!LauncherFindEmulator(Bundled.c_str(), &Dir));            // not executable
    TestTouch(Emu + "/Project64", 0755);
    char Real[PATH_MAX];
    CHECK(realpath(Emu.c_str(), Real) != nullptr);
    CHECK(LauncherFindEmulator(Bundled.c_str(), &Dir) && Dir == Real);
    Dir.clear();
    CHECK(LauncherFindEmulator((Emu + "/Project64-launcher").c_str(), &Dir) && Dir == Real);
}

static bool Has(const std::vector<std::string> & Env, const std::string & Entry)
{
    for (const std::string & E : Env) if (E == Entry) return true;
    return false;
}

static int CountPrefix(const std::vector<std::string> & Env, const char * Prefix)
{
    int N = 0;
    for (const std::string & E : Env) if (E.compare(0, strlen(Prefix), Prefix) == 0) N++;
    return N;
}

static void ChildEnvironment()
{
    // A caller's own PJ64_FACE, PJ64_MENU_AUTO and empty PJ64_INPUT_YAML never reach the game.
    const char * const Inherited[] = { "HOME=/Users/you", "PJ64_FACE=1", "PJ64_MENU_AUTO=0", "PJ64_INPUT_YAML=", nullptr };
    std::vector<std::string> Env = LauncherChildEnv(Inherited, "/emu", true, false);
    CHECK(Has(Env, "HOME=/Users/you"));
    CHECK(Has(Env, "PJ64_INPUT_YAML=/emu/Config/mouse/default.yaml") && CountPrefix(Env, "PJ64_INPUT_YAML=") == 1);
    CHECK(Has(Env, "PJ64_MENU_AUTO=1") && CountPrefix(Env, "PJ64_MENU_AUTO=") == 1);
    CHECK(Has(Env, "PJ64_FACE=0") && CountPrefix(Env, "PJ64_FACE=") == 1);

    // Face on: PJ64_FACE is absent, so the layout decides. A game with its own layout gets
    // no PJ64_INPUT_YAML: the frontend finds the layout itself.
    Env = LauncherChildEnv(Inherited, "/emu", false, true);
    CHECK(CountPrefix(Env, "PJ64_FACE=") == 0);
    CHECK(CountPrefix(Env, "PJ64_INPUT_YAML=") == 0);
    CHECK(Has(Env, "PJ64_MENU_AUTO=1"));

    // A caller's explicit layout wins over the generic one, as the frontend's own rule says.
    const char * const Explicit[] = { "PJ64_INPUT_YAML=/mine.yaml", nullptr };
    Env = LauncherChildEnv(Explicit, "/emu", true, false);
    CHECK(Has(Env, "PJ64_INPUT_YAML=/mine.yaml") && CountPrefix(Env, "PJ64_INPUT_YAML=") == 1);
}

void RunLauncherTests()
{
    GamesAndPages();
    ScanAndEmulator();
    ChildEnvironment();
}
