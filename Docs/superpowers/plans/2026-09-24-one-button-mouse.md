# One-Button Mouse Play Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A player with only a pointer and its left button can hold one N64 button while pressing another (toggle slots) and keep the stick tilted while reaching for the panel (the stick hold), so the shipped mouse layouts play with no keyboard and no face gestures.

**Architecture:** The new rules are pure functions in `Source/Common/PointerLayout.h` (`PointerSettleStep`, `PointerClickStep`, `PointerParseSettle`), unit-tested headless. The YAML reader (`InputConfig`) gains `toggle:` on `{zone:}` and `hold:` on `{stick: pointer}` plus two cross-binding checks. The input plugin's `GetKeys` runs the pure steps in place of its old latch lines and publishes the toggled slots through two new `PointerState` fields, which the overlay draws. The wizard only round-trips the new keys. The three `Config/mouse/` layouts become mouse-only.

**Tech Stack:** C++14, SDL3, yaml-cpp, OpenGL immediate mode (overlay), POSIX sh (self-test), hand-written `Makefile`.

**Spec:** `Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md`

## Global Constraints

- Only the left button is read; `main.cpp`'s `PublishMouse` is not touched.
- The cursor is never captured, confined or warped (no `SDL_SetWindowRelativeMouseMode`, `SDL_CaptureMouse`, `SDL_SetWindowMouseGrab`, `SDL_WarpMouse*`).
- A layout with no `toggle:` and no `hold:` behaves exactly as today in what `GetKeys` returns.
- Settle defaults: `POINTER_SETTLE_PX` 8, `POINTER_SETTLE_POLLS` 9. `PJ64_POINTER_SETTLE=<px>,<polls>`; `0` never settles; anything else keeps the defaults and prints one stderr line naming the variable.
- The hold slot's label is `Ho`. The hold slot is any zone but `game`, and is no control's zone.
- Every zone binding on one slot agrees on `toggle`.
- Toggles and the hold clear on `RomClosed`.
- The Makefile compiles C++14: `Binding`'s new members use default member initialisers so `Binding{kind, code, positive, up, down, left, right}` and `Binding B = {}` stay valid.
- Every file touched here is LF (checked with `file`); keep it so.
- The wizard's committed pictures (`Docs/img/wizard/`) must not change: `make wizard-screenshots-check` passes untouched.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.

## Review Focus

1. **Dragging onto a toggle slot with the button already down** must not flip it — only a press that starts over the slot counts. Pinned in Task 1 (`PointerClickStep` drag case).
2. **Resting the cursor in the panel while holding** must not end the hold — only a rest in the game image does. Pinned in Task 1 (settle: panel polls never settle).
3. **A press on a gap between slots** must change nothing: no latch, no toggle, no hold. Pinned in Task 1 (latch case).
4. **Two controls on one toggle slot** must both be accepted and both toggle; the check is agreement, not uniqueness. Pinned in Task 2 (Z and R on mid2).
5. **Re-capturing the same slot in the wizard** must not silently drop that control's toggle. Pinned in Task 4 (SetZone onto its own toggle slot).

---

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `Source/Common/PointerLayout.h` | modify | settle, click step, settle-variable parser (pure) |
| `Source/Project64-sdl/PointerLayoutTest.cpp` | modify | tests for the three |
| `Source/Project64-sdl/InputConfig.h` | modify | `Binding::Toggle`, `Binding::Hold`, two queries |
| `Source/Project64-sdl/InputConfig.cpp` | modify | grammar, cross-binding checks, `Ho` label |
| `Source/Project64-sdl/InputConfigTest.cpp` | modify | grammar and error tests; shipped layouts |
| `Source/Common/PointerState.h` | modify | `ToggleZones`, `ToggledZones` |
| `Source/Project64-sdl/PluginInput.cpp` | modify | run the steps, publish, reset on `RomClosed` |
| `Source/Project64-sdl/Overlay.cpp` | modify | light toggled slots, corner mark |
| `Scripts/pointer_selftest.sh` | modify | the toggle case |
| `Makefile` | modify | `pointer-selftest` help text |
| `Source/Project64-wizard/WizardDraft.{h,cpp}` | modify | round-trip, `HoldZone()`, slot-consistency rules |
| `Source/Project64-wizard/WizardDraftTest.cpp` | modify | round-trip tests |
| `Source/Project64-wizard/Screens.cpp` | modify | `Ho` on the zone screen |
| `Config/mouse/*.yaml` | rewrite | mouse-only layouts |
| `Docs/UserGuide.md`, `AGENTS.md` | modify | docs |
| `Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md` | modify | Result section |

---

### Task 1: Settle, click step and settle parser in PointerLayout.h

**Files:**
- Modify: `Source/Common/PointerLayout.h` (defines near line 19; new code after `PointerGateStick`, before the `PointerFitToBase` comment block)
- Test: `Source/Project64-sdl/PointerLayoutTest.cpp`

**Interfaces:**
- Consumes: `PointerEval`, `PointerLayoutEvaluate`, `PointerGate`, `PointerGateStick`, `POINTER_ZONE_GAME`, `POINTER_ZONE_NONE` (all existing in this header).
- Produces:
  - `#define POINTER_SETTLE_PX 8.0f`, `#define POINTER_SETTLE_POLLS 9`
  - `struct PointerSettle { bool HaveAnchor; float AnchorX, AnchorY; int Count; bool HaveSettled; int8_t SettledX, SettledY; bool JustSettled; };` — zero-initialise with `{}` or `PointerSettle()`.
  - `void PointerSettleStep(PointerSettle * S, const PointerEval & E, float X, float Y, float Radius, int Polls)`
  - `struct PointerClicks { bool PrevButton; int Latched; uint32_t Toggled; bool Holding; int8_t HeldX, HeldY; };`
  - `PointerClicks PointerClicksInit()`
  - `void PointerClickStep(PointerClicks * C, bool Button, int Zone, uint32_t ToggleMask, int HoldZone, const PointerSettle & Settle)`
  - `bool PointerParseSettle(const char * Text, float * Radius, int * Polls)`

- [ ] **Step 1: Write the failing tests**

In `Source/Project64-sdl/PointerLayoutTest.cpp`, add this helper after `ZoneAt` (before `int main()`):

```cpp
// One poll at (X, Y) in the 640x640 window through the gate and the settle, in the order
// GetKeys runs them. Returns the evaluated zone.
static int SettlePoll(PointerGate * G, PointerSettle * S, float X, float Y,
                      float Radius = POINTER_SETTLE_PX, int Polls = POINTER_SETTLE_POLLS)
{
    PointerEval E = PointerLayoutEvaluate(X, Y, 640, 640, true);
    PointerGateStick(G, &E, X, Y, POINTER_FLICK_PX);
    PointerSettleStep(S, E, X, Y, Radius, Polls);
    return E.Zone;
}
```

Then, inside `main()`, immediately before the `// Gesture names.` comment, add:

```cpp
    // Settling: nine polls within 8 px in the game image record the gated stick, once.
    // (400,240) is half tilt right: X 40, Y 0.
    {
        PointerGate G = { false, 0, 0, 0, 0 };
        PointerSettle S = {};
        CHECK(!S.HaveSettled);                        // nothing before any rest
        for (int i = 0; i < 8; i++)
        {
            SettlePoll(&G, &S, i % 2 == 0 ? 400.0f : 405.0f, 240);   // jitter inside the radius
            CHECK(!S.JustSettled);
        }
        CHECK(!S.HaveSettled);
        SettlePoll(&G, &S, 400, 240);
        CHECK(S.JustSettled && S.HaveSettled);
        CHECK(S.SettledX == 40 && S.SettledY == 0);
        SettlePoll(&G, &S, 400, 240);
        CHECK(!S.JustSettled && S.HaveSettled);       // resting on does not record again

        // Leaving the image resets the count and keeps the tilt; resting in the panel never
        // settles, however long.
        for (int i = 0; i < 20; i++)
        {
            CHECK(SettlePoll(&G, &S, 256, 512) == 9); // mid2
            CHECK(!S.JustSettled);
        }
        CHECK(S.HaveSettled && S.SettledX == 40 && S.SettledY == 0);

        // Back in the image at the top edge: the flick from the panel reads neutral on its
        // first poll (the gate), then the rest records full tilt up.
        for (int i = 0; i < 8; i++)
        {
            SettlePoll(&G, &S, 320, 80);
            CHECK(!S.JustSettled);
        }
        SettlePoll(&G, &S, 320, 80);
        CHECK(S.JustSettled && S.SettledX == 0 && S.SettledY == 80);
    }

    // A slow drag from the centre to the panel, 2 px a poll, never rests, so it never records
    // the backward tilt the bottom edge reads as.
    {
        PointerGate G = { false, 0, 0, 0, 0 };
        PointerSettle S = {};
        for (float Y = 240; Y < 480; Y += 2)
        {
            SettlePoll(&G, &S, 320, Y);
        }
        CHECK(!S.HaveSettled);
    }

    // A flick from a rest to the panel keeps the tilt the rest recorded.
    {
        PointerGate G = { false, 0, 0, 0, 0 };
        PointerSettle S = {};
        for (int i = 0; i < 9; i++)
        {
            SettlePoll(&G, &S, 400, 240);
        }
        CHECK(S.JustSettled && S.SettledX == 40);
        SettlePoll(&G, &S, 330, 470);                 // one 239 px jump inside the image
        CHECK(SettlePoll(&G, &S, 320, 512) == 10);    // mid3
        CHECK(S.HaveSettled && S.SettledX == 40 && S.SettledY == 0);
    }

    // A radius of 0 never settles.
    {
        PointerGate G = { false, 0, 0, 0, 0 };
        PointerSettle S = {};
        for (int i = 0; i < 20; i++)
        {
            SettlePoll(&G, &S, 400, 240, 0.0f, POINTER_SETTLE_POLLS);
        }
        CHECK(!S.HaveSettled);
    }

    // The click step with no toggles and no hold is the old latch: the zone under the press
    // stays held wherever the cursor goes until release, and a press on a gap does nothing.
    {
        const PointerSettle None = {};
        PointerClicks C = PointerClicksInit();
        CHECK(C.Latched == POINTER_ZONE_NONE && C.Toggled == 0u && !C.Holding && !C.PrevButton);
        PointerClickStep(&C, true, 8, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == 8);
        PointerClickStep(&C, true, POINTER_ZONE_GAME, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == 8);
        PointerClickStep(&C, false, POINTER_ZONE_GAME, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == POINTER_ZONE_NONE);
        PointerClickStep(&C, true, POINTER_ZONE_NONE, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == POINTER_ZONE_NONE && C.Toggled == 0u && !C.Holding);
    }

    // A toggle slot flips on the press edge, ignores the release, and leaves the button free.
    {
        const PointerSettle None = {};
        const uint32_t Mid2 = 1u << 9;
        PointerClicks C = PointerClicksInit();
        PointerClickStep(&C, true, 9, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Toggled == Mid2 && C.Latched == POINTER_ZONE_NONE);
        PointerClickStep(&C, true, 9, Mid2, POINTER_ZONE_NONE, None);   // still down: no second flip
        CHECK(C.Toggled == Mid2);
        PointerClickStep(&C, false, 9, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Toggled == Mid2);                                        // the release does nothing
        PointerClickStep(&C, true, POINTER_ZONE_GAME, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == POINTER_ZONE_GAME && C.Toggled == Mid2);      // Z and A together
        PointerClickStep(&C, false, POINTER_ZONE_GAME, Mid2, POINTER_ZONE_NONE, None);
        PointerClickStep(&C, true, 9, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Toggled == 0u);                                          // the next press lets go
    }

    // Dragging onto a toggle slot with the button already down does not flip it.
    {
        const PointerSettle None = {};
        const uint32_t Mid2 = 1u << 9;
        PointerClicks C = PointerClicksInit();
        PointerClickStep(&C, true, 8, Mid2, POINTER_ZONE_NONE, None);
        PointerClickStep(&C, true, 9, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == 8 && C.Toggled == 0u);
    }

    // The hold slot (mid5, zone 12): a press copies the settled tilt, or neutral before any
    // settle; a second press lets go; other presses keep it; a settle in the image ends it.
    {
        PointerSettle S = {};
        PointerClicks C = PointerClicksInit();
        PointerClickStep(&C, true, 12, 0u, 12, S);
        CHECK(C.Holding && C.HeldX == 0 && C.HeldY == 0 && C.Latched == POINTER_ZONE_NONE);
        PointerClickStep(&C, false, 12, 0u, 12, S);
        PointerClickStep(&C, true, 12, 0u, 12, S);
        CHECK(!C.Holding);
        S.HaveSettled = true;
        S.SettledX = 40;
        S.SettledY = -20;
        PointerClickStep(&C, false, 12, 0u, 12, S);
        PointerClickStep(&C, true, 12, 0u, 12, S);
        CHECK(C.Holding && C.HeldX == 40 && C.HeldY == -20);
        PointerClickStep(&C, false, 10, 0u, 12, S);
        PointerClickStep(&C, true, 10, 0u, 12, S);                       // B on mid3 while holding
        CHECK(C.Holding && C.Latched == 10);
        PointerClickStep(&C, false, POINTER_ZONE_GAME, 0u, 12, S);
        PointerClickStep(&C, true, POINTER_ZONE_GAME, 0u, 12, S);        // A before the cursor rests
        CHECK(C.Holding && C.Latched == POINTER_ZONE_GAME);
        S.JustSettled = true;
        PointerClickStep(&C, true, POINTER_ZONE_GAME, 0u, 12, S);
        CHECK(!C.Holding);
    }

    // PJ64_POINTER_SETTLE: "0" or "<px>,<polls>", both positive; anything else changes nothing.
    {
        float R = POINTER_SETTLE_PX;
        int P = POINTER_SETTLE_POLLS;
        CHECK(PointerParseSettle("12,20", &R, &P) && R == 12.0f && P == 20);
        CHECK(PointerParseSettle("0", &R, &P) && R == 0.0f && P == 20);
        R = 5.0f;
        P = 7;
        CHECK(!PointerParseSettle("", &R, &P));
        CHECK(!PointerParseSettle("abc", &R, &P));
        CHECK(!PointerParseSettle("8", &R, &P));
        CHECK(!PointerParseSettle("8,0", &R, &P));
        CHECK(!PointerParseSettle("-1,9", &R, &P));
        CHECK(!PointerParseSettle("8,9x", &R, &P));
        CHECK(R == 5.0f && P == 7);
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make pointer-layout-test`
Expected: compile error, `unknown type name 'PointerSettle'` (and the other new names).

- [ ] **Step 3: Implement**

In `Source/Common/PointerLayout.h`, add `#include <stdio.h>` beside the other includes, and below `#define POINTER_FLICK_PX` add:

```cpp
#define POINTER_SETTLE_PX 8.0f     // a cursor within this distance of its anchor is resting
#define POINTER_SETTLE_POLLS 9     // polls it must rest for to settle (about 150 ms at 60 a second)
```

After the closing brace of `PointerGateStick`, add:

```cpp
// Where the cursor last rested in the game image, for the stick hold. A slow drag to the
// panel never rests, so it never records the backward tilt the image's bottom edge reads
// as. Zero-initialise for "nothing yet".
// Design: Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md
struct PointerSettle
{
    bool HaveAnchor;
    float AnchorX, AnchorY;
    int Count;                 // consecutive polls within the radius, capped at Polls + 1
    bool HaveSettled;
    int8_t SettledX, SettledY; // the gated stick at the last settle
    bool JustSettled;          // true only on the poll the count reached Polls
};

// Feeds one poll, after PointerGateStick, so E carries the gated stick. Outside the game
// image the anchor and the count reset but the recorded tilt stays. Radius <= 0 or
// Polls <= 0 never settles.
inline void PointerSettleStep(PointerSettle * S, const PointerEval & E, float X, float Y, float Radius, int Polls)
{
    S->JustSettled = false;
    if (E.Zone != POINTER_ZONE_GAME || Radius <= 0.0f || Polls <= 0)
    {
        S->HaveAnchor = false;
        S->Count = 0;
        return;
    }
    const float Dx = X - S->AnchorX, Dy = Y - S->AnchorY;
    if (!S->HaveAnchor || sqrtf(Dx * Dx + Dy * Dy) > Radius)
    {
        S->HaveAnchor = true;
        S->AnchorX = X;
        S->AnchorY = Y;
        S->Count = 1;
    }
    else if (S->Count <= Polls)
    {
        S->Count++;
    }
    if (S->Count == Polls)
    {
        S->HaveSettled = true;
        S->SettledX = E.StickX;
        S->SettledY = E.StickY;
        S->JustSettled = true;
    }
}

// The one button's state across polls: the momentary latch, the toggle slots that are on,
// and the stick hold. Start from PointerClicksInit().
struct PointerClicks
{
    bool PrevButton;
    int Latched;               // the zone pressed and still held, or POINTER_ZONE_NONE
    uint32_t Toggled;          // one bit per zone: the toggle slots that are on
    bool Holding;
    int8_t HeldX, HeldY;       // the stick while Holding
};

inline PointerClicks PointerClicksInit()
{
    PointerClicks C = { false, POINTER_ZONE_NONE, 0u, false, 0, 0 };
    return C;
}

// One poll of the button over Zone. On the press edge the hold slot turns the hold on or
// off (on copies the last settled tilt, or neutral before any settle), a slot in ToggleMask
// flips its bit and latches nothing, and anything else is latched until release. A settle
// in the game image ends the hold. With ToggleMask 0 and HoldZone POINTER_ZONE_NONE this is
// exactly the latch GetKeys always had.
inline void PointerClickStep(PointerClicks * C, bool Button, int Zone, uint32_t ToggleMask, int HoldZone, const PointerSettle & Settle)
{
    if (Button && !C->PrevButton && Zone != POINTER_ZONE_NONE)
    {
        if (Zone == HoldZone)
        {
            C->Holding = !C->Holding;
            C->HeldX = C->Holding && Settle.HaveSettled ? Settle.SettledX : 0;
            C->HeldY = C->Holding && Settle.HaveSettled ? Settle.SettledY : 0;
        }
        else if ((ToggleMask & (1u << Zone)) != 0)
        {
            C->Toggled ^= 1u << Zone;
        }
        else
        {
            C->Latched = Zone;
        }
    }
    if (!Button)
    {
        C->Latched = POINTER_ZONE_NONE;
    }
    if (C->Holding && Settle.JustSettled)
    {
        C->Holding = false;
    }
    C->PrevButton = Button;
}

// PJ64_POINTER_SETTLE: "0" never settles; "<px>,<polls>" with both positive sets the two.
// Anything else returns false and leaves both outputs as they were.
inline bool PointerParseSettle(const char * Text, float * Radius, int * Polls)
{
    if (strcmp(Text, "0") == 0)
    {
        *Radius = 0.0f;
        return true;
    }
    float R = 0.0f;
    int P = 0;
    char Tail = '\0';
    if (sscanf(Text, "%f,%d%c", &R, &P, &Tail) != 2 || R <= 0.0f || P <= 0)
    {
        return false;
    }
    *Radius = R;
    *Polls = P;
    return true;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make pointer-layout-test`
Expected: `ok: pointer layout`

- [ ] **Step 5: Commit**

```bash
git add Source/Common/PointerLayout.h Source/Project64-sdl/PointerLayoutTest.cpp
git commit -m "Add the one-button pointer rules: settle, toggle and hold steps, and the settle variable parser

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: The grammar — `toggle:`, `hold:` and the slot checks

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.h` (`struct Binding`, `class InputConfig`)
- Modify: `Source/Project64-sdl/InputConfig.cpp` (`PointerLabels` ~line 94, `ControlFromName` ~line 200, `ParseBinding` ~line 245, `Load` ~line 368)
- Test: `Source/Project64-sdl/InputConfigTest.cpp`

**Interfaces:**
- Consumes: `POINTER_ZONE_NONE`, `POINTER_ZONE_GAME`, `POINTER_ZONE_COUNT`, `PointerZoneFromName`, `PointerZoneName` (PointerLayout.h / PointerState.h).
- Produces:
  - `Binding::Toggle` (`bool`, default `false`), `Binding::Hold` (`int`, default `POINTER_ZONE_NONE`)
  - `uint32_t InputConfig::PointerToggleZones() const` — one bit per zone bound with `toggle: true`
  - `int InputConfig::PointerHoldZone() const` — the stick's hold slot or `POINTER_ZONE_NONE`
  - `PointerLabels` writes `"Ho"` into the hold slot's label.

- [ ] **Step 1: Write the failing tests**

In `Source/Project64-sdl/InputConfigTest.cpp`, add `#include <string>` to the includes, and after `LoadWasSilent` add:

```cpp
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
```

In `main()`, immediately before `CHECK(C.Load("Config/input.yaml"));`, add:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make input-config-test`
Expected: compile error, `no member named 'Toggle' in 'Binding'`.

- [ ] **Step 3: Implement the header**

In `Source/Project64-sdl/InputConfig.h`, replace the last member line of `struct Binding` (`SDL_Scancode UpKey, DownKey, LeftKey, RightKey;   // Keys only`) with:

```cpp
    SDL_Scancode UpKey, DownKey, LeftKey, RightKey;   // Keys only
    // One-button play (Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md). A
    // member of its own each, with a default, so every existing Binding{...} and Binding{}
    // stays valid; Hold is not the pointer's unused code, which every constructor sets to 0,
    // the pad-up zone.
    bool Toggle = false;                  // Zone only: a press turns it on, the next off
    int Hold = POINTER_ZONE_NONE;         // Pointer only: the stick's hold slot
```

In `class InputConfig`, after `UsesHeadStick()`, add:

```cpp
    // One bit per zone bound with {zone: ..., toggle: true}.
    uint32_t PointerToggleZones() const;

    // The stick's hold slot from {stick: pointer, hold: <slot>}, or POINTER_ZONE_NONE.
    int PointerHoldZone() const;
```

- [ ] **Step 4: Implement the reader**

In `Source/Project64-sdl/InputConfig.cpp`:

(a) Add after `UsesHeadStick`'s definition (before `PointerLabels`):

```cpp
uint32_t InputConfig::PointerToggleZones() const
{
    uint32_t Mask = 0;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone && B.Toggle) Mask |= 1u << B.code;
        }
    }
    return Mask;
}

int InputConfig::PointerHoldZone() const
{
    const std::vector<Binding> & Stick = m_Bindings[(int)N64Control::Stick];
    return (!Stick.empty() && Stick[0].kind == Binding::Kind::Pointer) ? Stick[0].Hold : POINTER_ZONE_NONE;
}
```

(b) At the end of `PointerLabels`, after its loop, add:

```cpp
    // The hold slot belongs to no control; the overlay still has to say it is taken.
    const int Hold = PointerHoldZone();
    if (Hold != POINTER_ZONE_NONE)
    {
        snprintf(Labels[Hold], POINTER_LABEL_SIZE, "%s", "Ho");
    }
```

(c) Replace `ControlFromName` with a file-scope name table and two functions:

```cpp
static const char * const kControlNames[] = {
    "A", "B", "Z", "Start", "L", "R",
    "CUp", "CDown", "CLeft", "CRight",
    "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
    "Stick"
};

static int ControlFromName(const std::string & Name)
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (Name == kControlNames[i]) return i;
    }
    return -1;
}
```

(d) In `ParseBinding`, replace these lines:

```cpp
    if (Form.empty() || (Form != "axis" && Value.size() != 1))
    {
        ConfigError(Path, Value, FormError);
        return false;
    }
    if (Form == "axis")
    {
        for (const auto & Entry : Value)
        {
            const std::string Key = Entry.first.as<std::string>();
            if (Key != "axis" && Key != "sign") { ConfigError(Path, Value, FormError); return false; }
        }
    }
```

with:

```cpp
    if (Form.empty())
    {
        ConfigError(Path, Value, FormError);
        return false;
    }
    // Three forms take a second key: {axis:, sign:}, {zone:, toggle:}, {stick:, hold:}. The
    // two one-button keys name their own form when they turn up anywhere else.
    for (const auto & Entry : Value)
    {
        const std::string Key = Entry.first.as<std::string>();
        if (Key == Form) continue;
        if ((Form == "axis" && Key == "sign") || (Form == "zone" && Key == "toggle") || (Form == "stick" && Key == "hold")) continue;
        if (Key == "toggle") { ConfigError(Path, Entry.first, "toggle only applies to {zone:}"); return false; }
        if (Key == "hold") { ConfigError(Path, Entry.first, "hold only applies to {stick: pointer}"); return false; }
        ConfigError(Path, Value, FormError);
        return false;
    }
```

(e) In the `if (Form == "zone")` branch, replace `Out = MakeZone(Zone);` and the `return true;` after it with:

```cpp
        Out = MakeZone(Zone);
        if (Value["toggle"])
        {
            bool Toggle = false;
            if (!YAML::convert<bool>::decode(Value["toggle"], Toggle))
            {
                ConfigError(Path, Value["toggle"], "toggle must be true or false");
                return false;
            }
            Out.Toggle = Toggle;
        }
        return true;
```

(f) In the `if (Form == "stick")` branch, replace the line `if (Name == "pointer") { Out = MakePointer(); return true; }` with the following, and insert the `HasHold` check directly after `const std::string Name = Value[Form].as<std::string>();`:

```cpp
        const bool HasHold = (bool)Value["hold"];
        if (HasHold && Name != "pointer") { ConfigError(Path, Value["hold"], "hold only applies to {stick: pointer}"); return false; }
```

```cpp
        if (Name == "pointer")
        {
            Out = MakePointer();
            if (HasHold)
            {
                const std::string Slot = Value["hold"].as<std::string>();
                const int Zone = PointerZoneFromName(Slot.c_str());
                if (Zone == POINTER_ZONE_NONE || Zone == POINTER_ZONE_GAME)
                {
                    ConfigError(Path, Value["hold"], "hold must name a panel slot");
                    return false;
                }
                Out.Hold = Zone;
            }
            return true;
        }
```

(g) Between `ParseBinding` and `InputConfig::Load`, add:

```cpp
// The one-button slot rules (Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md):
// every zone binding on one slot agrees on toggle, and the stick's hold slot is no control's
// zone. Run over the resolved table after every control is read, so the order the file
// names them in does not matter; controls are compared in enum order, which fixes which one
// an error names. Nodes[i] is the file's value for control i (null for an unnamed one,
// which is never a zone).
static bool CheckSlots(const char * Path, const std::vector<Binding> * Next, const YAML::Node * Nodes)
{
    int Hold = POINTER_ZONE_NONE;
    const std::vector<Binding> & Stick = Next[(int)N64Control::Stick];
    if (!Stick.empty() && Stick[0].kind == Binding::Kind::Pointer) Hold = Stick[0].Hold;

    int Owner[POINTER_ZONE_COUNT];
    bool OwnerToggle[POINTER_ZONE_COUNT];
    for (int z = 0; z < POINTER_ZONE_COUNT; z++)
    {
        Owner[z] = -1;
        OwnerToggle[z] = false;
    }
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : Next[i])
        {
            if (B.kind != Binding::Kind::Zone) continue;
            const std::string Slot = PointerZoneName(B.code);
            if (B.code == Hold)
            {
                ConfigError(Path, Nodes[i], Slot + " is the stick's hold slot and cannot also be bound");
                return false;
            }
            if (Owner[B.code] < 0)
            {
                Owner[B.code] = i;
                OwnerToggle[B.code] = B.Toggle;
                continue;
            }
            if (OwnerToggle[B.code] != B.Toggle)
            {
                const int T = B.Toggle ? i : Owner[B.code];
                const int M = B.Toggle ? Owner[B.code] : i;
                ConfigError(Path, Nodes[i], Slot + " is a toggle for " + kControlNames[T] + " but not for " + kControlNames[M]);
                return false;
            }
        }
    }
    return true;
}
```

(h) In `InputConfig::Load`, declare `YAML::Node ControlNodes[(int)N64Control::Count];` next to `bool Seen[...]`; in the bindings loop, directly after `Seen[Index] = true;`, add `ControlNodes[Index] = Entry.second;`; and directly before `if (StickIsHead && !HeadGestureName.empty())`, add:

```cpp
        if (!CheckSlots(Path, Next, ControlNodes)) return false;
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `make input-config-test && make wizard-draft-test`
Expected: `ok: input config` and the wizard draft test's `ok:` line (it links `InputConfig.o`; nothing there changes yet).

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/InputConfigTest.cpp
git commit -m "Read toggle slots and the stick's hold slot from a layout, and reject slots that disagree

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Run the rules in the plugin and draw them in the overlay

**Files:**
- Modify: `Source/Common/PointerState.h` (struct `PointerState`)
- Modify: `Source/Project64-sdl/PluginInput.cpp` (globals ~line 36, `OpenPointerState` ~line 72, `GetKeys` pointer block ~line 276, `RomClosed` ~line 396, `PublishPointerLabels` ~line 421)
- Modify: `Source/Project64-sdl/Overlay.cpp` (`DrawPanel` ~line 174, `DrawGuide` ~line 196, `OverlayDraw` ~line 294)
- Modify: `Scripts/pointer_selftest.sh`, `Makefile:490`

**Interfaces:**
- Consumes: Task 1's `PointerSettle`, `PointerSettleStep`, `PointerClicks`, `PointerClicksInit`, `PointerClickStep`, `PointerParseSettle`, `POINTER_SETTLE_PX`, `POINTER_SETTLE_POLLS`; Task 2's `InputConfig::PointerToggleZones()`, `InputConfig::PointerHoldZone()`.
- Produces: `PointerState::ToggleZones` (`std::atomic<uint32_t>`, written once by `PublishPointerLabels`), `PointerState::ToggledZones` (`std::atomic<uint32_t>`, written every `GetKeys`).

- [ ] **Step 1: Write the failing end-to-end case**

In `Scripts/pointer_selftest.sh`, extend the header comment's list of runs with:

```sh
# A fourth run loads a small layout with Z as a toggle on mid2: one static press over mid2 is
# one press edge, so Z must be on and nothing latched (zone=-1).
```

and directly before the final `if [ "$FAIL" -eq 0 ]; then`, add:

```sh
# Fourth run: a toggle slot turns its control on without latching the press.
TOGGLE_YAML="$(mktemp /tmp/pj64-toggle-XXXXXX)"
cat >"$TOGGLE_YAML" <<'EOF'
bindings:
  Stick: {stick: pointer}
  A:     {zone: game}
  Z:     {zone: mid2, toggle: true}
EOF
one_run "256,512,1" "zone=-1 a=0 start=0 z=1 x=0 y=0" "$ROM" "$TOGGLE_YAML" || FAIL=1  # mid2's centre
rm -f "$TOGGLE_YAML"
```

and change the success line to:

```sh
    echo "ok: pointer path maps a game click to A and mid1 to Start, finds a layout named after the ROM, and toggles Z on mid2"
```

In `Makefile:490`, change the help text to `## Prove the injected-pointer path maps a game click to A, mid1 to Start and a toggle slot to Z (usage: make pointer-selftest rom=/path/to/game.z64)`.

- [ ] **Step 2: Run it to verify it fails**

Run: `make all && make pointer-selftest rom=<path to a Super Mario 64 ROM>` (ask the user for the ROM path if it is not known; the repository ships none).
Expected: the first three runs pass; the fourth prints `FAIL: inject 256,512,1: got 'pointer-selftest zone=9 a=0 start=0 z=1 x=0 y=0', wanted 'pointer-selftest zone=-1 ...'` — the reader accepts the toggle since Task 2, but the plugin still latches it.

- [ ] **Step 3: Add the shared fields**

In `Source/Common/PointerState.h`, in `struct PointerState`, after `HeadStickWanted`, add:

```cpp
    std::atomic<uint32_t> ToggleZones;   // one bit per zone: the toggle slots and the hold slot (corner mark)
```

and after `Quadrant`, add:

```cpp
    std::atomic<uint32_t> ToggledZones;  // one bit per zone: toggles that are on, and the hold slot while holding
```

- [ ] **Step 4: Run the rules in the plugin**

In `Source/Project64-sdl/PluginInput.cpp`:

(a) Replace

```cpp
// Latch: the zone under the cursor when the button went down stays pressed until release.
static bool g_PointerPrevButton = false;
static int g_PointerLatched = POINTER_ZONE_NONE;
```

with

```cpp
// The one button: the momentary latch, the toggle slots that are on and the stick hold
// (PointerClickStep), and where the cursor last rested, which the hold copies
// (PointerSettleStep). PJ64_POINTER_SETTLE overrides the rest's radius and length.
// Design: Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md
static PointerClicks g_PointerClicks = PointerClicksInit();
static PointerSettle g_PointerSettle = PointerSettle();
static float g_PointerSettleRadius = POINTER_SETTLE_PX;
static int g_PointerSettlePolls = POINTER_SETTLE_POLLS;
```

(b) At the end of `OpenPointerState`, after the `PJ64_POINTER_FLICK` block, add:

```cpp
    const char * Settle = getenv("PJ64_POINTER_SETTLE");
    if (Settle != nullptr && !PointerParseSettle(Settle, &g_PointerSettleRadius, &g_PointerSettlePolls))
    {
        fprintf(stderr, "input: PJ64_POINTER_SETTLE=%s is not 0 or <px>,<polls>; using %g,%d\n",
                Settle, (double)g_PointerSettleRadius, g_PointerSettlePolls);
    }
```

(c) In `GetKeys`, replace the lines from `PointerGateStick(&g_PointerGate, &E, S.X, S.Y, g_PointerFlick);` through `g_Pointer->Quadrant.store(PointerQuadrant(E.StickX, E.StickY), std::memory_order_relaxed);` with:

```cpp
        PointerGateStick(&g_PointerGate, &E, S.X, S.Y, g_PointerFlick);
        PointerSettleStep(&g_PointerSettle, E, S.X, S.Y, g_PointerSettleRadius, g_PointerSettlePolls);
        const int HoldZone = Config.PointerHoldZone();
        PointerClickStep(&g_PointerClicks, S.Button, E.Zone, Config.PointerToggleZones(), HoldZone, g_PointerSettle);
        // While holding, the game's stick is the tilt the hold copied, wherever the cursor is.
        const int8_t StickX = g_PointerClicks.Holding ? g_PointerClicks.HeldX : E.StickX;
        const int8_t StickY = g_PointerClicks.Holding ? g_PointerClicks.HeldY : E.StickY;
        uint32_t On = g_PointerClicks.Toggled;
        if (g_PointerClicks.Holding && HoldZone != POINTER_ZONE_NONE)
        {
            On |= 1u << HoldZone;
        }
        g_Pointer->LatchedZone.store(g_PointerClicks.Latched, std::memory_order_relaxed);
        g_Pointer->ToggledZones.store(On, std::memory_order_relaxed);
        g_Pointer->Quadrant.store(PointerQuadrant(StickX, StickY), std::memory_order_relaxed);
```

In the loop below it, replace the zone test `if (g_PointerLatched == B.code)` with

```cpp
                    if (g_PointerClicks.Latched == B.code || (g_PointerClicks.Toggled & (1u << B.code)) != 0)
```

replace, in the `Kind::Pointer` branch, `Keys->X_AXIS = E.StickX;` and `Keys->Y_AXIS = E.StickY;` with `Keys->X_AXIS = StickX;` and `Keys->Y_AXIS = StickY;`, and replace `PointerSelftestReport(S.Button, g_PointerLatched, Gestures, Keys);` with `PointerSelftestReport(S.Button, g_PointerClicks.Latched, Gestures, Keys);`.

(d) Replace `RomClosed` with:

```cpp
EXPORT void CALL RomClosed(void)
{
    CloseGamepad();
    // A toggle or a hold never outlives its game, and the next game starts with no rest.
    g_PointerClicks = PointerClicksInit();
    g_PointerSettle = PointerSettle();
    if (g_Pointer != nullptr)
    {
        g_Pointer->ToggledZones.store(0u, std::memory_order_relaxed);
    }
}
```

(e) In `PublishPointerLabels`, after `g_Pointer->LatchedZone.store(POINTER_ZONE_NONE);`, add:

```cpp
    const int Hold = Config.PointerHoldZone();
    g_Pointer->ToggleZones.store(Config.PointerToggleZones() | (Hold != POINTER_ZONE_NONE ? 1u << Hold : 0u));
    g_Pointer->ToggledZones.store(0u);
```

- [ ] **Step 5: Draw it in the overlay**

In `Source/Project64-sdl/Overlay.cpp`:

(a) Replace `DrawPanel`'s signature and slot loop:

```cpp
// The panel: an opaque ground over the rows below the game, every slot with its label, the
// lit ones bright (latched, toggled on, or the hold slot while holding), a mark across the
// top-right corner of each toggle and hold slot, and the tracker under the middle slots.
static void DrawPanel(const PointerState * State, int W, int H, int GameH, uint32_t Lit, uint32_t Toggles)
{
    glColor4f(kPanelGrey, kPanelGrey, kPanelGrey, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, (float)GameH);
    glVertex2f((float)W, (float)GameH);
    glVertex2f((float)W, (float)H);
    glVertex2f(0.0f, (float)H);
    glEnd();
    for (int Zone = 0; Zone < POINTER_ZONE_GAME; Zone++)
    {
        const float Alpha = (Lit & (1u << Zone)) != 0 ? kBright : kDim;
        float X0, Y0, X1, Y1;
        PointerZoneRect(Zone, W, H, &X0, &Y0, &X1, &Y1);
        DrawRect(X0, Y0, X1, Y1, Alpha);
        if ((Toggles & (1u << Zone)) != 0)
        {
            DrawLine(X1 - 12.0f, Y0 + 1.0f, X1 - 1.0f, Y0 + 12.0f, Alpha);
        }
        DrawText(State->Labels[Zone], (X0 + X1) / 2.0f, (Y0 + Y1) / 2.0f, Alpha);
    }
    DrawFaceStatus(State, 178.0f, (float)H - 88.0f);
}
```

(b) Change `DrawGuide`'s signature from `(const PointerState * State, int W, int H, int GameH, int Latched, int Quadrant)` to `(const PointerState * State, int W, int H, int GameH, bool GameLit, int Quadrant)` and its line `const float GameAlpha = Latched == POINTER_ZONE_GAME ? kBright : kDim;` to `const float GameAlpha = GameLit ? kBright : kDim;`.

(c) In `OverlayDraw`, replace

```cpp
    const int Latched = State->LatchedZone.load(std::memory_order_relaxed);
    const int Quadrant = State->Quadrant.load(std::memory_order_relaxed);
    if (H > GameH)
    {
        DrawPanel(State, W, H, GameH, Latched);
    }
    if (!GuideHidden)
    {
        DrawGuide(State, W, H, GameH, Latched, Quadrant);
    }
```

with

```cpp
    const int Latched = State->LatchedZone.load(std::memory_order_relaxed);
    const int Quadrant = State->Quadrant.load(std::memory_order_relaxed);
    uint32_t Lit = State->ToggledZones.load(std::memory_order_relaxed);
    if (Latched >= 0 && Latched < POINTER_ZONE_COUNT)
    {
        Lit |= 1u << Latched;
    }
    if (H > GameH)
    {
        DrawPanel(State, W, H, GameH, Lit, State->ToggleZones.load(std::memory_order_relaxed));
    }
    if (!GuideHidden)
    {
        DrawGuide(State, W, H, GameH, (Lit & (1u << POINTER_ZONE_GAME)) != 0, Quadrant);
    }
```

- [ ] **Step 6: Run everything to verify it passes**

Run: `make all && make test && make unit-test && make pointer-selftest rom=<SM64 ROM> && make face-selftest rom=<SM64 ROM>`
Expected: four `ok:` lines from `make test`; every unit test `ok:` and the screenshot check passing; `ok: pointer path maps a game click to A and mid1 to Start, finds a layout named after the ROM, and toggles Z on mid2`; the face self-test's `ok:` line unchanged.

Then check a boot still renders (AGENTS.md "Verifying a change"):

```sh
PJ64_FACE=0 PJ64_FRAME_DUMP=/tmp/frame.ppm PJ64_FRAME_DUMP_AT=400 \
  perl -e 'alarm 20; exec @ARGV' -- ./Bin/macOS/Project64 <SM64 ROM> || true
```

Expected: `/tmp/frame.ppm` exists, 640x480, roughly 89% non-black (below 80% means something broke).

And that a bad settle value warns once:

```sh
PJ64_POINTER_SETTLE=abc PJ64_INPUT_YAML=Config/mouse/super_mario_64_usa.yaml PJ64_FACE=0 \
  perl -e 'alarm 8; exec @ARGV' -- ./Bin/macOS/Project64 <SM64 ROM> 2>&1 | grep PJ64_POINTER_SETTLE
```

Expected: exactly one line, `input: PJ64_POINTER_SETTLE=abc is not 0 or <px>,<polls>; using 8,9`.

- [ ] **Step 7: Commit**

```bash
git add Source/Common/PointerState.h Source/Project64-sdl/PluginInput.cpp Source/Project64-sdl/Overlay.cpp Scripts/pointer_selftest.sh Makefile
git commit -m "Toggle slots and the stick hold in play: the plugin runs the rules, the panel lights and marks them

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: The wizard keeps toggles and the hold

**Files:**
- Modify: `Source/Project64-wizard/WizardDraft.h` (public methods)
- Modify: `Source/Project64-wizard/WizardDraft.cpp` (`BindingFacts`/`FactsOf` ~line 106, `ValueText` ~line 180, `SetZone` ~line 286, `DescribeBinding` ~line 390)
- Modify: `Source/Project64-wizard/Screens.cpp` (`DrawPanel` ~line 504)
- Test: `Source/Project64-wizard/WizardDraftTest.cpp`

**Interfaces:**
- Consumes: Task 2's `Binding::Toggle`, `Binding::Hold`.
- Produces: `int WizardDraft::HoldZone() const` — the stick's hold slot or `POINTER_ZONE_NONE`.

- [ ] **Step 1: Write the failing tests**

In `Source/Project64-wizard/WizardDraftTest.cpp`, add `#include <stdlib.h>` and `#include <unistd.h>` to the includes, and after `Has` add:

```cpp
// A base file under /tmp holding Text; the caller removes it.
static std::string WriteBase(const char * Text)
{
    char Path[64];
    snprintf(Path, sizeof(Path), "/tmp/pj64-wizard-base-XXXXXX");
    const int Fd = mkstemp(Path);
    if (Fd < 0) { perror("mkstemp"); exit(2); }
    write(Fd, Text, strlen(Text));
    close(Fd);
    return Path;
}
```

In `main()`, directly before the comment `// The five shipped layouts are offered as bases, by label and by path.`, add:

```cpp
    // A toggle and a hold survive a round trip and read as such on screen, and the draft
    // never writes a file the reader would reject when a control joins either slot.
    {
        const std::string Base = WriteBase(
            "bindings:\n"
            "  Stick: {stick: pointer, hold: mid5}\n"
            "  A: {zone: game}\n"
            "  Z: {zone: mid2, toggle: true}\n");
        WizardDraft D;
        CHECK(D.LoadBase(Base.c_str()));
        CHECK(D.Bindings(N64Control::Z)[0].Toggle);
        CHECK(D.HoldZone() == 12);
        const std::string Text = D.Emit("x");
        CHECK(Has(Text, "Z:         {zone: mid2, toggle: true}\n"));
        CHECK(Has(Text, "Stick:     {stick: pointer, hold: mid5}\n"));
        CHECK(Has(Text, "A:         {zone: game}\n"));
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
        CHECK(Has(D.Emit("x"), "Stick:     {stick: pointer}\n"));
        CHECK(D.Validate("x"));
        remove(Base.c_str());
    }

    // Choosing the pointer stick afresh starts with no hold.
    {
        WizardDraft D;
        D.SetStickPointer();
        CHECK(D.HoldZone() == POINTER_ZONE_NONE);
        CHECK(Has(D.Emit("x"), "Stick:     {stick: pointer}\n"));
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make wizard-draft-test`
Expected: compile error, `no member named 'HoldZone' in 'WizardDraft'`.

- [ ] **Step 3: Implement the draft**

In `Source/Project64-wizard/WizardDraft.h`, after `ZoneOwner`'s declaration, add:

```cpp
    // The stick's hold slot ({stick: pointer, hold: <slot>}), or POINTER_ZONE_NONE. It
    // belongs to no control, so ZoneOwner never names it.
    int HoldZone() const;
```

In `Source/Project64-wizard/WizardDraft.cpp`:

(a) In `struct BindingFacts`, after `const char * Keys[4];`, add:

```cpp
    bool Toggle;            // Kind::Zone only: {zone: ..., toggle: true}
    const char * Hold;      // Kind::Pointer only: the hold slot's name, or nullptr
```

(b) In `FactsOf`, in `case Binding::Kind::Zone:` add `F.Toggle = B.Toggle;` before its `break;`, and in `case Binding::Kind::Pointer:` add `F.Hold = B.Hold != POINTER_ZONE_NONE ? PointerZoneName(B.Hold) : nullptr;` before its `break;`.

(c) In `ValueText`, replace the final `else` branch's `snprintf` line with:

```cpp
        std::string Extra;
        if (F.Toggle) Extra += ", toggle: true";
        if (F.Hold != nullptr) { Extra += ", hold: "; Extra += F.Hold; }
        snprintf(Buf, sizeof(Buf), "{%s: %s%s}", F.YamlKey, Scalar(F.Name).c_str(), Extra.c_str());
```

(d) In `DescribeBinding`, replace the final `else` branch's `snprintf(Buf, sizeof(Buf), "%s %s", F.Noun, F.Name);` with:

```cpp
        std::string Extra;
        if (F.Toggle) Extra += ", toggle";
        if (F.Hold != nullptr) { Extra += ", hold "; Extra += F.Hold; }
        snprintf(Buf, sizeof(Buf), "%s %s%s", F.Noun, F.Name, Extra.c_str());
```

(e) Replace `SetZone` with:

```cpp
void WizardDraft::SetZone(N64Control Control, int Zone)
{
    Binding B = {};
    B.kind = Binding::Kind::Zone;
    B.code = Zone;
    // A slot is a toggle for every control on it or for none (the reader's rule), so a
    // control joining a toggle slot, or re-capturing its own, is a toggle too.
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (m_Bindings[i].empty()) continue;
        const Binding & Other = m_Bindings[i][0];
        if (Other.kind == Binding::Kind::Zone && Other.code == Zone && Other.Toggle) B.Toggle = true;
    }
    // The hold slot is the stick's; a control taking it takes it off the stick.
    std::vector<Binding> & Stick = m_Bindings[(int)N64Control::Stick];
    if (!Stick.empty() && Stick[0].kind == Binding::Kind::Pointer && Stick[0].Hold == Zone)
    {
        Stick[0].Hold = POINTER_ZONE_NONE;
    }
    Replace(Control, B);
}
```

(f) After `ZoneOwner`'s definition, add:

```cpp
int WizardDraft::HoldZone() const
{
    const std::vector<Binding> & Stick = m_Bindings[(int)N64Control::Stick];
    return (!Stick.empty() && Stick[0].kind == Binding::Kind::Pointer) ? Stick[0].Hold : POINTER_ZONE_NONE;
}
```

`SetStickPointer` needs no change: `Binding B = {};` gives `Hold = POINTER_ZONE_NONE` from the default member initialiser.

- [ ] **Step 4: Label the hold slot on the zone screen**

In `Source/Project64-wizard/Screens.cpp`, in `DrawPanel`, directly before `const N64Control Owner = Draft.ZoneOwner(Zone);`, add:

```cpp
        // The stick's hold slot belongs to no control, so ZoneOwner never names it; show the
        // overlay's own label so the player sees it is taken.
        if (Zone == Draft.HoldZone())
        {
            Colour(Renderer, false);
            WizardText(Renderer, R.x + 4.0f, R.y + 4.0f, 1, "Ho");
            continue;
        }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `make wizard-draft-test && make wizard && make wizard-screenshots-check && make wizard-selftest`
Expected: the draft test's `ok:` line; the screenshot check passes with no picture changed; the wizard self-test's `ok:` line (it needs a window server).

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-wizard/WizardDraft.h Source/Project64-wizard/WizardDraft.cpp Source/Project64-wizard/WizardDraftTest.cpp Source/Project64-wizard/Screens.cpp
git commit -m "Keep toggle slots and the stick's hold through the wizard, and never write a slot the reader rejects

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Mouse-only shipped layouts

**Files:**
- Rewrite: `Config/mouse/super_mario_64_usa.yaml`, `Config/mouse/goldeneye_007_u.yaml`, `Config/mouse/mario_kart_64_u.yaml`
- Test: `Source/Project64-sdl/InputConfigTest.cpp` (~lines 238-246), `Source/Project64-wizard/WizardDraftTest.cpp` (~lines 185-192)

**Interfaces:**
- Consumes: Task 2's grammar, `PointerToggleZones()`, `PointerHoldZone()`.
- Produces: nothing code-facing.

- [ ] **Step 1: Write the failing tests**

In `Source/Project64-sdl/InputConfigTest.cpp`, replace

```cpp
    CHECK(C.Load("Config/mouse/super_mario_64_usa.yaml"));   // every shipped mouse layout must parse
    CHECK(C.UsesPointer());
    CHECK(C.UsesFace());                              // this layout binds Z, B and R to gestures
    CHECK(C.Load("Config/mouse/goldeneye_007_u.yaml"));
    CHECK(C.UsesPointer());
    CHECK(C.UsesFace());                              // this layout binds R, CLeft and CRight
    CHECK(C.Load("Config/mouse/mario_kart_64_u.yaml"));
    CHECK(C.UsesPointer());
    CHECK(C.UsesFace());                              // this layout binds R, Z and B
```

with

```cpp
    // Every shipped mouse layout parses, plays with one button, and never starts the camera.
    CHECK(C.Load("Config/mouse/super_mario_64_usa.yaml"));
    CHECK(C.UsesPointer() && !C.UsesFace());
    CHECK(C.PointerHoldZone() == 12);                                  // mid5
    CHECK(C.PointerToggleZones() == (1u << 9));                        // Z on mid2
    CHECK(C.Load("Config/mouse/goldeneye_007_u.yaml"));
    CHECK(C.UsesPointer() && !C.UsesFace());
    CHECK(C.PointerHoldZone() == 12);
    CHECK(C.PointerToggleZones() == (1u << 11));                       // R on mid4
    CHECK(C.Load("Config/mouse/mario_kart_64_u.yaml"));
    CHECK(C.UsesPointer() && !C.UsesFace());
    CHECK(C.PointerHoldZone() == 12);
    CHECK(C.PointerToggleZones() == ((1u << POINTER_ZONE_GAME) | (1u << 9)));   // A on the picture, R on mid2
```

In `Source/Project64-wizard/WizardDraftTest.cpp`, replace the block commented `// A mouse layout's zones come back as zones.` with:

```cpp
    // A mouse layout's zones, its toggle and its hold come back as they were.
    {
        WizardDraft D;
        CHECK(D.LoadBase(Layout("Config/mouse/super_mario_64_usa.yaml").c_str()));
        CHECK(D.Explicit(N64Control::Stick));
        CHECK(D.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Pointer);
        const std::string Text = D.Emit("x");
        CHECK(Has(Text, "Stick:     {stick: pointer, hold: mid5}\n"));
        CHECK(Has(Text, "Z:         {zone: mid2, toggle: true}\n"));
        CHECK(!Has(Text, "\n  L:"));                                   // L is left out
    }
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make input-config-test; make wizard-draft-test`
Expected: FAIL lines at the new `!C.UsesFace()` / `PointerHoldZone()` checks and the new `Has(...)` checks.

- [ ] **Step 3: Rewrite the layouts**

`Config/mouse/super_mario_64_usa.yaml`:

```yaml
# One-button mouse layout for Super Mario 64: the left button and the cursor are the whole
# controller, and the camera never starts. Select with
#   make run rom=... input=Config/mouse/super_mario_64_usa.yaml
# or keep a copy named after the ROM beside it. See Docs/UserGuide.md section 7, "Playing
# with a mouse".
#
# The game image is the stick and the zone "game" (a click anywhere in it). The panel
# below holds the slots: pad-up, pad-down, pad-left, pad-right (left cross), c-up,
# c-down, c-left, c-right (right cross) and mid1-mid5. Any control can take any slot.
#
# Z is a toggle slot: one press holds it, the next lets go, and the button is free in
# between. mid5 is the stick's hold slot (Ho): press it and the stick keeps the tilt the
# cursor last rested at while you reach for the panel; it lets go on a second press, or
# once the cursor rests in the game image again.
#   Dive:      run, press Ho, press B.
#   Long jump: run, press Ho, press Z, move up into the game image and click (A); press Z
#              again to stand.
# L is left out: the game does not need it, and it keeps its keyboard key.

bindings:
  Stick:     {stick: pointer, hold: mid5}
  A:         {zone: game}
  Start:     {zone: mid1}
  Z:         {zone: mid2, toggle: true}   # crouch; with A while moving, long jump
  B:         {zone: mid3}                 # punch, and dive while running
  R:         {zone: mid4}                 # camera mode
  CUp:       {zone: c-up}
  CDown:     {zone: c-down}
  CLeft:     {zone: c-left}
  CRight:    {zone: c-right}
  DPadUp:    {zone: pad-up}
  DPadDown:  {zone: pad-down}
  DPadLeft:  {zone: pad-left}
  DPadRight: {zone: pad-right}
```

`Config/mouse/goldeneye_007_u.yaml`:

```yaml
# One-button mouse layout for GoldenEye 007: the left button and the cursor are the whole
# controller, and the camera never starts. Select with
#   make run rom=... input=Config/mouse/goldeneye_007_u.yaml
# or keep a copy named after the ROM beside it. See Docs/UserGuide.md section 7, "Playing
# with a mouse".
#
# The game image is the stick and the zone "game": a click anywhere in it fires. R is a
# toggle slot: one press enters aim mode, the next leaves it, and the button is free to
# fire in between. mid5 is the stick's hold slot (Ho): press it to keep walking the way
# you were while you reach for the panel; it lets go on a second press, or once the cursor
# rests in the game image again.
# L is left out: R does the aiming, and L keeps its keyboard key.

bindings:
  Stick:     {stick: pointer, hold: mid5}
  Z:         {zone: game}                 # fire
  A:         {zone: mid1}                 # next weapon
  B:         {zone: mid2}                 # use, open doors
  Start:     {zone: mid3}
  R:         {zone: mid4, toggle: true}   # aim mode, on until pressed again
  CUp:       {zone: c-up}
  CDown:     {zone: c-down}
  CLeft:     {zone: c-left}               # strafe left
  CRight:    {zone: c-right}              # strafe right
  DPadUp:    {zone: pad-up}
  DPadDown:  {zone: pad-down}
  DPadLeft:  {zone: pad-left}
  DPadRight: {zone: pad-right}
```

`Config/mouse/mario_kart_64_u.yaml`:

```yaml
# One-button mouse layout for Mario Kart 64: the left button and the cursor are the whole
# controller, and the camera never starts. Select with
#   make run rom=... input=Config/mouse/mario_kart_64_u.yaml
# or keep a copy named after the ROM beside it. See Docs/UserGuide.md section 7, "Playing
# with a mouse".
#
# The game image steers. A is a toggle on the game image: click the picture once to
# accelerate and keep going, again to coast; steering is only where the cursor is, so no
# button needs holding. R is a toggle slot: one press hops and holds the drift, the next
# lets go. mid5 is the stick's hold slot (Ho): press it to keep steering the way you were
# while you reach for an item or the brake; it lets go on a second press, or once the
# cursor rests in the game image again.
# L is left out: the game does not need it, and it keeps its keyboard key.

bindings:
  Stick:     {stick: pointer, hold: mid5}
  A:         {zone: game, toggle: true}   # accelerate, on until clicked again
  Start:     {zone: mid1}
  R:         {zone: mid2, toggle: true}   # hop, and drift while on
  Z:         {zone: mid3}                 # use item
  B:         {zone: mid4}                 # brake, reverse
  CUp:       {zone: c-up}
  CDown:     {zone: c-down}
  CLeft:     {zone: c-left}
  CRight:    {zone: c-right}
  DPadUp:    {zone: pad-up}
  DPadDown:  {zone: pad-down}
  DPadLeft:  {zone: pad-left}
  DPadRight: {zone: pad-right}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make all && make unit-test && make pointer-selftest rom=<SM64 ROM> && make wizard-selftest`
Expected: every `ok:` line; the screenshot check unchanged; the pointer self-test's four runs pass (the SM64 layout still has A on `game` and Start on `mid1`).

- [ ] **Step 5: Commit**

```bash
git add Config/mouse/super_mario_64_usa.yaml Config/mouse/goldeneye_007_u.yaml Config/mouse/mario_kart_64_u.yaml Source/Project64-sdl/InputConfigTest.cpp Source/Project64-wizard/WizardDraftTest.cpp
git commit -m "Make the three shipped mouse layouts one-button: toggles and a stick hold replace the face gestures

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Documentation, the manual check, and the Result

**Files:**
- Modify: `Docs/UserGuide.md` (section 7 ~lines 226-262; section 11 table ~line 365)
- Modify: `AGENTS.md` (Commands ~line 34; mouse paragraph ~line 159; Traps)
- Modify: `Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md` (append `## Result`)

**Interfaces:** none.

- [ ] **Step 1: Update the user guide, section 7**

Replace the intro paragraph and code block (from `A layout can put every N64 button on a panel` through the closing fence of the two `make run` lines) with:

````markdown
A layout can put every N64 button on a panel under the game and make the game image the
stick, so a one-button mouse plays on its own. Three ship under `Config/mouse/`, one each
for Super Mario 64, GoldenEye 007 and Mario Kart 64. They use the left button only and
never start the camera:

```sh
make run rom=Roms/super_mario_64.z64 input=Config/mouse/super_mario_64_usa.yaml
```
````

Replace the last paragraph of the section (from `In a layout file the forms are` through `which is why the camera starts (next section).`) with:

```markdown
In a layout file the forms are `{zone: <name>}` for a slot and `{stick: pointer}` for the
stick. The slots are `game`, `pad-up`, `pad-down`, `pad-left`, `pad-right`, `c-up`,
`c-down`, `c-left`, `c-right` and `mid1` to `mid5`; any control can take any slot, and two
controls on one slot are both pressed. A button can be a face gesture instead (next
section), so your own layout can mix the two.

### Toggle slots and the stick hold

One button can only press one slot at a time, and the stick lets go whenever the cursor
leaves the game image. Two forms get around both, and the shipped layouts use them:

- **A toggle slot**, `Z: {zone: mid2, toggle: true}`. One press turns Z on, the next turns
  it off, and in between the button is free: with Z on, a click in the game image is Z and
  A together. Any slot can be a toggle, the game image included (Mario Kart's accelerate).
  Every control on one slot must agree on `toggle`. Toggles clear when the game closes.
- **The stick hold**, `Stick: {stick: pointer, hold: mid5}`. The slot shows `Ho`. Press it
  and the stick keeps the tilt the cursor last *rested* at in the game image, while you
  reach for any other slot. It lets go on a second press, or once the cursor rests in the
  game image again, so you can click in the picture on the way back up and still have the
  held tilt.

"Rested" means staying within 8 px for 9 polls, about 150 ms. A slow move down to the
panel never rests, so the hold never picks up the backward tilt the bottom of the picture
reads as. `PJ64_POINTER_SETTLE=<px>,<polls>` changes both numbers for a hand that moves
more or less; `0` turns resting off, and the hold then gives a centred stick and ends only
on a second press.

Toggle slots and the hold slot have a mark across their top-right corner, and are bright
while on. In Super Mario 64:

- **Dive:** run with the cursor, press `Ho`, press `B`.
- **Long jump:** run, press `Ho`, press `Z`, move up into the picture and click; press `Z`
  again to stand up.
```

- [ ] **Step 2: Update the user guide, section 11**

In the environment-variable table, directly after the `PJ64_POINTER_FLICK` row, add:

```markdown
| `PJ64_POINTER_SETTLE` | `<px>,<polls>`: how close, and for how many polls, the cursor must stay in the game image to count as resting, for the stick hold; `0` never rests. Default `8,9`. |
```

- [ ] **Step 3: Update AGENTS.md**

(a) Replace the Commands line `make run rom=Roms/game.z64 input=Config/mouse/super_mario_64_usa.yaml  # camera starts; face=0 stops it` with:

```sh
make run rom=Roms/game.z64 input=Config/mouse/super_mario_64_usa.yaml  # one-button mouse; no camera
```

(b) In the `*Then each frame.*` paragraph, replace `The plugin's \`GetKeys\` evaluates slots, the flick gate, gestures, the pointer stick` with `The plugin's \`GetKeys\` evaluates slots, the flick gate, the rest and the one button (\`PointerSettleStep\`, \`PointerClickStep\`: latch, toggle slots, the stick hold), gestures, the pointer stick`.

(c) Add to the Traps list, after the "SDL3 mouse state is main-thread only" trap:

```markdown
- **The cursor is never captured, confined or warped.** One-button play (toggle slots and
  the stick hold, `Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md`) exists so
  that no mouse mode is needed: the player reaches the panel with a free cursor. Relative
  mouse mode, a grab or a warp would take the panel away from a player who has nothing else.
```

- [ ] **Step 4: The manual check (a person plays)**

Ask the user to run, with their ROMs:

```sh
make run rom=<SM64 ROM> input=Config/mouse/super_mario_64_usa.yaml
make run rom=<GoldenEye ROM> input=Config/mouse/goldeneye_007_u.yaml
make run rom=<Mario Kart ROM> input=Config/mouse/mario_kart_64_u.yaml
```

and to confirm, for each: no camera prompt and no `face:` line on stderr; the toggle slots and `Ho` show the corner mark and light while on. For Super Mario 64 also: the dive; the long jump; a slow reach to the panel then `Ho` does not hold a backward tilt; a fast reach does not either. For GoldenEye: aim on with R, fire by clicking the picture, aim off. For Mario Kart: click the picture to accelerate, steer by hovering, R on and off to drift, `Ho` then Z to use an item without the kart turning straight. Record what they report, including anything that needed a different slot or `PJ64_POINTER_SETTLE` value.

- [ ] **Step 5: Record the Result in the spec**

Append to `Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md`:

```markdown
## Result

<Date>. Implemented as designed in <n> commits (<first hash>..<last hash>). `make unit-test`,
`make pointer-selftest` (four runs, the toggle case new) and `make wizard-screenshots-check`
pass; the wizard pictures did not change.

Manual check: <what the user reported per game, verbatim where it matters: which moves
worked, whether the settle constants were right, and any slot the table moved>.
```

filled in with the actual date, commit range and the user's report; every `<...>` above is replaced with that fact, not left in.

- [ ] **Step 6: Run the full suite once more and commit**

Run: `make unit-test && make test`
Expected: every `ok:` line.

```bash
git add Docs/UserGuide.md AGENTS.md Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md
git commit -m "Document toggle slots and the stick hold, and record the one-button result

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```
