// Project64 - A Nintendo 64 emulator
// Tests for GameConfigPath. Builds a temp tree; no window and no SDL init.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "GameConfig.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

static void Touch(const std::string & Path)
{
    FILE * F = fopen(Path.c_str(), "w");
    if (F == nullptr) { perror(Path.c_str()); exit(2); }
    fclose(F);
}

static void MakeDir(const std::string & Path)
{
    if (mkdir(Path.c_str(), 0700) != 0) { perror(Path.c_str()); exit(2); }
}

int main()
{
    char Root[] = "/tmp/pj64-gamecfg-XXXXXX";
    if (mkdtemp(Root) == nullptr) { perror("mkdtemp"); return 2; }
    const std::string Roms = std::string(Root) + "/roms";
    const std::string Exe = std::string(Root) + "/bin";
    MakeDir(Roms);
    MakeDir(Exe);
    MakeDir(Exe + "/Config");
    MakeDir(Exe + "/Config/mouse");
    const std::string Rom = Roms + "/game.z64";   // the ROM itself need not exist
    char Out[PATH_MAX];

    // Neither candidate: false, and Out is left alone.
    strcpy(Out, "untouched");
    CHECK(!GameConfigPath(Rom.c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(strcmp(Out, "untouched") == 0);

    // Only the installed copy under Config/mouse.
    Touch(Exe + "/Config/mouse/game.yaml");
    CHECK(GameConfigPath(Rom.c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Exe + "/Config/mouse/game.yaml");

    // The sibling wins when both exist.
    Touch(Roms + "/game.yaml");
    CHECK(GameConfigPath(Rom.c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Roms + "/game.yaml");

    // Only the last extension is stripped, so a .zip finds the same sibling.
    CHECK(GameConfigPath((Roms + "/game.zip").c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Roms + "/game.yaml");

    // A ROM with no extension uses its whole name.
    Touch(Roms + "/plain.yaml");
    CHECK(GameConfigPath((Roms + "/plain").c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Roms + "/plain.yaml");

    // A different ROM in the same folder does not pick up game.yaml.
    CHECK(!GameConfigPath((Roms + "/other.z64").c_str(), Exe.c_str(), Out, sizeof(Out)));

    // A path ending in a slash names no ROM.
    CHECK(!GameConfigPath((Roms + "/").c_str(), Exe.c_str(), Out, sizeof(Out)));

    // No directory component: the sibling is looked up in the working directory.
    if (chdir(Roms.c_str()) != 0) { perror("chdir"); return 2; }
    CHECK(GameConfigPath("game.z64", Exe.c_str(), Out, sizeof(Out)));
    CHECK(strcmp(Out, "./game.yaml") == 0);

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: game config\n");
    return 0;
}
