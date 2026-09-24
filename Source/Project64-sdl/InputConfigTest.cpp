// Project64 - A Nintendo 64 emulator
// Tests for InputConfig::Load. No window and no SDL init; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "InputConfig.h"
#include "UnitTest.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>

// Runs Run with stderr redirected to a scratch file and returns the first line it wrote,
// "" when it wrote nothing, so a test can check the reader's own words. A quiet Load's
// return value is false either way, so this is also the only way to prove the Quiet guards
// actually suppress output rather than merely returning false.
// Redirects the fd underneath stderr with dup2, not freopen: freopen would reassociate
// the stderr FILE object with a regular file and leave it fully buffered even after the
// fd is restored, reordering every fprintf(stderr, ...) after the first call.
template <class Fn>
static std::string FirstStderrLine(Fn Run)
{
    char ScratchPath[64];
    snprintf(ScratchPath, sizeof(ScratchPath), "/tmp/pj64-stderr-XXXXXX");
    int Fd = mkstemp(ScratchPath);
    if (Fd < 0) { perror("mkstemp"); exit(2); }

    fflush(stderr);
    int SavedStderr = dup(fileno(stderr));
    dup2(Fd, fileno(stderr));
    close(Fd);

    Run();

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

static std::string LoadStderr(InputConfig & C, const char * Path, bool Quiet = false)
{
    return FirstStderrLine([&] { C.Load(Path, Quiet); });
}

static bool LoadWasSilent(InputConfig & C, const char * Path, bool Quiet)
{
    return LoadStderr(C, Path, Quiet).empty();
}

void RunInputConfigTests()
{
    InputConfig & C = InputConfig::Get();

    const char * Valid =
        "bindings:\n"
        "  A: {key: Y}\n"
        "  Start: {key: Return}\n";
    CHECK(C.Load(TestWriteTemp(Valid)));
    CHECK(C.Bindings(N64Control::A).size() == 1);
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Key);
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_Y);
    CHECK(C.Bindings(N64Control::B).size() == 2);     // omitted control keeps its default pair

    const char * CommentsOnly = "# nothing here\n";
    CHECK(C.Load(TestWriteTemp(CommentsOnly)));
    CHECK(C.Bindings(N64Control::A).size() == 2);     // defaults restored

    const char * Gamepad =
        "bindings:\n"
        "  A: {button: a}\n"
        "  Z: {axis: lefttrigger, sign: +}\n"
        "  CUp: {axis: righty, sign: -}\n"
        "  Stick: {stick: left}\n";
    CHECK(C.Load(TestWriteTemp(Gamepad)));
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Button);
    CHECK(C.Bindings(N64Control::Z)[0].kind == Binding::Kind::Axis);
    CHECK(C.Bindings(N64Control::CUp)[0].positive == false);
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Stick);

    const char * DigitalStick =
        "bindings:\n"
        "  Stick: {keys: {up: Up, down: Down, left: Left, right: Right}}\n";
    CHECK(C.Load(TestWriteTemp(DigitalStick)));
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Keys);
    CHECK(C.Bindings(N64Control::Stick)[0].UpKey == SDL_SCANCODE_UP);

    const char * Pointer =
        "bindings:\n"
        "  Stick: {stick: pointer}\n"
        "  A: {zone: game}\n"
        "  Start: {zone: mid1}\n"
        "  Z: {face: eyebrows}\n"
        "  B: {face: head-left}\n";
    CHECK(C.Load(TestWriteTemp(Pointer)));
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

    CHECK(C.Load(TestWriteTemp(Valid)));
    CHECK(!C.UsesPointer());                          // keyboard-only file: no overlay
    CHECK(!C.UsesFace());

    const char * ZonesOnly =
        "bindings:\n"
        "  Stick: {stick: pointer}\n"
        "  A: {zone: game}\n";
    CHECK(C.Load(TestWriteTemp(ZonesOnly)));
    CHECK(C.UsesPointer());
    CHECK(!C.UsesFace());                             // pointer without gestures: no camera

    const char * Slots =
        "bindings:\n"
        "  DPadUp: {zone: pad-up}\n"
        "  CRight: {zone: c-right}\n"
        "  L: {zone: mid5}\n";
    CHECK(C.Load(TestWriteTemp(Slots)));
    CHECK(C.Bindings(N64Control::DPadUp)[0].code == 0);
    CHECK(C.Bindings(N64Control::CRight)[0].code == 7);
    CHECK(C.Bindings(N64Control::L)[0].code == 12);
    CHECK(C.UsesPointer());                           // slots alone turn the overlay on

    const char * FaceOnlyOk =
        "bindings:\n"
        "  Stick: {stick: head}\n"
        "  A: {face: mouth-open}\n"
        "  B: {face: smile}\n"
        "  CLeft: {face: wink-left}\n"
        "  CRight: {face: wink-right}\n"
        "  R: {face: tilt-left}\n"
        "  Start: {face: tilt-right}\n";
    CHECK(C.Load(TestWriteTemp(FaceOnlyOk)));
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

    CHECK(C.Load(TestWriteTemp("bindings:\n  Stick: {stick: head-digital}\n  Z: {face: eyebrows}\n")));
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::HeadStick);
    CHECK(C.Bindings(N64Control::Stick)[0].code == 1);
    CHECK(C.UsesHeadStick());

    // A pointer stick beside head-left is fine: only a head stick consumes the head turns.
    CHECK(C.Load(TestWriteTemp("bindings:\n  Stick: {stick: pointer}\n  B: {face: head-left}\n")));
    CHECK(!C.UsesHeadStick());
    // The rule holds in either order and for either head-stick form.
    CHECK(!C.Load(TestWriteTemp("bindings:\n  B: {face: head-down}\n  Stick: {stick: head}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  Stick: {stick: head-digital}\n  B: {face: head-right}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {stick: head}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  Stick: {stick: head-analog}\n")));

    CHECK(C.Load(TestWriteTemp(Valid)));                  // establish a known good state
    const size_t ABefore = C.Bindings(N64Control::A).size();

    CHECK(!C.Load(TestWriteTemp("bindings:\n  Nope: {key: X}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {key: NoSuchKey}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {button: sout}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {key: X}\n  A: {key: Y}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {key: X, button: a}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  Stick: {stick: middle}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {stick: left}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {keys: {up: Up}}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  Stick: {keys: {up: NoSuchKey, down: Down, left: Left, right: Right}}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings: [1, 2]\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {key: [X]}\n")));
    CHECK(!C.Load("/tmp/pj64-does-not-exist.yaml"));
    CHECK(!C.Load("/tmp/pj64-does-not-exist.yaml", true));   // quiet: same verdict, no print
    CHECK(LoadWasSilent(C, "/tmp/pj64-does-not-exist.yaml", true));   // ...and truly silent

    const char * BadZone = TestWriteTemp("bindings:\n  A: {zone: top4}\n");   // the grid names are gone
    CHECK(!C.Load(BadZone));
    CHECK(!LoadWasSilent(C, BadZone, false));   // ConfigError prints when not quiet...
    CHECK(LoadWasSilent(C, BadZone, true));     // ...and the same path stays silent when quiet

    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {zone: centre}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {zone: middle}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {face: wink}\n")));
    CHECK(C.Load(TestWriteTemp("bindings:\n  A: {face: wink-left}\n  B: {face: head-down}\n  Z: {face: tilt-right}\n")));
    CHECK(C.Bindings(N64Control::B)[0].code == POINTER_GESTURE_HEAD_DOWN);
    CHECK(C.Load(TestWriteTemp(Valid)));                  // back to the known good state
    CHECK(!C.Load(TestWriteTemp("bindings:\n  Stick: {zone: game}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  Stick: {face: eyebrows}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {stick: pointer}\n")));
    CHECK(!C.Load(TestWriteTemp("bindings:\n  A: {zone: game, face: eyebrows}\n")));

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
        CHECK(C.Load(TestWriteTemp(OneButton)));
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
        CHECK(C.Load(TestWriteTemp("bindings:\n  Stick: {stick: pointer}\n  A: {zone: game}\n  Z: {zone: mid2}\n")));
        CHECK(!C.Bindings(N64Control::Z)[0].Toggle);
        CHECK(C.Bindings(N64Control::Stick)[0].Hold == POINTER_ZONE_NONE);
        CHECK(C.PointerHoldZone() == POINTER_ZONE_NONE);
        CHECK(C.PointerToggleZones() == 0u);
        CHECK(!C.Bindings(N64Control::B)[0].Toggle);
        CHECK(C.Bindings(N64Control::B)[0].Hold == POINTER_ZONE_NONE);
    }

    // Each one-button error is reported in its own words and changes nothing.
    {
        CHECK(C.Load(TestWriteTemp("bindings:\n  Z: {zone: mid4}\n")));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Z: {zone: mid2, toggle: maybe}\n")),
                       "toggle must be true or false"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Z: {key: X, toggle: true}\n")),
                       "toggle only applies to {zone:}"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Stick: {stick: left, hold: mid5}\n")),
                       "hold only applies to {stick: pointer}"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Z: {zone: mid2, hold: mid5}\n")),
                       "hold only applies to {stick: pointer}"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Stick: {stick: pointer, hold: game}\n")),
                       "hold must name a panel slot"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Stick: {stick: pointer, hold: mid9}\n")),
                       "hold must name a panel slot"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp(
                           "bindings:\n  Stick: {stick: pointer, hold: mid5}\n  Start: {zone: mid5}\n")),
                       "mid5 is the stick's hold slot and cannot also be bound"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp(
                           "bindings:\n  Z: {zone: mid2, toggle: true}\n  R: {zone: mid2}\n")),
                       "mid2 is a toggle for Z but not for R"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp(
                           "bindings:\n  R: {zone: mid2, toggle: true}\n  Z: {zone: mid2}\n")),
                       "mid2 is a toggle for R but not for Z"));
        CHECK(C.Bindings(N64Control::Z)[0].kind == Binding::Kind::Zone);
        CHECK(C.Bindings(N64Control::Z)[0].code == 11);      // still mid4 after every rejection
    }

    // The Menu key: a slot or a gesture, never a control.
    {
        char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE];
        char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE];
        CHECK(C.Load(TestWriteTemp("bindings:\n  Menu: {zone: pad-down}\n  A: {zone: game}\n")));
        CHECK(C.MenuZone() == 1 && C.MenuGesture() == 0 && C.MenuBinding().size() == 1);
        CHECK(C.UsesPointer() && !C.UsesFace());
        C.PointerLabels(Labels, GestureLabels);
        CHECK(strcmp(Labels[1], "==") == 0);
        CHECK(C.Bindings(N64Control::DPadDown)[0].kind != Binding::Kind::Zone);   // the menu presses nothing
        CHECK(C.Load(TestWriteTemp("bindings:\n  Menu: {face: tilt-right}\n")));
        CHECK(C.MenuZone() == POINTER_ZONE_NONE && C.MenuGesture() == POINTER_GESTURE_TILT_RIGHT);
        CHECK(C.UsesPointer() && C.UsesFace());
        C.PointerLabels(Labels, GestureLabels);
        CHECK(strcmp(GestureLabels[PointerGestureIndex(POINTER_GESTURE_TILT_RIGHT)], "==") == 0);
        CHECK(C.Load(TestWriteTemp("bindings:\n  A: {key: X}\n")));
        CHECK(C.MenuBinding().empty() && C.MenuZone() == POINTER_ZONE_NONE && C.MenuGesture() == 0);
        CHECK(!C.UsesPointer());
    }

    // Each menu error in its own words; a rejected file changes nothing.
    {
        CHECK(C.Load(TestWriteTemp("bindings:\n  Menu: {zone: pad-down}\n")));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Menu: {key: X}\n")),
                      "menu must be {zone:} or {face:}"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Menu: {zone: mid1, toggle: true}\n")),
                      "menu must be {zone:} or {face:}"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Menu: {zone: game}\n")),
                      "the menu cannot be game"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Menu: {zone: mid1}\n  Start: {zone: mid1}\n")),
                      "mid1 is the menu slot and cannot also be bound"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Start: {zone: mid1}\n  Menu: {zone: mid1}\n")),
                      "mid1 is the menu slot and cannot also be bound"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Stick: {stick: pointer, hold: mid5}\n  Menu: {zone: mid5}\n")),
                      "mid5 is the menu slot and cannot also be bound"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Menu: {face: smile}\n  B: {face: smile}\n")),
                      "smile is the menu's gesture and cannot also be bound"));
        CHECK(TestHas(LoadStderr(C, TestWriteTemp("bindings:\n  Stick: {stick: head}\n  Menu: {face: head-up}\n")),
                      "head-up cannot be bound while Stick is head"));
        CHECK(C.MenuZone() == 1);                         // still pad-down after every rejection
    }

    CHECK(C.Load("Config/input.yaml"));               // the tracked file must parse
    CHECK(C.Bindings(N64Control::A).size() == 1);
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Key);
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_X);

    // Every shipped mouse layout parses, plays with one button, never starts the camera, and
    // has no head stick.
    CHECK(C.Load("Config/mouse/super_mario_64_usa.yaml"));
    CHECK(C.UsesPointer() && !C.UsesFace() && !C.UsesHeadStick());
    CHECK(C.PointerHoldZone() == 12);                                  // mid5
    CHECK(C.PointerToggleZones() == (1u << 9));                        // Z on mid2
    CHECK(C.MenuZone() == 1);   // pad-down
    CHECK(C.Load("Config/mouse/goldeneye_007_u.yaml"));
    CHECK(C.UsesPointer() && !C.UsesFace() && !C.UsesHeadStick());
    CHECK(C.PointerHoldZone() == 12);
    CHECK(C.PointerToggleZones() == (1u << 11));                       // R on mid4
    CHECK(C.MenuZone() == 1);   // pad-down
    CHECK(C.Load("Config/mouse/mario_kart_64_u.yaml"));
    CHECK(C.UsesPointer() && !C.UsesFace() && !C.UsesHeadStick());
    CHECK(C.PointerHoldZone() == 12);
    CHECK(C.PointerToggleZones() == ((1u << POINTER_ZONE_GAME) | (1u << 9)));   // A on the picture, R on mid2
    CHECK(C.MenuZone() == 1);   // pad-down

    CHECK(C.Load("Config/face/super_mario_64_usa.yaml"));   // every shipped face layout must parse
    CHECK(C.UsesPointer() && C.UsesFace() && C.UsesHeadStick());
    CHECK(C.Bindings(N64Control::A)[0].code == POINTER_GESTURE_MOUTH_OPEN);
    CHECK(C.Bindings(N64Control::L).size() == 2);     // unbound: keeps keyboard and gamepad
    CHECK(C.Load("Config/face/mario_kart_64_u.yaml"));
    CHECK(C.UsesPointer() && C.UsesFace() && C.UsesHeadStick());
    CHECK(C.Bindings(N64Control::R)[0].code == POINTER_GESTURE_EYEBROWS);

    // The launcher's added menu (PJ64_MENU_AUTO, Docs/superpowers/specs/2026-09-24-launcher-design.md):
    // the first free middle slot from mid5 down, the stick's hold counting as used; pad-down,
    // taken from its control, when every slot is used; nothing when the layout already has a
    // menu or has no panel.
    {
        const int Mid5 = PointerZoneFromName("mid5");
        CHECK(C.Load(TestWriteTemp("bindings:\n  Stick: {stick: pointer}\n  A: {zone: game}\n")));
        CHECK(C.ApplyAutoMenu(true) == Mid5);
        CHECK(C.MenuZone() == Mid5);
        CHECK(C.ApplyAutoMenu(true) == POINTER_ZONE_NONE);                  // it has one now
        CHECK(C.MenuZone() == Mid5);

        CHECK(C.Load(TestWriteTemp("bindings:\n  Stick: {stick: pointer, hold: mid5}\n  B: {zone: mid4}\n  Z: {zone: mid3, toggle: true}\n")));
        CHECK(C.ApplyAutoMenu(true) == PointerZoneFromName("mid2"));

        CHECK(C.Load(TestWriteTemp("bindings:\n  Menu: {zone: c-up}\n  A: {zone: game}\n")));
        CHECK(C.ApplyAutoMenu(true) == POINTER_ZONE_NONE);
        CHECK(C.MenuZone() == PointerZoneFromName("c-up"));

        CHECK(C.Load(TestWriteTemp("bindings:\n  A: {key: X}\n")));             // no panel
        CHECK(C.ApplyAutoMenu(true) == POINTER_ZONE_NONE);
        CHECK(C.MenuZone() == POINTER_ZONE_NONE);

        const char * Full =
            "bindings:\n"
            "  A: {zone: pad-up}\n  B: {zone: pad-down}\n  Z: {zone: pad-left}\n  Start: {zone: pad-right}\n"
            "  CUp: {zone: c-up}\n  CDown: {zone: c-down}\n  CLeft: {zone: c-left}\n  CRight: {zone: c-right}\n"
            "  L: {zone: mid1}\n  R: {zone: mid2}\n  DPadUp: {zone: mid3}\n  DPadDown: {zone: mid4}\n"
            "  DPadLeft: {zone: mid5}\n";
        CHECK(C.Load(TestWriteTemp(Full)));
        CHECK(FirstStderrLine([&] { C.ApplyAutoMenu(false); }) == "menu: took pad-down from B\n");
        CHECK(C.MenuZone() == PointerZoneFromName("pad-down"));
        CHECK(C.Bindings(N64Control::B).empty());

        // pad-down is the stick's hold slot here, so the fallback must not share it
        // (CheckSlots forbids a file from doing that): pad-up is next instead.
        const char * FullHeldDown =
            "bindings:\n"
            "  Stick: {stick: pointer, hold: pad-down}\n"
            "  A: {zone: pad-up}\n"
            "  B: {zone: mid1}\n  Z: {zone: mid2}\n  Start: {zone: mid3}\n"
            "  CUp: {zone: mid4}\n  CDown: {zone: mid5}\n";
        CHECK(C.Load(TestWriteTemp(FullHeldDown)));
        CHECK(FirstStderrLine([&] { C.ApplyAutoMenu(false); }) == "menu: took pad-up from A\n");
        CHECK(C.MenuZone() == PointerZoneFromName("pad-up"));
        CHECK(C.PointerHoldZone() == PointerZoneFromName("pad-down"));
        CHECK(C.Bindings(N64Control::A).empty());

        // The order itself, as play and the editor share it.
        bool Used[POINTER_ZONE_COUNT] = { false };
        CHECK(AutoMenuSlot(Used, POINTER_ZONE_NONE) == PointerZoneFromName("mid5"));
        Used[PointerZoneFromName("mid5")] = true;
        Used[PointerZoneFromName("mid4")] = true;
        CHECK(AutoMenuSlot(Used, POINTER_ZONE_NONE) == PointerZoneFromName("mid3"));
        for (const char * Name : { "mid3", "mid2", "mid1" }) Used[PointerZoneFromName(Name)] = true;
        CHECK(AutoMenuSlot(Used, POINTER_ZONE_NONE) == PointerZoneFromName("pad-down"));
        CHECK(AutoMenuSlot(Used, PointerZoneFromName("pad-down")) == PointerZoneFromName("pad-up"));

        CHECK(C.Load(TestWriteTemp("bindings:\n  Stick: {stick: pointer}\n")));
        CHECK(FirstStderrLine([&] { C.ApplyAutoMenu(false); }) == "menu: added on mid5\n");
        CHECK(C.Load(TestWriteTemp("bindings:\n  Stick: {stick: pointer}\n")));
        CHECK(FirstStderrLine([&] { C.ApplyAutoMenu(true); }).empty());

        // Only exactly "1" asks for it. The caller's value is put back afterwards.
        const char * Before = getenv("PJ64_MENU_AUTO");
        const std::string Saved = Before != nullptr ? Before : "";
        unsetenv("PJ64_MENU_AUTO");
        CHECK(!InputConfig::AutoMenuWanted());
        setenv("PJ64_MENU_AUTO", "0", 1);
        CHECK(!InputConfig::AutoMenuWanted());
        setenv("PJ64_MENU_AUTO", "1", 1);
        CHECK(InputConfig::AutoMenuWanted());
        if (Before != nullptr) setenv("PJ64_MENU_AUTO", Saved.c_str(), 1);
        else unsetenv("PJ64_MENU_AUTO");
    }

    // The launcher's generic layout: one button, no camera, its own menu on pad-down.
    CHECK(C.Load("Config/mouse/default.yaml"));
    CHECK(C.UsesPointer() && !C.UsesFace() && !C.UsesHeadStick());
    CHECK(C.PointerHoldZone() == PointerZoneFromName("mid5"));
    CHECK(C.PointerToggleZones() == (1u << PointerZoneFromName("mid2")));   // Z
    CHECK(C.MenuZone() == PointerZoneFromName("pad-down"));
    CHECK(C.Bindings(N64Control::L)[0].code == PointerZoneFromName("pad-up"));
    CHECK(C.Bindings(N64Control::DPadUp).size() == 2);                 // unbound: keeps key and gamepad
    CHECK(C.ApplyAutoMenu(true) == POINTER_ZONE_NONE);
}
