// Project64 - A Nintendo 64 emulator
// Tests for InputConfig::Load. No window and no SDL init; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "InputConfig.h"

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

    CHECK(C.Load(WriteTemp(Valid)));                  // establish a known good state
    const size_t ABefore = C.Bindings(N64Control::A).size();

    CHECK(!C.Load(WriteTemp("bindings:\n  Nope: {key: X}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: NoSuchKey}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {button: sout}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: X}\n  A: {key: Y}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: X, button: a}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {stick: middle}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {stick: left}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {keys: {up: NoSuchKey, down: Down, left: Left, right: Right}}\n")));
    CHECK(!C.Load(WriteTemp("bindings: [1, 2]\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: [X]}\n")));
    CHECK(!C.Load("/tmp/pj64-does-not-exist.yaml"));

    CHECK(C.Bindings(N64Control::A).size() == ABefore);   // failed loads changed nothing
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_Y);

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: input config\n");
    return 0;
}
