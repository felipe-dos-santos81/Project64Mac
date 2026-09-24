# Emulator Actions Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One panel slot (`Menu: {zone:}` or `Menu: {face:}`) opens a menu that pauses the game and offers Resume, Full screen, Recentre, Save, Load, soft Reset and Quit to a player with only a pointer and its left button.

**Architecture:** The menu's rules are pure functions in a new `Source/Common/PointerMenu.h`, tested headless. The frontend's main thread owns the menu through a new `MenuHost`: it reads clicks from the published `PointerSample` (the game is paused, so the input plugin cannot), pauses and resumes the core with `ExternalEvent`, and shows every change by letting the game run one frame so the overlay redraws. The input plugin gives the game no input while the menu is open; the overlay draws the menu's labels; the layout reader and the wizard learn the `Menu` key.

**Tech Stack:** C++14, SDL3, yaml-cpp, OpenGL immediate mode (overlay, emulation thread only), Objective-C++ (face tracker), POSIX sh (self-test), hand-written `Makefile`.

**Spec:** `Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md`

## Global Constraints

- **Prerequisite:** the one-program unit tests (`Source/Project64-sdl/UnitTest.h`, `UnitTestMain.cpp`, `make unit-test [only=<area>]`) are committed before Task 1 starts. New checks go into that program; never add a test program or a `make` target (AGENTS.md, "Unit tests live in one program").
- GL belongs to the emulation thread: the main thread never draws. The panel changes only when the game presents a frame.
- Menu item slots: `mid3` and the menu's own slot = Resume `Go`; `mid1` = Full screen `Fs`; `mid2` = Recentre `Fc` (only while the camera runs: `FACE_STARTING`, `FACE_TRACKING`, `FACE_NO_FACE`); `mid4` = Save `Sv`; `mid5` = Load `Ld`; `pad-left` = Reset `Rs`; `c-right` = Quit `Qt`. On an item's slot the item wins over the menu slot's Resume.
- Guarded (press twice): Save, Load, Reset, Quit. A click elsewhere disarms.
- Save and Load use slot 0 (`Game_CurrentSaveState` = 0). Reset is soft (`SysEvent_ResetCPU_Soft`) and clears toggles, the hold and the rest.
- Timeouts: 500 ms for the overlay to draw a frame; 1 s for the core to confirm a pause (then stderr `menu: the game did not pause`).
- While `MenuOpen` is set, `GetKeys` returns all zeros from every source and marks the button as already down.
- Reader errors, exact: `menu must be {zone:} or {face:}`; `the menu cannot be game`; `<slot> is the menu slot and cannot also be bound`; `<gesture> is the menu's gesture and cannot also be bound`; a menu `head-*` gesture beside a head stick gets the existing `<gesture> cannot be bound while Stick is head`.
- The menu slot's label is `==`. `PJ64_MENU_SELFTEST` prints `menu: <phase>` on each phase change.
- The shipped mouse layouts put `Menu: {zone: pad-down}` where `DPadDown: {zone: pad-down}` was.
- Every file touched is LF; `Docs/img/wizard/` must not change (`make unit-test` runs the drift check).
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.

## Review Focus

1. **The menu slot pressed while the game is still booting** (no frames yet): the menu must still pause the game, within the 500 ms timeout. Pinned in Task 6 (the self-test presses from the first frame of the run).
2. **The click that closes the menu lands on a game slot** (Resume on `mid3` is B in Super Mario 64): the game must not see that press. Pinned in Task 4 (a click state marked down needs a release).
3. **The menu gesture held after it opens the menu** must not close and reopen it. Pinned in Task 1.
4. **A layout putting the menu on the hold slot or a control's slot** must be rejected with the right message, whichever order the file names them in. Pinned in Task 2.
5. **The wizard binding a control to the menu's slot or gesture** must never write a file the reader rejects. Pinned in Task 3.

---

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `Source/Common/PointerMenu.h` | create | item table, labels, guard, `PointerMenuStep` (pure) |
| `Source/Project64-sdl/PointerMenuTest.cpp` | create | `pointer-menu` unit-test area |
| `Source/Project64-sdl/UnitTest.h`, `UnitTestMain.cpp`, `Makefile` | modify | register the area |
| `Source/Project64-sdl/InputConfig.{h,cpp}` | modify | the `Menu` key, its checks, `==` |
| `Source/Project64-sdl/InputConfigTest.cpp` | modify | `input-config` area |
| `Source/Project64-wizard/WizardDraft.{h,cpp}`, `Screens.cpp`, `WizardDraftTest.cpp` | modify | `Menu` round trip |
| `Source/Common/PointerState.h` | modify | six menu fields |
| `Source/Project64-sdl/PluginInput.cpp` | modify | no input while open, `ClearClicks`, `RomClosed` fix |
| `Source/Project64-sdl/FaceGestures.h`, `FaceTracker.mm`, `FaceGesturesTest.cpp` | modify | `Rebaseline()` on request |
| `Source/Project64-sdl/PointerLayoutTest.cpp` | modify | released-first click state |
| `Source/Project64-sdl/Overlay.{h,cpp}`, `SdlRenderWindow.{h,cpp}` | modify | menu drawing, glyphs, `OverlayFrames` |
| `Source/Project64-sdl/MenuHost.{h,cpp}` | create | main-thread menu host |
| `Source/Project64-sdl/main.cpp` | modify | create and poll the host |
| `Config/mouse/*.yaml` | modify | `Menu` on `pad-down` |
| `Scripts/pointer_selftest.sh` | modify | fifth run |
| `Docs/UserGuide.md`, `AGENTS.md` | modify | docs |

---

### Task 1: The menu's rules (PointerMenu.h) and the `pointer-menu` area

**Files:**
- Create: `Source/Common/PointerMenu.h`
- Create: `Source/Project64-sdl/PointerMenuTest.cpp`
- Modify: `Source/Project64-sdl/UnitTest.h` (the `Run*Tests` declarations), `Source/Project64-sdl/UnitTestMain.cpp` (`kAreas`), `Makefile` (`UNIT_TEST_OBJS`), `AGENTS.md` (the unit-test areas sentence)

**Interfaces:**
- Consumes: `POINTER_ZONE_NONE`, `POINTER_ZONE_GAME`, `PointerZoneFromName` (`PointerLayout.h`); `FaceStatus` values (`PointerState.h`); `CHECK`, `TestFailures` (`UnitTest.h`).
- Produces:
  - `enum PointerMenuItem { MENU_ITEM_NONE, MENU_ITEM_RESUME, MENU_ITEM_FULLSCREEN, MENU_ITEM_RECENTRE, MENU_ITEM_SAVE, MENU_ITEM_LOAD, MENU_ITEM_RESET, MENU_ITEM_QUIT };`
  - `PointerMenuItem PointerMenuItemAt(int Zone, int MenuZone, bool FaceOn)`
  - `const char * PointerMenuLabel(PointerMenuItem Item)`, `bool PointerMenuGuarded(PointerMenuItem Item)`, `bool PointerMenuFaceOn(uint32_t FaceStatus)`
  - `struct PointerMenu { bool Open = false; int Armed = POINTER_ZONE_NONE; bool PrevButton = false; bool PrevGesture = false; };`
  - `enum PointerMenuAction { MENU_ACTION_NONE, MENU_ACTION_OPEN, MENU_ACTION_REPAINT, MENU_ACTION_RESUME, MENU_ACTION_FULLSCREEN, MENU_ACTION_RECENTRE, MENU_ACTION_SAVE, MENU_ACTION_LOAD, MENU_ACTION_RESET, MENU_ACTION_QUIT };`
  - `PointerMenuAction PointerMenuStep(PointerMenu * M, bool Button, int Zone, bool Gesture, int MenuZone, bool FaceOn)`
  - `void RunPointerMenuTests()` (area `pointer-menu`)

- [ ] **Step 1: Write the failing test**

Create `Source/Project64-sdl/PointerMenuTest.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// Tests for the emulator actions menu's pure rules (PointerMenu.h). No window, no core.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include <Common/PointerMenu.h>
#include "UnitTest.h"

// A zone by name, so the cases read like the spec's table.
static int Z(const char * Name)
{
    return PointerZoneFromName(Name);
}

// One click: the button goes down over Zone on one poll and up on the next. Returns what
// the press did.
static PointerMenuAction Click(PointerMenu * M, int Zone, int MenuZone, bool FaceOn = true)
{
    const PointerMenuAction A = PointerMenuStep(M, true, Zone, false, MenuZone, FaceOn);
    PointerMenuStep(M, false, Zone, false, MenuZone, FaceOn);
    return A;
}

void RunPointerMenuTests()
{
    const int Menu = Z("pad-down");

    // The table: one item per slot, the menu's own slot resumes, Fc only with the camera.
    CHECK(PointerMenuItemAt(Z("mid3"), Menu, true) == MENU_ITEM_RESUME);
    CHECK(PointerMenuItemAt(Menu, Menu, true) == MENU_ITEM_RESUME);
    CHECK(PointerMenuItemAt(Z("mid1"), Menu, true) == MENU_ITEM_FULLSCREEN);
    CHECK(PointerMenuItemAt(Z("mid2"), Menu, true) == MENU_ITEM_RECENTRE);
    CHECK(PointerMenuItemAt(Z("mid2"), Menu, false) == MENU_ITEM_NONE);
    CHECK(PointerMenuItemAt(Z("mid4"), Menu, true) == MENU_ITEM_SAVE);
    CHECK(PointerMenuItemAt(Z("mid5"), Menu, true) == MENU_ITEM_LOAD);
    CHECK(PointerMenuItemAt(Z("pad-left"), Menu, true) == MENU_ITEM_RESET);
    CHECK(PointerMenuItemAt(Z("c-right"), Menu, true) == MENU_ITEM_QUIT);
    CHECK(PointerMenuItemAt(Z("pad-up"), Menu, true) == MENU_ITEM_NONE);
    CHECK(PointerMenuItemAt(POINTER_ZONE_GAME, Menu, true) == MENU_ITEM_NONE);
    CHECK(PointerMenuItemAt(POINTER_ZONE_NONE, Menu, true) == MENU_ITEM_NONE);
    CHECK(PointerMenuItemAt(POINTER_ZONE_NONE, POINTER_ZONE_NONE, true) == MENU_ITEM_NONE);
    // On an item's slot the item wins; on mid2 without the camera the menu slot resumes.
    CHECK(PointerMenuItemAt(Z("mid4"), Z("mid4"), true) == MENU_ITEM_SAVE);
    CHECK(PointerMenuItemAt(Z("mid2"), Z("mid2"), false) == MENU_ITEM_RESUME);

    // Labels: two characters for every item, none for a blank slot; four items are guarded.
    for (int I = MENU_ITEM_RESUME; I <= MENU_ITEM_QUIT; I++)
    {
        CHECK(strlen(PointerMenuLabel((PointerMenuItem)I)) == 2);
    }
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_NONE), "") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_RESUME), "Go") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_FULLSCREEN), "Fs") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_RECENTRE), "Fc") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_SAVE), "Sv") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_LOAD), "Ld") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_RESET), "Rs") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_QUIT), "Qt") == 0);
    CHECK(PointerMenuGuarded(MENU_ITEM_SAVE) && PointerMenuGuarded(MENU_ITEM_LOAD));
    CHECK(PointerMenuGuarded(MENU_ITEM_RESET) && PointerMenuGuarded(MENU_ITEM_QUIT));
    CHECK(!PointerMenuGuarded(MENU_ITEM_RESUME) && !PointerMenuGuarded(MENU_ITEM_FULLSCREEN));
    CHECK(!PointerMenuGuarded(MENU_ITEM_RECENTRE) && !PointerMenuGuarded(MENU_ITEM_NONE));

    // The camera "runs" while starting, tracking or looking for a face.
    CHECK(PointerMenuFaceOn(FACE_STARTING) && PointerMenuFaceOn(FACE_TRACKING) && PointerMenuFaceOn(FACE_NO_FACE));
    CHECK(!PointerMenuFaceOn(FACE_OFF) && !PointerMenuFaceOn(FACE_DENIED) && !PointerMenuFaceOn(FACE_ERROR));

    // Opening: a press on the menu slot, not elsewhere; holding it acts once; the menu's
    // own slot then resumes.
    {
        PointerMenu M;
        CHECK(Click(&M, Z("mid3"), Menu) == MENU_ACTION_NONE && !M.Open);
        CHECK(PointerMenuStep(&M, true, Menu, false, Menu, true) == MENU_ACTION_OPEN && M.Open);
        CHECK(PointerMenuStep(&M, true, Menu, false, Menu, true) == MENU_ACTION_NONE);
        PointerMenuStep(&M, false, Menu, false, Menu, true);
        CHECK(Click(&M, Menu, Menu) == MENU_ACTION_RESUME && !M.Open);
    }

    // The gesture's rising edge opens it and, again, resumes; holding the gesture does
    // neither twice. A layout with a gesture and no slot passes POINTER_ZONE_NONE.
    {
        PointerMenu M;
        CHECK(PointerMenuStep(&M, false, POINTER_ZONE_NONE, true, POINTER_ZONE_NONE, true) == MENU_ACTION_OPEN);
        CHECK(PointerMenuStep(&M, false, POINTER_ZONE_NONE, true, POINTER_ZONE_NONE, true) == MENU_ACTION_NONE);
        CHECK(M.Open);
        PointerMenuStep(&M, false, POINTER_ZONE_NONE, false, POINTER_ZONE_NONE, true);
        CHECK(PointerMenuStep(&M, false, POINTER_ZONE_NONE, true, POINTER_ZONE_NONE, true) == MENU_ACTION_RESUME);
        CHECK(!M.Open);
    }

    // A press already down when the gesture opens the menu counts only after a release.
    {
        PointerMenu M;
        PointerMenuStep(&M, true, Z("mid3"), false, Menu, true);
        CHECK(PointerMenuStep(&M, true, Z("mid3"), true, Menu, true) == MENU_ACTION_OPEN);
        CHECK(PointerMenuStep(&M, true, Z("mid3"), false, Menu, true) == MENU_ACTION_NONE && M.Open);
        PointerMenuStep(&M, false, Z("mid3"), false, Menu, true);
        CHECK(Click(&M, Z("mid3"), Menu) == MENU_ACTION_RESUME);
    }

    // Unguarded items act at once; Full screen and Recentre keep the menu open.
    {
        PointerMenu M;
        Click(&M, Menu, Menu);
        CHECK(Click(&M, Z("mid1"), Menu) == MENU_ACTION_FULLSCREEN && M.Open);
        CHECK(Click(&M, Z("mid2"), Menu) == MENU_ACTION_RECENTRE && M.Open);
        CHECK(Click(&M, Z("mid2"), Menu, false) == MENU_ACTION_NONE && M.Open);
        CHECK(Click(&M, Z("mid3"), Menu) == MENU_ACTION_RESUME && !M.Open);
    }

    // Each guarded item arms on the first click and acts on the second, closing the menu.
    const struct
    {
        const char * Slot;
        PointerMenuAction Action;
    } Guarded[] = {
        { "mid4", MENU_ACTION_SAVE },
        { "mid5", MENU_ACTION_LOAD },
        { "pad-left", MENU_ACTION_RESET },
        { "c-right", MENU_ACTION_QUIT },
    };
    for (const auto & G : Guarded)
    {
        PointerMenu M;
        Click(&M, Menu, Menu);
        CHECK(Click(&M, Z(G.Slot), Menu) == MENU_ACTION_REPAINT);
        CHECK(M.Armed == Z(G.Slot) && M.Open);
        CHECK(Click(&M, Z(G.Slot), Menu) == G.Action);
        CHECK(!M.Open && M.Armed == POINTER_ZONE_NONE);
    }

    // Disarming: a blank slot or the game image only disarms (and repaints only when
    // something was armed); another guarded item arms itself instead; an unguarded item
    // disarms and acts.
    {
        PointerMenu M;
        Click(&M, Menu, Menu);
        Click(&M, Z("mid4"), Menu);
        CHECK(Click(&M, Z("pad-up"), Menu) == MENU_ACTION_REPAINT);
        CHECK(M.Armed == POINTER_ZONE_NONE && M.Open);
        CHECK(Click(&M, Z("pad-up"), Menu) == MENU_ACTION_NONE);
        Click(&M, Z("mid4"), Menu);
        CHECK(Click(&M, POINTER_ZONE_GAME, Menu) == MENU_ACTION_REPAINT && M.Armed == POINTER_ZONE_NONE);
        Click(&M, Z("mid4"), Menu);
        CHECK(Click(&M, Z("c-right"), Menu) == MENU_ACTION_REPAINT && M.Armed == Z("c-right"));
        CHECK(Click(&M, Z("mid1"), Menu) == MENU_ACTION_FULLSCREEN && M.Armed == POINTER_ZONE_NONE);
        CHECK(Click(&M, Z("mid4"), Menu) == MENU_ACTION_REPAINT);
        CHECK(M.Open);
    }
}
```

Register it:
- `Source/Project64-sdl/UnitTest.h`: after `void RunPointerLayoutTests();` add `void RunPointerMenuTests();`.
- `Source/Project64-sdl/UnitTestMain.cpp`: after `{ "pointer-layout", RunPointerLayoutTests },` add `{ "pointer-menu", RunPointerMenuTests },`.
- `Makefile`: in `UNIT_TEST_OBJS`, change `UnitTestMain.o PointerLayoutTest.o \` to `UnitTestMain.o PointerLayoutTest.o PointerMenuTest.o \`.
- `AGENTS.md`: in the unit-test paragraph, change `` `pointer-layout` (the mouse panel's geometry
and the one-button rules), `` to `` `pointer-layout` (the mouse panel's geometry
and the one-button rules), `pointer-menu` (the emulator actions menu's rules), `` (re-wrap the paragraph to ~90 columns).

- [ ] **Step 2: Run the test to verify it fails**

Run: `make unit-test only=pointer-menu`
Expected: compile error, `'Common/PointerMenu.h' file not found`.

- [ ] **Step 3: Implement**

Create `Source/Common/PointerMenu.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// The emulator actions menu's rules, shared by the frontend's MenuHost, which acts on them,
// and the overlay, which draws the item labels. Pure functions: no SDL, no core.
// Design: Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <stdint.h>

enum PointerMenuItem
{
    MENU_ITEM_NONE,
    MENU_ITEM_RESUME,
    MENU_ITEM_FULLSCREEN,
    MENU_ITEM_RECENTRE,
    MENU_ITEM_SAVE,
    MENU_ITEM_LOAD,
    MENU_ITEM_RESET,
    MENU_ITEM_QUIT,
};

// The item on Zone while the menu is open. Zones are PointerZoneName's indices. An item's
// own slot wins over the menu slot's Resume; Recentre is there only while the camera runs.
inline PointerMenuItem PointerMenuItemAt(int Zone, int MenuZone, bool FaceOn)
{
    switch (Zone)
    {
    case 8: return MENU_ITEM_FULLSCREEN;                        // mid1
    case 9: if (FaceOn) return MENU_ITEM_RECENTRE; break;       // mid2
    case 10: return MENU_ITEM_RESUME;                           // mid3
    case 11: return MENU_ITEM_SAVE;                             // mid4
    case 12: return MENU_ITEM_LOAD;                             // mid5
    case 2: return MENU_ITEM_RESET;                             // pad-left
    case 7: return MENU_ITEM_QUIT;                              // c-right
    default: break;
    }
    return (Zone != POINTER_ZONE_NONE && Zone == MenuZone) ? MENU_ITEM_RESUME : MENU_ITEM_NONE;
}

// Two characters the overlay's font can draw, or "" for a blank slot.
inline const char * PointerMenuLabel(PointerMenuItem Item)
{
    switch (Item)
    {
    case MENU_ITEM_RESUME: return "Go";
    case MENU_ITEM_FULLSCREEN: return "Fs";
    case MENU_ITEM_RECENTRE: return "Fc";
    case MENU_ITEM_SAVE: return "Sv";
    case MENU_ITEM_LOAD: return "Ld";
    case MENU_ITEM_RESET: return "Rs";
    case MENU_ITEM_QUIT: return "Qt";
    default: return "";
    }
}

// The items a first click only arms: each loses progress if hit by mistake.
inline bool PointerMenuGuarded(PointerMenuItem Item)
{
    return Item == MENU_ITEM_SAVE || Item == MENU_ITEM_LOAD || Item == MENU_ITEM_RESET || Item == MENU_ITEM_QUIT;
}

// Whether the face tracker is running, from PointerState::Face.
inline bool PointerMenuFaceOn(uint32_t FaceStatus)
{
    return FaceStatus == FACE_STARTING || FaceStatus == FACE_TRACKING || FaceStatus == FACE_NO_FACE;
}

// The menu's state across polls. Default-constructed: closed, nothing armed, nothing held.
struct PointerMenu
{
    bool Open = false;
    int Armed = POINTER_ZONE_NONE;   // the guarded item's slot waiting for its second click
    bool PrevButton = false;
    bool PrevGesture = false;
};

enum PointerMenuAction
{
    MENU_ACTION_NONE,        // nothing changed
    MENU_ACTION_OPEN,        // the menu opened
    MENU_ACTION_REPAINT,     // armed or disarmed: the picture changed, the menu stays open
    MENU_ACTION_RESUME,      // the rest close the menu except Full screen and Recentre
    MENU_ACTION_FULLSCREEN,
    MENU_ACTION_RECENTRE,
    MENU_ACTION_SAVE,
    MENU_ACTION_LOAD,
    MENU_ACTION_RESET,
    MENU_ACTION_QUIT,
};

// One poll. Button and Zone are the left button and the zone under the cursor; Gesture is
// whether the menu's gesture is held (false when the layout has none). Closed, a press on
// the menu slot or the gesture's rising edge opens it. Open, the gesture's rising edge
// resumes, and a press does what its slot's item does: a guarded item needs a second press
// on the same slot; any other press disarms first.
inline PointerMenuAction PointerMenuStep(PointerMenu * M, bool Button, int Zone, bool Gesture, int MenuZone, bool FaceOn)
{
    const bool Press = Button && !M->PrevButton;
    const bool Rise = Gesture && !M->PrevGesture;
    M->PrevButton = Button;
    M->PrevGesture = Gesture;
    if (!M->Open)
    {
        if (Rise || (Press && Zone != POINTER_ZONE_NONE && Zone == MenuZone))
        {
            M->Open = true;
            M->Armed = POINTER_ZONE_NONE;
            return MENU_ACTION_OPEN;
        }
        return MENU_ACTION_NONE;
    }
    PointerMenuItem Item = MENU_ITEM_NONE;
    if (Rise)
    {
        Item = MENU_ITEM_RESUME;
    }
    else if (Press)
    {
        Item = PointerMenuItemAt(Zone, MenuZone, FaceOn);
        if (PointerMenuGuarded(Item) && M->Armed != Zone)
        {
            M->Armed = Zone;
            return MENU_ACTION_REPAINT;
        }
    }
    else
    {
        return MENU_ACTION_NONE;
    }
    const bool WasArmed = M->Armed != POINTER_ZONE_NONE;
    M->Armed = POINTER_ZONE_NONE;
    switch (Item)
    {
    case MENU_ITEM_FULLSCREEN: return MENU_ACTION_FULLSCREEN;
    case MENU_ITEM_RECENTRE: return MENU_ACTION_RECENTRE;
    case MENU_ITEM_NONE: return WasArmed ? MENU_ACTION_REPAINT : MENU_ACTION_NONE;
    default: break;
    }
    M->Open = false;
    switch (Item)
    {
    case MENU_ITEM_SAVE: return MENU_ACTION_SAVE;
    case MENU_ITEM_LOAD: return MENU_ACTION_LOAD;
    case MENU_ITEM_RESET: return MENU_ACTION_RESET;
    case MENU_ITEM_QUIT: return MENU_ACTION_QUIT;
    default: return MENU_ACTION_RESUME;
    }
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make unit-test`
Expected: `ok: pointer-layout`, `ok: pointer-menu`, `ok: face-gestures`, `ok: game-config`, `ok: input-config`, `ok: wizard-draft`, `ok: wizard screenshots`, `ok: unit tests`.

- [ ] **Step 5: Commit**

```bash
git add Source/Common/PointerMenu.h Source/Project64-sdl/PointerMenuTest.cpp Source/Project64-sdl/UnitTest.h Source/Project64-sdl/UnitTestMain.cpp Makefile AGENTS.md
git commit -m "Add the emulator actions menu's rules: items, press-twice and opening, as pure functions

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: The `Menu` key in the layout reader

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.h` (class `InputConfig`)
- Modify: `Source/Project64-sdl/InputConfig.cpp` (`Reset` ~line 18, `UsesPointer`/`UsesFace` ~line 52, `PointerLabels` end ~line 137, `CheckSlots` ~line 430, `Load` ~line 470)
- Test: `Source/Project64-sdl/InputConfigTest.cpp`

**Interfaces:**
- Consumes: `ParseBinding`, `CheckSlots`, `ConfigError`, `PointerGestureName`, `PointerGestureIndex`, `POINTER_GESTURE_HEAD_DIRECTIONS` (existing).
- Produces:
  - `const std::vector<Binding> & InputConfig::MenuBinding() const` — empty when the file names no `Menu`
  - `int InputConfig::MenuZone() const` — slot or `POINTER_ZONE_NONE`
  - `uint32_t InputConfig::MenuGesture() const` — gesture bit or 0
  - `PointerLabels` writes `"=="` on the menu slot and on the menu gesture's label.

- [ ] **Step 1: Write the failing tests**

In `Source/Project64-sdl/InputConfigTest.cpp`, directly before `CHECK(C.Load("Config/input.yaml"));`, add:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make unit-test only=input-config`
Expected: compile error, `no member named 'MenuZone' in 'InputConfig'`.

- [ ] **Step 3: Implement the header**

In `Source/Project64-sdl/InputConfig.h`, after `int PointerHoldZone() const;` add:

```cpp
    // The Menu key of bindings, {zone:} or {face:}, which opens the emulator actions menu
    // and presses no N64 control. Empty when the file names none.
    const std::vector<Binding> & MenuBinding() const;

    // The menu's slot, or POINTER_ZONE_NONE; its gesture bit, or 0.
    int MenuZone() const;
    uint32_t MenuGesture() const;
```

and after `std::vector<Binding> m_Bindings[(int)N64Control::Count];` add `std::vector<Binding> m_Menu;`.

- [ ] **Step 4: Implement the reader**

In `Source/Project64-sdl/InputConfig.cpp`:

(a) `Reset` becomes:

```cpp
void InputConfig::Reset()
{
    DefaultBindings(m_Bindings);
    m_Menu.clear();
}
```

(b) At the top of `UsesPointer`'s body add `if (!m_Menu.empty()) return true;` and at the top of `UsesFace`'s body add `if (MenuGesture() != 0) return true;`.

(c) After `PointerHoldZone`'s definition add:

```cpp
const std::vector<Binding> & InputConfig::MenuBinding() const
{
    return m_Menu;
}

int InputConfig::MenuZone() const
{
    return (!m_Menu.empty() && m_Menu[0].kind == Binding::Kind::Zone) ? m_Menu[0].code : POINTER_ZONE_NONE;
}

uint32_t InputConfig::MenuGesture() const
{
    return (!m_Menu.empty() && m_Menu[0].kind == Binding::Kind::Face) ? (uint32_t)m_Menu[0].code : 0u;
}
```

(d) At the end of `PointerLabels`, after the hold-slot block, add:

```cpp
    // The menu presses no control either; its slot and its gesture read "==".
    if (MenuZone() != POINTER_ZONE_NONE)
    {
        snprintf(Labels[MenuZone()], POINTER_LABEL_SIZE, "%s", "==");
    }
    if (MenuGesture() != 0)
    {
        snprintf(GestureLabels[PointerGestureIndex(MenuGesture())], POINTER_LABEL_SIZE, "%s", "==");
    }
```

(e) Replace `CheckSlots`'s signature line and the start of its body, through the line `const int Hold = StickHoldZone(Next[(int)N64Control::Stick]);`, with:

```cpp
static bool CheckSlots(const char * Path, const std::vector<Binding> * Next, const YAML::Node * Nodes,
                       const std::vector<Binding> & Menu, const YAML::Node & MenuNode)
{
    const int Hold = StickHoldZone(Next[(int)N64Control::Stick]);
    const int MenuSlot = (!Menu.empty() && Menu[0].kind == Binding::Kind::Zone) ? Menu[0].code : POINTER_ZONE_NONE;
    const uint32_t MenuBit = (!Menu.empty() && Menu[0].kind == Binding::Kind::Face) ? (uint32_t)Menu[0].code : 0u;
    if (MenuSlot != POINTER_ZONE_NONE && MenuSlot == Hold)
    {
        ConfigError(Path, MenuNode, std::string(PointerZoneName(MenuSlot)) + " is the menu slot and cannot also be bound");
        return false;
    }
```

and, in its loop, replace `            if (B.kind != Binding::Kind::Zone) continue;` with:

```cpp
            if (B.kind == Binding::Kind::Face && MenuBit != 0 && (uint32_t)B.code == MenuBit)
            {
                ConfigError(Path, Nodes[i], std::string(PointerGestureName(PointerGestureIndex(MenuBit))) +
                                                " is the menu's gesture and cannot also be bound");
                return false;
            }
            if (B.kind != Binding::Kind::Zone) continue;
            if (B.code == MenuSlot)
            {
                ConfigError(Path, Nodes[i], std::string(PointerZoneName(B.code)) + " is the menu slot and cannot also be bound");
                return false;
            }
```

Update the comment above `CheckSlots` to add: `The menu's slot is no control's and not the hold slot, and its gesture is no control's.`

(f) In `Load`: next to `YAML::Node ControlNodes[(int)N64Control::Count];` add

```cpp
    std::vector<Binding> NextMenu;
    YAML::Node MenuNode;
```

In the bindings loop, directly after `const std::string ControlName = Entry.first.as<std::string>();`, add:

```cpp
                // Menu is a key of bindings but no N64 control (the emulator actions menu).
                if (ControlName == "Menu")
                {
                    if (!NextMenu.empty()) { ConfigError(Path, Entry.first, "control named twice"); return false; }
                    const YAML::Node & Value = Entry.second;
                    if (!Value.IsMap() || Value.size() != 1 || !(Value["zone"] || Value["face"]))
                    {
                        ConfigError(Path, Value, "menu must be {zone:} or {face:}");
                        return false;
                    }
                    Binding B;
                    if (!ParseBinding(Path, Value, N64Control::A, B)) return false;
                    if (B.kind == Binding::Kind::Zone && B.code == POINTER_ZONE_GAME)
                    {
                        ConfigError(Path, Value["zone"], "the menu cannot be game");
                        return false;
                    }
                    if (B.kind == Binding::Kind::Face && ((uint32_t)B.code & POINTER_GESTURE_HEAD_DIRECTIONS) != 0 && HeadGestureName.empty())
                    {
                        HeadGestureNode = Value["face"];
                        HeadGestureName = PointerGestureName(PointerGestureIndex((uint32_t)B.code));
                    }
                    NextMenu.push_back(B);
                    MenuNode = Value;
                    continue;
                }
```

Change `if (!CheckSlots(Path, Next, ControlNodes)) return false;` to `if (!CheckSlots(Path, Next, ControlNodes, NextMenu, MenuNode)) return false;`, and after the loop that copies `m_Bindings[i] = Next[i];` add `m_Menu = NextMenu;`.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `make unit-test`
Expected: every area `ok:` and `ok: unit tests`.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/InputConfigTest.cpp
git commit -m "Read the Menu key from a layout: a slot or a gesture that opens the actions menu

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: The wizard keeps the `Menu` key

**Files:**
- Modify: `Source/Project64-wizard/WizardDraft.h`, `Source/Project64-wizard/WizardDraft.cpp` (`LoadDefaults` ~line 228, `LoadBase` ~line 240, `SetZone`, `SetGesture` ~line 311, `Emit` ~line 453), `Source/Project64-wizard/Screens.cpp` (`DrawPanel` ~line 517)
- Test: `Source/Project64-wizard/WizardDraftTest.cpp`

**Interfaces:**
- Consumes: Task 2's `InputConfig::MenuBinding()`.
- Produces: `int WizardDraft::MenuZone() const` — the draft's menu slot or `POINTER_ZONE_NONE`.

- [ ] **Step 1: Write the failing tests**

In `Source/Project64-wizard/WizardDraftTest.cpp`, directly before the comment `// The five shipped layouts are offered as bases, by label and by path.`, add:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make unit-test only=wizard-draft`
Expected: compile error, `no member named 'MenuZone' in 'WizardDraft'`.

- [ ] **Step 3: Implement**

`Source/Project64-wizard/WizardDraft.h`: after `int HoldZone() const;` add

```cpp
    // The draft's menu slot ({zone:} under Menu), or POINTER_ZONE_NONE. Like the hold slot
    // it belongs to no control. The wizard keeps Menu from a base and writes it back;
    // choosing it is the clickable wizard's job.
    int MenuZone() const;
```

and after `std::string m_LastEmit;` add `std::vector<Binding> m_Menu;   // the base's Menu key, empty for none`.

`Source/Project64-wizard/WizardDraft.cpp`:

- In `LoadDefaults`, before `m_Error.clear();`, add `m_Menu.clear();`.
- In `LoadBase`, before `return true;`, add `m_Menu = C.MenuBinding();`.
- In `SetZone`, directly before `Replace(Control, B);`, add:

```cpp
    // So is the menu's slot; a control taking it takes it off the menu.
    if (Zone != POINTER_ZONE_NONE && Zone == MenuZone()) m_Menu.clear();
```

- `SetGesture` becomes:

```cpp
void WizardDraft::SetGesture(N64Control Control, uint32_t Bit)
{
    Binding B = {};
    B.kind = Binding::Kind::Face;
    B.code = (int)Bit;
    // A control taking the menu's gesture takes it off the menu.
    if (!m_Menu.empty() && m_Menu[0].kind == Binding::Kind::Face && (uint32_t)m_Menu[0].code == Bit) m_Menu.clear();
    Replace(Control, B);
}
```

- After `HoldZone`'s definition add:

```cpp
int WizardDraft::MenuZone() const
{
    return (!m_Menu.empty() && m_Menu[0].kind == Binding::Kind::Zone) ? m_Menu[0].code : POINTER_ZONE_NONE;
}
```

- In `Emit`, change `if (Count == 0)` to `if (Count == 0 && m_Menu.empty())`, and directly before the final `return Out;` add:

```cpp
    if (!m_Menu.empty())
    {
        char Line[256];
        snprintf(Line, sizeof(Line), "  %-10s %s\n", "Menu:", ValueText(m_Menu[0]).c_str());
        Out += Line;
    }
```

`Source/Project64-wizard/Screens.cpp`, in `DrawPanel`, directly after the hold-slot block (the one ending `WizardText(Renderer, R.x + 4.0f, R.y + 4.0f, 1, "Ho"); continue; }`), add:

```cpp
        if (Zone == Draft.MenuZone())
        {
            Colour(Renderer, false);
            WizardText(Renderer, R.x + 4.0f, R.y + 4.0f, 1, "==");
            continue;
        }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make unit-test && make wizard-selftest`
Expected: every area `ok:`, `ok: wizard screenshots` (pictures unchanged), `ok: unit tests`, and the wizard self-test's `ok:` line.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-wizard/WizardDraft.h Source/Project64-wizard/WizardDraft.cpp Source/Project64-wizard/Screens.cpp Source/Project64-wizard/WizardDraftTest.cpp
git commit -m "Keep the Menu key through the wizard, and take it off the menu when a control claims its slot or gesture

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Shared state, the plugin's menu guard, and recentring the face

**Files:**
- Modify: `Source/Common/PointerState.h` (struct `PointerState`)
- Modify: `Source/Project64-sdl/main.cpp` (`CreatePointerState` ~line 111)
- Modify: `Source/Project64-sdl/PluginInput.cpp` (globals ~line 40, `GetKeys` ~line 238, `RomClosed`, `PublishPointerLabels`)
- Modify: `Source/Project64-sdl/FaceGestures.h`, `Source/Project64-sdl/FaceTracker.mm` (~line 167)
- Test: `Source/Project64-sdl/FaceGesturesTest.cpp`, `Source/Project64-sdl/PointerLayoutTest.cpp`

**Interfaces:**
- Consumes: Task 2's `InputConfig::MenuZone()`.
- Produces:
  - `PointerState` fields: `std::atomic<int32_t> MenuZone`, `std::atomic<uint32_t> MenuOpen`, `std::atomic<int32_t> MenuArmed`, `std::atomic<uint32_t> OverlayFrames`, `std::atomic<uint32_t> ClearClicks`, `std::atomic<uint32_t> RecentreFace`.
  - `void GestureClassifier::Rebaseline()`.

- [ ] **Step 1: Write the failing tests**

In `Source/Project64-sdl/FaceGesturesTest.cpp`, directly before the comment `// A face at rest for the new measures:`, add:

```cpp
    {
        // Rebaseline forgets the rest: a brow raise held across it becomes the new rest and
        // reads as off once the debounce passes.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(Feed(C, T, 2, true, 0.15f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        C.Rebaseline();
        CHECK(Feed(C, T, 2, true, 0.15f, 0.0f) == 0);
        CHECK(C.BrowBaseline() > 0.149f && C.BrowBaseline() < 0.151f);
    }
```

In `Source/Project64-sdl/PointerLayoutTest.cpp`, directly after the block commented `// Dragging onto a toggle slot with the button already down does not flip it.`, add:

```cpp
    // A click state marked as already down (the plugin does this while the menu is open,
    // on ClearClicks and on RomClosed) needs a release before the next press counts, so the
    // click that closes the menu never lands on the game.
    {
        const PointerSettle None = {};
        PointerClicks C;
        C.PrevButton = true;
        PointerClickStep(&C, true, 10, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == POINTER_ZONE_NONE);
        PointerClickStep(&C, false, 10, 0u, POINTER_ZONE_NONE, None);
        PointerClickStep(&C, true, 10, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == 10);
    }
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make unit-test`
Expected: compile error, `no member named 'Rebaseline' in 'GestureClassifier'` (the pointer-layout case already passes: it pins existing behaviour the plugin now relies on).

- [ ] **Step 3: Implement the classifier and the tracker**

`Source/Project64-sdl/FaceGestures.h`, after `float YawBaseline() const { return m_Baseline[FACE_YAW]; }`, add:

```cpp
    // Forgets the resting pose: the next sample with a face becomes the new rest, as at
    // start-up. The emulator actions menu's Recentre.
    void Rebaseline() { m_HaveBaseline = false; }
```

`Source/Project64-sdl/FaceTracker.mm`, directly before `const bool HeadStick = m_State->HeadStickWanted.load(std::memory_order_acquire) != 0;`, add:

```objc
    // The menu's Recentre, requested from the main thread; handled here, on this queue,
    // where the classifier lives.
    if (m_State->RecentreFace.exchange(0, std::memory_order_acq_rel) != 0)
    {
        m_Classifier->Rebaseline();
    }
```

- [ ] **Step 4: Implement the shared fields**

`Source/Common/PointerState.h`, in `struct PointerState`:
- after `ToggleMarkZones`, add `std::atomic<int32_t> MenuZone;          // the menu's slot, POINTER_ZONE_NONE for none (plugin at load)`
- after `ToggledZones`, add:

```cpp
    // The emulator actions menu. The frontend's MenuHost -> plugin, overlay and tracker.
    std::atomic<uint32_t> MenuOpen;      // 1 while the menu is open: the game gets no input
    std::atomic<int32_t> MenuArmed;      // the armed item's slot, or POINTER_ZONE_NONE
    std::atomic<uint32_t> ClearClicks;   // bumped to make the plugin forget toggles, the hold and the rest
    std::atomic<uint32_t> RecentreFace;  // 1 asks the tracker to forget the resting pose; it clears it
    // Overlay -> MenuHost: bumped after every draw, so the host knows the menu is on screen.
    std::atomic<uint32_t> OverlayFrames;
```

`Source/Project64-sdl/main.cpp`, in `CreatePointerState`, after `State->Quadrant.store(-1);` add:

```cpp
    State->MenuZone.store(-1);
    State->MenuArmed.store(-1);
```

- [ ] **Step 5: Implement the plugin's guard**

`Source/Project64-sdl/PluginInput.cpp`:

(a) After `static int g_PointerHoldZone = POINTER_ZONE_NONE;` add:

```cpp
// The last PointerState::ClearClicks this plugin acted on.
static uint32_t g_PointerClearSeen = 0;

// Forgets toggles, the hold and the rest, keeping the button marked as down so a press
// already in progress (the menu's closing click, a click held through a reset) needs a
// release before it counts.
static void ForgetPointerClicks(void)
{
    g_PointerClicks = PointerClicks();
    g_PointerClicks.PrevButton = true;
    g_PointerSettle = PointerSettle();
    if (g_Pointer != nullptr)
    {
        g_Pointer->ToggledZones.store(0u, std::memory_order_relaxed);
    }
}
```

(b) In `GetKeys`, directly after the `if (Control != 0) { return; }` block, add:

```cpp
    // The emulator actions menu owns every input while it is open: the game gets nothing,
    // and the click that closes it must be released first
    // (Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md).
    OpenPointerState();
    if (g_Pointer != nullptr)
    {
        if (g_Pointer->MenuOpen.load(std::memory_order_acquire) != 0)
        {
            g_PointerClicks.PrevButton = true;
            return;
        }
        const uint32_t Clear = g_Pointer->ClearClicks.load(std::memory_order_acquire);
        if (Clear != g_PointerClearSeen)
        {
            g_PointerClearSeen = Clear;
            ForgetPointerClicks();
        }
    }
```

(c) `RomClosed` becomes:

```cpp
EXPORT void CALL RomClosed(void)
{
    CloseGamepad();
    // A toggle or a hold never outlives its game, and a button held through the reset is
    // not a new press.
    ForgetPointerClicks();
}
```

(d) In `PublishPointerLabels`, after the `g_Pointer->ToggledZones.store(0u);` line add `g_Pointer->MenuZone.store(Config.MenuZone());`.

- [ ] **Step 6: Run everything**

Run: `make -j8 all && make test && make unit-test && make pointer-selftest rom=<SM64 ROM> && make face-selftest rom=<SM64 ROM>`
Expected: four `ok:` smoke lines; every unit-test area `ok:`; the pointer self-test's four runs pass (nothing opens a menu yet); the face self-test passes. (The ROM path is the user's; ask if not known.)

- [ ] **Step 7: Commit**

```bash
git add Source/Common/PointerState.h Source/Project64-sdl/main.cpp Source/Project64-sdl/PluginInput.cpp Source/Project64-sdl/FaceGestures.h Source/Project64-sdl/FaceTracker.mm Source/Project64-sdl/FaceGesturesTest.cpp Source/Project64-sdl/PointerLayoutTest.cpp
git commit -m "Share the menu's state: the plugin goes quiet while it is open, clears clicks on request, and the tracker recentres

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: The overlay draws the menu

**Files:**
- Modify: `Source/Project64-sdl/Overlay.h:14`, `Source/Project64-sdl/Overlay.cpp` (glyph table ~line 24, `DrawPanel` ~line 174, `OverlayDraw` ~line 235)
- Modify: `Source/Project64-sdl/SdlRenderWindow.h:16,37`, `Source/Project64-sdl/SdlRenderWindow.cpp:16`

**Interfaces:**
- Consumes: Task 1's `PointerMenuItemAt`, `PointerMenuLabel`, `PointerMenuFaceOn`; Task 4's `MenuOpen`, `MenuArmed`, `MenuZone`, `OverlayFrames`.
- Produces: `void OverlayDraw(PointerState * State, const int Viewport[4], bool GuideHidden)` (no longer `const`), bumping `OverlayFrames` after every draw.

- [ ] **Step 1: Let the overlay write its frame counter**

Change `const PointerState *` to `PointerState *` in: `OverlayDraw`'s declaration (`Overlay.h`) and definition (`Overlay.cpp`); `CSdlRenderWindow`'s constructor parameter in `SdlRenderWindow.h` and `SdlRenderWindow.cpp`; and the member `const PointerState * m_Pointer;` in `SdlRenderWindow.h`. `DrawPanel`, `DrawGuide` and `DrawFaceStatus` keep taking `const PointerState *`.

- [ ] **Step 2: Add the glyphs**

In `Overlay.cpp`'s `kGlyphs`, after the `'='` entry, add:

```cpp
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'c', { 0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E } },
    { 'd', { 0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F } },
    { 's', { 0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E } },
```

and extend the comment above the table: `The slot labels, the guide arrows, the gesture tags and the menu's items.`

- [ ] **Step 3: Draw the menu**

Add `#include <Common/PointerMenu.h>` beside the other `Common/` includes in `Overlay.cpp`. Extract the ground quad at the start of `DrawPanel` into a function both panels use:

```cpp
// The panel's opaque ground over the rows below the game.
static void DrawPanelGround(int W, int H, int GameH)
{
    glColor4f(kPanelGrey, kPanelGrey, kPanelGrey, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, (float)GameH);
    glVertex2f((float)W, (float)GameH);
    glVertex2f((float)W, (float)H);
    glVertex2f(0.0f, (float)H);
    glEnd();
}
```

replace those seven lines in `DrawPanel` with `DrawPanelGround(W, H, GameH);`, and add after `DrawPanel`:

```cpp
// The emulator actions menu in place of the panel: each item's label in its slot, the armed
// item bright, blank slots not drawn, no corner marks and no tracker strip.
static void DrawMenuPanel(const PointerState * State, int W, int H, int GameH)
{
    DrawPanelGround(W, H, GameH);
    const int MenuZone = State->MenuZone.load(std::memory_order_relaxed);
    const int Armed = State->MenuArmed.load(std::memory_order_relaxed);
    const bool FaceOn = PointerMenuFaceOn(State->Face.load(std::memory_order_relaxed));
    for (int Zone = 0; Zone < POINTER_ZONE_GAME; Zone++)
    {
        const char * Label = PointerMenuLabel(PointerMenuItemAt(Zone, MenuZone, FaceOn));
        if (Label[0] == '\0') continue;
        const float Alpha = Zone == Armed ? kBright : kDim;
        float X0, Y0, X1, Y1;
        PointerZoneRect(Zone, W, H, &X0, &Y0, &X1, &Y1);
        DrawRect(X0, Y0, X1, Y1, Alpha);
        DrawText(Label, (X0 + X1) / 2.0f, (Y0 + Y1) / 2.0f, Alpha);
    }
}
```

In `OverlayDraw`, replace

```cpp
    if (H > GameH)
    {
        DrawPanel(State, W, H, GameH, Lit, State->ToggleMarkZones.load(std::memory_order_relaxed));
    }
    if (!GuideHidden)
    {
        DrawGuide(State, W, H, GameH, (Lit & (1u << POINTER_ZONE_GAME)) != 0, Quadrant);
    }
```

with

```cpp
    if (State->MenuOpen.load(std::memory_order_acquire) != 0)
    {
        if (H > GameH)
        {
            DrawMenuPanel(State, W, H, GameH);
        }
    }
    else
    {
        if (H > GameH)
        {
            DrawPanel(State, W, H, GameH, Lit, State->ToggleMarkZones.load(std::memory_order_relaxed));
        }
        if (!GuideHidden)
        {
            DrawGuide(State, W, H, GameH, (Lit & (1u << POINTER_ZONE_GAME)) != 0, Quadrant);
        }
    }
```

and after the final `glUseProgram((GLuint)Program);` add:

```cpp
    // The menu host waits for this before it pauses the game: the menu is now on screen.
    State->OverlayFrames.fetch_add(1u, std::memory_order_release);
```

- [ ] **Step 4: Build and check nothing regressed**

Run: `make -j8 all && make test && make unit-test && make pointer-selftest rom=<SM64 ROM>`
Then the render check from AGENTS.md:
`PJ64_FACE=0 PJ64_FRAME_DUMP=/tmp/frame.ppm PJ64_FRAME_DUMP_AT=400 perl -e 'alarm 20; exec @ARGV' -- ./Bin/macOS/Project64 <SM64 ROM> || true`
Expected: all `ok:`; the four pointer runs pass; `/tmp/frame.ppm` is 640x480 and about 89% non-black (measure with a short Python script reading the P6 PPM).

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-sdl/Overlay.h Source/Project64-sdl/Overlay.cpp Source/Project64-sdl/SdlRenderWindow.h Source/Project64-sdl/SdlRenderWindow.cpp
git commit -m "Draw the actions menu in the panel while it is open, and count every overlay frame for the menu host

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: The menu host, the shipped layouts and the end-to-end check

**Files:**
- Create: `Source/Project64-sdl/MenuHost.h`, `Source/Project64-sdl/MenuHost.cpp`
- Modify: `Source/Project64-sdl/main.cpp` (includes; after `RunFileImage` ~line 468; the main loop ~line 481)
- Modify: `Makefile:294` (`FRONTEND_SRC`)
- Modify: `Config/mouse/super_mario_64_usa.yaml`, `Config/mouse/goldeneye_007_u.yaml`, `Config/mouse/mario_kart_64_u.yaml`
- Modify: `Scripts/pointer_selftest.sh`, `Makefile` (`pointer-selftest` help text)
- Test: `Source/Project64-sdl/InputConfigTest.cpp`, `Source/Project64-wizard/WizardDraftTest.cpp` (shipped-layout checks)

**Interfaces:**
- Consumes: Task 1's `PointerMenu`, `PointerMenuStep`, `PointerMenuFaceOn`, `PointerMenuAction`; Task 2's `InputConfig::MenuZone()`, `MenuGesture()`; Task 4's `PointerState` fields; Task 5's `OverlayFrames` bumps.
- Produces: `class MenuHost { public: MenuHost(PointerState * State, SDL_Window * Window, int MenuZone, uint32_t MenuGesture); bool Poll(); }` — `Poll` returns false when the player chose Quit.

- [ ] **Step 1: Write the failing checks**

`Source/Project64-sdl/InputConfigTest.cpp`, in the "Every shipped mouse layout parses" block, add `CHECK(C.MenuZone() == 1);   // pad-down` after each of the three `CHECK(C.PointerToggleZones() == ...);` lines.

`Source/Project64-wizard/WizardDraftTest.cpp`, in the block commented `// A mouse layout's zones, its toggle and its hold come back as they were.`, add after `CHECK(!TestHas(Text, "\n  L:"));`:

```cpp
        CHECK(TestHas(Text, "  Menu:      {zone: pad-down}\n"));
        CHECK(D.MenuZone() == 1);
```

`Scripts/pointer_selftest.sh`: add to the header comment

```sh
# A fifth run presses the shipped Super Mario 64 layout's menu slot (pad-down) from the first
# frame, while the game is still booting: the menu host must report that it paused the game.
```

add after `one_run()`:

```sh
# $1 = inject spec, $2 = ROM path, $3 = PJ64_INPUT_YAML value. Passes when the menu host
# reports "menu: paused" (PJ64_MENU_SELFTEST prints each phase).
menu_run() {
    LOG="$(mktemp)"
    PJ64_INPUT_YAML="$3" PJ64_FACE=0 PJ64_MENU_SELFTEST=1 PJ64_POINTER_INJECT="$1" "$BIN" "$2" >"$LOG" 2>&1 &
    PID=$!
    I=0
    while [ "$I" -lt "$TIMEOUT" ]; do
        grep -q '^menu: paused' "$LOG" 2>/dev/null && break
        sleep 1
        I=$((I + 1))
    done
    kill -TERM "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    if grep -q '^menu: paused' "$LOG"; then
        rm -f "$LOG"
        return 0
    fi
    echo "FAIL: inject $1: the menu never reported paused (log: $LOG)" >&2
    return 1
}
```

add directly before `if [ "$FAIL" -eq 0 ]; then`:

```sh
# Fifth run: the menu slot opens the menu and the game pauses.
menu_run "80,608,1" "$ROM" "$YAML" || FAIL=1   # pad-down's centre at 640x640
```

change the success line to `echo "ok: pointer path maps a game click to A and mid1 to Start, finds a layout named after the ROM, toggles Z on mid2, and pauses from the menu"`, and change the `Makefile`'s `pointer-selftest` help text to `## Prove the injected-pointer path: a game click, mid1, a per-ROM layout, a toggle slot and the menu (usage: make pointer-selftest rom=/path/to/game.z64)`.

- [ ] **Step 2: Run them to verify they fail**

Run: `make unit-test; make pointer-selftest rom=<SM64 ROM>`
Expected: `FAIL` lines at the new `MenuZone() == 1` and `Menu:` checks; the fifth self-test run fails with `the menu never reported paused`.

- [ ] **Step 3: Put the menu in the shipped layouts**

In each of the three `Config/mouse/*.yaml`, replace the line `  DPadDown:  {zone: pad-down}` with `  Menu:      {zone: pad-down}` and add to the header comment, before the blank line that precedes `bindings:`:

```yaml
# pad-down is the menu (==): it pauses the game and offers Resume, Full screen, Save, Load,
# Reset and Quit (Docs/UserGuide.md section 7). D-pad down keeps its keyboard key.
```

- [ ] **Step 4: Write the host**

Create `Source/Project64-sdl/MenuHost.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// The emulator actions menu's host: the frontend's main-thread side. It reads menu clicks
// from the published pointer sample (the input plugin cannot while the game is paused),
// pauses and resumes the core, and shows every change by letting the game run until the
// overlay has drawn one frame, since the main thread must never draw.
// Design: Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <Common/PointerMenu.h>
#include <Common/PointerState.h>
#include <SDL3/SDL.h>
#include <stdint.h>

class MenuHost
{
public:
    MenuHost(PointerState * State, SDL_Window * Window, int MenuZone, uint32_t MenuGesture);

    // One pass of the main loop, on the main thread. False when the player chose Quit.
    bool Poll();

private:
    enum class Phase { Closed, Drawing, Pausing, Paused, Stepping };

    void Enter(Phase Next);
    bool Act(PointerMenuAction Action);
    void Close();
    void ResumeUntilDrawn(Phase Next);

    PointerState * m_State;
    SDL_Window * m_Window;
    int m_MenuZone;
    uint32_t m_MenuGesture;
    PointerMenu m_Menu;
    Phase m_Phase;
    Uint64 m_Since;       // when the current phase began, in SDL ticks
    uint32_t m_Frames;    // OverlayFrames when the current wait began
    bool m_Trace;         // PJ64_MENU_SELFTEST
};
```

Create `Source/Project64-sdl/MenuHost.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// See MenuHost.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "MenuHost.h"
#include <Common/PointerLayout.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/SettingsID.h>
#include <stdio.h>
#include <stdlib.h>

static const Uint64 kFrameWaitMs = 500;    // longest wait for the overlay to draw
static const Uint64 kPauseWaitMs = 1000;   // longest wait for the core to confirm a pause

MenuHost::MenuHost(PointerState * State, SDL_Window * Window, int MenuZone, uint32_t MenuGesture) :
    m_State(State),
    m_Window(Window),
    m_MenuZone(MenuZone),
    m_MenuGesture(MenuGesture),
    m_Phase(Phase::Closed),
    m_Since(0),
    m_Frames(0),
    m_Trace(getenv("PJ64_MENU_SELFTEST") != nullptr)
{
}

void MenuHost::Enter(Phase Next)
{
    static const char * const kNames[] = { "closed", "drawing", "pausing", "paused", "stepping" };
    m_Phase = Next;
    m_Since = SDL_GetTicks();
    if (m_Trace)
    {
        fprintf(stderr, "menu: %s\n", kNames[(int)Next]);
    }
}

// Lets the game run until the overlay has drawn once more (or the wait times out), after
// which Poll pauses it again.
void MenuHost::ResumeUntilDrawn(Phase Next)
{
    m_Frames = m_State->OverlayFrames.load(std::memory_order_acquire);
    g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_FromMenu);
    Enter(Next);
}

void MenuHost::Close()
{
    m_State->MenuArmed.store(POINTER_ZONE_NONE, std::memory_order_release);
    m_State->MenuOpen.store(0u, std::memory_order_release);
    g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_FromMenu);
    Enter(Phase::Closed);
}

bool MenuHost::Act(PointerMenuAction Action)
{
    switch (Action)
    {
    case MENU_ACTION_NONE:
        return true;
    case MENU_ACTION_OPEN:
        m_State->MenuArmed.store(POINTER_ZONE_NONE, std::memory_order_release);
        m_Frames = m_State->OverlayFrames.load(std::memory_order_acquire);
        m_State->MenuOpen.store(1u, std::memory_order_release);
        Enter(Phase::Drawing);
        return true;
    case MENU_ACTION_RESUME:
        Close();
        return true;
    case MENU_ACTION_SAVE:
    case MENU_ACTION_LOAD:
        g_Settings->SaveDword(Game_CurrentSaveState, 0);
        g_BaseSystem->ExternalEvent(Action == MENU_ACTION_SAVE ? SysEvent_SaveMachineState : SysEvent_LoadMachineState);
        Close();
        return true;
    case MENU_ACTION_RESET:
        // A soft reset never reaches the plugin's RomClosed, so the clicks are cleared here.
        m_State->ClearClicks.fetch_add(1u, std::memory_order_release);
        g_BaseSystem->ExternalEvent(SysEvent_ResetCPU_Soft);
        Close();
        return true;
    case MENU_ACTION_QUIT:
        return false; // CloseSystem resumes a paused CPU before stopping it
    case MENU_ACTION_FULLSCREEN:
        SDL_SetWindowFullscreen(m_Window, (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN) == 0);
        break;
    case MENU_ACTION_RECENTRE:
        m_State->RecentreFace.store(1u, std::memory_order_release);
        break;
    case MENU_ACTION_REPAINT:
        break;
    }
    // The menu stays open and its picture changed: show it by stepping one frame.
    m_State->MenuArmed.store(m_Menu.Armed, std::memory_order_release);
    ResumeUntilDrawn(Phase::Stepping);
    return true;
}

bool MenuHost::Poll()
{
    if (g_BaseSystem == nullptr)
    {
        return true;
    }
    PointerSample S;
    PointerSnapshot(m_State, &S);
    const int Zone = PointerLayoutEvaluate(S.X, S.Y, S.W, S.H, S.Inside).Zone;
    const bool Gesture = m_MenuGesture != 0 && (m_State->Gestures.load(std::memory_order_relaxed) & m_MenuGesture) != 0;
    const bool FaceOn = PointerMenuFaceOn(m_State->Face.load(std::memory_order_relaxed));
    const Uint64 Now = SDL_GetTicks();

    switch (m_Phase)
    {
    case Phase::Closed:
    case Phase::Paused:
        return Act(PointerMenuStep(&m_Menu, S.Button, Zone, Gesture, m_MenuZone, FaceOn));
    case Phase::Drawing:
    case Phase::Stepping:
        if (m_State->OverlayFrames.load(std::memory_order_acquire) != m_Frames || Now - m_Since >= kFrameWaitMs)
        {
            g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_FromMenu);
            Enter(Phase::Pausing);
        }
        break;
    case Phase::Pausing:
        if (g_Settings->LoadBool(GameRunning_CPU_Paused))
        {
            Enter(Phase::Paused);
        }
        else if (Now - m_Since >= kPauseWaitMs)
        {
            fprintf(stderr, "menu: the game did not pause\n");
            Enter(Phase::Paused);
        }
        break;
    }
    // Presses while waiting are ignored, but the button and the gesture are still tracked,
    // so one that began now needs a release before it counts.
    m_Menu.PrevButton = S.Button;
    m_Menu.PrevGesture = Gesture;
    return true;
}
```

- [ ] **Step 5: Wire it into the frontend**

`Makefile:294`: add `MenuHost.cpp` to `FRONTEND_SRC` after `Overlay.cpp`.

`Source/Project64-sdl/main.cpp`: add `#include "MenuHost.h"` after `#include "InputConfig.h"` and `#include <memory>` after `#include <string>`. After the `if (!CN64System::RunFileImage(RomPath)) { ... }` block, add:

```cpp
    // The emulator actions menu, for a single game whose layout names a Menu slot or gesture
    // (Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md). The frontend's own
    // InputConfig already holds that layout: LayoutUsesPointer loaded it to size the window.
    std::unique_ptr<MenuHost> menu;
    if (pointer != nullptr && !TileMode)
    {
        const InputConfig & Layout = InputConfig::Get();
        if (Layout.MenuZone() != POINTER_ZONE_NONE || Layout.MenuGesture() != 0)
        {
            menu.reset(new MenuHost(pointer, window, Layout.MenuZone(), Layout.MenuGesture()));
        }
    }
```

In the main loop, directly after the `if (pointer != nullptr) { PublishMouse(...); ... }` block, add:

```cpp
        if (menu && !menu->Poll())
        {
            running = false; // Quit from the menu
        }
```

- [ ] **Step 6: Run everything to verify it passes**

Run: `make -j8 all && make test && make unit-test && make pointer-selftest rom=<SM64 ROM> && make face-selftest rom=<SM64 ROM>`
Expected: all `ok:`; the pointer self-test's five runs pass with the new success line.

- [ ] **Step 7: Commit**

```bash
git add Source/Project64-sdl/MenuHost.h Source/Project64-sdl/MenuHost.cpp Source/Project64-sdl/main.cpp Makefile Config/mouse/super_mario_64_usa.yaml Config/mouse/goldeneye_007_u.yaml Config/mouse/mario_kart_64_u.yaml Scripts/pointer_selftest.sh Source/Project64-sdl/InputConfigTest.cpp Source/Project64-wizard/WizardDraftTest.cpp
git commit -m "Open the actions menu from the panel: the host pauses the game, steps a frame per change, and acts on each item

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: Documentation and the manual check

**Files:**
- Modify: `Docs/UserGuide.md` (section 7, after the "Toggle slots and the stick hold" subsection; section 11's test-hook row ~line 410)
- Modify: `AGENTS.md` (the mouse-and-face paragraph; Traps)
- Modify: `Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md` (append `## Result`)

- [ ] **Step 1: The user guide**

Add at the end of section 7, before `## 8. Playing with your face`:

```markdown
### The menu

The panel's `==` slot opens a menu: `pad-down` in the three shipped layouts, whose D-pad
down now lives only on its keyboard key. A layout can put it anywhere with
`Menu: {zone: <slot>}`, or on a gesture with `Menu: {face: <gesture>}`. Opening it pauses
the game, and the panel shows the menu's items instead of the buttons:

| Slot | Item |
|---|---|
| middle slot, and `==` itself | `Go`: carry on playing |
| first middle slot | `Fs`: full screen on or off |
| second middle slot | `Fc`: recentre the face, while the camera runs |
| fourth middle slot | `Sv`: save the game's state |
| fifth middle slot | `Ld`: load the saved state |
| left of the D-pad cross | `Rs`: reset the game |
| right of the C cross | `Qt`: quit |

`Sv`, `Ld`, `Rs` and `Qt` need two clicks: the first lights the slot, the second does it,
and a click anywhere else lets go. There is one save slot, so `Sv` replaces the last save.
The game gets no input while the menu is open. Each click that changes the menu lets the
game run for a single frame so the panel can redraw; the character stands still for it.
The click that closes the menu counts in the game only after you release it.
```

In section 11, change the row `| \`PJ64_POINTER_INJECT\`, \`PJ64_FACE_INJECT\`, \`PJ64_POINTER_SELFTEST\`, \`PJ64_GRID_SELFTEST\` | Test hooks used by the \`*-selftest\` targets. |` to list `PJ64_MENU_SELFTEST` after `PJ64_POINTER_SELFTEST`.

- [ ] **Step 2: AGENTS.md**

In the mouse-and-face section, after the paragraph beginning `*Then each frame.*`, add:

```markdown
*The menu.* A `Menu:` key in a layout gives the panel an `==` slot (or a gesture) that opens
the emulator actions menu. `Source/Project64-sdl/MenuHost.cpp` runs it on the main thread,
because a paused game runs neither `GetKeys` nor the overlay: it reads clicks from the
published `PointerSample`, applies the pure rules in `Source/Common/PointerMenu.h`, pauses
and resumes the core with `ExternalEvent`, and waits on `PointerState::OverlayFrames` so it
pauses only once the menu is on screen. While `MenuOpen` is set the plugin returns no input.
```

Add to the Traps, after "The cursor is never captured, confined or warped.":

```markdown
- **A paused game never redraws the panel.** The overlay is drawn by the emulation thread
  when the game presents a frame, and GL never leaves that thread. Every change to the menu
  is therefore shown by resuming the game until `OverlayFrames` moves, then pausing again;
  never by drawing from the main thread. A soft reset does not call the plugin's
  `RomClosed`, so anything that resets the game must bump `ClearClicks` as the menu does.
```

- [ ] **Step 3: Run the suites and commit**

Run: `make unit-test && make test`
Expected: all `ok:`.

```bash
git add Docs/UserGuide.md AGENTS.md
git commit -m "Document the actions menu

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 4: The manual check (the user plays)**

Ask the user to run each shipped layout (`make run rom=<ROM> input=Config/mouse/<game>.yaml`) and confirm: `==` on the D-pad's bottom slot; opening it pauses the game and shows the items; `Fs` toggles full screen and the menu stays usable in it; `Sv` then `Ld` restores the saved moment; `Rs` restarts the game with no toggle left on; `Qt` closes the emulator from the paused menu; a click on `Go` over B's slot does not punch. Record what they report.

- [ ] **Step 5: Record the Result**

Append to the spec a `## Result` section: the date, the commit range, the suites that passed, and the user's manual-check report in their words. Commit it:

```bash
git add Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md
git commit -m "Record the actions menu's result and manual check

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```
