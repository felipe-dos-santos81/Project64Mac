# Mouse panel and direction quadrants Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the mouse buttons out of the game image into a controller-shaped panel below it, make the whole game image the stick with four direction quadrants drawn as a guide, and keep flicks to the panel from reading as a backward tilt.

**Architecture:** The pure geometry header `Source/Common/PointerLayout.h` is rewritten (fourteen zones, the panel, the quadrant rule and a flick gate) and its consumers follow: the input plugin gates the stick and publishes the lit quadrant, the overlay paints the panel in the rows the renderer's viewport leaves free and the guide over the game, and the frontend parses the layout before the window exists so it can open a 640x640 window and hand the video plugin `PJ64_VIEWPORT_OFFSET=160`, which the renderer's dormant status-bar offset already honours everywhere on screen.

**Tech Stack:** C++14, fixed-function OpenGL 2.1, SDL3, yaml-cpp (now linked into the frontend as well), POSIX (`setenv`, `shm_open`), GNU Make, `/bin/sh`.

**Spec:** `Docs/superpowers/specs/2026-09-15-mouse-panel-design.md`

## Global Constraints

- Build with the existing `Makefile` only: `-std=c++14`, `-I$(SRC)`; test binaries follow the `pointer-layout-test` pattern (`$(CXX) $(LDFLAGS) -o $(BUILD)/<name> $^`, then `@$(BUILD)/<name>`). No new dependencies: yaml-cpp and SDL3 are already declared.
- Constants, verbatim from the spec: `POINTER_PANEL_HEIGHT = 160`, `POINTER_ZONE_COUNT = 14`, `POINTER_ZONE_GAME = 13`, `POINTER_FLICK_PX = 24`. The window is 640x640 only with a layout that uses the pointer, never for a grid tile; the game renders unscaled in the top 640x480.
- Zone names, exactly: `pad-up`, `pad-down`, `pad-left`, `pad-right`, `c-up`, `c-down`, `c-left`, `c-right`, `mid1`, `mid2`, `mid3`, `mid4`, `mid5`, `game`, in that index order. The old names (`top1`…`bottom4`, `left2`, `left3`, `right2`, `right3`, `centre`) are rejected.
- `PJ64_VIEWPORT_OFFSET` is set or cleared by the frontend only, before the plugins load. The overlay derives the game rectangle and the window from the GL viewport and never calls SDL from the emulation thread.
- Never capture the screen. Windowed runs are wrapped in `perl -e 'alarm N; exec @ARGV' -- <cmd> || true`. Every automated run sets `PJ64_FACE=0`; the camera is only ever started by the human partner's manual runs.
- Line endings: run `file <path>` before editing a tracked file and preserve what is there. Everything touched here is LF except `Source/Project64-video/Main.cpp`, which is CRLF; edit it with the Python snippet in Task 4, never with `sed` or a plain editor that would rewrite the endings.
- Commit after each task with the message given; end every commit message with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- Test ROM: `/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`.
- Between tasks some targets are red by design: after Task 1 `make input-config-test` does not compile until Task 2, and `make pointer-selftest` fails until Task 4. `make all` and `make test` stay green after every task.

---

## File structure

| File | Responsibility after this plan |
|---|---|
| `Source/Common/PointerLayout.h` | Pure geometry: zone table, slot rectangles, game-image stick, quadrant rule, flick gate. |
| `Source/Common/PointerState.h` | Shared struct; grows by the lit quadrant. |
| `Source/Project64-sdl/PointerLayoutTest.cpp` | Unit test for the header. |
| `Source/Project64-sdl/InputConfig.{h,cpp}` | YAML reader; gains a quiet load for the frontend. |
| `Source/Project64-sdl/InputConfigTest.cpp` | Parser test, new zone names. |
| `Config/mouse/*.yaml` | The three shipped layouts on the new slots. |
| `Source/Project64-sdl/PluginInput.cpp` | Evaluates, gates, latches, publishes latched zone and quadrant. |
| `Source/Project64-video/Main.cpp` | `ChangeSize` reads `PJ64_VIEWPORT_OFFSET` into `g_viewport_offset`. |
| `Source/Project64-sdl/main.cpp` | Parses the layout, opens the taller window, owns the offset variable. |
| `Source/Project64-sdl/Overlay.{h,cpp}` | Paints the panel and the guide from the viewport. |
| `Source/Project64-sdl/SdlRenderWindow.{h,cpp}` | Passes the viewport and the guide flag; frame dump reads at the viewport origin. |
| `Scripts/pointer_selftest.sh` | End-to-end proof on the new coordinates. |
| `Makefile` | Frontend sources and link, layout install policy, help text. |
| `README.md`, `AGENTS.md`, old spec | Documentation. |

---

### Task 1: Geometry header, shared struct, layout test

**Files:**
- Modify: `Source/Common/PointerLayout.h` (replace the whole file)
- Modify: `Source/Common/PointerState.h:31` and `:59`
- Modify: `Source/Project64-sdl/PointerLayoutTest.cpp` (replace the whole file)
- Modify: `Source/Project64-sdl/Overlay.cpp:177` (rename one constant so the frontend keeps building; Task 5 rewrites this file)
- Modify: `Source/Project64-sdl/main.cpp:108` (initialise the new atomic)

**Interfaces:**
- Consumes: nothing new.
- Produces, all in `PointerLayout.h`: `POINTER_PANEL_HEIGHT`, `POINTER_FLICK_PX`, `POINTER_ZONE_GAME`, `int PointerGameHeight(int H)`, `float PointerStickRadius(int W, int H)`, `void PointerZoneRect(int Zone, int W, int H, float * X0, float * Y0, float * X1, float * Y1)`, `PointerEval PointerLayoutEvaluate(float X, float Y, int W, int H, bool Inside)`, `int PointerQuadrant(int8_t StickX, int8_t StickY)`, `struct PointerGate`, `void PointerGateStick(PointerGate * G, PointerEval * E, float X, float Y, float Threshold)`. In `PointerState.h`: `POINTER_ZONE_COUNT` is 14 and `std::atomic<int32_t> Quadrant` sits after `LatchedZone`. Tasks 3 and 5 use these.

- [ ] **Step 1: Write the failing test**

Replace `Source/Project64-sdl/PointerLayoutTest.cpp` with:

```cpp
// Project64 - A Nintendo 64 emulator
// Tests for PointerLayout. No window and no SDL init; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <string.h>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

// True when (X, Y) evaluates to Zone in the 640x640 window.
static bool ZoneAt(float X, float Y, int Zone)
{
    return PointerLayoutEvaluate(X, Y, 640, 640, true).Zone == Zone;
}

int main()
{
    // The 640x640 window: game image 640x480, centre (320,240), R = 160, panel from y=480.
    const int W = 640, H = 640;
    CHECK(POINTER_ZONE_COUNT == 14);
    CHECK(POINTER_PANEL_HEIGHT == 160);
    CHECK(PointerGameHeight(H) == 480);
    CHECK(PointerStickRadius(W, H) == 160.0f);

    // Zone names round-trip and cover the whole table.
    CHECK(strcmp(PointerZoneName(0), "pad-up") == 0);
    CHECK(strcmp(PointerZoneName(1), "pad-down") == 0);
    CHECK(strcmp(PointerZoneName(2), "pad-left") == 0);
    CHECK(strcmp(PointerZoneName(3), "pad-right") == 0);
    CHECK(strcmp(PointerZoneName(4), "c-up") == 0);
    CHECK(strcmp(PointerZoneName(7), "c-right") == 0);
    CHECK(strcmp(PointerZoneName(8), "mid1") == 0);
    CHECK(strcmp(PointerZoneName(12), "mid5") == 0);
    CHECK(strcmp(PointerZoneName(POINTER_ZONE_GAME), "game") == 0);
    for (int i = 0; i < POINTER_ZONE_COUNT; i++)
    {
        CHECK(PointerZoneFromName(PointerZoneName(i)) == i);
    }
    CHECK(PointerZoneFromName("top4") == POINTER_ZONE_NONE);
    CHECK(PointerZoneFromName("centre") == POINTER_ZONE_NONE);
    CHECK(PointerZoneFromName("") == POINTER_ZONE_NONE);

    // Every slot at its centre.
    CHECK(ZoneAt(80, 512, 0));     // pad-up
    CHECK(ZoneAt(80, 608, 1));     // pad-down
    CHECK(ZoneAt(32, 560, 2));     // pad-left
    CHECK(ZoneAt(128, 560, 3));    // pad-right
    CHECK(ZoneAt(560, 512, 4));    // c-up
    CHECK(ZoneAt(560, 608, 5));    // c-down
    CHECK(ZoneAt(512, 560, 6));    // c-left
    CHECK(ZoneAt(608, 560, 7));    // c-right
    CHECK(ZoneAt(192, 512, 8));    // mid1
    CHECK(ZoneAt(256, 512, 9));    // mid2
    CHECK(ZoneAt(320, 512, 10));   // mid3
    CHECK(ZoneAt(384, 512, 11));   // mid4
    CHECK(ZoneAt(448, 512, 12));   // mid5
    // pad-left's right edge: x=55 is the slot, x=56 is the cross's empty centre.
    CHECK(ZoneAt(55, 560, 2));
    CHECK(ZoneAt(56, 560, POINTER_ZONE_NONE));
    // Gaps: between mid1 and mid2, the panel's top-left corner, the right cross's centre.
    CHECK(ZoneAt(224, 512, POINTER_ZONE_NONE));
    CHECK(ZoneAt(2, 482, POINTER_ZONE_NONE));
    CHECK(ZoneAt(560, 560, POINTER_ZONE_NONE));
    // The whole game image is one zone; the first panel row has no slot.
    CHECK(ZoneAt(0, 0, POINTER_ZONE_GAME));
    CHECK(ZoneAt(639, 479, POINTER_ZONE_GAME));
    CHECK(ZoneAt(320, 240, POINTER_ZONE_GAME));
    CHECK(ZoneAt(320, 480, POINTER_ZONE_NONE));

    // Stick: centre is neutral; the dead zone is 0.1 R = 16 px.
    PointerEval E = PointerLayoutEvaluate(320, 240, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(320 + 15, 240, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    // At the ring the tilt is full.
    E = PointerLayoutEvaluate(320 + 160, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_GAME && E.StickX == 80 && E.StickY == 0);
    // Half way is half tilt.
    E = PointerLayoutEvaluate(320 + 80, 240, W, H, true);
    CHECK(E.StickX == 40 && E.StickY == 0);
    // Beyond the ring, still in the image, clamps to full tilt.
    E = PointerLayoutEvaluate(320 + 300, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_GAME && E.StickX == 80 && E.StickY == 0);
    // Screen up is stick up (positive N64 Y).
    E = PointerLayoutEvaluate(320, 240 - 159, W, H, true);
    CHECK(E.StickX == 0 && E.StickY > 78);
    E = PointerLayoutEvaluate(320, 240 + 159, W, H, true);
    CHECK(E.StickY < -78);
    // A diagonal beyond the ring is clamped to unit length, not per axis.
    E = PointerLayoutEvaluate(320 + 160, 240 - 159, W, H, true);
    CHECK(E.StickX > 50 && E.StickX < 62 && E.StickY > 50 && E.StickY < 62);
    // In the panel the stick is neutral, slot or not.
    E = PointerLayoutEvaluate(192, 512, W, H, true);
    CHECK(E.Zone == 8 && E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(320, 600, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0 && E.StickY == 0);
    // Outside the window or unfocused: no zone, neutral stick.
    E = PointerLayoutEvaluate(320, 240, W, H, false);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(-5, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE);
    E = PointerLayoutEvaluate(320, 640, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE);
    // A window too short for the panel, or degenerate, is safe.
    E = PointerLayoutEvaluate(320, 100, 640, 160, true);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0);
    E = PointerLayoutEvaluate(0, 0, 0, 0, true);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0);

    // Slot rectangles: exact positions, inside the panel, no overlaps.
    float X0, Y0, X1, Y1;
    PointerZoneRect(0, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 56 && Y0 == 488 && X1 == 104 && Y1 == 536);
    PointerZoneRect(7, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 584 && Y0 == 536 && X1 == 632 && Y1 == 584);
    PointerZoneRect(12, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 420 && Y0 == 488 && X1 == 476 && Y1 == 536);
    PointerZoneRect(POINTER_ZONE_GAME, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 0 && Y0 == 0 && X1 == 640 && Y1 == 480);
    for (int A = 0; A < POINTER_ZONE_GAME; A++)
    {
        float Ax0, Ay0, Ax1, Ay1;
        PointerZoneRect(A, W, H, &Ax0, &Ay0, &Ax1, &Ay1);
        CHECK(Ax0 >= 0 && Ay0 >= 480 && Ax1 <= 640 && Ay1 <= 640);
        for (int B = A + 1; B < POINTER_ZONE_GAME; B++)
        {
            float Bx0, By0, Bx1, By1;
            PointerZoneRect(B, W, H, &Bx0, &By0, &Bx1, &By1);
            const bool Apart = Ax1 <= Bx0 || Bx1 <= Ax0 || Ay1 <= By0 || By1 <= Ay0;
            CHECK(Apart);
        }
    }

    // Quadrants: vertical wins a tie, so 45 degrees is up and 46 is right.
    CHECK(PointerQuadrant(0, 80) == 0);
    CHECK(PointerQuadrant(80, 0) == 1);
    CHECK(PointerQuadrant(0, -80) == 2);
    CHECK(PointerQuadrant(-80, 0) == 3);
    CHECK(PointerQuadrant(0, 0) == -1);
    CHECK(PointerQuadrant(56, 58) == 0);   // 44 degrees from vertical
    CHECK(PointerQuadrant(58, 56) == 1);   // 46 degrees from vertical
    CHECK(PointerQuadrant(56, 56) == 0);
    CHECK(PointerQuadrant(-56, -58) == 2);

    // Flick gate. A 300 px jump inside the image keeps the previous tilt; a 5 px move follows.
    PointerGate G = { false, 0, 0, 0, 0 };
    E = PointerLayoutEvaluate(320, 140, W, H, true);
    PointerGateStick(&G, &E, 320, 140, POINTER_FLICK_PX);
    CHECK(E.StickX == 0 && E.StickY == 50);           // first poll is never gated
    E = PointerLayoutEvaluate(320, 440, W, H, true);
    PointerGateStick(&G, &E, 320, 440, POINTER_FLICK_PX);
    CHECK(E.StickX == 0 && E.StickY == 50);           // jumped 300 px: held
    E = PointerLayoutEvaluate(320, 445, W, H, true);
    PointerGateStick(&G, &E, 320, 445, POINTER_FLICK_PX);
    CHECK(E.StickY == -80);                           // moved 5 px: follows the cursor
    // A jump that lands in the panel reads neutral, gate or not.
    E = PointerLayoutEvaluate(320, 600, W, H, true);
    PointerGateStick(&G, &E, 320, 600, POINTER_FLICK_PX);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0 && E.StickY == 0);
    // Coming back fast from the panel holds neutral until the cursor slows.
    E = PointerLayoutEvaluate(320, 400, W, H, true);
    PointerGateStick(&G, &E, 320, 400, POINTER_FLICK_PX);
    CHECK(E.Zone == POINTER_ZONE_GAME && E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(320, 398, W, H, true);
    PointerGateStick(&G, &E, 320, 398, POINTER_FLICK_PX);
    CHECK(E.StickY == -79);
    // Threshold 0 disables the gate.
    PointerGate Off = { false, 0, 0, 0, 0 };
    E = PointerLayoutEvaluate(320, 140, W, H, true);
    PointerGateStick(&Off, &E, 320, 140, 0.0f);
    E = PointerLayoutEvaluate(320, 440, W, H, true);
    PointerGateStick(&Off, &E, 320, 440, 0.0f);
    CHECK(E.StickY == -80);

    // Gesture names.
    CHECK(PointerGestureFromName("eyebrows") == POINTER_GESTURE_EYEBROWS);
    CHECK(PointerGestureFromName("head-left") == POINTER_GESTURE_HEAD_LEFT);
    CHECK(PointerGestureFromName("head-right") == POINTER_GESTURE_HEAD_RIGHT);
    CHECK(PointerGestureFromName("wink") == 0);
    CHECK(PointerGestureIndex(POINTER_GESTURE_EYEBROWS) == 0);
    CHECK(PointerGestureIndex(POINTER_GESTURE_HEAD_RIGHT) == 2);

    // The seqlock round-trips a sample.
    PointerState State;
    memset(&State, 0, sizeof(State));
    PointerSample In = { 12.5f, 34.0f, 640, 640, true, true };
    PointerPublish(&State, In);
    PointerSample Out;
    PointerSnapshot(&State, &Out);
    CHECK(Out.X == 12.5f && Out.Y == 34.0f && Out.W == 640 && Out.H == 640 && Out.Inside && Out.Button);
    CHECK(State.Seq == 2);

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: pointer layout\n");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make pointer-layout-test`
Expected: compile errors naming `POINTER_PANEL_HEIGHT`, `PointerGameHeight`, `POINTER_ZONE_GAME`, `PointerQuadrant`, `PointerGate`.

- [ ] **Step 3: Write the header**

Replace `Source/Common/PointerLayout.h` with:

```cpp
// Project64 - A Nintendo 64 emulator
// The mouse geometry, shared by the input plugin (evaluation) and the overlay (drawing) so
// the two can never disagree about where a slot is. The game image is the stick and the
// zone "game"; the panel below it holds thirteen slots. Pure functions, no SDL.
// Design: Docs/superpowers/specs/2026-09-15-mouse-panel-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

enum
{
    POINTER_ZONE_NONE = -1,
    POINTER_ZONE_GAME = 13,
};

#define POINTER_PANEL_HEIGHT 160   // rows below the game image, in window pixels
#define POINTER_FLICK_PX 24.0f     // cursor travel per poll above which the stick holds

// Zone order: the left cross, the right cross, the five middle slots, then the game image.
inline const char * PointerZoneName(int Zone)
{
    static const char * const kNames[14] = {
        "pad-up", "pad-down", "pad-left", "pad-right",
        "c-up", "c-down", "c-left", "c-right",
        "mid1", "mid2", "mid3", "mid4", "mid5",
        "game",
    };
    return (Zone >= 0 && Zone < 14) ? kNames[Zone] : "";
}

inline int PointerZoneFromName(const char * Name)
{
    for (int i = 0; i < 14; i++)
    {
        if (strcmp(Name, PointerZoneName(i)) == 0) return i;
    }
    return POINTER_ZONE_NONE;
}

// Height of the game image: everything above the panel.
inline int PointerGameHeight(int H)
{
    return H - POINTER_PANEL_HEIGHT;
}

// Full-tilt radius: a third of the game image's height (160 px at 640x480).
inline float PointerStickRadius(int W, int H)
{
    (void)W;
    return (float)PointerGameHeight(H) / 3.0f;
}

// Pixel rectangle of a zone, top-left origin. Slots are fixed-size cells hung from the
// panel's top edge: the left cross from the left edge, the right cross from the right edge,
// the middle slots from the left edge (symmetric at W = 640). The centre cell of each cross
// is empty.
inline void PointerZoneRect(int Zone, int W, int H, float * X0, float * Y0, float * X1, float * Y1)
{
    const float Top = (float)PointerGameHeight(H);
    float X = 0, Y = 0, Cw = 48, Ch = 48;
    switch (Zone)
    {
    case 0: X = 56; Y = Top + 8; break;                 // pad-up
    case 1: X = 56; Y = Top + 104; break;               // pad-down
    case 2: X = 8; Y = Top + 56; break;                 // pad-left
    case 3: X = 104; Y = Top + 56; break;               // pad-right
    case 4: X = (float)W - 104; Y = Top + 8; break;     // c-up
    case 5: X = (float)W - 104; Y = Top + 104; break;   // c-down
    case 6: X = (float)W - 152; Y = Top + 56; break;    // c-left
    case 7: X = (float)W - 56; Y = Top + 56; break;     // c-right
    case 8: case 9: case 10: case 11: case 12:          // mid1..mid5
        X = 164 + 64 * (float)(Zone - 8); Y = Top + 8; Cw = 56; break;
    case POINTER_ZONE_GAME:
        X = 0; Y = 0; Cw = (float)W; Ch = Top; break;
    default:
        *X0 = *Y0 = *X1 = *Y1 = 0; return;
    }
    *X0 = X; *Y0 = Y; *X1 = X + Cw; *Y1 = Y + Ch;
}

struct PointerEval
{
    int Zone;
    int8_t StickX, StickY;
};

// X,Y in window pixels from the top left. Inside false means "not over this window",
// which reads as no zone and a neutral stick. Anywhere in the game image is the zone
// "game" with the stick from the offset to its centre; the panel gives its slot and a
// neutral stick; gaps in the panel give no zone.
inline PointerEval PointerLayoutEvaluate(float X, float Y, int W, int H, bool Inside)
{
    PointerEval E = { POINTER_ZONE_NONE, 0, 0 };
    const int Gh = PointerGameHeight(H);
    if (!Inside || W <= 0 || Gh <= 0 || X < 0 || Y < 0 || X >= (float)W || Y >= (float)H)
    {
        return E;
    }
    if (Y >= (float)Gh)
    {
        for (int Zone = 0; Zone < POINTER_ZONE_GAME; Zone++)
        {
            float X0, Y0, X1, Y1;
            PointerZoneRect(Zone, W, H, &X0, &Y0, &X1, &Y1);
            if (X >= X0 && X < X1 && Y >= Y0 && Y < Y1)
            {
                E.Zone = Zone;
                return E;
            }
        }
        return E;
    }
    E.Zone = POINTER_ZONE_GAME;
    const float R = PointerStickRadius(W, H);
    float Nx = (X - (float)W / 2.0f) / R;
    float Ny = (Y - (float)Gh / 2.0f) / R;
    const float Len = sqrtf(Nx * Nx + Ny * Ny);
    if (Len < 0.1f)
    {
        return E;
    }
    if (Len > 1.0f)
    {
        Nx /= Len;
        Ny /= Len;
    }
    E.StickX = (int8_t)lroundf(Nx * 80.0f);
    E.StickY = (int8_t)lroundf(-Ny * 80.0f); // screen y grows downward; N64 y grows upward
    return E;
}

// Which direction a tilt points: 0 up, 1 right, 2 down, 3 left, -1 neutral. Vertical wins
// a tie, so the 45-degree guide lines and this rule agree.
inline int PointerQuadrant(int8_t StickX, int8_t StickY)
{
    if (StickX == 0 && StickY == 0) return -1;
    const int Ax = StickX < 0 ? -StickX : StickX;
    const int Ay = StickY < 0 ? -StickY : StickY;
    if (Ay >= Ax) return StickY > 0 ? 0 : 2;
    return StickX > 0 ? 1 : 3;
}

// Flick gate state: the previous poll's cursor and the stick it produced.
struct PointerGate
{
    bool HavePrev;
    float PrevX, PrevY;
    int8_t PrevStickX, PrevStickY;
};

// Applies the flick rule to E, already evaluated at X,Y, and records this poll in G:
// outside the game image the stick is neutral; inside, a cursor that moved more than
// Threshold pixels since the previous poll keeps that poll's stick, so a flick to the panel
// never reads as a tilt on the way. Threshold <= 0 disables the gate.
inline void PointerGateStick(PointerGate * G, PointerEval * E, float X, float Y, float Threshold)
{
    if (E->Zone != POINTER_ZONE_GAME)
    {
        E->StickX = 0;
        E->StickY = 0;
    }
    else if (G->HavePrev && Threshold > 0.0f)
    {
        const float Dx = X - G->PrevX, Dy = Y - G->PrevY;
        if (sqrtf(Dx * Dx + Dy * Dy) > Threshold)
        {
            E->StickX = G->PrevStickX;
            E->StickY = G->PrevStickY;
        }
    }
    G->HavePrev = true;
    G->PrevX = X;
    G->PrevY = Y;
    G->PrevStickX = E->StickX;
    G->PrevStickY = E->StickY;
}
```

- [ ] **Step 4: Grow the shared struct**

In `Source/Common/PointerState.h`, change line 31 from

```cpp
#define POINTER_ZONE_COUNT 13   // 12 outer cells + centre; see PointerLayout.h
```

to

```cpp
#define POINTER_ZONE_COUNT 14   // 13 panel slots + the game image; see PointerLayout.h
```

and after line 59 (`std::atomic<int32_t> LatchedZone;    // POINTER_ZONE_NONE when nothing is held`) add:

```cpp
    std::atomic<int32_t> Quadrant;       // PointerQuadrant of the stick the game got; -1 neutral
```

- [ ] **Step 5: Keep the two consumers building**

In `Source/Project64-sdl/Overlay.cpp` line 177, change `if (Zone != POINTER_ZONE_CENTRE)` to `if (Zone != POINTER_ZONE_GAME)`. Nothing else in that file changes now; Task 5 rewrites it.

In `Source/Project64-sdl/main.cpp`, after line 108 (`State->LatchedZone.store(-1);`) add:

```cpp
    State->Quadrant.store(-1);
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `make pointer-layout-test`
Expected: `ok: pointer layout`

- [ ] **Step 7: Confirm the build and the smoke test**

Run: `make -j8 all && make test`
Expected: the version line and four `ok:` lines. (`make input-config-test` does not compile until Task 2; that is expected.)

- [ ] **Step 8: Commit**

```bash
git add Source/Common/PointerLayout.h Source/Common/PointerState.h Source/Project64-sdl/PointerLayoutTest.cpp Source/Project64-sdl/Overlay.cpp Source/Project64-sdl/main.cpp
git commit -m "Replace the pointer grid with the panel geometry, quadrants and flick gate

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Quiet load, parser test, shipped layouts, install policy

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.h:42-44`
- Modify: `Source/Project64-sdl/InputConfig.cpp:186` (before `ConfigError`), `:327-338` (`Load`)
- Modify: `Source/Project64-sdl/InputConfigTest.cpp:67-142`
- Modify: `Config/mouse/super_mario_64_usa.yaml`, `Config/mouse/goldeneye_007_u.yaml`, `Config/mouse/mario_kart_64_u.yaml` (replace each whole file)
- Modify: `Makefile:441-442` (`config` target)

**Interfaces:**
- Consumes: the zone table from Task 1 (through `PointerZoneFromName`, unchanged call).
- Produces: `bool InputConfig::Load(const char * Path, bool Quiet = false);` Task 4's frontend calls it with `Quiet = true`.

- [ ] **Step 1: Update the parser test**

In `Source/Project64-sdl/InputConfigTest.cpp` make these exact replacements.

Lines 67-73, the `Pointer` file:

```cpp
    const char * Pointer =
        "bindings:\n"
        "  Stick: {stick: pointer}\n"
        "  A: {zone: game}\n"
        "  Start: {zone: mid1}\n"
        "  Z: {face: eyebrows}\n"
        "  B: {face: head-left}\n";
```

Line 79: `CHECK(C.Bindings(N64Control::A)[0].code == POINTER_ZONE_CENTRE);` becomes `CHECK(C.Bindings(N64Control::A)[0].code == POINTER_ZONE_GAME);`

Line 80: `CHECK(C.Bindings(N64Control::Start)[0].code == 3);` becomes `CHECK(C.Bindings(N64Control::Start)[0].code == 8);`

Lines 87-88:

```cpp
    CHECK(strcmp(Labels[POINTER_ZONE_GAME], "A") == 0);
    CHECK(strcmp(Labels[8], "St") == 0);
```

Line 103 (in `ZonesOnly`): `"  A: {zone: centre}\n";` becomes `"  A: {zone: game}\n";`

After line 106 (`CHECK(!C.UsesFace()); // pointer without gestures: no camera`) insert:

```cpp

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
```

Line 122: after `CHECK(!C.Load("/tmp/pj64-does-not-exist.yaml"));` insert:

```cpp
    CHECK(!C.Load("/tmp/pj64-does-not-exist.yaml", true));   // quiet: same verdict, no print
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {zone: top4}\n")));     // the grid names are gone
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {zone: centre}\n")));
```

Line 125: `"bindings:\n  Stick: {zone: centre}\n"` becomes `"bindings:\n  Stick: {zone: game}\n"`

Line 128: `"bindings:\n  A: {zone: centre, face: eyebrows}\n"` becomes `"bindings:\n  A: {zone: game, face: eyebrows}\n"`

The shipped-file block at the end (lines 138-142) is unchanged: the three files must still parse, use the pointer, and the Mario file must still use the face.

- [ ] **Step 2: Run the test to verify it fails**

Run: `make input-config-test`
Expected: it compiles and prints `FAIL` lines for the shipped files (`Config/mouse/*.yaml` still name `centre` and `top4`), and the compiler rejects nothing. If the `Quiet` line fails to compile first, that is the same signal.

- [ ] **Step 3: Add the quiet load**

In `Source/Project64-sdl/InputConfig.h`, replace lines 42-44 with:

```cpp
    // Apply a file over the built-in defaults. On any error returns false, logs one
    // line unless Quiet, and leaves the instance exactly as it was. The frontend loads
    // the same file quietly to size its window; the plugin's load reports the error.
    bool Load(const char * Path, bool Quiet = false);
```

In `Source/Project64-sdl/InputConfig.cpp`, before `static void ConfigError(...)` (line 186) insert:

```cpp
// Set for the duration of a quiet Load: errors are still errors, but nothing is printed.
static bool g_Quiet = false;

struct QuietScope
{
    explicit QuietScope(bool Quiet) { g_Quiet = Quiet; }
    ~QuietScope() { g_Quiet = false; }
};

```

Make `ConfigError` start with `if (g_Quiet) return;`:

```cpp
static void ConfigError(const char * Path, const YAML::Node & Node, const std::string & Message)
{
    if (g_Quiet) return;
    const YAML::Mark Mark = Node.Mark();
```

Change the `Load` signature and its first catch:

```cpp
bool InputConfig::Load(const char * Path, bool Quiet)
{
    QuietScope Scope(Quiet);
    YAML::Node Root;
    try
    {
        Root = YAML::LoadFile(Path);
    }
    catch (const YAML::Exception & e)
    {
        if (!g_Quiet) fprintf(stderr, "input: %s: %s; using built-in defaults\n", Path, e.what());
        return false;
    }
```

- [ ] **Step 4: Rewrite the three layouts**

`Config/mouse/super_mario_64_usa.yaml`:

```yaml
# Mouse-and-face layout for Super Mario 64. Select with
#   make run rom=... input=Config/mouse/super_mario_64_usa.yaml face=1
# or PJ64_INPUT_YAML. See README "Playing with a mouse".
#
# The game image is the stick and the zone "game" (a click anywhere in it). The panel
# below holds the slots: pad-up, pad-down, pad-left, pad-right (left cross), c-up,
# c-down, c-left, c-right (right cross) and mid1-mid5. Any control can take any slot.
# Face gestures: eyebrows, head-left, head-right. Each needs the camera (face=1).

bindings:
  Stick:     {stick: pointer}
  A:         {zone: game}
  Z:         {face: eyebrows}     # held: crouch; brows + click while tilted = long jump
  B:         {face: head-left}    # punch, and dive while running
  R:         {face: head-right}
  Start:     {zone: mid1}
  L:         {zone: mid2}
  CUp:       {zone: c-up}
  CDown:     {zone: c-down}
  CLeft:     {zone: c-left}
  CRight:    {zone: c-right}
  DPadUp:    {zone: pad-up}
  DPadDown:  {zone: pad-down}
  DPadLeft:  {zone: pad-left}
  DPadRight: {zone: pad-right}
```

`Config/mouse/mario_kart_64_u.yaml`:

```yaml
# Mouse-and-face layout for Mario Kart 64.
# Select with make run rom=... input=Config/mouse/mario_kart_64_u.yaml face=1
# Hold the click anywhere in the game image to accelerate while steering with the cursor.
# Slots: pad-* (left cross), c-* (right cross), mid1-mid5; gestures: eyebrows, head-left,
# head-right.

bindings:
  Stick:     {stick: pointer}
  A:         {zone: game}         # accelerate, held
  R:         {face: eyebrows}     # hop and drift, held
  Z:         {face: head-left}    # use item
  B:         {face: head-right}   # brake / reverse
  Start:     {zone: mid1}
  L:         {zone: mid2}
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
# Mouse-and-face layout for GoldenEye 007 / Perfect Dark (control style 1.1).
# Select with make run rom=... input=Config/mouse/goldeneye_007_u.yaml face=1
# The stick aims and moves; strafe follows your head; fire is a click in the game image.
# Slots: pad-* (left cross), c-* (right cross), mid1-mid5; c-left, c-right and mid5 are
# free here because strafe is on the head turns.

bindings:
  Stick:     {stick: pointer}
  Z:         {zone: game}         # fire
  R:         {face: eyebrows}     # aim mode, held
  CLeft:     {face: head-left}    # strafe left
  CRight:    {face: head-right}   # strafe right
  A:         {zone: mid1}
  B:         {zone: mid2}
  Start:     {zone: mid3}
  L:         {zone: mid4}
  CUp:       {zone: c-up}
  CDown:     {zone: c-down}
  DPadUp:    {zone: pad-up}
  DPadDown:  {zone: pad-down}
  DPadLeft:  {zone: pad-left}
  DPadRight: {zone: pad-right}
```

- [ ] **Step 5: Refresh installed layouts on every build**

In `Makefile`, replace lines 441-442

```make
	@# -Rn: mouse layouts are user data once installed.
	@cp -Rn Config/mouse $(BIN)/Config/ 2>/dev/null || true
```

with

```make
	@# -f: the shipped mouse layouts are examples and follow the tracked files. A player's
	@# own layout goes beside the ROM as <rom>.yaml, which the lookup checks first.
	@mkdir -p $(BIN)/Config/mouse
	@cp -f Config/mouse/*.yaml $(BIN)/Config/mouse/
```

(Recipe lines start with a tab.)

- [ ] **Step 6: Run the tests to verify they pass**

Run: `make input-config-test && make pointer-layout-test`
Expected: `ok: input config` and `ok: pointer layout`.

Run: `make config && grep -l 'zone: game' Bin/macOS/Config/mouse/*.yaml`
Expected: the three installed files listed, proving a stale copy is overwritten.

- [ ] **Step 7: Commit**

```bash
git add Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/InputConfigTest.cpp Config/mouse Makefile
git commit -m "Move the shipped layouts to the panel slots and add a quiet load

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Plugin gate and quadrant

**Files:**
- Modify: `Source/Project64-sdl/PluginInput.cpp:36-38`, `:84`, `:254-266`

**Interfaces:**
- Consumes: `PointerGate`, `PointerGateStick`, `PointerQuadrant`, `POINTER_FLICK_PX` (Task 1); `PointerState::Quadrant` (Task 1).
- Produces: `Quadrant` written every poll, read by the overlay in Task 5. `PJ64_POINTER_FLICK` read once.

- [ ] **Step 1: Add the gate state**

Replace lines 36-38

```cpp
// Latch: the zone under the cursor when the button went down stays pressed until release.
static bool g_PointerPrevButton = false;
static int g_PointerLatched = POINTER_ZONE_NONE;
```

with

```cpp
// Latch: the zone under the cursor when the button went down stays pressed until release.
static bool g_PointerPrevButton = false;
static int g_PointerLatched = POINTER_ZONE_NONE;
// Flick gate: a cursor that jumps more than g_PointerFlick px between polls keeps the
// previous poll's tilt, so reaching for the panel never reads as a tilt on the way.
// PJ64_POINTER_FLICK overrides the threshold; 0 disables the gate.
static PointerGate g_PointerGate = { false, 0.0f, 0.0f, 0, 0 };
static float g_PointerFlick = POINTER_FLICK_PX;
```

- [ ] **Step 2: Read the override once**

In `OpenPointerState`, after line 84 (`g_Pointer = (PointerState *)Mapped;`) add:

```cpp
    const char * Flick = getenv("PJ64_POINTER_FLICK");
    if (Flick != nullptr)
    {
        g_PointerFlick = (float)atof(Flick);
    }
```

- [ ] **Step 3: Gate the stick and publish the quadrant**

Replace lines 254-266

```cpp
        PointerSample S;
        PointerSnapshot(g_Pointer, &S);
        const PointerEval E = PointerLayoutEvaluate(S.X, S.Y, S.W, S.H, S.Inside);
        if (S.Button && !g_PointerPrevButton)
        {
            g_PointerLatched = E.Zone; // press edge: latch whatever is under the cursor now
        }
        if (!S.Button)
        {
            g_PointerLatched = POINTER_ZONE_NONE;
        }
        g_PointerPrevButton = S.Button;
        g_Pointer->LatchedZone.store(g_PointerLatched, std::memory_order_relaxed);
```

with

```cpp
        PointerSample S;
        PointerSnapshot(g_Pointer, &S);
        PointerEval E = PointerLayoutEvaluate(S.X, S.Y, S.W, S.H, S.Inside);
        PointerGateStick(&g_PointerGate, &E, S.X, S.Y, g_PointerFlick);
        if (S.Button && !g_PointerPrevButton)
        {
            g_PointerLatched = E.Zone; // press edge: latch whatever is under the cursor now
        }
        if (!S.Button)
        {
            g_PointerLatched = POINTER_ZONE_NONE;
        }
        g_PointerPrevButton = S.Button;
        g_Pointer->LatchedZone.store(g_PointerLatched, std::memory_order_relaxed);
        g_Pointer->Quadrant.store(PointerQuadrant(E.StickX, E.StickY), std::memory_order_relaxed);
```

- [ ] **Step 4: Build and run what can run**

Run: `make -j8 all && make test && make pointer-layout-test`
Expected: the smoke suite's five lines and `ok: pointer layout`. The gate itself is proven by the layout test; the end-to-end proof waits for Task 4's window.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-sdl/PluginInput.cpp
git commit -m "Gate flicks to the panel and publish the lit quadrant

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: The taller window and the lifted viewport

**Files:**
- Modify: `Source/Project64-video/Main.cpp:95-107` (`ChangeSize`; CRLF, use the Python snippet)
- Modify: `Source/Project64-sdl/main.cpp:9`, `:18`, after `:165`, after `:238`
- Modify: `Source/Project64-sdl/SdlRenderWindow.cpp:158`
- Modify: `Makefile:293`, `:425`, `:465`
- Modify: `Scripts/pointer_selftest.sh:2-8`, `:22-24`, `:51-52`, `:54-56`, `:62`, `:66`

**Interfaces:**
- Consumes: `InputConfig::Load(path, true)` and `UsesPointer()` (Task 2); `POINTER_PANEL_HEIGHT` (Task 1).
- Produces: a 640x640 window and `PJ64_VIEWPORT_OFFSET=160` whenever the layout uses the pointer; the renderer's viewport at `(0, 160, 640, 480)`. Task 5's overlay reads that viewport.

- [ ] **Step 1: Update the self-test to the new coordinates**

In `Scripts/pointer_selftest.sh`, replace lines 2-8 with:

```sh
# Prove the pointer path end to end: the frontend publishes an injected pointer sample, the
# plugin latches the zone under it with Config/mouse/super_mario_64_usa.yaml loaded, and the
# N64 bits come out right. Three runs: a click in the game image must set A; a click in the
# middle of mid1 (192,512, a point that only exists when the window is 640 tall, so this
# also proves the taller window and the lifted viewport) must set Start; and with no
# PJ64_INPUT_YAML at all, a copy of the layout named after the ROM and sitting beside it
# must be found by the frontend's own lookup (the game click must set A again).
# Design: Docs/superpowers/specs/2026-09-15-mouse-panel-design.md and
# Docs/superpowers/specs/2026-09-15-per-game-input-yaml-design.md
```

Replace lines 22-24 with:

```sh
# $1 = inject spec, $2 = expected report tail, $3 = ROM path, $4 = PJ64_INPUT_YAML value,
# or "" to leave it unset. PJ64_FACE=0 keeps the camera prompt out of a test. The window
# is 640x640 with a mouse layout: game image 640x480, panel below it.
```

Replace lines 51-52 with:

```sh
one_run "320,240,1" "zone=13 a=1 start=0" "$ROM" "$YAML" || FAIL=1
one_run "192,512,1" "zone=8 a=0 start=1" "$ROM" "$YAML" || FAIL=1
```

Replace lines 54-56 with:

```sh
# Third run: symlink the ROM into a temp folder under its own name and extension, with the
# layout beside it as <base>.yaml. Without the lookup the keyboard mapping loads and a
# click in the game image cannot set A, so a=1 here proves the sibling file was used.
```

Replace line 62 with:

```sh
one_run "320,240,1" "zone=13 a=1 start=0" "$TMP/$NAME" "" || FAIL=1
```

Replace line 66 with:

```sh
    echo "ok: pointer path maps a game click to A and mid1 to Start, and finds a layout named after the ROM"
```

- [ ] **Step 2: Run the self-test to verify it fails**

Run: `make pointer-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: `FAIL: inject 192,512,1: got 'pointer-selftest zone=-1 a=0 start=0' ...` because the window is still 480 tall, so (192,512) is outside it.

- [ ] **Step 3: Teach the video plugin the offset**

`Source/Project64-video/Main.cpp` is CRLF. Run this from the repository root; it asserts the file is CRLF and the anchor is unique before writing:

```sh
python3 - <<'EOF'
p = 'Source/Project64-video/Main.cpp'
s = open(p, 'rb').read()
assert b'\r\n' in s, 'expected CRLF'
anchor = (b"        g_height = ev_fullscreen ? GetFullScreenResHeight(g_settings->FullScreenRes()) : GetScreenResHeight(g_settings->ScreenRes());\r\n"
          b"    }\r\n")
assert s.count(anchor) == 1, 'anchor not unique'
insert = (
    b"    // The macOS frontend lifts the game above its mouse panel by setting this to the\r\n"
    b"    // panel height (Source/Common/PointerLayout.h). The renderer adds g_viewport_offset\r\n"
    b"    // to every on-screen viewport, scissor and read-back, as it once did for the Windows\r\n"
    b"    // status bar; the render-to-texture paths draw offscreen and ignore it.\r\n"
    b"    const char * ViewportOffset = getenv(\"PJ64_VIEWPORT_OFFSET\");\r\n"
    b"    g_viewport_offset = (ViewportOffset != nullptr && atoi(ViewportOffset) > 0) ? atoi(ViewportOffset) : 0;\r\n"
)
s = s.replace(anchor, anchor + insert)
open(p, 'wb').write(s)
print('ok')
EOF
file Source/Project64-video/Main.cpp
```

Expected: `ok`, and `file` still reports CRLF. The insert lands inside the `#else` branch of `ChangeSize`, after the tile-size block and before `#endif`. `g_viewport_offset` is already declared `extern int` at line 50 and `getenv`/`atoi` are already used in this file.

- [ ] **Step 4: Size the window from the layout**

In `Source/Project64-sdl/main.cpp`:

After line 9 (`#include "GameConfig.h"`) add:

```cpp
#include "InputConfig.h"
```

After line 18 (`#include <Common/PointerState.h>`) add:

```cpp
#include <Common/PointerLayout.h>
```

After `FaceModeFromEnv` (line 165) add:

```cpp

// Whether the layout the plugin is about to load binds the pointer. The frontend reads the
// same file with the same code, quietly (the plugin reports a bad file), because the
// window and the video plugin's viewport must be sized before either exists. The default
// path is the frontend's own: the plugin's DefaultConfigPath resolves from its dylib.
static bool LayoutUsesPointer(const std::string & ExeDir)
{
    const char * Env = getenv("PJ64_INPUT_YAML");
    const std::string Path = (Env != nullptr && Env[0] != '\0') ? std::string(Env) : ExeDir + "/Config/input.yaml";
    InputConfig & Config = InputConfig::Get();
    return Config.Load(Path.c_str(), true) && Config.UsesPointer();
}
```

After the layout-lookup block (line 238, the closing brace after `fprintf(stderr, "input layout: %s\n", Layout);`) add:

```cpp

    // A layout that uses the pointer gets the panel: the window grows by its height and the
    // video plugin lifts the game by the same amount, so the game renders unscaled at the
    // top (Design: Docs/superpowers/specs/2026-09-15-mouse-panel-design.md). Tiles never
    // get one. The variable is cleared otherwise, so a value inherited from the caller
    // cannot lift a keyboard run.
    if (!TileMode && LayoutUsesPointer(ExecutableDirectory()))
    {
        TileRect.h += POINTER_PANEL_HEIGHT;
        char Offset[16];
        snprintf(Offset, sizeof(Offset), "%d", POINTER_PANEL_HEIGHT);
        setenv("PJ64_VIEWPORT_OFFSET", Offset, 1);
    }
    else
    {
        unsetenv("PJ64_VIEWPORT_OFFSET");
    }
```

- [ ] **Step 5: Read the frame dump from the viewport origin**

In `Source/Project64-sdl/SdlRenderWindow.cpp`, replace line 158

```cpp
    glReadPixels(0, 0, Width, Height, GL_RGB, GL_UNSIGNED_BYTE, &Pixels[0]);
```

with

```cpp
    // From the viewport's origin, not the window's: with the mouse panel the game sits
    // above an offset and the dump must stay the game alone.
    glReadPixels(Viewport[0], Viewport[1], Width, Height, GL_RGB, GL_UNSIGNED_BYTE, &Pixels[0]);
```

- [ ] **Step 6: Link the reader into the frontend**

In `Makefile`:

Line 293 becomes two lines:

```make
# InputConfig.cpp is shared with the input plugin: the frontend parses the layout to size its window.
FRONTEND_SRC = $(addprefix Project64-sdl/, main.cpp SdlNotification.cpp SdlRenderWindow.cpp GridHost.cpp FaceGestures.cpp Overlay.cpp GameConfig.cpp InputConfig.cpp)
```

Line 425, the frontend link, gains `$(YAML_LIBS)` after `$(SDL_LIBS)`:

```make
	$(CXX) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(YAML_LIBS) -framework OpenGL -framework Foundation -framework AVFoundation -framework Vision -framework CoreMedia -framework CoreVideo -lobjc -lpthread
```

Line 465 becomes:

```make
pointer-selftest: ## Prove the injected-pointer path maps a game click to A and mid1 to Start (usage: make pointer-selftest rom=/path/to/game.z64)
```

- [ ] **Step 7: Build, smoke, self-test**

Run: `make -j8 all && make test`
Expected: green. (`InputConfig.o` is one object in two link lines; its yaml-cpp and SDL flags already come from the `INPUT_OBJS` rules.)

Run: `make pointer-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: `ok: pointer path maps a game click to A and mid1 to Start, and finds a layout named after the ROM` (about 60 s; needs a window server).

- [ ] **Step 8: Prove the dump is still the game alone**

The ROM's name matches the installed Mario layout, so this run opens the 640x640 window with the offset in force:

```sh
PJ64_FACE=0 PJ64_FRAME_DUMP=/tmp/frame.ppm PJ64_FRAME_DUMP_AT=400 \
  perl -e 'alarm 25; exec @ARGV' -- ./Bin/macOS/Project64 /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>&1 | grep -E 'input layout|Wrote frame' || true
python3 - <<'EOF'
data = open('/tmp/frame.ppm', 'rb').read()
magic, size, depth, px = data.split(b'\n', 3)
w, h = map(int, size.split())
sampled = range(0, w * h * 3, 3 * 8)
nonblack = sum(1 for i in sampled if max(px[i:i + 3]) > 16)
print(w, h, round(100.0 * nonblack / len(sampled), 1))
EOF
```

Expected: `input layout: .../Config/mouse/super_mario_64_usa.yaml` on stderr, then `640 480` and a non-black share of about 89, and never below 80. A share near 60 means the read-back is still at the window corner; a `640 640` means the viewport was not lifted.

- [ ] **Step 9: Commit**

```bash
git add Source/Project64-video/Main.cpp Source/Project64-sdl/main.cpp Source/Project64-sdl/SdlRenderWindow.cpp Makefile Scripts/pointer_selftest.sh
git commit -m "Open a 640x640 window for mouse layouts and lift the game above the panel

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: The overlay paints the panel and the guide

**Files:**
- Modify: `Source/Project64-sdl/Overlay.h` (replace the whole file)
- Modify: `Source/Project64-sdl/Overlay.cpp` (replace the whole file)
- Modify: `Source/Project64-sdl/SdlRenderWindow.cpp:213-218`
- Modify: `Source/Project64-sdl/SdlRenderWindow.h:34-37`

**Interfaces:**
- Consumes: `PointerZoneRect`, `PointerStickRadius`, `POINTER_ZONE_GAME` (Task 1); `PointerState::Quadrant` (Task 1, written in Task 3); the renderer's viewport `(0, 160, 640, 480)` (Task 4).
- Produces: `void OverlayDraw(const PointerState * State, const int Viewport[4], bool GuideHidden);`

- [ ] **Step 1: Write the header**

Replace `Source/Project64-sdl/Overlay.h` with:

```cpp
// Project64 - A Nintendo 64 emulator
// The mouse overlay: the panel of slots below the game and the direction guide over it.
// Drawn by the render window on the emulation thread right before each present, with the
// GL 2.1 compatibility context current. Leaves every piece of GL state as it found it.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

struct PointerState;

// Viewport is the renderer's GL viewport (x, y, w, h) in drawable pixels: the game image
// is that rectangle and the window is (x + w) by (y + h), so the y rows below the game are
// the panel. GuideHidden skips the guide over the game (PJ64_OVERLAY=0); the panel is the
// controls and always draws.
void OverlayDraw(const PointerState * State, const int Viewport[4], bool GuideHidden);
```

- [ ] **Step 2: Write the overlay**

Replace `Source/Project64-sdl/Overlay.cpp` with:

```cpp
// Project64 - A Nintendo 64 emulator
// Fixed-function GL overlay for the mouse layout: the panel of slots below the game and
// the direction guide over it. See Overlay.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Overlay.h"
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <OpenGL/gl.h>
#include <math.h>
#include <string.h>

static const float kDim = 0.35f;       // resting line and label alpha
static const float kBright = 1.0f;     // held slot, lit arrow and active gesture alpha
static const float kWedge = 0.08f;     // fill of the lit quadrant
static const float kPanelGrey = 0.12f; // the panel's opaque ground
static const int kScale = 3;           // font pixel size; a glyph is 15x21 window pixels

// 5x7 glyphs, one byte per row, bit 4 is the left column. Only what the labels need.
struct Glyph { char C; unsigned char Rows[7]; };
static const Glyph kGlyphs[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 't', { 0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '^', { 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00 } },
    { 'v', { 0x00, 0x00, 0x00, 0x00, 0x11, 0x0A, 0x04 } },
    { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
    { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
};

static const Glyph * FindGlyph(char C)
{
    for (size_t i = 0; i < sizeof(kGlyphs) / sizeof(kGlyphs[0]); i++)
    {
        if (kGlyphs[i].C == C) return &kGlyphs[i];
    }
    return nullptr;
}

// Draws Text with its centre at (Cx, Cy).
static void DrawText(const char * Text, float Cx, float Cy, float Alpha)
{
    const int Len = (int)strlen(Text);
    if (Len == 0) return;
    const float GlyphW = 6.0f * kScale; // 5 columns plus one of spacing
    const float GlyphH = 7.0f * kScale;
    float X = Cx - (Len * GlyphW - kScale) / 2.0f;
    const float Y = Cy - GlyphH / 2.0f;
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(GL_QUADS);
    for (int i = 0; i < Len; i++, X += GlyphW)
    {
        const Glyph * G = FindGlyph(Text[i]);
        if (G == nullptr) continue;
        for (int Row = 0; Row < 7; Row++)
        {
            for (int Col = 0; Col < 5; Col++)
            {
                if ((G->Rows[Row] & (0x10 >> Col)) == 0) continue;
                const float Px = X + Col * kScale, Py = Y + Row * kScale;
                glVertex2f(Px, Py);
                glVertex2f(Px + kScale, Py);
                glVertex2f(Px + kScale, Py + kScale);
                glVertex2f(Px, Py + kScale);
            }
        }
    }
    glEnd();
}

static void DrawCircle(float Cx, float Cy, float R, float Alpha, bool Filled)
{
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(Filled ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
    if (Filled) glVertex2f(Cx, Cy);
    for (int i = 0; i <= 48; i++)
    {
        const float A = (float)i / 48.0f * 6.2831853f;
        glVertex2f(Cx + cosf(A) * R, Cy + sinf(A) * R);
    }
    glEnd();
}

static void DrawRect(float X0, float Y0, float X1, float Y1, float Alpha)
{
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(GL_LINE_LOOP);
    glVertex2f(X0 + 0.5f, Y0 + 0.5f);
    glVertex2f(X1 - 0.5f, Y0 + 0.5f);
    glVertex2f(X1 - 0.5f, Y1 - 0.5f);
    glVertex2f(X0 + 0.5f, Y1 - 0.5f);
    glEnd();
}

static void DrawLine(float X0, float Y0, float X1, float Y1, float Alpha)
{
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(GL_LINES);
    glVertex2f(X0, Y0);
    glVertex2f(X1, Y1);
    glEnd();
}

// The tracker mark with the three gesture labels to its right, bright while held.
static void DrawFaceStatus(const PointerState * State, float Cx, float Cy)
{
    const uint32_t Face = State->Face.load(std::memory_order_relaxed);
    if (Face == FACE_OFF) return;
    const float R = 6.0f;
    if (Face == FACE_TRACKING)
    {
        DrawCircle(Cx, Cy, R, kBright, true);
    }
    else
    {
        DrawCircle(Cx, Cy, R, kDim, false);
        if (Face == FACE_DENIED || Face == FACE_ERROR)
        {
            DrawLine(Cx - R, Cy - R, Cx + R, Cy + R, kDim);
            DrawLine(Cx - R, Cy + R, Cx + R, Cy - R, kDim);
        }
    }
    const uint32_t Bits = State->Gestures.load(std::memory_order_relaxed);
    const uint32_t kOrder[3] = { POINTER_GESTURE_EYEBROWS, POINTER_GESTURE_HEAD_LEFT, POINTER_GESTURE_HEAD_RIGHT };
    for (int i = 0; i < 3; i++)
    {
        if (State->GestureLabels[i][0] == '\0') continue;
        DrawText(State->GestureLabels[i], Cx + 28.0f + 36.0f * i, Cy, (Bits & kOrder[i]) != 0 ? kBright : kDim);
    }
}

// The panel: an opaque ground over the rows below the game, every slot with its label, the
// latched one bright, and the tracker under the middle slots.
static void DrawPanel(const PointerState * State, int W, int H, int GameH, int Latched)
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
        const float Alpha = Zone == Latched ? kBright : kDim;
        float X0, Y0, X1, Y1;
        PointerZoneRect(Zone, W, H, &X0, &Y0, &X1, &Y1);
        DrawRect(X0, Y0, X1, Y1, Alpha);
        DrawText(State->Labels[Zone], (X0 + X1) / 2.0f, (Y0 + Y1) / 2.0f, Alpha);
    }
    DrawFaceStatus(State, 178.0f, (float)H - 40.0f);
}

// The guide over the game: the lit quadrant's wedge, the four 45-degree rays, the ring and
// dead zone, the game zone's label, and an arrow per edge with the lit one bright.
static void DrawGuide(const PointerState * State, int W, int H, int GameH, int Latched, int Quadrant)
{
    const float R = PointerStickRadius(W, H);
    const float Cx = (float)W / 2.0f, Cy = (float)GameH / 2.0f;
    const float Gw = (float)W, Gh = (float)GameH;
    const float Lx = Cx - Cy, Rx = Cx + Cy; // where the 45-degree rays meet the top and bottom edges
    if (Quadrant >= 0)
    {
        glColor4f(1.0f, 1.0f, 1.0f, kWedge);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(Cx, Cy);
        switch (Quadrant)
        {
        case 0: glVertex2f(Lx, 0.0f); glVertex2f(Rx, 0.0f); break;
        case 1: glVertex2f(Rx, 0.0f); glVertex2f(Gw, 0.0f); glVertex2f(Gw, Gh); glVertex2f(Rx, Gh); break;
        case 2: glVertex2f(Rx, Gh); glVertex2f(Lx, Gh); break;
        default: glVertex2f(Lx, Gh); glVertex2f(0.0f, Gh); glVertex2f(0.0f, 0.0f); glVertex2f(Lx, 0.0f); break;
        }
        glEnd();
    }
    DrawLine(Cx, Cy, Lx, 0.0f, kDim);
    DrawLine(Cx, Cy, Rx, 0.0f, kDim);
    DrawLine(Cx, Cy, Lx, Gh, kDim);
    DrawLine(Cx, Cy, Rx, Gh, kDim);
    const float GameAlpha = Latched == POINTER_ZONE_GAME ? kBright : kDim;
    DrawCircle(Cx, Cy, R, GameAlpha, false);
    DrawCircle(Cx, Cy, R * 0.1f, GameAlpha, false);
    DrawText(State->Labels[POINTER_ZONE_GAME], Cx, Cy - R + 7.0f * kScale, GameAlpha); // just inside the ring's top
    DrawText("^", Cx, 24.0f, Quadrant == 0 ? kBright : kDim);
    DrawText(">", Gw - 24.0f, Cy, Quadrant == 1 ? kBright : kDim);
    DrawText("v", Cx, Gh - 24.0f, Quadrant == 2 ? kBright : kDim);
    DrawText("<", 24.0f, Cy, Quadrant == 3 ? kBright : kDim);
}

void OverlayDraw(const PointerState * State, const int Viewport[4], bool GuideHidden)
{
    if (State == nullptr || Viewport == nullptr) return;
    // The renderer's viewport is the game image; the window is its union with the rows
    // below it, which the frontend reserved for the panel (Design: Docs/superpowers/specs/
    // 2026-09-15-mouse-panel-design.md). With no offset there is no panel to draw.
    const int GameH = Viewport[3];
    const int W = Viewport[0] + Viewport[2];
    const int H = Viewport[1] + GameH;
    if (W <= 0 || GameH <= 0) return;

    // Save what glPushAttrib does not cover: the bound program and the matrices.
    GLint Program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)W, (double)H, 0.0, -1.0, 1.0); // y down, like the cursor
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glUseProgram(0);
    glViewport(0, 0, W, H);
    glDisable(GL_DEPTH_TEST);
    // The video plugin leaves GL_TEXTURE_2D enabled on texture units 0-2 (see
    // OGLcombiner.cpp's init_combiner/gfxStippleMode) and never disables them; glDisable
    // is per-unit, so disable every unit here or the overlay gets multitextured through a
    // stale game texture and can vanish. The glPushAttrib(GL_ALL_ATTRIB_BITS) above
    // restores both the per-unit enables and the active-unit selector, so this cannot leak.
    for (int Unit = 3; Unit >= 0; Unit--)
    {
        glActiveTexture(GL_TEXTURE0 + Unit);
        glDisable(GL_TEXTURE_2D);
    }                                   // leaves unit 0 active, matching prior behavior
    glDisable(GL_FOG);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);

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

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glUseProgram((GLuint)Program);
}
```

- [ ] **Step 3: Pass the viewport and the guide flag**

In `Source/Project64-sdl/SdlRenderWindow.cpp`, replace lines 213-218

```cpp
    if (m_Pointer != nullptr && !m_OverlayHidden && m_Pointer->OverlayWanted.load(std::memory_order_relaxed) != 0)
    {
        GLint Viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, Viewport);
        OverlayDraw(m_Pointer, (int)Viewport[2], (int)Viewport[3]);
    }
```

with

```cpp
    if (m_Pointer != nullptr && m_Pointer->OverlayWanted.load(std::memory_order_relaxed) != 0)
    {
        GLint Viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, Viewport);
        const int V[4] = { (int)Viewport[0], (int)Viewport[1], (int)Viewport[2], (int)Viewport[3] };
        OverlayDraw(m_Pointer, V, m_OverlayHidden);
    }
```

In `Source/Project64-sdl/SdlRenderWindow.h`, replace lines 34-37

```cpp
    // The overlay's data, owned by main(); null under a failed shm setup. PJ64_OVERLAY=0
    // hides the overlay for a player who has memorised the layout.
    const PointerState * m_Pointer;
    bool m_OverlayHidden;
```

with

```cpp
    // The overlay's data, owned by main(); null under a failed shm setup. PJ64_OVERLAY=0
    // hides the guide over the game for a player who has memorised it; the panel is the
    // controls and always draws.
    const PointerState * m_Pointer;
    bool m_OverlayHidden;
```

- [ ] **Step 4: Build and re-run the proofs**

Run: `make -j8 all && make test && make pointer-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: green, and the self-test's `ok:` line. Re-run Task 4 Step 8's dump check: still `640 480` at about 89, proving the overlay draws after the dump and outside it.

- [ ] **Step 5: Ask the human partner to look**

The dump excludes the overlay by design, so this is the one manual check in the plan. Ask them to run:

```sh
make run rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 face=0
```

and confirm: the game fills the top of a taller window; a dark panel below shows a cross of four cells on the left labelled D^ Dv D< D>, a cross on the right labelled C^ Cv C< C>, and St and L in the first two middle slots; four faint rays, a ring and a small circle over the game with an A near the ring's top; the arrow at the edge the cursor's tilt points to is bright and its wedge faintly filled; holding the click on a slot makes it bright; nothing smears into the panel while the game moves. Record what they report in the task report.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-sdl/Overlay.h Source/Project64-sdl/Overlay.cpp Source/Project64-sdl/SdlRenderWindow.cpp Source/Project64-sdl/SdlRenderWindow.h
git commit -m "Paint the mouse panel below the game and the direction guide over it

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Documentation

**Files:**
- Modify: `README.md:71-114` and `:118-122`
- Modify: `AGENTS.md:105-116` and `:167-172`
- Modify: `Docs/superpowers/specs/2026-09-15-mouse-and-face-input-design.md:4`

**Interfaces:**
- Consumes: everything above, as documented behaviour.
- Produces: nothing for code.

- [ ] **Step 1: Rewrite the README's mouse section**

Replace lines 71-114 of `README.md` (from `## Playing with a mouse` through the `make grid-selftest` sentence) with:

````markdown
## Playing with a mouse

A one-button mouse drives the stick and up to thirteen of the fourteen N64 buttons; the
shipped layouts put three of them on face gestures instead. Pick a layout under
`Config/mouse/` and run with it:

```sh
make run rom=Roms/sm64.z64 input=Config/mouse/super_mario_64_usa.yaml          # camera starts
make run rom=Roms/sm64.z64 input=Config/mouse/super_mario_64_usa.yaml face=0   # mouse only
```

With a mouse layout the window is 640x640: the game, unscaled, in the top 640x480 and a
panel of buttons below it. The game image is the stick. The cursor's distance from the
centre is the tilt, full at 160 px with a 16 px dead zone, and four faint lines split the
image into direction quadrants so the one your tilt points into lights up. A click
anywhere in the game image is one button (A in the Mario layout), and the button under
the cursor when you press stays pressed until you release, so "click A, then tilt" is a
running jump. The panel is a cross of four cells on the left, a cross on the right and
five slots between them: hover and click, hold to hold. A quick flick from the game down
to a button keeps the stick where it was until the cursor lands, so reaching for a button
never reads as a backward tilt; `PJ64_POINTER_FLICK=<px>` tunes that (default 24, `0`
disables it). `PJ64_OVERLAY=0` hides the lines over the game; the panel always draws.

When the layout binds a face gesture the webcam starts by itself (`face=1` or `--face`
forces it, `face=0` or `PJ64_FACE=0` keeps it off) and adds three held buttons: raising
both eyebrows, turning your head left, and turning it right. macOS asks for camera
permission once, attributed to the terminal or IDE you launched from; a past denial is
fixed in System Settings > Privacy & Security > Camera. Frames stay in memory and are
never saved, shown, or logged.

`PJ64_FACE_DEBUG=1` prints the two measures once a second, and `PJ64_FACE_BROW` /
`PJ64_FACE_YAW` override the thresholds (defaults 0.035 and 0.25). A dot in the panel,
under the middle slots, shows the tracker: hollow while looking for a face, filled while
tracking, crossed when the camera is unavailable; the three gesture labels beside it light
up while held.

Layout files use two more binding forms, `{zone: <name>}` and
`{face: eyebrows|head-left|head-right}`, plus `{stick: pointer}` for `Stick`. The zones are
`game` (the game image), `pad-up`, `pad-down`, `pad-left`, `pad-right` (the left cross),
`c-up`, `c-down`, `c-left`, `c-right` (the right cross) and `mid1` to `mid5`; any control
can take any slot. Shipped: `super_mario_64_usa.yaml`, `goldeneye_007_u.yaml`,
`mario_kart_64_u.yaml`. Everything without the camera keeps working when the camera is
denied or absent, though the buttons mapped only to face gestures are unavailable.

Name a layout after the ROM and it loads without `input=`: `Roms/sm64.yaml` beside
`Roms/sm64.z64`, or `Config/mouse/sm64.yaml` under the binary — the shipped layouts are
named after their ROM's own base name (`super_mario_64_usa.yaml` and so on). The first
that exists wins, the frontend prints `input layout: <path>` when it picks one, and an
explicit `input=` beats both. Grid tiles ignore per-game files. `make all` refreshes the
shipped layouts beside the binary on every build, so keep a layout of your own beside the
ROM rather than editing the installed copy.

`make pointer-selftest rom=Roms/a.z64` proves the mouse path end to end, the same way
`make grid-selftest` proves the grid's key broadcast.
````

Then replace lines 118-122 (the first paragraph of "What works") with:

```markdown
Super Mario 64 renders, plays audio and runs at full speed, with keyboard input through
SDL3; a gamepad works after swapping in the commented block in `Config/input.yaml`, and a
one-button mouse with optional face gestures works with a layout from `Config/mouse/` (see
"Playing with a mouse"). The window is 640x480, or 640x640 with a mouse layout, and the
mouse cursor is never captured.
```

- [ ] **Step 2: Update AGENTS.md**

Replace lines 105-116 (the paragraph starting `**Mouse and face input go through one shared struct.**`) with:

```markdown
**Mouse and face input go through one shared struct.** `Source/Common/PointerState.h` is
a seqlock over `shm_open`, created by the frontend and passed to the input plugin by
descriptor in `PJ64_POINTER_FD`, the same way the grid passes keys. The frontend parses
the layout itself before the window exists (`InputConfig.cpp` is compiled into the
frontend and the input dylib, each with its own singleton): a layout that uses the pointer
makes the window 640x640 and sets `PJ64_VIEWPORT_OFFSET=160`, which the video plugin's
`ChangeSize` stores in the renderer's dormant status-bar offset `g_viewport_offset`, so the
game renders in the top 640x480 and the panel owns the rows below. The frontend's main
loop samples the mouse (SDL3's mouse state functions are main-thread only) and publishes
it; `Source/Project64-sdl/FaceTracker.mm` runs AVFoundation and Vision on a private queue
and writes three gesture bits through `FaceGestures.{h,cpp}`; the plugin evaluates slots,
the flick gate, gestures and the pointer stick in `GetKeys` with the pure geometry in
`Source/Common/PointerLayout.h`, and writes labels, the latched zone and the lit quadrant
back for `Overlay.cpp`, which reads the GL viewport to find the game rectangle and paints
the panel below it in `CSdlRenderWindow::SwapWindow` before the flush, and `FaceWanted`,
which the frontend's main loop polls to start the camera when `PJ64_FACE` is unset (`0`
never, anything else at once). Layouts are the `{zone:}`, `{face:}` and `{stick: pointer}`
YAML forms in `Config/mouse/`.
```

Replace lines 167-172 (the trap starting `- **A YAML beside a ROM silently changes that game's bindings.**`) with these three traps:

```markdown
- **A YAML beside a ROM silently changes that game's bindings.** `<rom>.yaml` next to
  `<rom>.z64`, or `Config/mouse/<rom>.yaml`, is loaded instead of `input.yaml`, the window
  becomes 640x640 with the panel, and the camera starts if it binds a gesture. The stderr
  line `input layout: <path>` is the tell; `PJ64_INPUT_YAML` and `PJ64_FACE=0` override it.
  Any unattended launcher over a ROM folder must set `PJ64_FACE=0` itself, the way
  `Scripts/run_rom_pack.py` does — otherwise a matching layout can open the camera with
  nobody watching.
- **The frontend owns `PJ64_VIEWPORT_OFFSET`.** It sets the variable to the panel height,
  or clears it, before the plugins load, from the layout it parsed itself. Never set it by
  hand: a value the video plugin honours without the taller window pushes the game off the
  top, and a stale video dylib that ignores it shows the game at the bottom of a tall
  window with no panel, since the overlay draws the panel wherever the viewport is not.
- **Installed mouse layouts are refreshed by `make config`.** The files under
  `Bin/macOS/Config/mouse/` are copies of the tracked examples and are overwritten on every
  build; a player's own layout goes beside the ROM as `<rom>.yaml`. `Config/input.yaml` is
  still copied once.
```

- [ ] **Step 3: Point the old spec at the new one**

In `Docs/superpowers/specs/2026-09-15-mouse-and-face-input-design.md`, after line 4 (`Status: approved (design), implementation not started`) insert:

```markdown
Superseded in part: Part 2 (the 4x4 grid) and Part 4 (the overlay) by
`2026-09-15-mouse-panel-design.md`; the rest stands.
```

- [ ] **Step 4: Check the prose wrap and the whole suite**

Run:

```sh
awk 'length > 90 && !/^```/ && !/^\|/ {print FILENAME":"NR": "length}' README.md AGENTS.md
```

Expected: only lines inside code blocks (the `make run` examples and the AGENTS Commands block, which already run to 103). Reflow any prose line it names.

Run: `make -j8 all && make test && make input-config-test && make pointer-layout-test && make face-gesture-test && make game-config-test`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add README.md AGENTS.md Docs/superpowers/specs/2026-09-15-mouse-and-face-input-design.md
git commit -m "Document the mouse panel, the quadrant guide and the flick gate

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Self-review against the spec

- **Part 1 geometry:** Task 1 (constants, table, rectangles, stick, quadrant, gate; every number in the spec's table appears in the test).
- **Part 2 frontend, video plugin, headers, plugin, overlay, reader:** Tasks 4, 4, 1, 3, 5, 2 respectively. The frame-dump origin is Task 4 Step 5.
- **Part 3 layouts and install policy:** Task 2.
- **Part 4 errors:** the quiet load (Task 2) and `unsetenv` (Task 4) implement the degraded cases; nothing else needed code.
- **Part 5 build and tests:** Makefile in Tasks 2 and 4; the five tests map to Task 1 (layout), Task 2 (parser), Task 4 (self-test and dump), Task 5 (manual look), Task 6 (whole suite). `make rom-test` is unchanged and slow; the dump check in Task 4 Step 8 is the same measurement on one ROM.
- **Part 6 docs:** Task 6.
- **Type consistency:** `OverlayDraw(const PointerState *, const int Viewport[4], bool)` in Task 5's header, source and caller; `Load(const char *, bool Quiet = false)` in Task 2's header and source and Task 4's caller; `PointerGateStick(PointerGate *, PointerEval *, float, float, float)` in Task 1's header and test and Task 3's caller; zone indices 8 (`mid1`) and 13 (`game`) in Tasks 1, 2 and 4.
