// Project64 - A Nintendo 64 emulator
// Tests for the launcher's model (LauncherModel.h). Builds temp trees; no window, no SDL.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "LauncherModel.h"
#include <Project64-sdl/UnitTest.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>
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

    CHECK(!LauncherHasEditor(Real));
    TestTouch(Emu + "/Project64-wizard", 0755);
    CHECK(LauncherHasEditor(Real));
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
    LauncherGame Generic;
    Generic.Generic = true;
    const LauncherGame OwnLayout;

    // A caller's own PJ64_FACE, PJ64_MENU_AUTO and empty PJ64_INPUT_YAML never reach the game.
    const char * const Inherited[] = { "HOME=/Users/you", "PJ64_FACE=1", "PJ64_MENU_AUTO=0", "PJ64_INPUT_YAML=", nullptr };
    LauncherEnv Env = LauncherChildEnv(Inherited, "/emu", Generic, false);
    CHECK(!Env.InheritedLayout);                       // an empty PJ64_INPUT_YAML is not kept
    CHECK(Has(Env.Vars, "HOME=/Users/you"));
    CHECK(Has(Env.Vars, "PJ64_INPUT_YAML=/emu/Config/mouse/default.yaml") && CountPrefix(Env.Vars, "PJ64_INPUT_YAML=") == 1);
    CHECK(Has(Env.Vars, "PJ64_MENU_AUTO=1") && CountPrefix(Env.Vars, "PJ64_MENU_AUTO=") == 1);
    CHECK(Has(Env.Vars, "PJ64_FACE=0") && CountPrefix(Env.Vars, "PJ64_FACE=") == 1);

    // Face on: PJ64_FACE is absent, so the layout decides. A game with its own layout gets
    // no PJ64_INPUT_YAML: the frontend finds the layout itself.
    Env = LauncherChildEnv(Inherited, "/emu", OwnLayout, true);
    CHECK(CountPrefix(Env.Vars, "PJ64_FACE=") == 0);
    CHECK(CountPrefix(Env.Vars, "PJ64_INPUT_YAML=") == 0);
    CHECK(Has(Env.Vars, "PJ64_MENU_AUTO=1"));

    // A caller's explicit layout wins over the generic one, as the frontend's own rule says.
    const char * const Explicit[] = { "PJ64_INPUT_YAML=/mine.yaml", nullptr };
    Env = LauncherChildEnv(Explicit, "/emu", Generic, false);
    CHECK(Has(Env.Vars, "PJ64_INPUT_YAML=/mine.yaml") && CountPrefix(Env.Vars, "PJ64_INPUT_YAML=") == 1);
    CHECK(Env.InheritedLayout);
}

static void Settings()
{
    const std::string Root = TestMakeTempDir("pj64-launcher-settings");

    // PJ64_LAUNCHER_HOME names the folder; unset or empty, the caller's per-user folder is
    // used. The caller's own value is put back afterwards.
    const char * Before = getenv("PJ64_LAUNCHER_HOME");
    const std::string Saved = Before != nullptr ? Before : "";
    setenv("PJ64_LAUNCHER_HOME", "", 1);
    CHECK(LauncherHomeSettingsPath().empty());
    unsetenv("PJ64_LAUNCHER_HOME");
    CHECK(LauncherHomeSettingsPath().empty());
    setenv("PJ64_LAUNCHER_HOME", Root.c_str(), 1);
    const std::string Path = LauncherHomeSettingsPath();
    CHECK(Path == Root + "/launcher.yaml");
    if (Before != nullptr) setenv("PJ64_LAUNCHER_HOME", Saved.c_str(), 1);
    else unsetenv("PJ64_LAUNCHER_HOME");

    const std::string Odd = Root + "/N64: Games \"best\"";   // a colon and quotes survive
    TestMakeDir(Odd);
    TestTouch(Odd + "/a.z64");
    TestTouch(Root + "/b.z64");

    LauncherSettings S;
    S.Folder = "stale";
    CHECK(LauncherLoadSettings(Path.c_str(), &S) == LauncherLoad::Missing);
    CHECK(S.Folder.empty() && !S.Face && S.Recent.empty());   // defaults: Face off

    S.Folder = Odd;
    S.Face = true;
    S.Recent = { Odd + "/a.z64", Root + "/gone.z64", Root + "/b.z64" };
    CHECK(LauncherSaveSettings(Path.c_str(), S));
    CHECK(access((Path + ".tmp").c_str(), F_OK) != 0);        // renamed into place

    LauncherSettings Back;
    CHECK(LauncherLoadSettings(Path.c_str(), &Back) == LauncherLoad::Ok);
    CHECK(Back.Folder == Odd && Back.Face);
    CHECK(Back.Recent.size() == 2 && Back.Recent[0] == Odd + "/a.z64" && Back.Recent[1] == Root + "/b.z64");

    S.Recent.clear();
    CHECK(LauncherSaveSettings(Path.c_str(), S));
    CHECK(LauncherLoadSettings(Path.c_str(), &Back) == LauncherLoad::Ok && Back.Recent.empty());

    for (const char * Bad : { "folder: [1, 2]\n", "- a list\n", "recent: /not/a/list\n", "face: maybe\n", "{unclosed\n" })
    {
        FILE * F = fopen(Path.c_str(), "w");
        fputs(Bad, F);
        fclose(F);
        Back.Folder = "stale";
        CHECK(LauncherLoadSettings(Path.c_str(), &Back) == LauncherLoad::Malformed);
        CHECK(Back.Folder.empty() && !Back.Face && Back.Recent.empty());
    }
}

static LauncherState StateOf(int GameCount, int RecentCount)
{
    LauncherState S;
    for (int i = 0; i < GameCount; i++)
    {
        LauncherGame G;
        G.Title = std::string(1, (char)('A' + i % 26)) + std::to_string(i);
        G.Path = "/roms/" + G.Title;
        S.Games.push_back(G);
    }
    LauncherSort(&S.Games);
    for (int i = 0; i < RecentCount; i++) S.Recent.push_back(S.Games.empty() ? LauncherGame() : S.Games[i]);
    return S;
}

static LauncherTarget Target(LauncherTargetKind Kind, int Index = 0)
{
    LauncherTarget Out;
    Out.Kind = Kind;
    Out.Index = Index;
    return Out;
}

static LauncherTarget HitCentre(const LauncherState & S, LauncherTarget Target)
{
    const LauncherRect R = LauncherTargetRect(Target);
    return LauncherHit(S, R.X + R.W / 2, R.Y + R.H / 2);
}

static void Screen()
{
    // Every target lies inside the window, and no two overlap except Choose, which only
    // exists when there are no rows.
    const std::vector<LauncherTarget> & All = LauncherTargets();
    CHECK(All.size() == 3 + 1 + 26 + LAUNCHER_ROWS + LAUNCHER_ROWS + 2 + 1);
    for (size_t i = 0; i < All.size(); i++)
    {
        const LauncherRect A = LauncherTargetRect(All[i]);
        CHECK(A.X >= 0 && A.Y >= 0 && A.X + A.W <= LAUNCHER_WIDTH && A.Y + A.H <= LAUNCHER_HEIGHT && A.W > 0 && A.H > 0);
        for (size_t j = i + 1; j < All.size(); j++)
        {
            if (All[i].Kind == LauncherTargetKind::Choose || All[j].Kind == LauncherTargetKind::Choose) continue;
            const LauncherRect B = LauncherTargetRect(All[j]);
            CHECK(A.X + A.W <= B.X || B.X + B.W <= A.X || A.Y + A.H <= B.Y || B.Y + B.H <= A.Y);
        }
    }

    // 25 games, no recent games: three pages, opening on page 1.
    LauncherState S = StateOf(25, 0);
    const LauncherGame * G = nullptr;
    CHECK(LauncherInitialView(S) == 0);
    for (const LauncherTarget & Each : All) CHECK(!LauncherEnabled(S, Each) || HitCentre(S, Each) == Each);
    CHECK(LauncherHit(S, 2, 2).Kind == LauncherTargetKind::None);        // the corner is no target
    CHECK(LauncherHit(S, 300, 20).Kind == LauncherTargetKind::None);     // beside the title
    CHECK(LauncherHit(S, 184, 70).Kind == LauncherTargetKind::None);     // between the A and B cells
    CHECK(LauncherHit(S, 150, 98).Kind == LauncherTargetKind::None);     // between the two letter rows
    CHECK(LauncherHit(S, 100, 187).Kind == LauncherTargetKind::None);    // between the first two game rows
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Prev)));             // page 1, no recent games
    CHECK(HitCentre(S, Target(LauncherTargetKind::Prev)).Kind == LauncherTargetKind::None);
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Recent)));
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Choose)));
    CHECK(LauncherEnabled(S, Target(LauncherTargetKind::Letter, 24)));        // Y24, the last title
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Letter, 25)));       // no title starts with Z

    // Edit: its own target beside each row's title, dimmed without a wizard.
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Edit, 0)));
    S.EditorFound = true;
    CHECK(LauncherEnabled(S, Target(LauncherTargetKind::Edit, 0)));
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Edit, LauncherRowCount(S))));
    const LauncherRect Title = LauncherTargetRect(Target(LauncherTargetKind::Row, 3));
    const LauncherRect Edit = LauncherTargetRect(Target(LauncherTargetKind::Edit, 3));
    CHECK(Title.Y == Edit.Y && Title.H == Edit.H && Title.X + Title.W < Edit.X);
    CHECK(LauncherHit(S, Title.X + Title.W - 1, Title.Y + 10) == Target(LauncherTargetKind::Row, 3));
    CHECK(LauncherHit(S, Edit.X + 1, Edit.Y + 10) == Target(LauncherTargetKind::Edit, 3));
    CHECK(LauncherHit(S, Title.X + Title.W + 2, Title.Y + 10).Kind == LauncherTargetKind::None);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Edit, 3), &G) == LauncherCommand::Edit && G == &S.Games[3]);
    S.EditorFound = false;

    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Next), &G) == LauncherCommand::None && S.View == 1);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Next), &G) == LauncherCommand::None && S.View == 2);
    CHECK(LauncherRowCount(S) == 5);
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Next)));             // last page
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Row, 5)));           // beyond the page's games
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Next), &G) == LauncherCommand::None && S.View == 2);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Row, 4), &G) == LauncherCommand::Start);
    CHECK(G == &S.Games[24]);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Letter, 0), &G) == LauncherCommand::None && S.View == 0);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Face), &G) == LauncherCommand::ToggleFace);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Folder), &G) == LauncherCommand::PickFolder);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Quit), &G) == LauncherCommand::Quit);

    // With recent games: opens on Recent; < on page 1 goes there; > from it goes to page 1.
    S = StateOf(25, 2);
    CHECK(LauncherInitialView(S) == LAUNCHER_VIEW_RECENT);
    S.View = 0;
    CHECK(LauncherEnabled(S, Target(LauncherTargetKind::Prev)));
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Prev), &G) == LauncherCommand::None && S.View == LAUNCHER_VIEW_RECENT);
    CHECK(LauncherRowCount(S) == 2 && !LauncherEnabled(S, Target(LauncherTargetKind::Prev)));
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Row, 1), &G) == LauncherCommand::Start && G == &S.Recent[1]);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Next), &G) == LauncherCommand::None && S.View == 0);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Recent), &G) == LauncherCommand::None && S.View == LAUNCHER_VIEW_RECENT);

    // No games: Choose in place of the rows, and a disabled target does nothing.
    S = StateOf(0, 0);
    CHECK(LauncherEmpty(S) && LauncherRowCount(S) == 0);
    CHECK(HitCentre(S, Target(LauncherTargetKind::Choose)).Kind == LauncherTargetKind::Choose);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Choose), &G) == LauncherCommand::PickFolder);
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Row, 0), &G) == LauncherCommand::None);
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Next)));
    for (int L = 0; L < 26; L++) CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Letter, L)));

    // No games but recent ones (a folder that vanished): Recent still works.
    S = StateOf(0, 0);
    S.Recent.push_back(LauncherGame());
    CHECK(LauncherEnabled(S, Target(LauncherTargetKind::Prev)));
    CHECK(LauncherAct(&S, Target(LauncherTargetKind::Prev), &G) == LauncherCommand::None && !LauncherEmpty(S));
    CHECK(!LauncherEnabled(S, Target(LauncherTargetKind::Next)));

    // No emulator: only Quit.
    S = StateOf(25, 2);
    S.EmulatorFound = false;
    for (const LauncherTarget & Each : All) CHECK(LauncherEnabled(S, Each) == (Each.Kind == LauncherTargetKind::Quit));
}

void RunLauncherTests()
{
    GamesAndPages();
    ScanAndEmulator();
    ChildEnvironment();
    Settings();
    Screen();
}
