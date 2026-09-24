// Project64 - A Nintendo 64 emulator
// Tests for WizardDraft. No window and no renderer; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "WizardDraft.h"
#include <Project64-sdl/UnitTest.h>

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// The shipped layouts this test loads as bases live at the repository root, which is neither
// the test binary's own directory (build/macos/) nor necessarily the working directory: with
// a bare relative path, every LoadBase below failed for anyone who ran the binary from
// anywhere but the root, and the CHECKs that failed with it said nothing about why. Resolved
// the way Scripts/wizard_selftest.sh now resolves ROOT — from its own location — by walking
// up from SDL_GetBasePath(), and from the working directory too, so a run from the root still
// costs nothing. FindRoot's failure is a hard failure below, never a silent pass.
static std::string g_Root;

static bool HasLayouts(const std::string & Dir)
{
    FILE * F = fopen((Dir + "Config/face/super_mario_64_usa.yaml").c_str(), "r");
    if (F == nullptr) return false;
    fclose(F);
    return true;
}

static bool FindRoot()
{
    const char * Base = SDL_GetBasePath();
    const std::string Starts[2] = { std::string("./"), std::string(Base != nullptr ? Base : "") };
    for (int s = 0; s < 2; s++)
    {
        if (Starts[s].empty()) continue;
        std::string Dir = Starts[s];
        // Six levels is well past build/macos/ back to the root, and bounded so a checkout
        // without the layouts fails rather than walking to "/" one "../" at a time.
        for (int Up = 0; Up < 6; Up++)
        {
            if (HasLayouts(Dir)) { g_Root = Dir; return true; }
            Dir += "../";
        }
    }
    return false;
}

// A repository-relative path, against the root FindRoot located.
static std::string Layout(const char * Relative)
{
    return g_Root + Relative;
}

void RunWizardDraftTests()
{
    if (!FindRoot())
    {
        fprintf(stderr, "FAIL: cannot find Config/face/super_mario_64_usa.yaml from the "
                        "working directory or from the test binary's own directory\n");
        TestFailures()++;
        return;
    }

    // Control names are the reader's own spelling.
    CHECK(strcmp(WizardControlName(N64Control::A), "A") == 0);
    CHECK(strcmp(WizardControlName(N64Control::CUp), "CUp") == 0);
    CHECK(strcmp(WizardControlName(N64Control::DPadLeft), "DPadLeft") == 0);
    CHECK(strcmp(WizardControlName(N64Control::Stick), "Stick") == 0);

    // A fresh draft inherits everything, so it writes an empty map, and the reader
    // accepts it.
    {
        WizardDraft D;
        CHECK(!D.Explicit(N64Control::A));
        CHECK(TestHas(D.Emit("the built-in bindings"), "bindings: {}"));
        CHECK(D.Validate("the built-in bindings"));
        CHECK(strcmp(D.Error(), "") == 0);
    }

    // Each simple form emits its own syntax and survives the reader.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_X);
        D.SetButton(N64Control::B, SDL_GAMEPAD_BUTTON_SOUTH);
        D.SetAxis(N64Control::Z, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true);
        D.SetAxis(N64Control::R, SDL_GAMEPAD_AXIS_RIGHTY, false);
        D.SetZone(N64Control::Start, 8);
        D.SetGesture(N64Control::L, POINTER_GESTURE_MOUTH_OPEN);
        const std::string Text = D.Emit("the built-in bindings");
        CHECK(TestHas(Text, "A:         {key: X}"));
        CHECK(TestHas(Text, "B:         {button: a}"));
        CHECK(TestHas(Text, "Z:         {axis: lefttrigger, sign: +}"));
        CHECK(TestHas(Text, "R:         {axis: righty, sign: -}"));
        CHECK(TestHas(Text, "Start:     {zone: mid1}"));
        CHECK(TestHas(Text, "L:         {face: mouth-open}"));
        CHECK(D.Validate("the built-in bindings"));
        CHECK(D.Explicit(N64Control::A));
        CHECK(D.Bindings(N64Control::A).size() == 1);
        CHECK(D.Bindings(N64Control::A)[0].code == SDL_SCANCODE_X);
    }

    // A name with a space is quoted, and the reader still takes it.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_LSHIFT);
        CHECK(TestHas(D.Emit("x"), "{key: \"Left Shift\"}"));
        CHECK(D.Validate("x"));
    }

    // Clearing returns a control to inherited: it leaves the file and keeps whatever the
    // built-in bindings gave it, which for B is a pair the grammar could not write out.
    {
        WizardDraft D;
        const size_t BDefaults = D.Bindings(N64Control::B).size();
        CHECK(BDefaults == 2);
        D.SetKey(N64Control::B, SDL_SCANCODE_Q);
        CHECK(TestHas(D.Emit("x"), "B:"));
        D.Clear(N64Control::B);
        CHECK(!D.Explicit(N64Control::B));
        CHECK(D.Bindings(N64Control::B).size() == BDefaults);
        CHECK(!TestHas(D.Emit("x"), "\n  B:"));
    }

    // The header names the base, so a file says where it came from.
    {
        WizardDraft D;
        CHECK(TestHas(D.Emit("Config/mouse/goldeneye_007_u.yaml"),
                  "# Written by Project64-wizard from Config/mouse/goldeneye_007_u.yaml."));
    }

    // A rule the reader owns is reported in the reader's own words, not paraphrased.
    {
        WizardDraft D;
        D.SetGesture(N64Control::Z, POINTER_GESTURE_HEAD_UP);
        D.SetKey(N64Control::A, SDL_SCANCODE_X);
        CHECK(D.Validate("x"));            // head-up alone is fine
        CHECK(strcmp(D.Error(), "") == 0);
    }

    // A toggle and a hold survive a round trip and read as such on screen, and the draft
    // never writes a file the reader would reject when a control joins either slot.
    {
        const std::string Base = TestWriteTemp(
            "bindings:\n"
            "  Stick: {stick: pointer, hold: mid5}\n"
            "  A: {zone: game}\n"
            "  Z: {zone: mid2, toggle: true}\n");
        WizardDraft D;
        CHECK(D.LoadBase(Base.c_str()));
        CHECK(D.Bindings(N64Control::Z)[0].Toggle);
        CHECK(D.HoldZone() == 12);
        const std::string Text = D.Emit("x");
        CHECK(TestHas(Text, "Z:         {zone: mid2, toggle: true}\n"));
        CHECK(TestHas(Text, "Stick:     {stick: pointer, hold: mid5}\n"));
        CHECK(TestHas(Text, "A:         {zone: game}\n"));
        CHECK(D.Describe(N64Control::Z) == "zone mid2, toggle");
        CHECK(D.Describe(N64Control::Stick) == "stick pointer, hold mid5");
        CHECK(D.Validate("x"));

        D.SetZone(N64Control::Z, 9);                  // re-capturing its own slot keeps the toggle
        CHECK(D.Bindings(N64Control::Z)[0].Toggle);
        D.SetZone(N64Control::R, 9);                  // joining a toggle slot makes a toggle
        CHECK(D.Bindings(N64Control::R)[0].Toggle);
        CHECK(D.Validate("x"));
        D.SetZone(N64Control::B, 10);                 // an empty slot stays momentary
        CHECK(!D.Bindings(N64Control::B)[0].Toggle);

        D.SetZone(N64Control::Start, 12);             // taking the hold slot takes it off the stick
        CHECK(D.HoldZone() == POINTER_ZONE_NONE);
        CHECK(TestHas(D.Emit("x"), "Stick:     {stick: pointer}\n"));
        CHECK(D.Validate("x"));
        remove(Base.c_str());
    }

    // Choosing the pointer stick afresh starts with no hold.
    {
        WizardDraft D;
        D.SetStickPointer();
        CHECK(D.HoldZone() == POINTER_ZONE_NONE);
        CHECK(TestHas(D.Emit("x"), "Stick:     {stick: pointer}\n"));
    }

    // The Menu key round-trips after the controls, and a control taking its slot or its
    // gesture takes it off the menu, so the draft never writes a file the reader rejects.
    {
        const std::string Base = TestWriteTemp("bindings:\n  Menu: {zone: pad-down}\n  A: {zone: game}\n");
        WizardDraft D;
        CHECK(D.LoadBase(Base.c_str()));
        CHECK(D.MenuZone() == 1);
        CHECK(TestHas(D.Emit("x"), "  A:         {zone: game}\n  Menu:      {zone: pad-down}\n"));
        CHECK(D.Validate("x"));
        D.SetZone(N64Control::DPadDown, 1);
        CHECK(D.MenuZone() == POINTER_ZONE_NONE);
        CHECK(!TestHas(D.Emit("x"), "Menu:"));
        CHECK(D.Validate("x"));
        remove(Base.c_str());
    }
    {
        const std::string Base = TestWriteTemp("bindings:\n  Menu: {face: tilt-right}\n");
        WizardDraft D;
        CHECK(D.LoadBase(Base.c_str()));
        CHECK(TestHas(D.Emit("x"), "bindings:\n  Menu:      {face: tilt-right}\n"));
        D.SetGesture(N64Control::Start, POINTER_GESTURE_TILT_RIGHT);
        CHECK(!TestHas(D.Emit("x"), "Menu:"));
        CHECK(D.Validate("x"));
        D.LoadBase(Base.c_str());
        D.LoadDefaults();
        CHECK(!TestHas(D.Emit("x"), "Menu:"));
        remove(Base.c_str());
    }

    // The five shipped layouts are offered as bases, by label and by path.
    CHECK(WizardBaseCount() == 5);
    CHECK(strcmp(WizardBaseFile(0), "Config/mouse/super_mario_64_usa.yaml") == 0);
    CHECK(strcmp(WizardBaseFile(4), "Config/face/mario_kart_64_u.yaml") == 0);
    CHECK(strcmp(WizardBaseLabel(0), "Mouse: Super Mario 64") == 0);
    CHECK(strcmp(WizardBaseFile(-1), "") == 0);
    CHECK(strcmp(WizardBaseFile(5), "") == 0);

    // A base marks exactly the controls it named, and re-emits to something the reader
    // still accepts. The SM64 face layout names eight controls and inherits the rest.
    {
        WizardDraft D;
        CHECK(D.LoadBase(Layout("Config/face/super_mario_64_usa.yaml").c_str()));
        CHECK(D.Explicit(N64Control::Stick));
        CHECK(D.Explicit(N64Control::A));
        CHECK(D.Explicit(N64Control::CRight));
        CHECK(!D.Explicit(N64Control::DPadUp));      // not named: inherited
        CHECK(D.Bindings(N64Control::A)[0].kind == Binding::Kind::Face);
        CHECK(D.Bindings(N64Control::A)[0].code == (int)POINTER_GESTURE_MOUTH_OPEN);
        const std::string Text = D.Emit("Config/face/super_mario_64_usa.yaml");
        CHECK(TestHas(Text, "Stick:     {stick: head}"));
        CHECK(TestHas(Text, "A:         {face: mouth-open}"));
        CHECK(!TestHas(Text, "\n  DPadUp:"));
    }

    // Every shipped layout survives the same round trip.
    for (int i = 0; i < WizardBaseCount(); i++)
    {
        WizardDraft D;
        CHECK(D.LoadBase(Layout(WizardBaseFile(i)).c_str()));
        const bool Valid = D.Validate(WizardBaseFile(i));
        CHECK(Valid);
        if (!Valid) fprintf(stderr, "  base %s: %s\n", WizardBaseFile(i), D.Error());
    }

    // A mouse layout's zones, its toggle and its hold come back as they were.
    {
        WizardDraft D;
        CHECK(D.LoadBase(Layout("Config/mouse/super_mario_64_usa.yaml").c_str()));
        CHECK(D.Explicit(N64Control::Stick));
        CHECK(D.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Pointer);
        const std::string Text = D.Emit("x");
        CHECK(TestHas(Text, "Stick:     {stick: pointer, hold: mid5}\n"));
        CHECK(TestHas(Text, "Z:         {zone: mid2, toggle: true}\n"));
        CHECK(!TestHas(Text, "\n  L:"));                                   // L is left out
    }

    // A base that does not exist, or that the reader rejects, leaves the draft alone and
    // reports why.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_X);
        CHECK(!D.LoadBase("/tmp/pj64-wizard-no-such-file.yaml"));
        CHECK(strcmp(D.Error(), "") != 0);
        CHECK(D.Explicit(N64Control::A));                       // untouched
        CHECK(D.Bindings(N64Control::A)[0].code == SDL_SCANCODE_X);
    }

    // The head-direction rule belongs to the reader, and the wizard surfaces its words.
    // Validate rejects through its own temp file (WizardDraft.cpp:461-473 strips that
    // file's own path back out of the reader's message before returning it) — asserted here
    // since the substring check above passes whether or not the stripping actually happened.
    {
        WizardDraft D;
        CHECK(D.LoadBase(Layout("Config/face/super_mario_64_usa.yaml").c_str()));
        D.SetGesture(N64Control::DPadUp, POINTER_GESTURE_HEAD_UP);
        CHECK(!D.Validate("x"));
        CHECK(strstr(D.Error(), "head-up cannot be bound while Stick is head") != NULL);
        CHECK(strstr(D.Error(), "/tmp/pj64-wizard") == NULL);
    }

    // Each Stick form emits its own syntax and survives the reader.
    {
        WizardDraft D;
        D.SetStickWhole(false);
        CHECK(TestHas(D.Emit("x"), "Stick:     {stick: left}"));
        CHECK(D.Validate("x"));
        D.SetStickWhole(true);
        CHECK(TestHas(D.Emit("x"), "Stick:     {stick: right}"));
        CHECK(D.Validate("x"));
        D.SetStickPointer();
        CHECK(TestHas(D.Emit("x"), "Stick:     {stick: pointer}"));
        CHECK(D.Validate("x"));
        D.SetStickHead(false);
        CHECK(TestHas(D.Emit("x"), "Stick:     {stick: head}"));
        CHECK(D.Validate("x"));
        D.SetStickHead(true);
        CHECK(TestHas(D.Emit("x"), "Stick:     {stick: head-digital}"));
        CHECK(D.Validate("x"));
        D.SetStickKeys(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
        CHECK(TestHas(D.Emit("x"), "Stick:     {keys: {up: Up, down: Down, left: Left, right: Right}}"));
        CHECK(D.Validate("x"));
    }

    // A head stick beside a head-direction gesture is the reader's to reject, and the
    // wizard reports the reader's line.
    {
        WizardDraft D;
        D.SetStickHead(true);
        D.SetGesture(N64Control::A, POINTER_GESTURE_HEAD_LEFT);
        CHECK(!D.Validate("x"));
        CHECK(strstr(D.Error(), "head-left cannot be bound while Stick is head") != NULL);
        CHECK(strstr(D.Error(), "/tmp/pj64-wizard") == NULL);
    }

    // Save's own errors (a destination the reader never sees, because they happen after
    // Validate already accepted the draft) never go through Validate's temp-path stripping
    // at all, and so must come back exactly as Save set them.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_X);
        const char * const kNoSuchDir = "/pj64-wizard-test-no-such-directory-3f7a91/out.yaml";
        CHECK(!D.Save(kNoSuchDir, "x"));
        std::string Expected = "could not write ";
        Expected += kNoSuchDir;
        CHECK(D.Error() == Expected);
        CHECK(strstr(D.Error(), "/tmp/pj64-wizard") == NULL);
    }

    // Descriptions read as English, and an inherited control says so.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_X);
        CHECK(D.Describe(N64Control::A) == "key X");
        D.SetButton(N64Control::B, SDL_GAMEPAD_BUTTON_SOUTH);
        CHECK(D.Describe(N64Control::B) == "button a");
        D.SetAxis(N64Control::Z, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true);
        CHECK(D.Describe(N64Control::Z) == "axis lefttrigger +");
        D.SetZone(N64Control::Start, 8);
        CHECK(D.Describe(N64Control::Start) == "zone mid1");
        D.SetGesture(N64Control::L, POINTER_GESTURE_MOUTH_OPEN);
        CHECK(D.Describe(N64Control::L) == "gesture mouth-open (Mo)");
        D.SetStickHead(true);
        CHECK(D.Describe(N64Control::Stick) == "stick head-digital");
        D.SetStickKeys(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
        CHECK(D.Describe(N64Control::Stick) == "keys Up/Down/Left/Right");
        D.Clear(N64Control::A);
        CHECK(D.Describe(N64Control::A).compare(0, 11, "inherited: ") == 0);
    }

    // The Stick chooser's six rows.
    CHECK(WizardStickFormCount() == 6);
    CHECK(strcmp(WizardStickFormLabel(0), "gamepad left stick") == 0);
    CHECK(strcmp(WizardStickFormLabel(5), "four keyboard keys") == 0);
    CHECK(strcmp(WizardStickFormLabel(6), "") == 0);

}
