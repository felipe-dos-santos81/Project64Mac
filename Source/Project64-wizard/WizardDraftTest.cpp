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

static int Slot(const char * Name) { return PointerZoneFromName(Name); }

static EditPlace At(const char * Name) { EditPlace P; P.Index = Slot(Name); return P; }

static EditPlace OnGesture(uint32_t Bit) { EditPlace P; P.Gesture = true; P.Index = PointerGestureIndex(Bit); return P; }

static bool Holds(const WizardDraft & D, EditPlace P, N64Control C)
{
    const std::vector<N64Control> Here = D.Occupants(P);
    return Here.size() == 1 && Here[0] == C;
}

// The generic layout's shape, built through the editing rules themselves.
static WizardDraft PanelDraft()
{
    WizardDraft D;
    std::string N;
    EditPlace Picture;
    Picture.Index = POINTER_ZONE_GAME;
    CHECK(D.SetStickForm(EditStick::Pointer, &N));
    CHECK(D.PlaceControl(Picture, N64Control::A, &N));
    CHECK(D.PlaceControl(At("mid1"), N64Control::Start, &N));
    CHECK(D.PlaceControl(At("mid2"), N64Control::Z, &N));
    CHECK(D.SetToggle(Slot("mid2"), true, &N));
    CHECK(D.PlaceControl(At("mid3"), N64Control::B, &N));
    CHECK(D.PlaceControl(At("mid4"), N64Control::R, &N));
    CHECK(D.PlaceHold(Slot("mid5"), &N));
    CHECK(D.PlaceControl(At("pad-up"), N64Control::L, &N));
    CHECK(D.PlaceControl(At("c-up"), N64Control::CUp, &N));
    CHECK(D.PlaceControl(At("c-down"), N64Control::CDown, &N));
    CHECK(D.PlaceControl(At("c-left"), N64Control::CLeft, &N));
    CHECK(D.PlaceControl(At("c-right"), N64Control::CRight, &N));
    CHECK(D.PlaceControl(At("pad-left"), N64Control::DPadLeft, &N));
    CHECK(D.PlaceControl(At("pad-right"), N64Control::DPadRight, &N));
    CHECK(D.PlaceMenu(Slot("pad-down"), &N));
    return D;
}

static void PanelEditing()
{
    std::string N;
    EditPlace Picture;
    Picture.Index = POINTER_ZONE_GAME;

    // The builder itself: a layout the reader accepts, with every control but D^ and Dv placed.
    WizardDraft D = PanelDraft();
    CHECK(D.Validate("x"));
    CHECK(D.NotPlaced().size() == 2);
    CHECK(D.StickForm() == EditStick::Pointer && D.HoldZone() == Slot("mid5") && D.MenuZone() == Slot("pad-down"));

    // Placing a control moves it and displaces the target's occupant.
    CHECK(D.PlaceControl(At("mid4"), N64Control::L, &N));
    CHECK(N == "R is not placed now");
    CHECK(Holds(D, At("mid4"), N64Control::L) && D.Occupants(At("pad-up")).empty());
    CHECK(!D.Explicit(N64Control::R));

    // Toggle follows its control: kept when it is placed again, gone when it moves.
    CHECK(D.SetToggle(Slot("mid4"), true, &N) && D.Toggled(Slot("mid4")));
    CHECK(D.PlaceControl(At("mid4"), N64Control::L, &N) && D.Toggled(Slot("mid4")));
    CHECK(D.PlaceControl(At("mid3"), N64Control::L, &N) && N == "B is not placed now");
    CHECK(!D.Toggled(Slot("mid3")) && !D.Toggled(Slot("mid4")));
    CHECK(!D.CanToggle(Slot("mid4"), &N) && N == "a toggle needs a control in the slot");
    CHECK(!D.SetToggle(Slot("mid4"), true, &N) && N == "a toggle needs a control in the slot");

    // The picture takes a toggle, but neither the menu nor the hold.
    CHECK(D.SetToggle(POINTER_ZONE_GAME, true, &N) && D.Toggled(POINTER_ZONE_GAME));
    CHECK(!D.PlaceMenu(POINTER_ZONE_GAME, &N) && N == "the picture cannot hold the menu");
    CHECK(!D.PlaceHold(POINTER_ZONE_GAME, &N) && N == "the hold cannot go on the picture");
    CHECK(D.Validate("x"));

    // The menu always has a place. With mid1-mid4 and the hold on mid5, emptying the menu's
    // slot has nowhere to send it: refused, and nothing changed.
    D = PanelDraft();
    const std::string Before = D.Emit("x");
    CHECK(!D.PlaceNothing(At("pad-down"), &N) && N == "The panel is full: free a slot for the menu first");
    CHECK(!D.PlaceControl(At("pad-down"), N64Control::DPadUp, &N));
    CHECK(D.Emit("x") == Before);
    CHECK(D.PlaceNothing(At("mid4"), &N) && N == "R is not placed now");
    CHECK(D.PlaceNothing(At("pad-down"), &N) && N == "the menu moved to mid4" && D.MenuZone() == Slot("mid4"));
    CHECK(D.PlaceControl(At("mid4"), N64Control::R, &N) && N == "the menu moved to pad-down");
    CHECK(D.PlaceMenu(Slot("c-up"), &N) && N == "CUp is not placed now" && D.MenuZone() == Slot("c-up"));
    CHECK(D.Validate("x"));

    // The hold: only with the pointer; moving it frees its old slot; Nothing removes it.
    WizardDraft Plain;
    CHECK(!Plain.CanPlaceHold(Slot("mid5"), &N) && N == "the hold needs the stick to be the pointer");
    CHECK(!Plain.PlaceHold(Slot("mid5"), &N));
    D = PanelDraft();
    CHECK(D.PlaceNothing(At("pad-up"), &N) && N == "L is not placed now");
    CHECK(D.PlaceHold(Slot("pad-up"), &N) && D.HoldZone() == Slot("pad-up") && D.Occupants(At("mid5")).empty());
    CHECK(D.PlaceNothing(At("pad-up"), &N) && N == "the hold is gone" && D.HoldZone() == POINTER_ZONE_NONE);
    CHECK(D.Validate("x"));

    // A shared slot from a loaded layout is displaced whole.
    WizardDraft Shared;
    CHECK(Shared.LoadBase(TestWriteTemp("bindings:\n  Stick: {stick: pointer}\n  Z: {zone: mid2}\n  L: {zone: mid2}\n")));
    CHECK(Shared.Occupants(At("mid2")).size() == 2);
    CHECK(Shared.PlaceControl(At("mid2"), N64Control::B, &N) && N == "Z is not placed now; L is not placed now");
    CHECK(Holds(Shared, At("mid2"), N64Control::B));
    CHECK(Shared.Validate("x"));

    // A gesture menu from a loaded layout: shown, and moved to a slot when replaced.
    WizardDraft Faced;
    CHECK(Faced.LoadBase(TestWriteTemp("bindings:\n  Stick: {stick: pointer}\n  A: {zone: game}\n  Menu: {face: smile}\n")));
    CHECK(Faced.MenuGesture() == PointerGestureIndex(POINTER_GESTURE_SMILE));
    CHECK(!Faced.EnsureMenu(&N));
    CHECK(Faced.PlaceControl(OnGesture(POINTER_GESTURE_SMILE), N64Control::B, &N) && N == "the menu moved to mid5");
    CHECK(Faced.MenuZone() == Slot("mid5") && Faced.MenuGesture() == -1);
    CHECK(Holds(Faced, OnGesture(POINTER_GESTURE_SMILE), N64Control::B));
    CHECK(Faced.Validate("x"));

    // A head stick clears the four head-direction gestures and the hold, and keeps them shut.
    D = PanelDraft();
    CHECK(D.PlaceNothing(At("mid4"), &N));
    CHECK(D.PlaceControl(OnGesture(POINTER_GESTURE_HEAD_LEFT), N64Control::R, &N));
    CHECK(D.SetStickForm(EditStick::Head, &N) && N == "the hold is gone; R is not placed now");
    CHECK(D.StickForm() == EditStick::Head && D.HoldZone() == POINTER_ZONE_NONE);
    CHECK(!D.CanUseGesture(PointerGestureIndex(POINTER_GESTURE_HEAD_LEFT), &N) && N == "the head moves the stick");
    CHECK(!D.PlaceControl(OnGesture(POINTER_GESTURE_HEAD_UP), N64Control::R, &N));
    CHECK(D.PlaceControl(OnGesture(POINTER_GESTURE_MOUTH_OPEN), N64Control::R, &N));
    CHECK(!D.PlaceHold(Slot("mid5"), &N));
    CHECK(D.Validate("x"));
    CHECK(D.SetStickForm(EditStick::Pointer, &N) && D.StickForm() == EditStick::Pointer);
    CHECK(!D.SetStickForm(EditStick::Other, &N));

    // A base without a menu gets one where play would put it.
    WizardDraft Bare;
    CHECK(Bare.SetStickForm(EditStick::Pointer, &N));
    CHECK(Bare.EnsureMenu(&N) && N == "the menu was added on mid5" && Bare.MenuZone() == Slot("mid5"));
    CHECK(!Bare.EnsureMenu(&N));
    WizardDraft Full;
    CHECK(Full.LoadBase(TestWriteTemp(
        "bindings:\n  Stick: {stick: pointer}\n"
        "  A: {zone: pad-up}\n  B: {zone: pad-down}\n  Z: {zone: pad-left}\n  Start: {zone: pad-right}\n"
        "  CUp: {zone: c-up}\n  CDown: {zone: c-down}\n  CLeft: {zone: c-left}\n  CRight: {zone: c-right}\n"
        "  L: {zone: mid1}\n  R: {zone: mid2}\n  DPadUp: {zone: mid3}\n  DPadDown: {zone: mid4}\n"
        "  DPadLeft: {zone: mid5}\n")));
    CHECK(Full.EnsureMenu(&N) && N == "the menu took pad-down from B");
    CHECK(Full.MenuZone() == Slot("pad-down") && !Full.Explicit(N64Control::B));
    CHECK(Full.Validate("x"));
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
        CHECK(TestHas(Text, "  Menu:      {zone: pad-down}\n"));
        CHECK(D.MenuZone() == 1);
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

    PanelEditing();
}
