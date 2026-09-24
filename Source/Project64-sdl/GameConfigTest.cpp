// Project64 - A Nintendo 64 emulator
// Tests for GameConfigPath. Builds a temp tree; no window and no SDL init.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "GameConfig.h"
#include "UnitTest.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

void RunGameConfigTests()
{
    const std::string Root = TestMakeTempDir("pj64-gamecfg");
    const std::string Roms = Root + "/roms";
    const std::string Exe = Root + "/bin";
    TestMakeDir(Roms);
    TestMakeDir(Exe);
    TestMakeDir(Exe + "/Config");
    TestMakeDir(Exe + "/Config/mouse");
    const std::string Rom = Roms + "/game.z64";   // the ROM itself need not exist
    char Out[PATH_MAX];

    // Neither candidate: false, and Out is left alone.
    strcpy(Out, "untouched");
    CHECK(!GameConfigPath(Rom.c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(strcmp(Out, "untouched") == 0);

    // Only the installed copy under Config/mouse.
    TestTouch(Exe + "/Config/mouse/game.yaml");
    CHECK(GameConfigPath(Rom.c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Exe + "/Config/mouse/game.yaml");

    // The sibling wins when both exist.
    TestTouch(Roms + "/game.yaml");
    CHECK(GameConfigPath(Rom.c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Roms + "/game.yaml");

    // Only the last extension is stripped, so a .zip finds the same sibling.
    CHECK(GameConfigPath((Roms + "/game.zip").c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Roms + "/game.yaml");

    // A ROM with no extension uses its whole name.
    TestTouch(Roms + "/plain.yaml");
    CHECK(GameConfigPath((Roms + "/plain").c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Roms + "/plain.yaml");

    // A different ROM in the same folder does not pick up game.yaml.
    CHECK(!GameConfigPath((Roms + "/other.z64").c_str(), Exe.c_str(), Out, sizeof(Out)));

    // A path ending in a slash names no ROM.
    CHECK(!GameConfigPath((Roms + "/").c_str(), Exe.c_str(), Out, sizeof(Out)));

    // No directory component: the sibling is looked up in the working directory, which is
    // put back afterwards for the areas that run next.
    char Cwd[PATH_MAX];
    if (getcwd(Cwd, sizeof(Cwd)) == nullptr || chdir(Roms.c_str()) != 0) { perror("chdir"); TestFailures()++; return; }
    CHECK(GameConfigPath("game.z64", Exe.c_str(), Out, sizeof(Out)));
    CHECK(strcmp(Out, "./game.yaml") == 0);
    if (chdir(Cwd) != 0) { perror("chdir"); TestFailures()++; }

}
