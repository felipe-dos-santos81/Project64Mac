// Project64 - A Nintendo 64 emulator
// Tests for InputConfig::Load. No window and no SDL init; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "InputConfig.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        "  A: {zone: centre}\n"
        "  Start: {zone: top4}\n"
        "  Z: {face: eyebrows}\n"
        "  B: {face: head-left}\n";
    CHECK(C.Load(WriteTemp(Pointer)));
    CHECK(C.UsesPointer());
    CHECK(C.UsesFace());                              // Z and B are gestures
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Pointer);
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Zone);
    CHECK(C.Bindings(N64Control::A)[0].code == POINTER_ZONE_CENTRE);
    CHECK(C.Bindings(N64Control::Start)[0].code == 3);
    CHECK(C.Bindings(N64Control::Z)[0].kind == Binding::Kind::Face);
    CHECK(C.Bindings(N64Control::Z)[0].code == POINTER_GESTURE_EYEBROWS);
    CHECK(C.Bindings(N64Control::B)[0].code == POINTER_GESTURE_HEAD_LEFT);
    char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE];
    char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE];
    C.PointerLabels(Labels, GestureLabels);
    CHECK(strcmp(Labels[POINTER_ZONE_CENTRE], "A") == 0);
    CHECK(strcmp(Labels[3], "St") == 0);
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
        "  A: {zone: centre}\n";
    CHECK(C.Load(WriteTemp(ZonesOnly)));
    CHECK(C.UsesPointer());
    CHECK(!C.UsesFace());                             // pointer without gestures: no camera

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
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {zone: middle}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {face: wink}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {zone: centre}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {face: eyebrows}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {stick: pointer}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {zone: centre, face: eyebrows}\n")));

    CHECK(C.Bindings(N64Control::A).size() == ABefore);   // failed loads changed nothing
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_Y);

    CHECK(C.Load("Config/input.yaml"));               // the tracked file must parse
    CHECK(C.Bindings(N64Control::A).size() == 1);
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Key);
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_X);

    CHECK(C.Load("Config/mouse/super_mario_64_usa.yaml"));   // every shipped mouse layout must parse
    CHECK(C.UsesPointer());
    CHECK(C.UsesFace());                              // this layout binds Z, B and R to gestures
    CHECK(C.Load("Config/mouse/goldeneye_007_u.yaml"));
    CHECK(C.Load("Config/mouse/mario_kart_64_u.yaml"));

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: input config\n");
    return 0;
}
