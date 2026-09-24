// Project64 - A Nintendo 64 emulator
// Tests for InputConfig::Load. No window and no SDL init; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "InputConfig.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

static const char * WriteTemp(const char * Text)
{
    static char Path[64];
    snprintf(Path, sizeof(Path), "/tmp/pj64-input-XXXXXX");
    int Fd = mkstemp(Path);
    if (Fd < 0) { perror("mkstemp"); exit(2); }
    write(Fd, Text, strlen(Text));
    close(Fd);
    return Path;
}

// Runs one Load with stderr redirected to a scratch file, then reports whether anything
// was written. A quiet Load's return value is false either way, so this is the only way
// to prove the Quiet guards actually suppress output rather than merely returning false.
// Redirects the fd underneath stderr with dup2, not freopen: freopen would reassociate
// the stderr FILE object with a regular file and leave it fully buffered even after the
// fd is restored, reordering every fprintf(stderr, ...) after the first call.
static bool LoadWasSilent(InputConfig & C, const char * Path, bool Quiet)
{
    char ScratchPath[64];
    snprintf(ScratchPath, sizeof(ScratchPath), "/tmp/pj64-stderr-XXXXXX");
    int Fd = mkstemp(ScratchPath);
    if (Fd < 0) { perror("mkstemp"); exit(2); }

    fflush(stderr);
    int SavedStderr = dup(fileno(stderr));
    dup2(Fd, fileno(stderr));
    close(Fd);

    C.Load(Path, Quiet);

    fflush(stderr);
    dup2(SavedStderr, fileno(stderr));
    close(SavedStderr);

    FILE * Scratch = fopen(ScratchPath, "r");
    const bool Empty = (Scratch == NULL) || (fgetc(Scratch) == EOF);
    if (Scratch) fclose(Scratch);
    remove(ScratchPath);
    return Empty;
}

// Loads Path with stderr captured and returns the first line the reader wrote, "" when it
// wrote nothing, so a test can check the reader's own words.
static std::string LoadError(InputConfig & C, const char * Path)
{
    char ScratchPath[64];
    snprintf(ScratchPath, sizeof(ScratchPath), "/tmp/pj64-stderr-XXXXXX");
    int Fd = mkstemp(ScratchPath);
    if (Fd < 0) { perror("mkstemp"); exit(2); }

    fflush(stderr);
    int SavedStderr = dup(fileno(stderr));
    dup2(Fd, fileno(stderr));
    close(Fd);

    C.Load(Path);

    fflush(stderr);
    dup2(SavedStderr, fileno(stderr));
    close(SavedStderr);

    std::string Line;
    FILE * Scratch = fopen(ScratchPath, "r");
    if (Scratch != NULL)
    {
        char Buf[512];
        if (fgets(Buf, sizeof(Buf), Scratch) != NULL) Line = Buf;
        fclose(Scratch);
    }
    remove(ScratchPath);
    return Line;
}

static bool Contains(const std::string & Text, const char * Needle)
{
    return Text.find(Needle) != std::string::npos;
}

int main()
{
    InputConfig & C = InputConfig::Get();

    const char * Valid =
        "bindings:\n"
        "  A: {key: Y}\n"
        "  Start: {key: Return}\n";
    CHECK(C.Load(WriteTemp(Valid)));
    CHECK(C.Bindings(N64Control::A).size() == 1);
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Key);
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_Y);
    CHECK(C.Bindings(N64Control::B).size() == 2);     // omitted control keeps its default pair

    const char * CommentsOnly = "# nothing here\n";
    CHECK(C.Load(WriteTemp(CommentsOnly)));
    CHECK(C.Bindings(N64Control::A).size() == 2);     // defaults restored

    const char * Gamepad =
        "bindings:\n"
        "  A: {button: a}\n"
        "  Z: {axis: lefttrigger, sign: +}\n"
        "  CUp: {axis: righty, sign: -}\n"
        "  Stick: {stick: left}\n";
    CHECK(C.Load(WriteTemp(Gamepad)));
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Button);
    CHECK(C.Bindings(N64Control::Z)[0].kind == Binding::Kind::Axis);
    CHECK(C.Bindings(N64Control::CUp)[0].positive == false);
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Stick);

    const char * DigitalStick =
        "bindings:\n"
        "  Stick: {keys: {up: Up, down: Down, left: Left, right: Right}}\n";
    CHECK(C.Load(WriteTemp(DigitalStick)));
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Keys);
    CHECK(C.Bindings(N64Control::Stick)[0].UpKey == SDL_SCANCODE_UP);

    const char * Pointer =
        "bindings:\n"
        "  Stick: {stick: pointer}\n"
        "  A: {zone: game}\n"
        "  Start: {zone: mid1}\n"
        "  Z: {face: eyebrows}\n"
        "  B: {face: head-left}\n";
    CHECK(C.Load(WriteTemp(Pointer)));
    CHECK(C.UsesPointer());
    CHECK(C.UsesFace());                              // Z and B are gestures
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Pointer);
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Zone);
    CHECK(C.Bindings(N64Control::A)[0].code == POINTER_ZONE_GAME);
    CHECK(C.Bindings(N64Control::Start)[0].code == 8);
    CHECK(C.Bindings(N64Control::Z)[0].kind == Binding::Kind::Face);
    CHECK(C.Bindings(N64Control::Z)[0].code == POINTER_GESTURE_EYEBROWS);
    CHECK(C.Bindings(N64Control::B)[0].code == POINTER_GESTURE_HEAD_LEFT);
    char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE];
    char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE];
    C.PointerLabels(Labels, GestureLabels);
    CHECK(strcmp(Labels[POINTER_ZONE_GAME], "A") == 0);
    CHECK(strcmp(Labels[8], "St") == 0);
    CHECK(strcmp(Labels[0], "") == 0);
    CHECK(strcmp(GestureLabels[0], "Z") == 0);
    CHECK(strcmp(GestureLabels[1], "B") == 0);
    CHECK(strcmp(GestureLabels[2], "") == 0);
    CHECK(strcmp(InputConfig::ControlLabel(N64Control::CUp), "C^") == 0);
    CHECK(strcmp(InputConfig::ControlLabel(N64Control::DPadRight), "D>") == 0);

    CHECK(C.Load(WriteTemp(Valid)));
    CHECK(!C.UsesPointer());                          // keyboard-only file: no overlay
    CHECK(!C.UsesFace());

    const char * ZonesOnly =
        "bindings:\n"
        "  Stick: {stick: pointer}\n"
        "  A: {zone: game}\n";
    CHECK(C.Load(WriteTemp(ZonesOnly)));
    CHECK(C.UsesPointer());
    CHECK(!C.UsesFace());                             // pointer without gestures: no camera

    const char * Slots =
        "bindings:\n"
        "  DPadUp: {zone: pad-up}\n"
        "  CRight: {zone: c-right}\n"
        "  L: {zone: mid5}\n";
    CHECK(C.Load(WriteTemp(Slots)));
    CHECK(C.Bindings(N64Control::DPadUp)[0].code == 0);
    CHECK(C.Bindings(N64Control::CRight)[0].code == 7);
    CHECK(C.Bindings(N64Control::L)[0].code == 12);
    CHECK(C.UsesPointer());                           // slots alone turn the overlay on

    const char * FaceOnly =
        "bindings:\n"
        "  Stick: {stick: head}\n"
        "  A: {face: mouth-open}\n"
        "  B: {face: smile}\n"
        "  CLeft: {face: wink-left}\n"
        "  CRight: {face: wink-right}\n"
        "  R: {face: tilt-left}\n"
        "  Start: {face: tilt-right}\n"
        "  Z: {face: head-up}\n";
    CHECK(!C.Load(WriteTemp(FaceOnly)));              // head-up beside a head stick is rejected...
    const char * FaceOnlyOk =
        "bindings:\n"
        "  Stick: {stick: head}\n"
        "  A: {face: mouth-open}\n"
        "  B: {face: smile}\n"
        "  CLeft: {face: wink-left}\n"
        "  CRight: {face: wink-right}\n"
        "  R: {face: tilt-left}\n"
        "  Start: {face: tilt-right}\n";
    CHECK(C.Load(WriteTemp(FaceOnlyOk)));             // ...and the same file without it loads
    CHECK(C.UsesPointer() && C.UsesFace() && C.UsesHeadStick());
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::HeadStick);
    CHECK(C.Bindings(N64Control::Stick)[0].code == 0);
    CHECK(C.Bindings(N64Control::A)[0].code == POINTER_GESTURE_MOUTH_OPEN);
    CHECK(C.Bindings(N64Control::CLeft)[0].code == POINTER_GESTURE_WINK_LEFT);
    CHECK(C.Bindings(N64Control::Start)[0].code == POINTER_GESTURE_TILT_RIGHT);
    C.PointerLabels(Labels, GestureLabels);
    CHECK(strcmp(GestureLabels[7], "A") == 0);        // mouth-open is bit 7
    CHECK(strcmp(GestureLabels[9], "C<") == 0);       // wink-left is bit 9
    CHECK(strcmp(GestureLabels[1], "") == 0);         // head-left unbound

    CHECK(C.Load(WriteTemp("bindings:\n  Stick: {stick: head-digital}\n  Z: {face: eyebrows}\n")));
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::HeadStick);
    CHECK(C.Bindings(N64Control::Stick)[0].code == 1);
    CHECK(C.UsesHeadStick());

    // A pointer stick beside head-left is fine: only a head stick consumes the head turns.
    CHECK(C.Load(WriteTemp("bindings:\n  Stick: {stick: pointer}\n  B: {face: head-left}\n")));
    CHECK(!C.UsesHeadStick());
    // The rule holds in either order and for either head-stick form.
    CHECK(!C.Load(WriteTemp("bindings:\n  B: {face: head-down}\n  Stick: {stick: head}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {stick: head-digital}\n  B: {face: head-right}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {stick: head}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {stick: head-analog}\n")));

    CHECK(C.Load(WriteTemp(Valid)));                  // establish a known good state
    const size_t ABefore = C.Bindings(N64Control::A).size();

    CHECK(!C.Load(WriteTemp("bindings:\n  Nope: {key: X}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: NoSuchKey}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {button: sout}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: X}\n  A: {key: Y}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: X, button: a}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {stick: middle}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {stick: left}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {keys: {up: Up}}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {keys: {up: NoSuchKey, down: Down, left: Left, right: Right}}\n")));
    CHECK(!C.Load(WriteTemp("bindings: [1, 2]\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: [X]}\n")));
    CHECK(!C.Load("/tmp/pj64-does-not-exist.yaml"));
    CHECK(!C.Load("/tmp/pj64-does-not-exist.yaml", true));   // quiet: same verdict, no print
    CHECK(LoadWasSilent(C, "/tmp/pj64-does-not-exist.yaml", true));   // ...and truly silent

    const char * BadZone = WriteTemp("bindings:\n  A: {zone: top4}\n");   // the grid names are gone
    CHECK(!C.Load(BadZone));
    CHECK(!LoadWasSilent(C, BadZone, false));   // ConfigError prints when not quiet...
    CHECK(LoadWasSilent(C, BadZone, true));     // ...and the same path stays silent when quiet

    CHECK(!C.Load(WriteTemp("bindings:\n  A: {zone: centre}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {zone: middle}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {face: wink}\n")));
    CHECK(C.Load(WriteTemp("bindings:\n  A: {face: wink-left}\n  B: {face: head-down}\n  Z: {face: tilt-right}\n")));
    CHECK(C.Bindings(N64Control::B)[0].code == POINTER_GESTURE_HEAD_DOWN);
    CHECK(C.Load(WriteTemp(Valid)));                  // back to the known good state
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {zone: game}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {face: eyebrows}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {stick: pointer}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {zone: game, face: eyebrows}\n")));

    CHECK(C.Bindings(N64Control::A).size() == ABefore);   // failed loads changed nothing
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_Y);

    // One-button forms: toggle slots (two controls may share one) and the stick's hold slot.
    {
        const char * OneButton =
            "bindings:\n"
            "  Stick: {stick: pointer, hold: mid5}\n"
            "  A: {zone: game}\n"
            "  Z: {zone: mid2, toggle: true}\n"
            "  R: {zone: mid2, toggle: true}\n"
            "  B: {zone: mid3, toggle: false}\n";
        CHECK(C.Load(WriteTemp(OneButton)));
        CHECK(C.Bindings(N64Control::Z)[0].Toggle);
        CHECK(C.Bindings(N64Control::R)[0].Toggle);
        CHECK(!C.Bindings(N64Control::A)[0].Toggle);
        CHECK(!C.Bindings(N64Control::B)[0].Toggle);         // toggle: false is the same as leaving it out
        CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Pointer);
        CHECK(C.Bindings(N64Control::Stick)[0].Hold == 12);
        CHECK(C.PointerHoldZone() == 12);
        CHECK(C.PointerToggleZones() == (1u << 9));
        CHECK(C.UsesPointer() && !C.UsesFace());
        char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE];
        char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE];
        C.PointerLabels(Labels, GestureLabels);
        CHECK(strcmp(Labels[12], "Ho") == 0);
        CHECK(strcmp(Labels[POINTER_ZONE_GAME], "A") == 0);
    }

    // Without the new keys nothing is a toggle and nothing holds, built-in bindings included.
    {
        CHECK(C.Load(WriteTemp("bindings:\n  Stick: {stick: pointer}\n  A: {zone: game}\n  Z: {zone: mid2}\n")));
        CHECK(!C.Bindings(N64Control::Z)[0].Toggle);
        CHECK(C.Bindings(N64Control::Stick)[0].Hold == POINTER_ZONE_NONE);
        CHECK(C.PointerHoldZone() == POINTER_ZONE_NONE);
        CHECK(C.PointerToggleZones() == 0u);
        CHECK(!C.Bindings(N64Control::B)[0].Toggle);
        CHECK(C.Bindings(N64Control::B)[0].Hold == POINTER_ZONE_NONE);
    }

    // Each one-button error is reported in its own words and changes nothing.
    {
        CHECK(C.Load(WriteTemp("bindings:\n  Z: {zone: mid4}\n")));
        CHECK(Contains(LoadError(C, WriteTemp("bindings:\n  Z: {zone: mid2, toggle: maybe}\n")),
                       "toggle must be true or false"));
        CHECK(Contains(LoadError(C, WriteTemp("bindings:\n  Z: {key: X, toggle: true}\n")),
                       "toggle only applies to {zone:}"));
        CHECK(Contains(LoadError(C, WriteTemp("bindings:\n  Stick: {stick: left, hold: mid5}\n")),
                       "hold only applies to {stick: pointer}"));
        CHECK(Contains(LoadError(C, WriteTemp("bindings:\n  Z: {zone: mid2, hold: mid5}\n")),
                       "hold only applies to {stick: pointer}"));
        CHECK(Contains(LoadError(C, WriteTemp("bindings:\n  Stick: {stick: pointer, hold: game}\n")),
                       "hold must name a panel slot"));
        CHECK(Contains(LoadError(C, WriteTemp("bindings:\n  Stick: {stick: pointer, hold: mid9}\n")),
                       "hold must name a panel slot"));
        CHECK(Contains(LoadError(C, WriteTemp(
                           "bindings:\n  Stick: {stick: pointer, hold: mid5}\n  Start: {zone: mid5}\n")),
                       "mid5 is the stick's hold slot and cannot also be bound"));
        CHECK(Contains(LoadError(C, WriteTemp(
                           "bindings:\n  Z: {zone: mid2, toggle: true}\n  R: {zone: mid2}\n")),
                       "mid2 is a toggle for Z but not for R"));
        CHECK(Contains(LoadError(C, WriteTemp(
                           "bindings:\n  R: {zone: mid2, toggle: true}\n  Z: {zone: mid2}\n")),
                       "mid2 is a toggle for R but not for Z"));
        CHECK(C.Bindings(N64Control::Z)[0].kind == Binding::Kind::Zone);
        CHECK(C.Bindings(N64Control::Z)[0].code == 11);      // still mid4 after every rejection
    }

    CHECK(C.Load("Config/input.yaml"));               // the tracked file must parse
    CHECK(C.Bindings(N64Control::A).size() == 1);
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Key);
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_X);

    CHECK(C.Load("Config/mouse/super_mario_64_usa.yaml"));   // every shipped mouse layout must parse
    CHECK(C.UsesPointer());
    CHECK(C.UsesFace());                              // this layout binds Z, B and R to gestures
    CHECK(C.Load("Config/mouse/goldeneye_007_u.yaml"));
    CHECK(C.UsesPointer());
    CHECK(C.UsesFace());                              // this layout binds R, CLeft and CRight
    CHECK(C.Load("Config/mouse/mario_kart_64_u.yaml"));
    CHECK(C.UsesPointer());
    CHECK(C.UsesFace());                              // this layout binds R, Z and B

    CHECK(C.Load("Config/face/super_mario_64_usa.yaml"));   // every shipped face layout must parse
    CHECK(C.UsesPointer() && C.UsesFace() && C.UsesHeadStick());
    CHECK(C.Bindings(N64Control::A)[0].code == POINTER_GESTURE_MOUTH_OPEN);
    CHECK(C.Bindings(N64Control::L).size() == 2);     // unbound: keeps keyboard and gamepad
    CHECK(C.Load("Config/face/mario_kart_64_u.yaml"));
    CHECK(C.UsesPointer() && C.UsesFace() && C.UsesHeadStick());
    CHECK(C.Bindings(N64Control::R)[0].code == POINTER_GESTURE_EYEBROWS);
    CHECK(C.Load("Config/mouse/super_mario_64_usa.yaml"));
    CHECK(!C.UsesHeadStick());                        // the mouse layouts have no head stick
    CHECK(C.Load("Config/mouse/goldeneye_007_u.yaml"));
    CHECK(!C.UsesHeadStick());
    CHECK(C.Load("Config/mouse/mario_kart_64_u.yaml"));
    CHECK(!C.UsesHeadStick());

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: input config\n");
    return 0;
}
