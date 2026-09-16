// Project64 - A Nintendo 64 emulator
// Tests for WizardDraft. No window and no renderer; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "WizardDraft.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <string.h>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

// True when Text contains Needle. The emitted file is small, so substring checks read
// better here than parsing it back a second time.
static bool Has(const std::string & Text, const char * Needle)
{
    return Text.find(Needle) != std::string::npos;
}

int main()
{
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
        CHECK(Has(D.Emit("the built-in bindings"), "bindings: {}"));
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
        CHECK(Has(Text, "A:         {key: X}"));
        CHECK(Has(Text, "B:         {button: a}"));
        CHECK(Has(Text, "Z:         {axis: lefttrigger, sign: +}"));
        CHECK(Has(Text, "R:         {axis: righty, sign: -}"));
        CHECK(Has(Text, "Start:     {zone: mid1}"));
        CHECK(Has(Text, "L:         {face: mouth-open}"));
        CHECK(D.Validate("the built-in bindings"));
        CHECK(D.Explicit(N64Control::A));
        CHECK(D.Bindings(N64Control::A).size() == 1);
        CHECK(D.Bindings(N64Control::A)[0].code == SDL_SCANCODE_X);
    }

    // A name with a space is quoted, and the reader still takes it.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_LSHIFT);
        CHECK(Has(D.Emit("x"), "{key: \"Left Shift\"}"));
        CHECK(D.Validate("x"));
    }

    // Clearing returns a control to inherited: it leaves the file and keeps whatever the
    // built-in bindings gave it, which for B is a pair the grammar could not write out.
    {
        WizardDraft D;
        const size_t BDefaults = D.Bindings(N64Control::B).size();
        CHECK(BDefaults == 2);
        D.SetKey(N64Control::B, SDL_SCANCODE_Q);
        CHECK(Has(D.Emit("x"), "B:"));
        D.Clear(N64Control::B);
        CHECK(!D.Explicit(N64Control::B));
        CHECK(D.Bindings(N64Control::B).size() == BDefaults);
        CHECK(!Has(D.Emit("x"), "\n  B:"));
    }

    // The header names the base, so a file says where it came from.
    {
        WizardDraft D;
        CHECK(Has(D.Emit("Config/mouse/goldeneye_007_u.yaml"),
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
        CHECK(D.LoadBase("Config/face/super_mario_64_usa.yaml"));
        CHECK(D.Explicit(N64Control::Stick));
        CHECK(D.Explicit(N64Control::A));
        CHECK(D.Explicit(N64Control::CRight));
        CHECK(!D.Explicit(N64Control::DPadUp));      // not named: inherited
        CHECK(D.Bindings(N64Control::A)[0].kind == Binding::Kind::Face);
        CHECK(D.Bindings(N64Control::A)[0].code == (int)POINTER_GESTURE_MOUTH_OPEN);
        const std::string Text = D.Emit("Config/face/super_mario_64_usa.yaml");
        CHECK(Has(Text, "Stick:     {stick: head}"));
        CHECK(Has(Text, "A:         {face: mouth-open}"));
        CHECK(!Has(Text, "\n  DPadUp:"));
        CHECK(D.Validate("Config/face/super_mario_64_usa.yaml"));
    }

    // Every shipped layout survives the same round trip.
    for (int i = 0; i < WizardBaseCount(); i++)
    {
        WizardDraft D;
        CHECK(D.LoadBase(WizardBaseFile(i)));
        CHECK(D.Validate(WizardBaseFile(i)));
        if (!D.Validate(WizardBaseFile(i))) fprintf(stderr, "  base %s: %s\n", WizardBaseFile(i), D.Error());
    }

    // A mouse layout's zones come back as zones.
    {
        WizardDraft D;
        CHECK(D.LoadBase("Config/mouse/super_mario_64_usa.yaml"));
        CHECK(D.Explicit(N64Control::Stick));
        CHECK(D.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Pointer);
        CHECK(Has(D.Emit("x"), "{stick: pointer}"));
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
    {
        WizardDraft D;
        CHECK(D.LoadBase("Config/face/super_mario_64_usa.yaml"));
        D.SetGesture(N64Control::DPadUp, POINTER_GESTURE_HEAD_UP);
        CHECK(!D.Validate("x"));
        CHECK(strstr(D.Error(), "head-up cannot be bound while Stick is head") != NULL);
    }

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: wizard draft\n");
    return 0;
}
