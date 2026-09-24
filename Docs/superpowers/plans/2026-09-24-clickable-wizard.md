# Clickable Wizard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A launcher `Edit` button opens `Project64-wizard --edit <rom>`, a panel editor operated by clicks only, where the player places controls, toggles, the stick's hold and the menu on the game's real panel and in the face gestures, and saves the layout beside the ROM.

**Architecture:** The editing rules are pure methods on `WizardDraft` (unit-tested in `wizard-draft`); the editor's geometry, enabling and click handling are pure functions in a new `EditLayout` (new unit-test area `wizard-edit`); `EditScreen` only paints and turns a press-and-release into `EditAct`. The added-menu order becomes one function in `InputConfig.h` shared by play and editing; the ROM-to-layout name becomes one function in `GameConfig`. The launcher gains an `Edit` target per row and a second kind of child.

**Tech Stack:** C++14, SDL3 (renderer, debug font), yaml-cpp (through `InputConfig`), POSIX, POSIX sh, hand-written `Makefile`.

**Spec:** `Docs/superpowers/specs/2026-09-24-clickable-wizard-design.md`

## Global Constraints

- The editor screen has no keyboard handling; every target acts on the left button's **release inside the target it was pressed in**. The cursor is never captured, confined or warped (AGENTS.md).
- The editor window is 800x640, titled `Project64 layout editor`, fixed size; `SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH` is `1`. Text is the debug font at scale 2 via `Source/Project64-sdl/DebugText.h`.
- The panel is drawn at its real size from `PointerZoneRect(Zone, 640, 640, …)`, offset so the game panel's top (y 480) lands at y 424 and x 0 at x 80; the picture (zone `game`) is the rectangle x 80, y 96, 640x320.
- Chooser choices, in order: the fourteen controls other than Stick in `N64Control` order (labels from `InputConfig::ControlLabel`: `A B Z St L R C^ Cv C< C> D^ Dv D< D>`), then `Menu`, `Hold`, `Nothing` (indices 14, 15, 16). A gesture's chooser omits `Menu` and `Hold`; only a slot's chooser shows `Toggle`.
- Exact reasons (shared by the draft and the screen): `the picture cannot hold the menu`, `the hold cannot go on the picture`, `the hold needs the stick to be the pointer`, `a toggle needs a control in the slot`, `the head moves the stick`, `The panel is full: free a slot for the menu first`. Side-effect notes: `<Control> is not placed now`, `the hold is gone`, `the menu moved to <slot>`; at load, `the menu was added on <slot>` or `the menu took <slot> from <Control>`. Other strings: `Cancel again to discard your changes`, `Cancel again…` only when dirty; `Your layout could not be read (<reason>); starting from the generic layout`; `Cannot save beside the ROM: <reason>`; `Not placed: …` / `Every control has a place`.
- Menu placement order (play and editing): first free of `mid5`, `mid4`, `mid3`, `mid2`, `mid1`, else `pad-down`, or `pad-up` when `pad-down` is the stick's hold.
- Save: `<rom dir>/<rom name without its last extension>.yaml`; before the first save over an existing file, copy it to `<that>.orig` (never touched again); write through `<that>.tmp` and a rename.
- stderr: `wizard: editing <rom> from <layout path>`, `wizard: saved <path>`, `wizard: kept the original as <path>.orig`, `wizard: cannot save <path>: <reason>`; launcher: `launcher: editing <rom>`, `launcher: editor ended (exit N)`; status `<title>: the layout editor ended with an error (exit N)`.
- `PJ64_EDIT_SELFTEST=1` with `--edit` runs the scripted edit with no window and exits (0 when saved).
- The fifteen-step walk, its screens and pictures 01-11 are unchanged. The screenshot tour adds `12-edit-panel.png`, `13-edit-chooser.png`, `14-edit-gestures.png`, never reading a file.
- Unit tests live in the one program (AGENTS.md). Files are LF. Never run a bare `git stash`. Every unattended run sets `PJ64_FACE=0`. Never capture the screen.
- Commit trailers name the model that wrote the commit (user-approved).

## Review Focus

1. **A loaded layout with a shared slot, or with its menu on a face gesture** (a helper's or the pack's), must be editable and still save a file the reader accepts. Pinned in Task 2 (shared slot displaced whole; gesture menu moved to a slot; `Validate` after each).
2. **A full panel and a click on the menu's slot** must be refused with nothing changed, never a lost menu. Pinned in Task 2 (refusal leaves the draft equal) and Task 4 (the act keeps the chooser and shows the reason).
3. **Saving over the pack's generated file, again and again, or into a read-only folder** must keep exactly one `.orig` of the first original and never leave a half-written layout. Pinned in Task 3 and Task 5's selftest (three runs).
4. **Closing the editor window with unsaved changes** must not discard them on one click. Pinned in Task 4 (Cancel semantics) and Task 5 (close routes through Cancel).
5. **A click on a launcher row near `Edit`** must start exactly what the player aimed at. Pinned in Task 7 (the two rectangles do not touch; hit tests either side of the 8-point gap).

---

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `Source/Project64-sdl/InputConfig.{h,cpp}` | modify | `AutoMenuSlot`, used by `ApplyAutoMenu` |
| `Source/Project64-sdl/GameConfig.{h,cpp}` | modify | `GameConfigBesideRom`, used by `GameConfigPath` |
| `Source/Project64-sdl/ExecutablePath.h` | create | the executable's path and directory |
| `Source/Project64-sdl/{InputConfigTest,GameConfigTest}.cpp` | modify | the two shared rules |
| `Source/Project64-wizard/WizardDraft.{h,cpp}`, `WizardDraftTest.cpp` | modify | editing rules, load and save for a ROM |
| `Source/Project64-wizard/EditLayout.{h,cpp}`, `EditLayoutTest.cpp` | create | the editor's geometry and click logic; `wizard-edit` area |
| `Source/Project64-wizard/EditScreen.{h,cpp}` | create | painting and event handling |
| `Source/Project64-wizard/main.cpp`, `Screens.{h,cpp}`, `SyntheticEvents.h`, `Screenshots.cpp` | modify | `--edit`, the script, the face-status text, release events, stops 12-14 |
| `Source/Project64-launcher/*` | modify | `Edit` target, the editor child, the self-test step |
| `Source/Project64-sdl/UnitTest.h`, `UnitTestMain.cpp`, `Makefile` | modify | the new area and objects |
| `Scripts/{wizard_selftest,wizard_screenshots_check,launcher_selftest}.sh` | modify | end-to-end checks |
| `Docs/img/wizard/12-14*.png`, `Docs/UserGuide.md`, `AGENTS.md` | create/modify | docs |

---

### Task 1: Shared rules — the menu order, the layout name, the executable path

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.h`, `Source/Project64-sdl/InputConfig.cpp`, `Source/Project64-sdl/InputConfigTest.cpp`
- Modify: `Source/Project64-sdl/GameConfig.h`, `Source/Project64-sdl/GameConfig.cpp`, `Source/Project64-sdl/GameConfigTest.cpp`
- Create: `Source/Project64-sdl/ExecutablePath.h`
- Modify: `Source/Project64-launcher/main.cpp`

**Interfaces:**
- Produces: `inline int AutoMenuSlot(const bool Used[POINTER_ZONE_COUNT], int Hold)` in `InputConfig.h`; `bool GameConfigBesideRom(const char * RomPath, char * Out, size_t Size)` in `GameConfig.h`; `inline std::string ExecutablePath()` and `inline std::string ExecutableDirectory()` in `ExecutablePath.h`.

- [ ] **Step 1: Write the failing tests**

In `InputConfigTest.cpp`, inside the existing added-menu block (after the `Full` case), add:

```cpp
        // The order itself, as play and the editor share it.
        bool Used[POINTER_ZONE_COUNT] = { false };
        CHECK(AutoMenuSlot(Used, POINTER_ZONE_NONE) == PointerZoneFromName("mid5"));
        Used[PointerZoneFromName("mid5")] = true;
        Used[PointerZoneFromName("mid4")] = true;
        CHECK(AutoMenuSlot(Used, POINTER_ZONE_NONE) == PointerZoneFromName("mid3"));
        for (const char * Name : { "mid3", "mid2", "mid1" }) Used[PointerZoneFromName(Name)] = true;
        CHECK(AutoMenuSlot(Used, POINTER_ZONE_NONE) == PointerZoneFromName("pad-down"));
        CHECK(AutoMenuSlot(Used, PointerZoneFromName("pad-down")) == PointerZoneFromName("pad-up"));
```

In `GameConfigTest.cpp`, at the end of `RunGameConfigTests()` (before its cleanup, if any), add:

```cpp
    // The name a layout saved for a ROM takes: beside it, the last extension replaced.
    char Beside[PATH_MAX];
    CHECK(GameConfigBesideRom("/r/game.z64", Beside, sizeof(Beside)) && strcmp(Beside, "/r/game.yaml") == 0);
    CHECK(GameConfigBesideRom("/r/a.b.v64", Beside, sizeof(Beside)) && strcmp(Beside, "/r/a.b.yaml") == 0);
    CHECK(GameConfigBesideRom("game.n64", Beside, sizeof(Beside)) && strcmp(Beside, "./game.yaml") == 0);
    CHECK(GameConfigBesideRom("/r/.hidden", Beside, sizeof(Beside)) && strcmp(Beside, "/r/.hidden.yaml") == 0);
    CHECK(!GameConfigBesideRom("/r/", Beside, sizeof(Beside)));
```

- [ ] **Step 2: Run to verify they fail**

Run: `make unit-test only=input-config` and `make unit-test only=game-config`
Expected: compile errors — `AutoMenuSlot` and `GameConfigBesideRom` are not declared.

- [ ] **Step 3: Implement**

`InputConfig.h`, after `MenuGestureOf`:

```cpp
// Where the added menu goes: the first of mid5, mid4, mid3, mid2, mid1 that Used does not
// mark, else the fallback — pad-down, or pad-up when pad-down is the stick's Hold. The fallback
// comes back whether or not Used marks it: ApplyAutoMenu then takes it from its control, and
// the panel editor refuses. The one statement of the order, for play and for editing.
inline int AutoMenuSlot(const bool Used[POINTER_ZONE_COUNT], int Hold)
{
    static const char * const kOrder[] = { "mid5", "mid4", "mid3", "mid2", "mid1" };
    for (const char * Name : kOrder)
    {
        const int Zone = PointerZoneFromName(Name);
        if (!Used[Zone]) return Zone;
    }
    const int PadDown = PointerZoneFromName("pad-down");
    return Hold == PadDown ? PointerZoneFromName("pad-up") : PadDown;
}
```

`InputConfig.cpp`, `ApplyAutoMenu`: replace the `kOrder` loop and the `PadDown`/`Fallback` lines with one call, keeping the rest (the removal loop now runs only when the slot is used; when it is free nothing is removed and the "added on" line prints):

```cpp
    const int Slot = AutoMenuSlot(Used, Hold);
    const char * SlotName = PointerZoneName(Slot);
    const char * Taken = nullptr;
    if (Used[Slot])
    {
        for (int i = 0; i < (int)N64Control::Count; i++)
        {
            std::vector<Binding> & List = m_Bindings[i];
            for (size_t j = 0; j < List.size();)
            {
                if (List[j].kind == Binding::Kind::Zone && List[j].code == Slot)
                {
                    List.erase(List.begin() + j);
                    if (Taken == nullptr) Taken = kControlNames[i];
                }
                else
                {
                    j++;
                }
            }
        }
    }
    m_Menu.assign(1, MakeZone(Slot));
    if (!Quiet)
    {
        if (Taken != nullptr) fprintf(stderr, "menu: took %s from %s\n", SlotName, Taken);
        else fprintf(stderr, "menu: added on %s\n", SlotName);
    }
    return Slot;
```

(The existing `input-config` cases — `mid5`, `mid3`/`mid2` with a hold, `pad-down` from B, `pad-up` from the control when `pad-down` is the hold — must all still pass unchanged.)

`GameConfig.h`, after `GameConfigPath`:

```cpp
// The path GameConfigPath tries first for RomPath: <rom dir>/<base>.yaml. False when the ROM
// path has no file name. The panel editor saves there, so the saved layout wins at once.
bool GameConfigBesideRom(const char * RomPath, char * Out, size_t Size);
```

`GameConfig.cpp`: factor the directory/base split out of `GameConfigPath` into a static helper and use it in both:

```cpp
// RomPath's directory ("." for a bare name) and its file name without the last extension.
static bool SplitRom(const char * RomPath, std::string * Dir, std::string * Base)
{
    const std::string Rom = RomPath;
    const size_t Slash = Rom.find_last_of('/');
    *Dir = Slash == std::string::npos ? "." : Rom.substr(0, Slash);
    *Base = Slash == std::string::npos ? Rom : Rom.substr(Slash + 1);
    const size_t Dot = Base->find_last_of('.');
    if (Dot != std::string::npos && Dot > 0)   // ".hidden" keeps its name whole
    {
        Base->erase(Dot);
    }
    return !Base->empty();
}

bool GameConfigBesideRom(const char * RomPath, char * Out, size_t Size)
{
    std::string Dir, Base;
    if (!SplitRom(RomPath, &Dir, &Base)) return false;
    snprintf(Out, Size, "%s/%s.yaml", Dir.c_str(), Base.c_str());
    return true;
}

bool GameConfigPath(const char * RomPath, const char * ExeDir, char * Out, size_t Size)
{
    std::string Dir, Base;
    if (!SplitRom(RomPath, &Dir, &Base)) return false;
    const std::string Candidates[2] = {
        Dir + "/" + Base + ".yaml",
        std::string(ExeDir) + "/Config/mouse/" + Base + ".yaml",
    };
    for (const std::string & Path : Candidates)
    {
        if (access(Path.c_str(), R_OK) == 0)
        {
            snprintf(Out, Size, "%s", Path.c_str());
            return true;
        }
    }
    return false;
}
```

Create `Source/Project64-sdl/ExecutablePath.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// This process's executable and its folder, resolved through links. Header-only, so the
// launcher and the wizard share it without a link dependency. (The frontend's main.cpp and
// GridHost.cpp keep their own older copies.)
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>

// The executable's full path, or "" when macOS cannot say.
inline std::string ExecutablePath()
{
    char Buf[PATH_MAX];
    uint32_t Size = sizeof(Buf);
    if (_NSGetExecutablePath(Buf, &Size) != 0) return "";
    char Resolved[PATH_MAX];
    return realpath(Buf, Resolved) != nullptr ? std::string(Resolved) : std::string();
}

// The folder holding it, or "." when unknown.
inline std::string ExecutableDirectory()
{
    const std::string Path = ExecutablePath();
    const size_t Slash = Path.rfind('/');
    return Slash == std::string::npos ? std::string(".") : Path.substr(0, Slash);
}
```

`Source/Project64-launcher/main.cpp`: delete its own `ExecutablePath()` (in the anonymous namespace) and add `#include <Project64-sdl/ExecutablePath.h>`; the call site stays `ExecutablePath()`. Remove `<mach-o/dyld.h>` if nothing else there uses it.

- [ ] **Step 4: Run the tests and the build**

Run: `make -j8 all && make unit-test`
Expected: all areas `ok:` and `ok: unit tests`; no new warnings.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/InputConfigTest.cpp Source/Project64-sdl/GameConfig.h Source/Project64-sdl/GameConfig.cpp Source/Project64-sdl/GameConfigTest.cpp Source/Project64-sdl/ExecutablePath.h Source/Project64-launcher/main.cpp
git commit -m "Share the added menu's order, the per-ROM layout name and the executable's path

<your attribution trailer>"
```

---

### Task 2: The editing rules in WizardDraft

**Files:**
- Modify: `Source/Project64-wizard/WizardDraft.h`, `Source/Project64-wizard/WizardDraft.cpp`, `Source/Project64-wizard/WizardDraftTest.cpp`

**Interfaces:**
- Consumes: `AutoMenuSlot` (Task 1).
- Produces (in `WizardDraft.h`): `struct EditPlace { bool Gesture = false; int Index = POINTER_ZONE_NONE; }`; `enum class EditStick { Pointer, Head, HeadDigital, Other }`; on `WizardDraft`: `std::vector<N64Control> Occupants(EditPlace) const`, `bool Toggled(int Zone) const`, `int MenuGesture() const` (gesture index or -1), `EditStick StickForm() const`, `std::vector<N64Control> NotPlaced() const`, `bool CanPlaceMenu(int Zone, std::string * Why) const`, `bool CanPlaceHold(int Zone, std::string * Why) const`, `bool CanToggle(int Zone, std::string * Why) const`, `bool CanUseGesture(int Gesture, std::string * Why) const`, `bool PlaceControl(EditPlace, N64Control, std::string * Note)`, `bool PlaceMenu(int Zone, std::string * Note)`, `bool PlaceHold(int Zone, std::string * Note)`, `bool PlaceNothing(EditPlace, std::string * Note)`, `bool SetToggle(int Zone, bool On, std::string * Note)`, `bool SetStickForm(EditStick, std::string * Note)`, `bool EnsureMenu(std::string * Note)`.

Every `Place*`/`Set*` works on a copy and commits only on success: a refusal leaves the draft exactly as it was, with the reason in `*Note`; on success `*Note` lists side effects joined by `; `, or is empty.

- [ ] **Step 1: Write the failing tests**

Add to `WizardDraftTest.cpp`, above `RunWizardDraftTests`:

```cpp
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
```

Call it at the end of `RunWizardDraftTests()`: `PanelEditing();`. (Add `#include <string>` and `#include <vector>` if needed; `TestWriteTemp` and `TestHas` come from `UnitTest.h`.)

- [ ] **Step 2: Run to verify they fail**

Run: `make unit-test only=wizard-draft`
Expected: compile errors — `EditPlace`, `EditStick` and the new methods are not declared.

- [ ] **Step 3: Implement**

`WizardDraft.h`, before `class WizardDraft`:

```cpp
// A place the panel editor edits: a zone (0 … POINTER_ZONE_COUNT - 1, the picture included)
// or a face gesture (its index, 0 … POINTER_GESTURE_COUNT - 1).
struct EditPlace
{
    bool Gesture = false;
    int Index = POINTER_ZONE_NONE;
};

// The stick forms the panel editor offers; Other is any form it does not (a gamepad stick,
// four keys), kept as it is until one of the three is chosen.
enum class EditStick { Pointer, Head, HeadDigital, Other };
```

In `class WizardDraft`, public, after `MenuZone()`:

```cpp
    // ---- The panel editor (Docs/superpowers/specs/2026-09-24-clickable-wizard-design.md) ----
    // A slot holds one thing: a control, the menu, the hold or nothing; a gesture holds one
    // control or nothing. Each Place/Set call works on a copy and commits only on success: a
    // refusal leaves the draft as it was, with the reason in *Note. On success *Note lists
    // what moved as a side effect, joined by "; ", or is empty.

    // Every control bound to Place. A loaded layout may share a slot.
    std::vector<N64Control> Occupants(EditPlace Place) const;
    // True when the controls in Zone are a toggle slot.
    bool Toggled(int Zone) const;
    // The menu's gesture index, or -1 when the menu is not a gesture.
    int MenuGesture() const;
    EditStick StickForm() const;
    // Every control except Stick with neither a zone nor a gesture, in N64Control order.
    std::vector<N64Control> NotPlaced() const;

    // Whether a choice is allowed where it would go; *Why gets the reason when not.
    bool CanPlaceMenu(int Zone, std::string * Why) const;
    bool CanPlaceHold(int Zone, std::string * Why) const;
    bool CanToggle(int Zone, std::string * Why) const;
    bool CanUseGesture(int Gesture, std::string * Why) const;

    bool PlaceControl(EditPlace Place, N64Control Control, std::string * Note);
    bool PlaceMenu(int Zone, std::string * Note);
    bool PlaceHold(int Zone, std::string * Note);
    bool PlaceNothing(EditPlace Place, std::string * Note);
    bool SetToggle(int Zone, bool On, std::string * Note);
    bool SetStickForm(EditStick Form, std::string * Note);

    // For a draft with no menu: puts it where PJ64_MENU_AUTO would at play time (AutoMenuSlot),
    // taking that slot from its control when the panel is full. False when a menu exists.
    bool EnsureMenu(std::string * Note);
```

and private:

```cpp
    void ClearControl(N64Control Control, std::string * Note);
    void RemoveHold(std::string * Note);
    // Moves the menu to the first free slot by AutoMenuSlot, counting Avoid as taken. False,
    // with the full-panel reason in *Note, when no slot is free.
    bool MoveMenu(int Avoid, std::string * Note);
```

`WizardDraft.cpp`, after `MenuZone()`:

```cpp
namespace
{
const char * const kFullPanel = "The panel is full: free a slot for the menu first";

bool IsHeadDirection(int Gesture)
{
    return Gesture >= 0 && Gesture < POINTER_GESTURE_COUNT && (POINTER_GESTURE_HEAD_DIRECTIONS & (1u << Gesture)) != 0;
}

void AddNote(std::string * Note, const std::string & Text)
{
    if (!Note->empty()) *Note += "; ";
    *Note += Text;
}

Binding ZoneBinding(int Zone, bool Toggle)
{
    Binding B = {};
    B.kind = Binding::Kind::Zone;
    B.code = Zone;
    B.Toggle = Toggle;
    return B;
}

bool BindsPlace(const Binding & B, EditPlace Place)
{
    if (Place.Gesture) return B.kind == Binding::Kind::Face && (uint32_t)B.code == (1u << Place.Index);
    return B.kind == Binding::Kind::Zone && B.code == Place.Index;
}
}

std::vector<N64Control> WizardDraft::Occupants(EditPlace Place) const
{
    std::vector<N64Control> Out;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (i == (int)N64Control::Stick) continue;
        for (const Binding & B : m_Bindings[i])
        {
            if (BindsPlace(B, Place))
            {
                Out.push_back((N64Control)i);
                break;
            }
        }
    }
    return Out;
}

bool WizardDraft::Toggled(int Zone) const
{
    EditPlace Place;
    Place.Index = Zone;
    for (N64Control C : Occupants(Place))
    {
        for (const Binding & B : m_Bindings[(int)C])
        {
            if (BindsPlace(B, Place) && B.Toggle) return true;
        }
    }
    return false;
}

int WizardDraft::MenuGesture() const
{
    const uint32_t Bit = MenuGestureOf(m_Menu);
    return Bit != 0 ? PointerGestureIndex(Bit) : -1;
}

EditStick WizardDraft::StickForm() const
{
    const std::vector<Binding> & S = m_Bindings[(int)N64Control::Stick];
    if (S.size() != 1) return EditStick::Other;
    if (S[0].kind == Binding::Kind::Pointer) return EditStick::Pointer;
    if (S[0].kind == Binding::Kind::HeadStick) return S[0].code == 0 ? EditStick::Head : EditStick::HeadDigital;
    return EditStick::Other;
}

std::vector<N64Control> WizardDraft::NotPlaced() const
{
    std::vector<N64Control> Out;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (i == (int)N64Control::Stick) continue;
        bool Placed = false;
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone || B.kind == Binding::Kind::Face) Placed = true;
        }
        if (!Placed) Out.push_back((N64Control)i);
    }
    return Out;
}

bool WizardDraft::CanPlaceMenu(int Zone, std::string * Why) const
{
    if (Zone == POINTER_ZONE_GAME) { *Why = "the picture cannot hold the menu"; return false; }
    return true;
}

bool WizardDraft::CanPlaceHold(int Zone, std::string * Why) const
{
    if (Zone == POINTER_ZONE_GAME) { *Why = "the hold cannot go on the picture"; return false; }
    if (StickForm() != EditStick::Pointer) { *Why = "the hold needs the stick to be the pointer"; return false; }
    return true;
}

bool WizardDraft::CanToggle(int Zone, std::string * Why) const
{
    EditPlace Place;
    Place.Index = Zone;
    if (Occupants(Place).empty()) { *Why = "a toggle needs a control in the slot"; return false; }
    return true;
}

bool WizardDraft::CanUseGesture(int Gesture, std::string * Why) const
{
    const EditStick Form = StickForm();
    if ((Form == EditStick::Head || Form == EditStick::HeadDigital) && IsHeadDirection(Gesture))
    {
        *Why = "the head moves the stick";
        return false;
    }
    return true;
}

void WizardDraft::ClearControl(N64Control Control, std::string * Note)
{
    Clear(Control);
    AddNote(Note, std::string(WizardControlName(Control)) + " is not placed now");
}

void WizardDraft::RemoveHold(std::string * Note)
{
    m_Bindings[(int)N64Control::Stick][0].Hold = POINTER_ZONE_NONE;
    AddNote(Note, "the hold is gone");
}

bool WizardDraft::MoveMenu(int Avoid, std::string * Note)
{
    bool Used[POINTER_ZONE_COUNT] = { false };
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone) Used[B.code] = true;
        }
    }
    const int Hold = HoldZone();
    if (Hold != POINTER_ZONE_NONE) Used[Hold] = true;
    if (Avoid != POINTER_ZONE_NONE) Used[Avoid] = true;
    const int Slot = AutoMenuSlot(Used, Hold);
    if (Used[Slot])
    {
        *Note = kFullPanel;
        return false;
    }
    m_Menu.assign(1, ZoneBinding(Slot, false));
    AddNote(Note, std::string("the menu moved to ") + PointerZoneName(Slot));
    return true;
}

bool WizardDraft::PlaceControl(EditPlace Place, N64Control Control, std::string * Note)
{
    Note->clear();
    WizardDraft Next = *this;
    if (Place.Gesture)
    {
        if (!CanUseGesture(Place.Index, Note)) return false;
        for (N64Control C : Occupants(Place))
        {
            if (C != Control) Next.ClearControl(C, Note);
        }
        Binding B = {};
        B.kind = Binding::Kind::Face;
        B.code = (int)(1u << Place.Index);
        Next.Replace(Control, B);
        if (Next.MenuGesture() == Place.Index && !Next.MoveMenu(POINTER_ZONE_NONE, Note)) return false;
    }
    else
    {
        const int Zone = Place.Index;
        bool KeepToggle = false;
        for (N64Control C : Occupants(Place))
        {
            if (C == Control) KeepToggle = Toggled(Zone);
            else Next.ClearControl(C, Note);
        }
        if (Next.HoldZone() == Zone) Next.RemoveHold(Note);
        Next.Replace(Control, ZoneBinding(Zone, KeepToggle));
        if (Next.MenuZone() == Zone && !Next.MoveMenu(Zone, Note)) return false;
    }
    *this = Next;
    return true;
}

bool WizardDraft::PlaceMenu(int Zone, std::string * Note)
{
    Note->clear();
    if (!CanPlaceMenu(Zone, Note)) return false;
    WizardDraft Next = *this;
    EditPlace Place;
    Place.Index = Zone;
    for (N64Control C : Occupants(Place)) Next.ClearControl(C, Note);
    if (Next.HoldZone() == Zone) Next.RemoveHold(Note);
    Next.m_Menu.assign(1, ZoneBinding(Zone, false));
    *this = Next;
    return true;
}

bool WizardDraft::PlaceHold(int Zone, std::string * Note)
{
    Note->clear();
    if (!CanPlaceHold(Zone, Note)) return false;
    WizardDraft Next = *this;
    EditPlace Place;
    Place.Index = Zone;
    for (N64Control C : Occupants(Place)) Next.ClearControl(C, Note);
    Next.m_Bindings[(int)N64Control::Stick][0].Hold = Zone;
    if (Next.MenuZone() == Zone && !Next.MoveMenu(Zone, Note)) return false;
    *this = Next;
    return true;
}

bool WizardDraft::PlaceNothing(EditPlace Place, std::string * Note)
{
    Note->clear();
    WizardDraft Next = *this;
    for (N64Control C : Occupants(Place)) Next.ClearControl(C, Note);
    if (Place.Gesture)
    {
        if (Next.MenuGesture() == Place.Index && !Next.MoveMenu(POINTER_ZONE_NONE, Note)) return false;
    }
    else
    {
        if (Next.HoldZone() == Place.Index) Next.RemoveHold(Note);
        if (Next.MenuZone() == Place.Index && !Next.MoveMenu(Place.Index, Note)) return false;
    }
    *this = Next;
    return true;
}

bool WizardDraft::SetToggle(int Zone, bool On, std::string * Note)
{
    Note->clear();
    if (!CanToggle(Zone, Note)) return false;
    EditPlace Place;
    Place.Index = Zone;
    // Every control on the slot, so a shared slot stays all-or-nothing, as the reader requires.
    for (N64Control C : Occupants(Place))
    {
        for (Binding & B : m_Bindings[(int)C])
        {
            if (BindsPlace(B, Place)) B.Toggle = On;
        }
    }
    return true;
}

bool WizardDraft::SetStickForm(EditStick Form, std::string * Note)
{
    Note->clear();
    if (Form == EditStick::Other)
    {
        *Note = "that stick form is set in the step-by-step wizard";
        return false;
    }
    if (Form == StickForm()) return true;
    WizardDraft Next = *this;
    if (Form == EditStick::Pointer)
    {
        Next.SetStickPointer();
    }
    else
    {
        if (Next.HoldZone() != POINTER_ZONE_NONE) Next.RemoveHold(Note);
        for (int G = 0; G < POINTER_GESTURE_COUNT; G++)
        {
            if (!IsHeadDirection(G)) continue;
            EditPlace Place;
            Place.Gesture = true;
            Place.Index = G;
            for (N64Control C : Next.Occupants(Place)) Next.ClearControl(C, Note);
            if (Next.MenuGesture() == G && !Next.MoveMenu(POINTER_ZONE_NONE, Note)) return false;
        }
        Next.SetStickHead(Form == EditStick::HeadDigital);
    }
    *this = Next;
    return true;
}

bool WizardDraft::EnsureMenu(std::string * Note)
{
    Note->clear();
    if (!m_Menu.empty()) return false;
    bool Used[POINTER_ZONE_COUNT] = { false };
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone) Used[B.code] = true;
        }
    }
    const int Hold = HoldZone();
    if (Hold != POINTER_ZONE_NONE) Used[Hold] = true;
    const int Slot = AutoMenuSlot(Used, Hold);
    EditPlace Place;
    Place.Index = Slot;
    const char * Taken = nullptr;
    for (N64Control C : Occupants(Place))
    {
        Clear(C);
        if (Taken == nullptr) Taken = WizardControlName(C);
    }
    m_Menu.assign(1, ZoneBinding(Slot, false));
    *Note = Taken != nullptr ? std::string("the menu took ") + PointerZoneName(Slot) + " from " + Taken
                             : std::string("the menu was added on ") + PointerZoneName(Slot);
    return true;
}
```

- [ ] **Step 4: Run to verify they pass**

Run: `make unit-test only=wizard-draft`, then `make unit-test`
Expected: `ok: wizard-draft`; everything else still `ok:`, drift check included (nothing the walk draws changed).

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-wizard/WizardDraft.h Source/Project64-wizard/WizardDraft.cpp Source/Project64-wizard/WizardDraftTest.cpp
git commit -m "Teach the wizard's draft the panel editor's rules: one thing per slot, toggles, the hold, a menu that always has a place

<your attribution trailer>"
```

---

### Task 3: Loading and saving a game's layout

**Files:**
- Modify: `Source/Project64-wizard/WizardDraft.h`, `Source/Project64-wizard/WizardDraft.cpp`, `Source/Project64-wizard/WizardDraftTest.cpp`, `Makefile`

**Interfaces:**
- Consumes: `GameConfigPath`, `GameConfigBesideRom` (Task 1).
- Produces: `std::string WizardDraft::LoadForRom(const char * RomPath, const char * ExeDir, std::string * Note)` (returns the path loaded, or `""` with the reason in `Error()`); `bool WizardDraft::SaveBesideRom(const char * RomPath, const char * BaseName, std::string * Saved, bool * MadeOrig)`.

- [ ] **Step 1: Write the failing tests**

Add to `WizardDraftTest.cpp`, above `RunWizardDraftTests`:

```cpp
static std::string ReadAll(const std::string & Path)
{
    std::string Out;
    FILE * F = fopen(Path.c_str(), "r");
    if (F == nullptr) return Out;
    char Buf[4096];
    size_t N;
    while ((N = fread(Buf, 1, sizeof(Buf), F)) > 0) Out.append(Buf, N);
    fclose(F);
    return Out;
}

static void WriteAll(const std::string & Path, const char * Text)
{
    FILE * F = fopen(Path.c_str(), "w");
    if (F == nullptr) { perror(Path.c_str()); exit(2); }
    fputs(Text, F);
    fclose(F);
}

static void RomFiles()
{
    const std::string Dir = TestMakeTempDir("pj64-wizard-rom");
    const std::string Rom = Dir + "/game.z64";
    TestTouch(Rom);
    std::string Note;
    WizardDraft D;

    // No layout of its own: the generic one, from the executable's folder (the checkout here).
    std::string Loaded = D.LoadForRom(Rom.c_str(), g_Root.c_str(), &Note);
    CHECK(TestHas(Loaded, "Config/mouse/default.yaml") && Note.empty());
    CHECK(D.MenuZone() == PointerZoneFromName("pad-down"));

    // A layout that will not load: the generic one, and the reason.
    WriteAll(Dir + "/game.yaml", "bindings: [1, 2]\n");
    Loaded = D.LoadForRom(Rom.c_str(), g_Root.c_str(), &Note);
    CHECK(TestHas(Loaded, "Config/mouse/default.yaml"));
    CHECK(TestHas(Note, "Your layout could not be read (") && TestHas(Note, "); starting from the generic layout"));

    // Saving over it keeps the original as .orig, once; the temporary file never stays.
    std::string Saved;
    bool MadeOrig = false;
    CHECK(D.SaveBesideRom(Rom.c_str(), "default.yaml", &Saved, &MadeOrig));
    CHECK(Saved == Dir + "/game.yaml" && MadeOrig);
    CHECK(ReadAll(Dir + "/game.yaml.orig") == "bindings: [1, 2]\n");
    CHECK(TestHas(ReadAll(Saved), "Menu:      {zone: pad-down}"));
    CHECK(D.SaveBesideRom(Rom.c_str(), "default.yaml", &Saved, &MadeOrig) && !MadeOrig);
    CHECK(ReadAll(Dir + "/game.yaml.orig") == "bindings: [1, 2]\n");
    CHECK(access((Dir + "/game.yaml.tmp").c_str(), F_OK) != 0);

    // The saved layout is what the next edit starts from.
    Loaded = D.LoadForRom(Rom.c_str(), g_Root.c_str(), &Note);
    CHECK(Loaded == Dir + "/game.yaml" && Note.empty());

    // A folder that cannot be written: refused with a reason, and no file at all.
    const std::string Locked = TestMakeTempDir("pj64-wizard-locked");
    const std::string LockedRom = Locked + "/game.z64";
    TestTouch(LockedRom);
    chmod(Locked.c_str(), 0555);
    CHECK(!D.SaveBesideRom(LockedRom.c_str(), "x", &Saved, &MadeOrig));
    CHECK(TestHas(D.Error(), "could not write " + Locked + "/game.yaml"));
    CHECK(access((Locked + "/game.yaml").c_str(), F_OK) != 0 && access((Locked + "/game.yaml.tmp").c_str(), F_OK) != 0);
    chmod(Locked.c_str(), 0755);
}
```

Call `RomFiles();` at the end of `RunWizardDraftTests()` (after `FindRoot` has set `g_Root`, which the function's top already does). Add `#include <sys/stat.h>`.

- [ ] **Step 2: Run to verify they fail**

Run: `make unit-test only=wizard-draft`
Expected: compile errors — `LoadForRom` and `SaveBesideRom` are not declared.

- [ ] **Step 3: Implement**

`WizardDraft.h`, public, after `Save`:

```cpp
    // The panel editor's file for RomPath: GameConfigPath's (beside the ROM, then ExeDir's
    // Config/mouse/), else ExeDir's Config/mouse/default.yaml. A layout that will not load
    // falls back to the generic one with the reason in *Note. Returns the path loaded, or ""
    // with the reader's reason in Error() when even the generic layout failed.
    std::string LoadForRom(const char * RomPath, const char * ExeDir, std::string * Note);

    // Validates, then writes GameConfigBesideRom's path through <path>.tmp and a rename,
    // first copying an existing file to <path>.orig when no .orig exists yet. *Saved gets the
    // path; *MadeOrig whether this save made the .orig. False with the reason in Error().
    bool SaveBesideRom(const char * RomPath, const char * BaseName, std::string * Saved, bool * MadeOrig);
```

`WizardDraft.cpp`: add `#include <Project64-sdl/GameConfig.h>`, `#include <errno.h>` and `#include <limits.h>`, then:

```cpp
std::string WizardDraft::LoadForRom(const char * RomPath, const char * ExeDir, std::string * Note)
{
    Note->clear();
    char Own[PATH_MAX];
    if (GameConfigPath(RomPath, ExeDir, Own, sizeof(Own)))
    {
        if (LoadBase(Own)) return Own;
        *Note = "Your layout could not be read (" + m_Error + "); starting from the generic layout";
    }
    const std::string Generic = std::string(ExeDir) + "/Config/mouse/default.yaml";
    if (LoadBase(Generic.c_str())) return Generic;
    return "";
}

// A byte-for-byte copy; false when either side cannot be opened or the write falls short.
static bool CopyFile(const std::string & From, const std::string & To)
{
    FILE * In = fopen(From.c_str(), "rb");
    if (In == nullptr) return false;
    FILE * Out = fopen(To.c_str(), "wb");
    if (Out == nullptr)
    {
        fclose(In);
        return false;
    }
    bool Ok = true;
    char Buf[4096];
    size_t N;
    while (Ok && (N = fread(Buf, 1, sizeof(Buf), In)) > 0) Ok = fwrite(Buf, 1, N, Out) == N;
    fclose(In);
    Ok = fclose(Out) == 0 && Ok;
    if (!Ok) remove(To.c_str());
    return Ok;
}

bool WizardDraft::SaveBesideRom(const char * RomPath, const char * BaseName, std::string * Saved, bool * MadeOrig)
{
    *MadeOrig = false;
    char Path[PATH_MAX];
    if (!GameConfigBesideRom(RomPath, Path, sizeof(Path)))
    {
        m_Error = "the ROM has no name to save a layout under";
        return false;
    }
    *Saved = Path;
    if (!Validate(BaseName)) return false;

    const std::string Temp = std::string(Path) + ".tmp";
    FILE * F = fopen(Temp.c_str(), "w");
    if (F == nullptr)
    {
        m_Error = std::string("could not write ") + Path + ": " + strerror(errno);
        return false;
    }
    const bool Wrote = fwrite(m_LastEmit.data(), 1, m_LastEmit.size(), F) == m_LastEmit.size();
    if (fclose(F) != 0 || !Wrote)
    {
        remove(Temp.c_str());
        m_Error = std::string("could not write ") + Path;
        return false;
    }

    const std::string Orig = std::string(Path) + ".orig";
    if (access(Path, F_OK) == 0 && access(Orig.c_str(), F_OK) != 0)
    {
        if (!CopyFile(Path, Orig))
        {
            remove(Temp.c_str());
            m_Error = "could not keep the original as " + Orig;
            return false;
        }
        *MadeOrig = true;
    }
    if (rename(Temp.c_str(), Path) != 0)
    {
        m_Error = std::string("could not write ") + Path + ": " + strerror(errno);
        remove(Temp.c_str());
        return false;
    }
    return true;
}
```

`Makefile`: the wizard now links `GameConfig.o` — add ` $(BUILD)/Project64-sdl/GameConfig.o` to the `$(BIN)/Project64-wizard:` prerequisites (after `InputConfig.o`). `UNIT_TEST_OBJS` already has it.

- [ ] **Step 4: Run to verify they pass**

Run: `make -j8 all && make unit-test`
Expected: `ok: wizard-draft` and every other area; the wizard links.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-wizard/WizardDraft.h Source/Project64-wizard/WizardDraft.cpp Source/Project64-wizard/WizardDraftTest.cpp Makefile
git commit -m "Load a game's layout for editing, falling back to the generic one, and save it beside the ROM, keeping the first original

<your attribution trailer>"
```

---

### Task 4: The editor's layout and click logic (`wizard-edit` area)

**Files:**
- Create: `Source/Project64-wizard/EditLayout.h`, `Source/Project64-wizard/EditLayout.cpp`, `Source/Project64-wizard/EditLayoutTest.cpp`
- Modify: `Source/Project64-sdl/UnitTest.h`, `Source/Project64-sdl/UnitTestMain.cpp`, `Makefile`

**Interfaces:**
- Consumes: Task 2's draft API.
- Produces (in `EditLayout.h`): `EDIT_WIDTH` 800, `EDIT_HEIGHT` 640, `EDIT_CHOICE_MENU` 14, `EDIT_CHOICE_HOLD` 15, `EDIT_CHOICE_NOTHING` 16, `EDIT_CHOICE_COUNT` 17; `enum class EditView { Panel, Chooser, Gestures, Stick }`; `struct EditState { EditView View; EditPlace Place; bool Dirty; bool ConfirmCancel; std::string Status; }`; `enum class EditTargetKind { None, Stick, Gestures, Save, Cancel, Zone, Gesture, Choice, Toggle, Back, StickForm }`; `struct EditTarget { EditTargetKind Kind; int Index; }` with `operator==`; `struct EditRect { float X, Y, W, H; }`; `enum class EditCommand { None, Save, Quit }`; `bool EditShowsGestures(const EditState &)`; `std::vector<EditTarget> EditTargets(const EditState &)`; `EditRect EditTargetRect(EditTarget)`; `EditTarget EditHit(const EditState &, float X, float Y)`; `bool EditEnabled(const EditState &, const WizardDraft &, EditTarget, std::string * Why)` (Why may be null); `bool EditLit(const EditState &, const WizardDraft &, EditTarget)`; `std::string EditLabel(const EditState &, const WizardDraft &, EditTarget)`; `std::string EditHeader(const EditState &, const WizardDraft &)`; `std::string EditNotPlaced(const WizardDraft &)`; `EditCommand EditAct(EditState *, WizardDraft *, EditTarget)`.

- [ ] **Step 1: Write the failing tests**

Create `Source/Project64-wizard/EditLayoutTest.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// Tests for the panel editor's layout and click logic (EditLayout.h). No window, no renderer.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "EditLayout.h"
#include <Project64-sdl/UnitTest.h>

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <string>
#include <vector>

static int Slot(const char * Name) { return PointerZoneFromName(Name); }

static EditTarget Target(EditTargetKind Kind, int Index = 0)
{
    EditTarget T;
    T.Kind = Kind;
    T.Index = Index;
    return T;
}

static EditTarget HitCentre(const EditState & S, EditTarget T)
{
    const EditRect R = EditTargetRect(T);
    return EditHit(S, R.X + R.W / 2, R.Y + R.H / 2);
}

// A small layout: the pointer stick with its hold on mid5, A on the picture, Z a toggle on
// mid2, the menu on pad-down.
static WizardDraft Small()
{
    WizardDraft D;
    std::string N;
    EditPlace Picture;
    Picture.Index = POINTER_ZONE_GAME;
    EditPlace Mid2;
    Mid2.Index = Slot("mid2");
    CHECK(D.SetStickForm(EditStick::Pointer, &N));
    CHECK(D.PlaceControl(Picture, N64Control::A, &N));
    CHECK(D.PlaceControl(Mid2, N64Control::Z, &N));
    CHECK(D.SetToggle(Slot("mid2"), true, &N));
    CHECK(D.PlaceHold(Slot("mid5"), &N));
    CHECK(D.PlaceMenu(Slot("pad-down"), &N));
    return D;
}

static void Geometry()
{
    // Every view: every target inside the window, no two overlapping.
    EditState S;
    const EditView Views[] = { EditView::Panel, EditView::Chooser, EditView::Gestures, EditView::Stick };
    for (EditView V : Views)
    {
        for (int GestureChooser = 0; GestureChooser < 2; GestureChooser++)
        {
            S.View = V;
            S.Place.Gesture = GestureChooser == 1;
            S.Place.Index = GestureChooser == 1 ? 0 : Slot("mid2");
            const std::vector<EditTarget> All = EditTargets(S);
            for (size_t i = 0; i < All.size(); i++)
            {
                const EditRect A = EditTargetRect(All[i]);
                CHECK(A.X >= 0 && A.Y >= 0 && A.X + A.W <= EDIT_WIDTH && A.Y + A.H <= EDIT_HEIGHT && A.W > 0 && A.H > 0);
                for (size_t j = i + 1; j < All.size(); j++)
                {
                    const EditRect B = EditTargetRect(All[j]);
                    CHECK(A.X + A.W <= B.X || B.X + B.W <= A.X || A.Y + A.H <= B.Y || B.Y + B.H <= A.Y);
                }
            }
            for (const EditTarget & T : All) CHECK(HitCentre(S, T) == T);
        }
    }

    // The panel is the game's own: mid1 is 56x48, its top 8 points below the panel's top.
    const EditRect Mid1 = EditTargetRect(Target(EditTargetKind::Zone, Slot("mid1")));
    CHECK(Mid1.W == 56 && Mid1.H == 48 && Mid1.X == 80 + 164 && Mid1.Y == 424 + 8);
    const EditRect Picture = EditTargetRect(Target(EditTargetKind::Zone, POINTER_ZONE_GAME));
    CHECK(Picture.X == 80 && Picture.Y == 96 && Picture.W == 640 && Picture.H == 320);
    S.View = EditView::Panel;
    CHECK(EditHit(S, 80 + 164 + 56 + 4, 424 + 20).Kind == EditTargetKind::None);   // between mid1 and mid2
    CHECK(EditHit(S, 10, 300).Kind == EditTargetKind::None);                       // left of the picture

    // What each view offers.
    S.View = EditView::Chooser;
    S.Place.Gesture = false;
    S.Place.Index = Slot("mid2");
    CHECK(EditTargets(S).size() == 4 + EDIT_CHOICE_COUNT + 2);          // top bar, choices, Toggle, Back
    S.Place.Gesture = true;
    S.Place.Index = PointerGestureIndex(POINTER_GESTURE_SMILE);
    CHECK(EditTargets(S).size() == 4 + EDIT_CHOICE_COUNT - 2 + 1);      // no Menu, Hold or Toggle
    S.View = EditView::Gestures;
    CHECK(EditTargets(S).size() == 4 + POINTER_GESTURE_COUNT);
}

static void Rules()
{
    WizardDraft D = Small();
    EditState S;
    std::string Why;

    // The picture: no menu, no hold; a toggle once a control is there.
    S.View = EditView::Chooser;
    S.Place.Index = POINTER_ZONE_GAME;
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_MENU), &Why) && Why == "the picture cannot hold the menu");
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_HOLD), &Why) && Why == "the hold cannot go on the picture");
    CHECK(EditEnabled(S, D, Target(EditTargetKind::Toggle), &Why));
    CHECK(EditLit(S, D, Target(EditTargetKind::Choice, (int)N64Control::A)));
    CHECK(EditHeader(S, D) == "the picture: A");

    // A slot: lit choices, the header, the labels.
    S.Place.Index = Slot("mid2");
    CHECK(EditHeader(S, D) == "mid2: Z, toggle");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Toggle)) == "Toggle: on");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Choice, (int)N64Control::Start)) == "St");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_NOTHING)) == "Nothing");
    S.Place.Index = Slot("mid1");
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Toggle), &Why) && Why == "a toggle needs a control in the slot");
    CHECK(EditLit(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_NOTHING)));
    CHECK(EditHeader(S, D) == "mid1: nothing");
    S.Place.Index = Slot("mid5");
    CHECK(EditHeader(S, D) == "mid5: the hold");
    S.View = EditView::Panel;
    CHECK(EditLabel(S, D, Target(EditTargetKind::Zone, Slot("mid5"))) == "Ho");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Zone, Slot("pad-down"))) == "==");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Zone, Slot("mid2"))) == "Z");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Stick)) == "Stick: pointer");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Gestures)) == "Gestures");
    CHECK(EditNotPlaced(D) == "Not placed: B  St  L  R  C^  Cv  C<  C>  D^  Dv  D<  D>");

    // A head stick: no hold, and the head directions dimmed in the gesture list.
    std::string N;
    CHECK(D.SetStickForm(EditStick::HeadDigital, &N));
    S.View = EditView::Chooser;
    S.Place.Index = Slot("mid1");
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_HOLD), &Why) && Why == "the hold needs the stick to be the pointer");
    S.View = EditView::Gestures;
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Gesture, PointerGestureIndex(POINTER_GESTURE_HEAD_UP)), &Why) && Why == "the head moves the stick");
    CHECK(EditEnabled(S, D, Target(EditTargetKind::Gesture, PointerGestureIndex(POINTER_GESTURE_SMILE)), &Why));
    CHECK(EditLabel(S, D, Target(EditTargetKind::Stick)) == "Stick: head-digital");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Gestures)) == "Back to panel");
}

static void Acts()
{
    WizardDraft D = Small();
    EditState S;

    // A slot opens its chooser; a control closes it, marks the draft changed and says what happened.
    CHECK(EditAct(&S, &D, Target(EditTargetKind::Zone, Slot("mid1"))) == EditCommand::None);
    CHECK(S.View == EditView::Chooser && !S.Place.Gesture && S.Place.Index == Slot("mid1"));
    CHECK(EditAct(&S, &D, Target(EditTargetKind::Choice, (int)N64Control::Start)) == EditCommand::None);
    CHECK(S.View == EditView::Panel && S.Dirty && S.Status == "mid1: Start");

    // Toggle stays in the chooser; Back leaves it unchanged.
    EditAct(&S, &D, Target(EditTargetKind::Zone, Slot("mid1")));
    EditAct(&S, &D, Target(EditTargetKind::Toggle));
    CHECK(S.View == EditView::Chooser && D.Toggled(Slot("mid1")) && S.Status == "mid1: Start, toggle");
    EditAct(&S, &D, Target(EditTargetKind::Back));
    CHECK(S.View == EditView::Panel);

    // A refused choice keeps the chooser, shows the reason and changes nothing.
    WizardDraft Full;
    CHECK(Full.LoadBase(TestWriteTemp(
        "bindings:\n  Stick: {stick: pointer, hold: mid5}\n"
        "  A: {zone: mid1}\n  B: {zone: mid2}\n  Z: {zone: mid3}\n  Start: {zone: mid4}\n"
        "  L: {zone: pad-up}\n  R: {zone: pad-left}\n  CUp: {zone: pad-right}\n"
        "  CDown: {zone: c-up}\n  CLeft: {zone: c-down}\n  CRight: {zone: c-left}\n  DPadUp: {zone: c-right}\n"
        "  Menu: {zone: pad-down}\n")));
    EditState F;
    const std::string Before = Full.Emit("x");
    EditAct(&F, &Full, Target(EditTargetKind::Zone, Slot("pad-down")));
    CHECK(EditAct(&F, &Full, Target(EditTargetKind::Choice, EDIT_CHOICE_NOTHING)) == EditCommand::None);
    CHECK(F.View == EditView::Chooser && !F.Dirty && F.Status == "The panel is full: free a slot for the menu first");
    CHECK(Full.Emit("x") == Before);
    // A dimmed choice says why and does nothing.
    EditAct(&F, &Full, Target(EditTargetKind::Back));
    EditAct(&F, &Full, Target(EditTargetKind::Zone, POINTER_ZONE_GAME));
    CHECK(EditAct(&F, &Full, Target(EditTargetKind::Choice, EDIT_CHOICE_MENU)) == EditCommand::None);
    CHECK(F.Status == "the picture cannot hold the menu" && Full.Emit("x") == Before);

    // Gestures: the button flips between the list and the panel; a row's chooser returns to the list.
    EditState G;
    EditAct(&G, &D, Target(EditTargetKind::Gestures));
    CHECK(G.View == EditView::Gestures && EditShowsGestures(G));
    EditAct(&G, &D, Target(EditTargetKind::Gesture, PointerGestureIndex(POINTER_GESTURE_SMILE)));
    CHECK(G.View == EditView::Chooser && G.Place.Gesture && EditShowsGestures(G));
    EditAct(&G, &D, Target(EditTargetKind::Choice, (int)N64Control::B));
    CHECK(G.View == EditView::Gestures && G.Status == "smile: B");
    EditAct(&G, &D, Target(EditTargetKind::Gestures));
    CHECK(G.View == EditView::Panel && !EditShowsGestures(G));

    // The stick: its chooser, then back to the panel.
    EditAct(&G, &D, Target(EditTargetKind::Stick));
    CHECK(G.View == EditView::Stick);
    EditAct(&G, &D, Target(EditTargetKind::StickForm, 1));
    CHECK(G.View == EditView::Panel && D.StickForm() == EditStick::Head && D.HoldZone() == POINTER_ZONE_NONE);
    CHECK(G.Status == "The stick is head; the hold is gone");

    // Save and Cancel.
    EditState C;
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Save)) == EditCommand::Save);
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Cancel)) == EditCommand::Quit);          // nothing changed
    C.Dirty = true;
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Cancel)) == EditCommand::None);
    CHECK(C.ConfirmCancel && C.Status == "Cancel again to discard your changes");
    EditAct(&C, &D, Target(EditTargetKind::Zone, Slot("mid3")));                          // any other click forgets it
    CHECK(!C.ConfirmCancel);
    EditAct(&C, &D, Target(EditTargetKind::Back));
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Cancel)) == EditCommand::None);
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Cancel)) == EditCommand::Quit);
}

void RunWizardEditTests()
{
    Geometry();
    Rules();
    Acts();
}
```

Register: `Source/Project64-sdl/UnitTest.h` gains `void RunWizardEditTests();` after `RunWizardDraftTests`; `UnitTestMain.cpp`'s `kAreas` gains `{ "wizard-edit", RunWizardEditTests },` after `wizard-draft` (before `launcher`). `Makefile`: `WIZARD_SRC` gains `EditLayout.cpp`; `UNIT_TEST_OBJS`'s wizard line becomes `$(addprefix $(BUILD)/Project64-wizard/, WizardDraftTest.o WizardDraft.o EditLayoutTest.o EditLayout.o)`; add `$(BUILD)/Project64-wizard/EditLayoutTest.o` to the two flag lines that already name `WizardDraftTest.o` (SDL and yaml).

- [ ] **Step 2: Run to verify they fail**

Run: `make unit-test only=wizard-edit`
Expected: compile error — `EditLayout.h` does not exist.

- [ ] **Step 3: Implement**

Create `Source/Project64-wizard/EditLayout.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// The panel editor's screen, with no SDL: where every target is, whether it does anything
// now, what it says, and what a click on it does to the state and the draft. EditScreen.cpp
// only paints this and feeds it clicks. Design: Docs/superpowers/specs/2026-09-24-clickable-wizard-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include "WizardDraft.h"

#include <string>
#include <vector>

#define EDIT_WIDTH 800
#define EDIT_HEIGHT 640
// Chooser choices after the fourteen controls (0 … 13, N64Control order without Stick).
#define EDIT_CHOICE_MENU 14
#define EDIT_CHOICE_HOLD 15
#define EDIT_CHOICE_NOTHING 16
#define EDIT_CHOICE_COUNT 17

enum class EditView { Panel, Chooser, Gestures, Stick };

struct EditState
{
    EditView View = EditView::Panel;
    EditPlace Place;               // what the chooser is for
    bool Dirty = false;            // changed since loaded
    bool ConfirmCancel = false;    // Cancel was clicked once with changes
    std::string Status;            // the status line
};

enum class EditTargetKind { None, Stick, Gestures, Save, Cancel, Zone, Gesture, Choice, Toggle, Back, StickForm };

struct EditTarget
{
    EditTargetKind Kind = EditTargetKind::None;
    int Index = 0;   // Zone: 0 … 13; Gesture: 0 … 10; Choice: 0 … 16; StickForm: 0 pointer, 1 head, 2 head-digital
};

inline bool operator==(EditTarget A, EditTarget B)
{
    return A.Kind == B.Kind && A.Index == B.Index;
}

struct EditRect
{
    float X, Y, W, H;
};

enum class EditCommand { None, Save, Quit };

// True while the gesture list, or a gesture's chooser, is up.
bool EditShowsGestures(const EditState & S);

// The targets the current view offers, in drawing order: the top bar first.
std::vector<EditTarget> EditTargets(const EditState & S);

EditRect EditTargetRect(EditTarget T);

// The target under a point in window coordinates, enabled or not, or None.
EditTarget EditHit(const EditState & S, float X, float Y);

// Whether T does anything now; *Why (when not null) gets the reason when not.
bool EditEnabled(const EditState & S, const WizardDraft & D, EditTarget T, std::string * Why);

// Whether T shows the current choice (the control in the place, the toggle on, the stick form).
bool EditLit(const EditState & S, const WizardDraft & D, EditTarget T);

std::string EditLabel(const EditState & S, const WizardDraft & D, EditTarget T);

// The chooser's header, e.g. "mid2: Z, toggle"; also the status after a change.
std::string EditHeader(const EditState & S, const WizardDraft & D);

// "Not placed: L  D^  Dv", or "Every control has a place".
std::string EditNotPlaced(const WizardDraft & D);

// What a click on T does. A disabled target puts its reason on the status line and does nothing.
EditCommand EditAct(EditState * S, WizardDraft * D, EditTarget T);
```

Create `Source/Project64-wizard/EditLayout.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// See EditLayout.h. The panel's rectangles come from PointerZoneRect, the game's own geometry,
// so a slot here is the slot the player clicks in play.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "EditLayout.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>

namespace
{
const EditRect kPicture = { 80, 96, 640, 320 };
const float kPanelLeft = 80.0f;
const float kPanelTop = 424.0f;
const int kChoiceColumns = 5;
const float kChoiceLeft = 84.0f, kChoiceTop = 136.0f, kChoiceW = 120.0f, kChoiceH = 48.0f, kChoiceGap = 8.0f;
const float kGestureTop = 96.0f, kGesturePitch = 42.0f;
const EditStick kForms[3] = { EditStick::Pointer, EditStick::Head, EditStick::HeadDigital };
const char * const kFormNames[3] = { "pointer", "head", "head-digital" };

bool Contains(EditRect R, float X, float Y)
{
    return X >= R.X && X < R.X + R.W && Y >= R.Y && Y < R.Y + R.H;
}

std::string PlaceName(EditPlace P)
{
    if (P.Gesture) return PointerGestureName(P.Index);
    return P.Index == POINTER_ZONE_GAME ? "the picture" : PointerZoneName(P.Index);
}

const char * StickName(EditStick Form)
{
    switch (Form)
    {
    case EditStick::Pointer: return "pointer";
    case EditStick::Head: return "head";
    case EditStick::HeadDigital: return "head-digital";
    case EditStick::Other: break;
    }
    return "other";
}

// A control's short name as the game's overlay draws it; Stick never appears here.
std::string ShortName(N64Control C)
{
    return InputConfig::ControlLabel(C);
}

bool MenuAt(const WizardDraft & D, EditPlace P)
{
    return P.Gesture ? D.MenuGesture() == P.Index : D.MenuZone() == P.Index;
}

bool HoldAt(const WizardDraft & D, EditPlace P)
{
    return !P.Gesture && D.HoldZone() == P.Index;
}
}

bool EditShowsGestures(const EditState & S)
{
    return S.View == EditView::Gestures || (S.View == EditView::Chooser && S.Place.Gesture);
}

std::vector<EditTarget> EditTargets(const EditState & S)
{
    std::vector<EditTarget> Out;
    const EditTargetKind Bar[] = { EditTargetKind::Stick, EditTargetKind::Gestures, EditTargetKind::Save, EditTargetKind::Cancel };
    for (EditTargetKind K : Bar) Out.push_back(EditTarget{ K, 0 });
    switch (S.View)
    {
    case EditView::Panel:
        for (int Z = 0; Z < POINTER_ZONE_COUNT; Z++) Out.push_back(EditTarget{ EditTargetKind::Zone, Z });
        break;
    case EditView::Chooser:
        for (int C = 0; C < EDIT_CHOICE_COUNT; C++)
        {
            if (S.Place.Gesture && (C == EDIT_CHOICE_MENU || C == EDIT_CHOICE_HOLD)) continue;
            Out.push_back(EditTarget{ EditTargetKind::Choice, C });
        }
        if (!S.Place.Gesture) Out.push_back(EditTarget{ EditTargetKind::Toggle, 0 });
        Out.push_back(EditTarget{ EditTargetKind::Back, 0 });
        break;
    case EditView::Gestures:
        for (int G = 0; G < POINTER_GESTURE_COUNT; G++) Out.push_back(EditTarget{ EditTargetKind::Gesture, G });
        break;
    case EditView::Stick:
        for (int F = 0; F < 3; F++) Out.push_back(EditTarget{ EditTargetKind::StickForm, F });
        Out.push_back(EditTarget{ EditTargetKind::Back, 0 });
        break;
    }
    return Out;
}

EditRect EditTargetRect(EditTarget T)
{
    switch (T.Kind)
    {
    case EditTargetKind::Stick: return EditRect{ 24, 40, 320, 40 };
    case EditTargetKind::Gestures: return EditRect{ 352, 40, 216, 40 };
    case EditTargetKind::Save: return EditRect{ 576, 40, 96, 40 };
    case EditTargetKind::Cancel: return EditRect{ 680, 40, 104, 40 };
    case EditTargetKind::Zone:
    {
        if (T.Index == POINTER_ZONE_GAME) return kPicture;
        float X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
        PointerZoneRect(T.Index, 640, 640, &X0, &Y0, &X1, &Y1);
        const float Top = (float)PointerGameHeight(640);   // where the game's own panel starts
        return EditRect{ kPanelLeft + X0, kPanelTop + (Y0 - Top), X1 - X0, Y1 - Y0 };
    }
    case EditTargetKind::Gesture: return EditRect{ 80, kGestureTop + T.Index * kGesturePitch, 640, 40 };
    case EditTargetKind::Choice:
    {
        const int Column = T.Index % kChoiceColumns;
        const int Row = T.Index / kChoiceColumns;
        return EditRect{ kChoiceLeft + Column * (kChoiceW + kChoiceGap), kChoiceTop + Row * (kChoiceH + kChoiceGap), kChoiceW, kChoiceH };
    }
    case EditTargetKind::Toggle: return EditRect{ 88, 360, 200, 48 };
    case EditTargetKind::Back: return EditRect{ 512, 360, 200, 48 };
    case EditTargetKind::StickForm: return EditRect{ 88 + T.Index * 208.0f, 136, 200, 48 };
    case EditTargetKind::None: break;
    }
    return EditRect{ 0, 0, 0, 0 };
}

EditTarget EditHit(const EditState & S, float X, float Y)
{
    for (const EditTarget & T : EditTargets(S))
    {
        if (Contains(EditTargetRect(T), X, Y)) return T;
    }
    return EditTarget();
}

bool EditEnabled(const EditState & S, const WizardDraft & D, EditTarget T, std::string * Why)
{
    std::string Scratch;
    if (Why == nullptr) Why = &Scratch;
    Why->clear();
    switch (T.Kind)
    {
    case EditTargetKind::Choice:
        if (T.Index == EDIT_CHOICE_MENU) return D.CanPlaceMenu(S.Place.Index, Why);
        if (T.Index == EDIT_CHOICE_HOLD) return D.CanPlaceHold(S.Place.Index, Why);
        return true;
    case EditTargetKind::Toggle: return D.CanToggle(S.Place.Index, Why);
    case EditTargetKind::Gesture: return D.CanUseGesture(T.Index, Why);
    case EditTargetKind::None: return false;
    default: return true;
    }
}

bool EditLit(const EditState & S, const WizardDraft & D, EditTarget T)
{
    switch (T.Kind)
    {
    case EditTargetKind::Choice:
    {
        const std::vector<N64Control> Here = D.Occupants(S.Place);
        if (T.Index < EDIT_CHOICE_MENU)
        {
            for (N64Control C : Here) if ((int)C == T.Index) return true;
            return false;
        }
        if (T.Index == EDIT_CHOICE_MENU) return MenuAt(D, S.Place);
        if (T.Index == EDIT_CHOICE_HOLD) return HoldAt(D, S.Place);
        return Here.empty() && !MenuAt(D, S.Place) && !HoldAt(D, S.Place);
    }
    case EditTargetKind::Toggle: return D.Toggled(S.Place.Index);
    case EditTargetKind::StickForm: return D.StickForm() == kForms[T.Index];
    default: return false;
    }
}

std::string EditLabel(const EditState & S, const WizardDraft & D, EditTarget T)
{
    switch (T.Kind)
    {
    case EditTargetKind::Stick: return std::string("Stick: ") + StickName(D.StickForm());
    case EditTargetKind::Gestures: return EditShowsGestures(S) ? "Back to panel" : "Gestures";
    case EditTargetKind::Save: return "Save";
    case EditTargetKind::Cancel: return "Cancel";
    case EditTargetKind::Back: return "Back";
    case EditTargetKind::Toggle: return D.Toggled(S.Place.Index) ? "Toggle: on" : "Toggle: off";
    case EditTargetKind::StickForm: return kFormNames[T.Index];
    case EditTargetKind::Choice:
        if (T.Index < EDIT_CHOICE_MENU) return ShortName((N64Control)T.Index);
        if (T.Index == EDIT_CHOICE_MENU) return "Menu";
        if (T.Index == EDIT_CHOICE_HOLD) return "Hold";
        return "Nothing";
    case EditTargetKind::Zone:
    {
        EditPlace P;
        P.Index = T.Index;
        if (HoldAt(D, P)) return "Ho";
        if (MenuAt(D, P)) return "==";
        const std::vector<N64Control> Here = D.Occupants(P);
        if (Here.empty()) return "";
        return ShortName(Here[0]) + (Here.size() > 1 ? "+" : "");
    }
    case EditTargetKind::Gesture:
    {
        EditPlace P;
        P.Gesture = true;
        P.Index = T.Index;
        std::string Holder;
        if (MenuAt(D, P)) Holder = "==";
        for (N64Control C : D.Occupants(P))
        {
            if (!Holder.empty()) Holder += ", ";
            Holder += WizardControlName(C);
        }
        char Line[96];
        snprintf(Line, sizeof(Line), "%-4s%-12s%s", PointerGestureTag(T.Index), PointerGestureName(T.Index), Holder.c_str());
        return Line;
    }
    case EditTargetKind::None: break;
    }
    return "";
}

std::string EditHeader(const EditState & S, const WizardDraft & D)
{
    if (S.View == EditView::Stick) return "What moves the stick:";
    std::string What;
    if (MenuAt(D, S.Place)) What = "the menu";
    else if (HoldAt(D, S.Place)) What = "the hold";
    else
    {
        for (N64Control C : D.Occupants(S.Place))
        {
            if (!What.empty()) What += " and ";
            What += WizardControlName(C);
        }
        if (What.empty()) What = "nothing";
        else if (!S.Place.Gesture && D.Toggled(S.Place.Index)) What += ", toggle";
    }
    return PlaceName(S.Place) + ": " + What;
}

std::string EditNotPlaced(const WizardDraft & D)
{
    const std::vector<N64Control> Left = D.NotPlaced();
    if (Left.empty()) return "Every control has a place";
    std::string Line = "Not placed:";
    for (N64Control C : Left) Line += "  " + ShortName(C);
    return Line;
}

EditCommand EditAct(EditState * S, WizardDraft * D, EditTarget T)
{
    if (T.Kind != EditTargetKind::Cancel) S->ConfirmCancel = false;
    std::string Why;
    if (!EditEnabled(*S, *D, T, &Why))
    {
        S->Status = Why;
        return EditCommand::None;
    }
    std::string Note;
    switch (T.Kind)
    {
    case EditTargetKind::Stick:
        S->View = EditView::Stick;
        S->Status = "Choose what moves the stick.";
        return EditCommand::None;
    case EditTargetKind::Gestures:
        S->View = EditShowsGestures(*S) ? EditView::Panel : EditView::Gestures;
        S->Status.clear();
        return EditCommand::None;
    case EditTargetKind::Save:
        return EditCommand::Save;
    case EditTargetKind::Cancel:
        if (!S->Dirty || S->ConfirmCancel) return EditCommand::Quit;
        S->ConfirmCancel = true;
        S->Status = "Cancel again to discard your changes";
        return EditCommand::None;
    case EditTargetKind::Zone:
    case EditTargetKind::Gesture:
        S->View = EditView::Chooser;
        S->Place.Gesture = T.Kind == EditTargetKind::Gesture;
        S->Place.Index = T.Index;
        S->Status.clear();
        return EditCommand::None;
    case EditTargetKind::Choice:
    {
        bool Ok;
        if (T.Index < EDIT_CHOICE_MENU) Ok = D->PlaceControl(S->Place, (N64Control)T.Index, &Note);
        else if (T.Index == EDIT_CHOICE_MENU) Ok = D->PlaceMenu(S->Place.Index, &Note);
        else if (T.Index == EDIT_CHOICE_HOLD) Ok = D->PlaceHold(S->Place.Index, &Note);
        else Ok = D->PlaceNothing(S->Place, &Note);
        if (!Ok)
        {
            S->Status = Note;
            return EditCommand::None;
        }
        S->Dirty = true;
        S->Status = EditHeader(*S, *D) + (Note.empty() ? "" : "; " + Note);
        S->View = S->Place.Gesture ? EditView::Gestures : EditView::Panel;
        return EditCommand::None;
    }
    case EditTargetKind::Toggle:
        if (!D->SetToggle(S->Place.Index, !D->Toggled(S->Place.Index), &Note))
        {
            S->Status = Note;
            return EditCommand::None;
        }
        S->Dirty = true;
        S->Status = EditHeader(*S, *D);
        return EditCommand::None;
    case EditTargetKind::Back:
        S->View = (S->View == EditView::Chooser && S->Place.Gesture) ? EditView::Gestures : EditView::Panel;
        S->Status.clear();
        return EditCommand::None;
    case EditTargetKind::StickForm:
    {
        const EditStick Before = D->StickForm();
        if (!D->SetStickForm(kForms[T.Index], &Note))
        {
            S->Status = Note;
            return EditCommand::None;
        }
        if (D->StickForm() != Before) S->Dirty = true;
        S->Status = std::string("The stick is ") + StickName(D->StickForm()) + (Note.empty() ? "" : "; " + Note);
        S->View = EditView::Panel;
        return EditCommand::None;
    }
    case EditTargetKind::None:
        break;
    }
    return EditCommand::None;
}
```

- [ ] **Step 4: Run to verify they pass**

Run: `make -j8 all && make unit-test`
Expected: `ok: wizard-edit` between `wizard-draft` and `launcher`; everything else `ok:`.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-wizard/EditLayout.h Source/Project64-wizard/EditLayout.cpp Source/Project64-wizard/EditLayoutTest.cpp Source/Project64-sdl/UnitTest.h Source/Project64-sdl/UnitTestMain.cpp Makefile
git commit -m "Add the panel editor's layout and click logic, tested in a new wizard-edit area

<your attribution trailer>"
```

---

### Task 5: The editor window, `--edit`, and its self-test

**Files:**
- Create: `Source/Project64-wizard/EditScreen.h`, `Source/Project64-wizard/EditScreen.cpp`
- Modify: `Source/Project64-wizard/main.cpp`, `Source/Project64-wizard/Screens.h`, `Source/Project64-wizard/Screens.cpp`, `Source/Project64-wizard/SyntheticEvents.h`, `Scripts/wizard_selftest.sh`, `Makefile`

**Interfaces:**
- Consumes: Tasks 2-4; `ExecutableDirectory()` (Task 1); `DebugText.h`.
- Produces: `void EditDraw(SDL_Renderer *, const EditState &, const WizardDraft &, const char * Title, EditTarget Hover, uint32_t Gestures, uint32_t Face)`; `EditCommand EditHandleEvent(const SDL_Event &, EditState *, WizardDraft *, EditTarget * Hover, EditTarget * Pressed)`; `const char * WizardFaceStatus(uint32_t Face)` (Screens.h, renamed from the static `FaceStatusText`); `ReleaseEvent(X, Y)` in `SyntheticEvents.h`; `Project64-wizard --edit <rom>`.

- [ ] **Step 1: The shared pieces**

`Screens.cpp`: rename `static const char * FaceStatusText(uint32_t Face)` to `const char * WizardFaceStatus(uint32_t Face)` (not static), update its one caller; declare it in `Screens.h` after `WizardTextFit`:

```cpp
// What the camera is doing, in one line for a gesture list: "tracking", "camera off: …".
const char * WizardFaceStatus(uint32_t Face);
```

`SyntheticEvents.h`, after `ClickEvent`:

```cpp
// The release that completes ClickEvent, for screens that act on release (the panel editor).
static inline SDL_Event ReleaseEvent(float X, float Y)
{
    SDL_Event E = ClickEvent(X, Y);
    E.type = SDL_EVENT_MOUSE_BUTTON_UP;
    return E;
}
```

- [ ] **Step 2: Write `EditScreen.h` and `EditScreen.cpp`**

`EditScreen.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// The panel editor's window side: paints EditLayout's targets and turns a press-and-release
// into EditAct. No keyboard handling: a one-button player drives it with the pointer alone.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include "EditLayout.h"

#include <SDL3/SDL.h>

void EditDraw(SDL_Renderer * Renderer, const EditState & S, const WizardDraft & D, const char * Title,
              EditTarget Hover, uint32_t Gestures, uint32_t Face);

// One event. Motion updates *Hover; a left press records *Pressed; a left release on the
// target it was pressed on acts. Closing the window is a click on Cancel.
EditCommand EditHandleEvent(const SDL_Event & E, EditState * S, WizardDraft * D, EditTarget * Hover, EditTarget * Pressed);
```

`EditScreen.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// See EditScreen.h. Everything shown comes from EditLayout; this file only paints it.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "EditScreen.h"
#include "Screens.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <Project64-sdl/DebugText.h>

#include <string>

namespace
{
const int kScale = 2;
const float kGlyph = (float)(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * kScale);   // 16
const int kLineChars = 47;   // a line from x 24 at kScale

void Fill(SDL_Renderer * R, EditRect Rect, Uint8 Red, Uint8 Green, Uint8 Blue)
{
    SDL_SetRenderDrawColor(R, Red, Green, Blue, 255);
    const SDL_FRect F = { Rect.X, Rect.Y, Rect.W, Rect.H };
    SDL_RenderFillRect(R, &F);
}

// Bright for text that acts, dim for text that does not, gold for the current choice.
void Ink(SDL_Renderer * R, bool Bright, bool Lit)
{
    if (Lit) SDL_SetRenderDrawColor(R, 255, 220, 120, 255);
    else if (Bright) SDL_SetRenderDrawColor(R, 220, 220, 220, 255);
    else SDL_SetRenderDrawColor(R, 90, 90, 96, 255);
}

void Centred(SDL_Renderer * R, EditRect Rect, const std::string & Text)
{
    const float W = (float)Text.size() * kGlyph;
    DebugText(R, Rect.X + (Rect.W - W) / 2, Rect.Y + (Rect.H - kGlyph) / 2, kScale, Text.c_str());
}

void Button(SDL_Renderer * R, EditRect Rect, const std::string & Label, bool On, bool Lit, bool Hot)
{
    if (!On) Fill(R, Rect, 28, 28, 32);
    else if (Hot) Fill(R, Rect, 70, 90, 140);
    else if (Lit) Fill(R, Rect, 72, 62, 30);
    else Fill(R, Rect, 40, 44, 56);
    Ink(R, On, On && Lit);
    Centred(R, Rect, Label);
}

void Panel(SDL_Renderer * R, const EditState & S, const WizardDraft & D, EditTarget Hover)
{
    for (int Z = 0; Z < POINTER_ZONE_COUNT; Z++)
    {
        const EditTarget T = { EditTargetKind::Zone, Z };
        const EditRect Rect = EditTargetRect(T);
        const bool Hot = S.View == EditView::Panel && Hover == T;
        if (Hot) Fill(R, Rect, 70, 90, 140);
        else if (Z == POINTER_ZONE_GAME) Fill(R, Rect, 28, 28, 34);
        else Fill(R, Rect, 44, 44, 52);
        Ink(R, true, false);
        Centred(R, Rect, EditLabel(S, D, T));
        if (D.Toggled(Z))
        {
            // A toggle slot's mark: a gold bar along its foot.
            Fill(R, EditRect{ Rect.X, Rect.Y + Rect.H - 4, Rect.W, 4 }, 255, 220, 120);
        }
    }
}
}

void EditDraw(SDL_Renderer * R, const EditState & S, const WizardDraft & D, const char * Title,
              EditTarget Hover, uint32_t Gestures, uint32_t Face)
{
    SDL_SetRenderDrawColor(R, 16, 16, 20, 255);
    SDL_RenderClear(R);
    Ink(R, true, false);
    DebugText(R, 24, 12, kScale, DebugTextFitHead(Title, kLineChars).c_str());

    // The picture and the panel stay behind a chooser, so a change is made in context; only
    // the gesture list replaces them.
    if (S.View != EditView::Gestures) Panel(R, S, D, Hover);
    if (S.View == EditView::Chooser || S.View == EditView::Stick)
    {
        const EditRect Picture = EditTargetRect(EditTarget{ EditTargetKind::Zone, POINTER_ZONE_GAME });
        Fill(R, Picture, 24, 26, 32);
        Ink(R, true, false);
        DebugText(R, Picture.X + 8, Picture.Y + 8, kScale, DebugTextFitHead(EditHeader(S, D).c_str(), 39).c_str());
    }

    for (const EditTarget & T : EditTargets(S))
    {
        if (T.Kind == EditTargetKind::Zone) continue;   // drawn with the panel
        const bool On = EditEnabled(S, D, T, nullptr);
        const bool Hot = On && Hover == T;
        const EditRect Rect = EditTargetRect(T);
        if (T.Kind == EditTargetKind::Gesture)
        {
            const bool Firing = (Gestures & (1u << T.Index)) != 0;
            if (Hot) Fill(R, Rect, 70, 90, 140);
            else Fill(R, Rect, 24, 26, 32);
            Ink(R, On, On && Firing);
            DebugText(R, Rect.X + 8, Rect.Y + (Rect.H - kGlyph) / 2, kScale, EditLabel(S, D, T).c_str());
            continue;
        }
        Button(R, Rect, EditLabel(S, D, T), On, EditLit(S, D, T), Hot);
    }
    if (S.View == EditView::Gestures)
    {
        Ink(R, true, false);
        DebugText(R, 80, 564, kScale, DebugTextFitHead(WizardFaceStatus(Face), 40).c_str());
    }

    Ink(R, true, false);
    DebugText(R, 24, 592, kScale, DebugTextFitHead(EditNotPlaced(D).c_str(), kLineChars).c_str());
    // At twice the font's size when it fits, else at its own, so the end is never cut.
    const bool Fits = (int)S.Status.size() <= kLineChars;
    DebugText(R, 24, 616, Fits ? kScale : 1, DebugTextFitHead(S.Status.c_str(), 2 * kLineChars).c_str());
}

EditCommand EditHandleEvent(const SDL_Event & E, EditState * S, WizardDraft * D, EditTarget * Hover, EditTarget * Pressed)
{
    switch (E.type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return EditAct(S, D, EditTarget{ EditTargetKind::Cancel, 0 });
    case SDL_EVENT_MOUSE_MOTION:
        *Hover = EditHit(*S, E.motion.x, E.motion.y);
        return EditCommand::None;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (E.button.button == SDL_BUTTON_LEFT) *Pressed = EditHit(*S, E.button.x, E.button.y);
        return EditCommand::None;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        if (E.button.button != SDL_BUTTON_LEFT) return EditCommand::None;
        const EditTarget Released = EditHit(*S, E.button.x, E.button.y);
        const EditTarget Was = *Pressed;
        *Pressed = EditTarget();
        if (Released.Kind == EditTargetKind::None || !(Released == Was)) return EditCommand::None;
        return EditAct(S, D, Released);
    }
    default:
        return EditCommand::None;
    }
}
```

- [ ] **Step 3: `--edit` in `main.cpp`**

Add includes `"EditScreen.h"` and `<Project64-sdl/ExecutablePath.h>`, `<string>`. Above `main`:

```cpp
// The ROM's file name, for the editor's title.
static std::string FileName(const char * Path)
{
    const char * Slash = strrchr(Path, '/');
    return Slash != nullptr ? Slash + 1 : Path;
}

// Saves beside the ROM and says so on stderr, or puts the reason on the status line.
static bool SaveAndReport(WizardDraft & Draft, const char * Rom, const std::string & BaseName, EditState * S)
{
    std::string Saved;
    bool MadeOrig = false;
    if (Draft.SaveBesideRom(Rom, BaseName.c_str(), &Saved, &MadeOrig))
    {
        if (MadeOrig) fprintf(stderr, "wizard: kept the original as %s.orig\n", Saved.c_str());
        fprintf(stderr, "wizard: saved %s\n", Saved.c_str());
        return true;
    }
    S->Status = std::string("Cannot save beside the ROM: ") + Draft.Error();
    fprintf(stderr, "wizard: cannot save %s: %s\n", Saved.c_str(), Draft.Error());
    return false;
}

// PJ64_EDIT_SELFTEST=1: the scripted edit behind Scripts/wizard_selftest.sh and the launcher's
// self-test, driven through the real editor handler with no window. On the generic layout it
// moves L onto mid4 as a toggle (R leaves the panel), puts the hold on pad-up, R on pad-down
// (so the menu moves to mid5, freed by the hold) and Z on the picture (A leaves), then saves.
static int EditScript(EditState & S, WizardDraft & Draft, const char * Rom, const std::string & BaseName)
{
    const EditTarget Steps[] = {
        { EditTargetKind::Zone, PointerZoneFromName("mid4") }, { EditTargetKind::Choice, (int)N64Control::L },
        { EditTargetKind::Zone, PointerZoneFromName("mid4") }, { EditTargetKind::Toggle, 0 }, { EditTargetKind::Back, 0 },
        { EditTargetKind::Zone, PointerZoneFromName("pad-up") }, { EditTargetKind::Choice, EDIT_CHOICE_HOLD },
        { EditTargetKind::Zone, PointerZoneFromName("pad-down") }, { EditTargetKind::Choice, (int)N64Control::R },
        { EditTargetKind::Zone, POINTER_ZONE_GAME }, { EditTargetKind::Choice, (int)N64Control::Z },
        { EditTargetKind::Save, 0 },
    };
    EditTarget Hover, Pressed;
    for (const EditTarget & T : Steps)
    {
        const EditRect R = EditTargetRect(T);
        const float X = R.X + R.W / 2, Y = R.Y + R.H / 2;
        EditHandleEvent(ClickEvent(X, Y), &S, &Draft, &Hover, &Pressed);
        const EditCommand C = EditHandleEvent(ReleaseEvent(X, Y), &S, &Draft, &Hover, &Pressed);
        if (!S.Status.empty()) fprintf(stderr, "wizard: %s\n", S.Status.c_str());
        if (C == EditCommand::Save) return SaveAndReport(Draft, Rom, BaseName, &S) ? 0 : 1;
    }
    fprintf(stderr, "wizard: the scripted edit never reached Save\n");
    return 1;
}

// --edit <rom>: the panel editor for one game (Docs/superpowers/specs/2026-09-24-clickable-wizard-design.md).
static int RunEditor(const char * Rom)
{
    WizardDraft Draft;
    EditState S;
    const std::string ExeDir = ExecutableDirectory();
    const std::string Loaded = Draft.LoadForRom(Rom, ExeDir.c_str(), &S.Status);
    if (Loaded.empty())
    {
        fprintf(stderr, "wizard: cannot load a layout for %s: %s\n", Rom, Draft.Error());
        return 1;
    }
    fprintf(stderr, "wizard: editing %s from %s\n", Rom, Loaded.c_str());
    std::string MenuNote;
    if (Draft.EnsureMenu(&MenuNote)) S.Status = S.Status.empty() ? MenuNote : S.Status + "; " + MenuNote;
    if (S.Status.empty()) S.Status = "Click a slot or the picture to change it.";
    const std::string BaseName = FileName(Loaded.c_str());
    const std::string Title = "Layout for " + FileName(Rom);

    const char * Script = getenv("PJ64_EDIT_SELFTEST");
    if (Script != nullptr && strcmp(Script, "1") == 0) return EditScript(S, Draft, Rom, BaseName);

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window * Window = SDL_CreateWindow("Project64 layout editor", EDIT_WIDTH, EDIT_HEIGHT, 0);
    SDL_Renderer * Renderer = Window != nullptr ? SDL_CreateRenderer(Window, nullptr) : nullptr;
    if (Renderer == nullptr)
    {
        fprintf(stderr, "wizard: cannot open a window: %s\n", SDL_GetError());
        if (Window != nullptr) SDL_DestroyWindow(Window);
        SDL_Quit();
        return 1;
    }

    memset(&g_State, 0, sizeof(g_State));
    bool CameraStarted = false;
    bool CameraAsked = false;
    EditTarget Hover, Pressed;
    bool Running = true;
    while (Running)
    {
        SDL_Event E;
        while (Running && SDL_PollEvent(&E))
        {
            const EditCommand C = EditHandleEvent(E, &S, &Draft, &Hover, &Pressed);
            if (C == EditCommand::Quit) Running = false;
            else if (C == EditCommand::Save && SaveAndReport(Draft, Rom, BaseName, &S)) Running = false;
        }
        // The camera starts the first time the gesture list opens, and never before; PJ64_FACE=0 keeps it shut.
        if (!CameraAsked && EditShowsGestures(S))
        {
            CameraAsked = true;
            const char * Off = getenv("PJ64_FACE");
            if (Off != nullptr && strcmp(Off, "0") == 0) g_State.Face.store(FACE_OFF, std::memory_order_relaxed);
            else CameraStarted = FaceTrackerStart(&g_State);
        }
        EditDraw(Renderer, S, Draft, Title.c_str(), Hover,
                 g_State.Gestures.load(std::memory_order_relaxed), g_State.Face.load(std::memory_order_relaxed));
        SDL_RenderPresent(Renderer);
        SDL_Delay(16);
    }
    if (CameraStarted) FaceTrackerStop();
    SDL_DestroyRenderer(Renderer);
    SDL_DestroyWindow(Window);
    SDL_Quit();
    return 0;
}
```

In `main`, after the `--screenshots` branch:

```cpp
    if (argc >= 2 && strcmp(argv[1], "--edit") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "usage: %s --edit <rom>\n", argv[0]);
            return 2;
        }
        return RunEditor(argv[2]);
    }
```

(If `memset` of `g_State` is not how the walk resets it, reuse whatever the walk's loop does before its first frame. `FACE_OFF` and the `Face`/`Gestures` atomics are the ones the walk already uses.)

`Makefile`: `WIZARD_SRC` gains `EditScreen.cpp`; `wizard-selftest` becomes `wizard-selftest: wizard config ## …` (the editor run loads the installed `Config/mouse/default.yaml`) and its help text mentions the editor.

- [ ] **Step 4: The self-test's second run**

Append to `Scripts/wizard_selftest.sh`, before its final `echo`:

```sh
# The panel editor: --edit with PJ64_EDIT_SELFTEST=1 drives the real editor screen with scripted
# clicks and no window (main.cpp's EditScript), starting from the generic layout because the
# ROM has none of its own. Three runs: the first writes the layout and no .orig; the second
# saves over it and keeps it as .orig; the third must leave that .orig alone.
EDIR=$(mktemp -d /tmp/pj64-edit-selftest-XXXXXX)
trap 'rm -f "$OUT" "$OUT.expected" "$OUT.got"; rm -rf "$EDIR"' EXIT
: > "$EDIR/game.z64"
edit_run() {
    PJ64_FACE=0 PJ64_EDIT_SELFTEST=1 perl -e 'alarm 30; exec @ARGV' -- "$BIN" --edit "$EDIR/game.z64" \
        2>"$EDIR/log" || { cat "$EDIR/log" >&2; echo "wizard-selftest: --edit exited non-zero" >&2; exit 1; }
}
edit_run
[ ! -e "$EDIR/game.yaml.orig" ] || { echo "wizard-selftest: the first save made a .orig" >&2; exit 1; }
cat > "$OUT.expected" <<'YAML'
bindings:
  B:         {zone: mid3}
  Z:         {zone: game}
  Start:     {zone: mid1}
  L:         {zone: mid4, toggle: true}
  R:         {zone: pad-down}
  CUp:       {zone: c-up}
  CDown:     {zone: c-down}
  CLeft:     {zone: c-left}
  CRight:    {zone: c-right}
  DPadLeft:  {zone: pad-left}
  DPadRight: {zone: pad-right}
  Stick:     {stick: pointer, hold: pad-up}
  Menu:      {zone: mid5}
YAML
sed -n '/^bindings:/,$p' "$EDIR/game.yaml" > "$OUT.got"
if ! diff -u "$OUT.expected" "$OUT.got"; then
    echo "wizard-selftest: the editor's layout differs from what the scripted clicks should produce" >&2
    exit 1
fi
cp "$EDIR/game.yaml" "$EDIR/first.yaml"
edit_run
cmp -s "$EDIR/game.yaml.orig" "$EDIR/first.yaml" || { echo "wizard-selftest: the second save did not keep the first as .orig" >&2; exit 1; }
edit_run
cmp -s "$EDIR/game.yaml.orig" "$EDIR/first.yaml" || { echo "wizard-selftest: a third save touched the .orig" >&2; exit 1; }
[ ! -e "$EDIR/game.yaml.tmp" ] || { echo "wizard-selftest: a temporary file was left behind" >&2; exit 1; }
echo "ok: wizard edit selftest"
```

(Merge the existing `trap` into the new one rather than keeping two; the first run's `ok: wizard selftest` line stays.)

- [ ] **Step 5: Build and run**

Run: `make -j8 all && make test && make unit-test && make wizard-selftest`
Expected: no warnings from the new files; `ok: wizard selftest` and `ok: wizard edit selftest`.

Do not open the editor window yourself (a plain `--edit` run waits for a person). The launcher's self-test (Task 7) drives it from the launcher.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-wizard/EditScreen.h Source/Project64-wizard/EditScreen.cpp Source/Project64-wizard/main.cpp Source/Project64-wizard/Screens.h Source/Project64-wizard/Screens.cpp Source/Project64-wizard/SyntheticEvents.h Scripts/wizard_selftest.sh Makefile
git commit -m "Add the panel editor's window, Project64-wizard --edit, and a scripted self-test of it

<your attribution trailer>"
```

---

### Task 6: The editor's pictures

**Files:**
- Modify: `Source/Project64-wizard/Screenshots.cpp`, `Scripts/wizard_screenshots_check.sh`
- Create: `Docs/img/wizard/12-edit-panel.png`, `13-edit-chooser.png`, `14-edit-gestures.png` (generated)

**Interfaces:**
- Consumes: `EditDraw`, `EditAct`, the draft rules.

- [ ] **Step 1: Extend the tour**

In `Screenshots.cpp`, include `"EditScreen.h"`; add, after `Capture`:

```cpp
// One picture of the panel editor, drawn the way its window loop draws it.
void CaptureEdit(Tour & T, const char * Name, const EditState & S, const WizardDraft & D, uint32_t Gestures, uint32_t Face)
{
    if (T.Failed) return;
    EditDraw(T.Renderer, S, D, "Layout for super_mario_64_usa.z64", EditTarget(), Gestures, Face);
    SDL_RenderPresent(T.Renderer);
    char Path[1024];
    snprintf(Path, sizeof(Path), "%s/%s", T.Dir, Name);
    if (!SDL_SavePNG(T.Surface, Path))
    {
        fprintf(stderr, "wizard --screenshots: %s: %s\n", Path, SDL_GetError());
        T.Failed = true;
        return;
    }
    T.Written++;
}
```

and after `Walk`:

```cpp
// 12-14: the panel editor. Its draft is built in code from the built-in bindings — the
// generic layout's shape — never read from a file, so nothing machine-specific can show.
void EditWalk(Tour & T)
{
    WizardDraft D;
    std::string N;
    const auto At = [](const char * Name) { EditPlace P; P.Index = PointerZoneFromName(Name); return P; };
    EditPlace Picture;
    Picture.Index = POINTER_ZONE_GAME;
    bool Ok = D.SetStickForm(EditStick::Pointer, &N) && D.PlaceControl(Picture, N64Control::A, &N) &&
              D.PlaceControl(At("mid1"), N64Control::Start, &N) && D.PlaceControl(At("mid2"), N64Control::Z, &N) &&
              D.SetToggle(PointerZoneFromName("mid2"), true, &N) && D.PlaceControl(At("mid3"), N64Control::B, &N) &&
              D.PlaceControl(At("mid4"), N64Control::R, &N) && D.PlaceHold(PointerZoneFromName("mid5"), &N) &&
              D.PlaceControl(At("pad-up"), N64Control::L, &N) && D.PlaceControl(At("c-up"), N64Control::CUp, &N) &&
              D.PlaceControl(At("c-down"), N64Control::CDown, &N) && D.PlaceControl(At("c-left"), N64Control::CLeft, &N) &&
              D.PlaceControl(At("c-right"), N64Control::CRight, &N) && D.PlaceControl(At("pad-left"), N64Control::DPadLeft, &N) &&
              D.PlaceControl(At("pad-right"), N64Control::DPadRight, &N) && D.EnsureMenu(&N);
    if (!Expect(T, Ok && D.MenuZone() == PointerZoneFromName("pad-down"), "the editor's draft did not build")) return;

    // 12: the panel, as the editor opens.
    EditState S;
    S.Status = "Click a slot or the picture to change it.";
    CaptureEdit(T, "12-edit-panel.png", S, D, 0, FACE_OFF);

    // 13: mid2's chooser: Z lit, Toggle on.
    EditAct(&S, &D, EditTarget{ EditTargetKind::Zone, PointerZoneFromName("mid2") });
    if (!Expect(T, S.View == EditView::Chooser, "a click on mid2 did not open its chooser")) return;
    CaptureEdit(T, "13-edit-chooser.png", S, D, 0, FACE_OFF);
    EditAct(&S, &D, EditTarget{ EditTargetKind::Back, 0 });

    // 14: the gesture list after Start moved onto mouth-open, which is firing.
    EditAct(&S, &D, EditTarget{ EditTargetKind::Gestures, 0 });
    EditAct(&S, &D, EditTarget{ EditTargetKind::Gesture, PointerGestureIndex(POINTER_GESTURE_MOUTH_OPEN) });
    EditAct(&S, &D, EditTarget{ EditTargetKind::Choice, (int)N64Control::Start });
    if (!Expect(T, S.View == EditView::Gestures, "Start on mouth-open did not return to the list")) return;
    CaptureEdit(T, "14-edit-gestures.png", S, D, POINTER_GESTURE_MOUTH_OPEN, FACE_TRACKING);
}
```

In `WizardScreenshots`, call `if (!T.Failed) EditWalk(T);` after `Walk(T);`, and change the count check from 11 to 14 (both the `!=` and the message). Update the header comment: the tour now also pictures the panel editor, from a draft built in code.

`Scripts/wizard_screenshots_check.sh`: the comment's "eleven" becomes "fourteen" and `STOPS` gains `12-edit-panel.png 13-edit-chooser.png 14-edit-gestures.png`.

- [ ] **Step 2: Render, look, commit the pictures**

Run: `make wizard-screenshots && make unit-test`
Expected: three new PNGs under `Docs/img/wizard/`; 01-11 byte-identical (`git status` shows only the three new files); `ok: wizard screenshots`.

Open the three PNGs (they are files the tour wrote, not screen captures) and check each shows what its comment says. If text overruns a button or the line, fix the label budget in `EditLayout`/`EditScreen`, not the picture.

- [ ] **Step 3: Commit**

```bash
git add Source/Project64-wizard/Screenshots.cpp Scripts/wizard_screenshots_check.sh Docs/img/wizard/12-edit-panel.png Docs/img/wizard/13-edit-chooser.png Docs/img/wizard/14-edit-gestures.png
git commit -m "Picture the panel editor in the screenshot tour: the panel, a chooser, the gesture list

<your attribution trailer>"
```

---

### Task 7: The launcher's Edit button

**Files:**
- Modify: `Source/Project64-launcher/LauncherModel.h`, `LauncherModel.cpp`, `LauncherModelTest.cpp`, `Screens.cpp`, `main.cpp`
- Modify: `Scripts/launcher_selftest.sh`

**Interfaces:**
- Consumes: `Project64-wizard --edit` (Task 5), `PJ64_EDIT_SELFTEST`.
- Produces: `LauncherTargetKind::Edit`; `LauncherCommand::Edit`; `LauncherState::EditorFound` (default `false`); `bool LauncherHasEditor(const std::string & EmulatorDir)`.

- [ ] **Step 1: Write the failing tests**

In `LauncherModelTest.cpp`:
- `Screen()`: the count check becomes `CHECK(All.size() == 3 + 1 + 26 + LAUNCHER_ROWS + LAUNCHER_ROWS + 2 + 1);`.
- After the "25 games" block's hit checks, add:

```cpp
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
```

(Place it where `S` is the 25-game state on page 1 and `G` is declared — move `const LauncherGame * G = nullptr;` above it if needed.) The earlier loop `for (const LauncherTarget & Each : All) CHECK(!LauncherEnabled(S, Each) || HitCentre(S, Each) == Each);` must still pass with Edit targets present.
- In `ScanAndEmulator()`, after the emulator checks:

```cpp
    CHECK(!LauncherHasEditor(Real));
    TestTouch(Emu + "/Project64-wizard", 0755);
    CHECK(LauncherHasEditor(Real));
```

- [ ] **Step 2: Run to verify they fail**

Run: `make unit-test only=launcher`
Expected: compile errors — `Edit`, `EditorFound`, `LauncherHasEditor` do not exist.

- [ ] **Step 3: Implement the model**

`LauncherModel.h`: `LauncherTargetKind` gains `Edit` after `Row` (`Index`: the row); `LauncherCommand` gains `Edit`; `LauncherState` gains `bool EditorFound = false;   // Project64-wizard sits beside the emulator: Edit works`; declare, after `LauncherFindEmulator`:

```cpp
// True when an executable Project64-wizard sits in EmulatorDir, the layout editor Edit runs.
bool LauncherHasEditor(const std::string & EmulatorDir);
```

`LauncherModel.cpp`:
- Generalise `HoldsEmulator(Dir)` to `HoldsExecutable(Dir, Name)` (the same stat, `S_ISREG` and `X_OK` checks on `Dir + "/" + Name`); `LauncherFindEmulator` calls it with `"Project64"`; add `bool LauncherHasEditor(const std::string & EmulatorDir) { return HoldsExecutable(EmulatorDir, "Project64-wizard"); }`.
- `LauncherTargets()`: after the rows loop, `for (int i = 0; i < LAUNCHER_ROWS; i++) Targets.push_back(LauncherTarget{ LauncherTargetKind::Edit, i });`.
- `LauncherTargetRect`: `Row` becomes `LauncherRect{ 16, kRowTop + T.Index * kRowHeight, 676, kRowHeight - 2 }`; add `case LauncherTargetKind::Edit: return LauncherRect{ 700, kRowTop + T.Index * kRowHeight, 84, kRowHeight - 2 };`.
- `LauncherEnabled`: `case LauncherTargetKind::Edit: return S.EditorFound && T.Index >= 0 && T.Index < LauncherRowCount(S);`.
- `LauncherAct`: `case LauncherTargetKind::Edit: *Game = LauncherRowGame(*S, T.Index); return LauncherCommand::Edit;`.

`Screens.cpp` (launcher): the budgets become `kRowChars = 41` (a title with nothing beside it: 656 of the row's 676 points) and `kTitleChars = 33` (leaving room for "generic"); the `switch` gains

```cpp
        case LauncherTargetKind::Edit:
            if (LauncherRowGame(S, T.Index) != nullptr) Button(R, Rect, "Edit", On, Hot);
            break;
```

- [ ] **Step 4: Implement the child**

`Source/Project64-launcher/main.cpp`:
- `Launcher` gains `bool ChildIsEditor = false;` and `int EditorsEnded = 0;`.
- After finding the emulator: `L.State.EditorFound = L.State.EmulatorFound && LauncherHasEditor(L.EmulatorDir);`.
- `StartGame(L, Game)` becomes `StartChild(L, Game, bool Editor)`: the executable is `L.EmulatorDir + (Editor ? "/Project64-wizard" : "/Project64")`, and the argument list is built as a vector — `{ Exe, "--edit", Game.Path }` for the editor, `{ Exe, Game.Path }` for a game — ending in `nullptr`. Same environment (`LauncherChildEnv`) and `posix_spawn` path. On success it sets `L.ChildIsEditor = Editor`; prints `launcher: editing <rom>` for the editor instead of the `started` lines; arms `KillAt` only for a game. A spawn failure sets the status to `<title>: the layout editor could not start: <reason>` for the editor (the game wording stays for games).
- `PollChild`: after reaping, when `L.ChildIsEditor`:

```cpp
        L.EditorsEnded++;
        fprintf(stderr, "launcher: editor ended (%s)\n", How);
        L.Status = Clean ? std::string() : L.ChildGame.Title + ": the layout editor ended with an error (" + How + ")";
        Rescan(L);
        RebuildRecent(L);
```

  and the existing game branch (`GamesEnded`, `game ended`, the status, `LauncherPushRecent`, `RebuildRecent`, `Save`) otherwise. Showing and raising the window and `launcher: window back` stay common.
- `ActOnClick`: `case LauncherCommand::Start` calls `StartChild(L, Copy, false)`; add

```cpp
    case LauncherCommand::Edit:
        if (Game != nullptr)
        {
            const LauncherGame Copy = *Game;
            StartChild(L, Copy, true);
        }
        break;
```

- `SelftestStep`: the `Step == 2` branch, on success, pushes a click on `LauncherTarget{ LauncherTargetKind::Edit, 0 }` and sets `*Step = 3` instead of returning 0 (fail as before if the recent games are wrong; fail with `launcher: selftest failed: no Project64-wizard beside the emulator` when `!L.State.EditorFound`). Add:

```cpp
    else if (*Step == 3 && L.EditorsEnded == 1)
    {
        if (!L.State.Games.empty() && !L.State.Games[0].Generic)
        {
            fprintf(stderr, "launcher: selftest ok\n");
            return 0;
        }
        fprintf(stderr, "launcher: selftest failed: the edited game still has no layout of its own\n");
        return 1;
    }
```

  and update the function's comment (a third step: Edit on the first row, which `PJ64_EDIT_SELFTEST=1` makes the editor script and save).

- [ ] **Step 5: The self-test script**

`Scripts/launcher_selftest.sh`:
- Require the wizard: `[ -x "$ROOT/Bin/macOS/Project64-wizard" ] || { echo "wizard not built (make all)" >&2; exit 1; }`.
- The launcher run gains `PJ64_EDIT_SELFTEST=1` in its environment (it reaches the editor through `LauncherChildEnv`; the games ignore it).
- New checks, beside the existing ones:

```sh
need "^launcher: editing $WORK/games/nolayout.z64\$"
need "^launcher: editor ended (exit 0)\$"
count "^launcher: window back\$" 3
[ -f "$WORK/games/nolayout.yaml" ] || fail "the editor did not save nolayout.yaml beside the ROM"
grep -q '^  Menu:' "$WORK/games/nolayout.yaml" || fail "the edited layout has no Menu line"
```

  (the existing `count "^launcher: window back\$" 2` becomes 3; `started` stays 2.) Update the header comment: a third step edits the first game.

- [ ] **Step 6: Build and run**

Run: `make -j8 all && make test && make unit-test && make launcher-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: no warnings from the launcher; `ok: launcher`; `ok: launcher selftest` (it opens windows briefly; `PJ64_FACE=0`). Run the launcher self-test twice.

- [ ] **Step 7: Commit**

```bash
git add Source/Project64-launcher Scripts/launcher_selftest.sh
git commit -m "Add Edit to every launcher row: it opens the game's layout in the panel editor and rescans when it closes

<your attribution trailer>"
```

---

### Task 8: Documentation

**Files:**
- Modify: `Docs/UserGuide.md`, `AGENTS.md`

- [ ] **Step 1: The user guide**

Read the code each sentence describes before writing it (`EditLayout.cpp`, `WizardDraft.cpp`'s editing rules and `SaveBesideRom`, the launcher's `Screens.cpp`).

- Section 6, "The binding wizard": a new subsection at its end, `### Editing a game's layout by clicking`, in the guide's voice and ~90-column lines, covering: opening it with `Edit` in the launcher (or `./Bin/macOS/Project64-wizard --edit <rom>`); pictures 12, 13 and 14 with alt text; what the panel shows (the game's real slots, `Ho`, `==`, a gold bar for a toggle, "Not placed"); the chooser (controls, `Menu`, `Hold`, `Nothing`, `Toggle`, `Back`, and why a choice is dimmed); the rules (one thing per slot, a control moves, a displaced control is not placed and keeps its built-in key and gamepad pair, the menu always has a place and moves by the play-time order, the full-panel refusal, the hold only with the pointer, a head stick clearing the head gestures); gestures (the list, the camera lighting, `PJ64_FACE=0` / Face off); the stick chooser and `Stick: other`; saving (`<rom name>.yaml` beside the ROM, which wins at once; the first save over an existing file keeps it as `<rom name>.yaml.orig`; restore by renaming it back; a read-only folder); Cancel and the second click; that keys and gamepad buttons are still bound with the step-by-step walk.
- Section 3, "Starting from the launcher": each row has `Edit`, which opens that game's layout in the editor (section 6); `generic` sits beside it; editing does not change Recent.
- Section 11: `PJ64_EDIT_SELFTEST` joins the test-hooks row.

- [ ] **Step 2: AGENTS.md**

- The wizard paragraph in Architecture: `--edit <rom>` opens the panel editor — `EditLayout.{h,cpp}` pure (geometry, enabling, what a click does), `EditScreen.cpp` painting, the editing rules on `WizardDraft`; the tour now writes fourteen pictures (update "eleven files" wherever it appears); the wizard now also links `GameConfig.o`.
- The unit-test paragraph: `wizard-edit` (the panel editor's layout and click logic) after `wizard-draft`.
- The launcher paragraph: `Edit` runs `Project64-wizard --edit` with the same environment as a game.
- Commands: `make wizard-selftest` now also proves the editor (`--edit` with `PJ64_EDIT_SELFTEST=1`) and needs `make config`.
- A new trap: **An editor-made layout always has a menu, and its first save keeps `.orig`.** The editor places a missing menu by `AutoMenuSlot`, the play-time order, and never lets it go; the first save over an existing `<rom>.yaml` copies it to `<rom>.yaml.orig`, which nothing touches again — a helper restores a generated layout by renaming it back.
- The `AutoMenuSlot` sentence: the added menu's order lives in `InputConfig.h`, shared by `ApplyAutoMenu` and the editor.

- [ ] **Step 3: Check and commit**

Run: `make unit-test` (the drift check must pass; the pictures are Task 6's).

```bash
git add Docs/UserGuide.md AGENTS.md
git commit -m "Document the panel editor: opening it from the launcher, its rules, saving beside the ROM and the .orig backup

<your attribution trailer>"
```

- [ ] **Step 4: The manual check (the user's, never the implementer's)**

Recorded for the spec's Result after the user's run: from the launcher, `Edit` a game; move a control, make a toggle, move the hold, move the menu, try the full-panel refusal; save; play the game and find every change; `Edit` again, change something, `Cancel` (two clicks); confirm the `.orig` beside the ROM.
