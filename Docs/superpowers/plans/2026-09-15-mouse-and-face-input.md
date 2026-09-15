# Mouse-and-face input implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a player with a one-button mouse, optionally plus three face gestures from the webcam, drive every N64 control in this port.

**Architecture:** The frontend samples the mouse on its main thread and publishes it into a shared `PointerState` struct (a seqlock over `shm_open`, like the grid's `GridKeys`); a face tracker in the frontend writes three gesture bits into the same struct; the input plugin evaluates zones, gestures and a pointer stick from it in `GetKeys` using a pure geometry function shared with an overlay drawn in the render window before each present. Bindings are three new YAML forms in the existing `InputConfig`.

**Tech Stack:** C++14, Objective-C++ (ARC) for AVFoundation and Vision, SDL3, yaml-cpp, fixed-function OpenGL 2.1, GNU make.

**Spec:** `Docs/superpowers/specs/2026-09-15-mouse-and-face-input-design.md`

## Global Constraints

- Build with the hand-written `Makefile` only; `make -j8 all`, `make test`, `make input-config-test` must stay green after every task.
- No change to the core, the video plugin, the plugin ABI, or `GridKeys`. Grid mode stays keyboard-broadcast.
- The input plugin links SDL3 and yaml-cpp only; it never links a frontend object or Apple frameworks.
- `Config/input.yaml` stays keyboard-active. Mouse layouts are separate files under `Config/mouse/`.
- SDL3 mouse state functions are main-thread only; only `main.cpp`'s loop calls them.
- `GetKeys` runs on the emulation thread every frame: no I/O, no locks, no allocation there.
- The one-binding-per-control rule and all-or-nothing YAML load are unchanged; error lines keep the format `input: <path>:<line>:<col>: <message>; using built-in defaults`.
- Stick constants: `N64_AXIS_MAX` 80, dead zone `0.1 R`, `R = min(w, h) / 4`, Y inverted (screen down is stick down).
- Gesture defaults: brow threshold 0.035, yaw threshold 0.25 rad, release at 60 % of threshold, baseline EMA time constant 5 s frozen while active, debounce 2 frames, no-face timeout 0.5 s.
- Frames from the camera are never written anywhere, never previewed, and landmark values are never logged unless `PJ64_FACE_DEBUG=1`.
- Every file this plan creates is LF, ASCII, and starts with the three-line Project64 header comment used by `Source/Project64-sdl/PluginInput.cpp`. Every file it edits is already LF (checked with `file`).
- Commit messages are one short imperative sentence, as in `git log`, and end with the attribution line `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.

---

## File structure

| File | Responsibility |
|---|---|
| `Source/Common/PointerState.h` (new, header-only) | The shared struct, its seqlock publish/snapshot, the environment variable name, gesture bits, face status, gesture name lookup. |
| `Source/Common/PointerLayout.h` (new, header-only) | Pure geometry: zone names, `PointerLayoutEvaluate`, `PointerZoneRect`, `PointerStickRadius`. No SDL, no I/O. |
| `Source/Project64-sdl/PointerLayoutTest.cpp` (new) | Unit tests for the geometry. |
| `Source/Project64-sdl/InputConfig.{h,cpp}` (edit) | Three new binding kinds, their YAML forms, control labels, `UsesPointer`, `PointerLabels`. |
| `Source/Project64-sdl/InputConfigTest.cpp` (edit) | Cases for the new forms and rejections. |
| `Source/Project64-sdl/PluginInput.cpp` (edit) | Map the shared struct, latch clicks, evaluate zone/face/pointer bindings, publish labels and the latched zone, self-test report. |
| `Source/Project64-sdl/FaceGestures.{h,cpp}` (new) | `GestureClassifier`: baseline, hysteresis, debounce, no-face timeout. Pure C++. |
| `Source/Project64-sdl/FaceGesturesTest.cpp` (new) | Unit tests for the classifier. |
| `Source/Project64-sdl/FaceTracker.{h,mm}` (new) | AVFoundation capture, Vision landmarks, measures, feeds the classifier, writes atomics. |
| `Source/Project64-sdl/Overlay.{h,cpp}` (new) | Fixed-function GL overlay and the 5×7 font. |
| `Source/Project64-sdl/SdlRenderWindow.{h,cpp}` (edit) | Hold the struct pointer, call the overlay before the flush. |
| `Source/Project64-sdl/main.cpp` (edit) | Create the struct, sample the mouse each loop, `PJ64_POINTER_INJECT`, `--face`, tracker start/stop. |
| `Config/mouse/sm64.yaml`, `goldeneye.yaml`, `mk64.yaml` (new) | Shipped layouts. |
| `Scripts/pointer_selftest.sh` (new) | End-to-end proof with an injected pointer. |
| `Makefile` (edit) | New sources, `.mm` rule, frameworks, test targets, `run` arguments, config install. |
| `README.md`, `AGENTS.md` (edit) | Player docs and agent guidance. |

---

### Task 1: Shared pointer struct and pure geometry

**Files:**
- Create: `Source/Common/PointerState.h`
- Create: `Source/Common/PointerLayout.h`
- Create: `Source/Project64-sdl/PointerLayoutTest.cpp`
- Modify: `Makefile` (`.PHONY` line 342, test targets after `input-config-test` line 460-462)

**Interfaces:**
- Consumes: `Source/Common/GridKeys.h` as the seqlock pattern to copy.
- Produces:
  - `#define PJ64_POINTER_ENV "PJ64_POINTER_FD"`
  - `enum PointerGesture : uint32_t { POINTER_GESTURE_EYEBROWS = 1, POINTER_GESTURE_HEAD_LEFT = 2, POINTER_GESTURE_HEAD_RIGHT = 4 }`
  - `enum FaceStatus : uint32_t { FACE_OFF, FACE_STARTING, FACE_TRACKING, FACE_NO_FACE, FACE_DENIED, FACE_ERROR }`
  - `struct PointerSample { float X, Y; int32_t W, H; bool Inside; bool Button; }`
  - `struct PointerState` with `Seq`, `Sample`, `Gestures`, `Face`, `OverlayWanted`, `Labels[13][3]`, `GestureLabels[3][3]`, `LatchedZone`
  - `void PointerPublish(PointerState *, const PointerSample &)`, `void PointerSnapshot(const PointerState *, PointerSample *)`
  - `uint32_t PointerGestureFromName(const char *)` (0 on miss), `int PointerGestureIndex(uint32_t bit)` (0..2)
  - `#define POINTER_ZONE_COUNT 13`, `#define POINTER_LABEL_SIZE 3`
  - `enum { POINTER_ZONE_NONE = -1, POINTER_ZONE_CENTRE = 12 }`
  - `const char * PointerZoneName(int)`, `int PointerZoneFromName(const char *)`
  - `struct PointerEval { int Zone; int8_t StickX, StickY; }`
  - `PointerEval PointerLayoutEvaluate(float X, float Y, int W, int H, bool Inside)`
  - `float PointerStickRadius(int W, int H)`
  - `void PointerZoneRect(int Zone, int W, int H, float * X0, float * Y0, float * X1, float * Y1)`

- [ ] **Step 1: Write the failing geometry test**

Create `Source/Project64-sdl/PointerLayoutTest.cpp`:

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

int main()
{
    // A 640x480 window: cells are 160x120, centre (320,240), R = 120.
    const int W = 640, H = 480;
    CHECK(PointerStickRadius(W, H) == 120.0f);

    // Zone names round-trip and cover the whole table.
    CHECK(strcmp(PointerZoneName(0), "top1") == 0);
    CHECK(strcmp(PointerZoneName(3), "top4") == 0);
    CHECK(strcmp(PointerZoneName(4), "left2") == 0);
    CHECK(strcmp(PointerZoneName(5), "right2") == 0);
    CHECK(strcmp(PointerZoneName(6), "left3") == 0);
    CHECK(strcmp(PointerZoneName(7), "right3") == 0);
    CHECK(strcmp(PointerZoneName(8), "bottom1") == 0);
    CHECK(strcmp(PointerZoneName(11), "bottom4") == 0);
    CHECK(strcmp(PointerZoneName(POINTER_ZONE_CENTRE), "centre") == 0);
    for (int i = 0; i < POINTER_ZONE_COUNT; i++)
    {
        CHECK(PointerZoneFromName(PointerZoneName(i)) == i);
    }
    CHECK(PointerZoneFromName("middle") == POINTER_ZONE_NONE);
    CHECK(PointerZoneFromName("") == POINTER_ZONE_NONE);

    // Cell lookup at corners and edges.
    CHECK(PointerLayoutEvaluate(0, 0, W, H, true).Zone == 0);          // top1
    CHECK(PointerLayoutEvaluate(639, 0, W, H, true).Zone == 3);        // top4
    CHECK(PointerLayoutEvaluate(100, 200, W, H, true).Zone == 4);      // left2
    CHECK(PointerLayoutEvaluate(600, 200, W, H, true).Zone == 5);      // right2
    CHECK(PointerLayoutEvaluate(100, 300, W, H, true).Zone == 6);      // left3
    CHECK(PointerLayoutEvaluate(600, 300, W, H, true).Zone == 7);      // right3
    CHECK(PointerLayoutEvaluate(0, 479, W, H, true).Zone == 8);        // bottom1
    CHECK(PointerLayoutEvaluate(639, 479, W, H, true).Zone == 11);     // bottom4
    CHECK(PointerLayoutEvaluate(320, 240, W, H, true).Zone == POINTER_ZONE_CENTRE);
    CHECK(PointerLayoutEvaluate(160, 120, W, H, true).Zone == POINTER_ZONE_CENTRE);  // inner block's top-left pixel
    CHECK(PointerLayoutEvaluate(159, 120, W, H, true).Zone == 4);      // one pixel left of it is left2

    // Stick: centre is neutral; the dead zone is 0.1 R = 12 px.
    PointerEval E = PointerLayoutEvaluate(320, 240, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(320 + 11, 240, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    // At the ring edge the tilt is full.
    E = PointerLayoutEvaluate(320 + 120, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_CENTRE && E.StickX == 80 && E.StickY == 0);
    // Half way is half tilt.
    E = PointerLayoutEvaluate(320 + 60, 240, W, H, true);
    CHECK(E.StickX == 40 && E.StickY == 0);
    // Beyond the ring but still in the inner block clamps to full tilt.
    E = PointerLayoutEvaluate(320 + 150, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_CENTRE && E.StickX == 80 && E.StickY == 0);
    // Screen up is stick up (positive N64 Y).
    E = PointerLayoutEvaluate(320, 240 - 119, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_CENTRE && E.StickX == 0 && E.StickY > 78);
    E = PointerLayoutEvaluate(320, 240 + 119, W, H, true);
    CHECK(E.StickY < -78);
    // A diagonal beyond the ring is clamped to unit length, not per axis.
    E = PointerLayoutEvaluate(320 + 120, 240 - 119, W, H, true);
    CHECK(E.StickX > 50 && E.StickX < 62 && E.StickY > 50 && E.StickY < 62);

    // In an outer cell the stick is neutral.
    E = PointerLayoutEvaluate(600, 300, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    // Outside the window or unfocused: no zone, neutral stick.
    E = PointerLayoutEvaluate(320, 240, W, H, false);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(-5, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE);
    E = PointerLayoutEvaluate(320, 480, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE);
    // A degenerate window is safe.
    E = PointerLayoutEvaluate(0, 0, 0, 0, true);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0);

    // Cell rectangles tile the window.
    float X0, Y0, X1, Y1;
    PointerZoneRect(0, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 0 && Y0 == 0 && X1 == 160 && Y1 == 120);
    PointerZoneRect(11, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 480 && Y0 == 360 && X1 == 640 && Y1 == 480);
    PointerZoneRect(POINTER_ZONE_CENTRE, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 160 && Y0 == 120 && X1 == 480 && Y1 == 360);

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
    PointerSample In = { 12.5f, 34.0f, 640, 480, true, true };
    PointerPublish(&State, In);
    PointerSample Out;
    PointerSnapshot(&State, &Out);
    CHECK(Out.X == 12.5f && Out.Y == 34.0f && Out.W == 640 && Out.H == 480 && Out.Inside && Out.Button);
    CHECK(State.Seq == 2);

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: pointer layout\n");
    return 0;
}
```

- [ ] **Step 2: Add the make target and run it to see it fail**

In `Makefile`, add `pointer-layout-test` to the `.PHONY` line (line 342) and add after the `input-config-test` recipe (after line 462):

```make
pointer-layout-test: $(BUILD)/Project64-sdl/PointerLayoutTest.o ## Run the PointerLayout geometry tests
	$(CXX) $(LDFLAGS) -o $(BUILD)/pointer-layout-test $^
	@$(BUILD)/pointer-layout-test
```

Run: `make pointer-layout-test`
Expected: compile error, `'Common/PointerLayout.h' file not found`.

- [ ] **Step 3: Write `PointerState.h`**

Create `Source/Common/PointerState.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// Shared between the SDL frontend (mouse and face writers), the input plugin (evaluator)
// and the overlay (reader). One writer per field group; see the table in the design spec.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <atomic>
#include <stdint.h>
#include <string.h>

// Environment variable holding the descriptor number of a mapped PointerState. The
// frontend sets it before the plugins load; it is absent under any other frontend.
#define PJ64_POINTER_ENV "PJ64_POINTER_FD"

enum PointerGesture : uint32_t
{
    POINTER_GESTURE_EYEBROWS = 1u << 0,
    POINTER_GESTURE_HEAD_LEFT = 1u << 1,
    POINTER_GESTURE_HEAD_RIGHT = 1u << 2,
};

enum FaceStatus : uint32_t
{
    FACE_OFF = 0,
    FACE_STARTING,
    FACE_TRACKING,
    FACE_NO_FACE,
    FACE_DENIED,
    FACE_ERROR,
};

#define POINTER_ZONE_COUNT 13   // 12 outer cells + centre; see PointerLayout.h
#define POINTER_LABEL_SIZE 3    // up to two characters plus NUL
#define POINTER_GESTURE_COUNT 3

// What the frontend samples on its main thread. X,Y in window pixels from the top left;
// Inside is "cursor over this window and the window has mouse focus".
struct PointerSample
{
    float X, Y;
    int32_t W, H;
    bool Inside;
    bool Button;
};

struct PointerState
{
    // Frontend main loop -> plugin. Seq is a seqlock as in GridKeys.
    volatile uint32_t Seq;
    PointerSample Sample;
    // Face tracker -> plugin and overlay.
    std::atomic<uint32_t> Gestures;      // PointerGesture bits
    std::atomic<uint32_t> Face;          // FaceStatus
    // Plugin at load -> overlay. Written before the ROM opens; plain storage.
    std::atomic<uint32_t> OverlayWanted;
    char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE];
    char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE];
    // Plugin each GetKeys -> overlay.
    std::atomic<int32_t> LatchedZone;    // POINTER_ZONE_NONE when nothing is held
};

inline void PointerPublish(PointerState * State, const PointerSample & Sample)
{
    State->Seq = State->Seq + 1; // odd: a write is in flight
    std::atomic_thread_fence(std::memory_order_release);
    State->Sample = Sample;
    std::atomic_thread_fence(std::memory_order_release);
    State->Seq = State->Seq + 1; // even: stable
}

inline void PointerSnapshot(const PointerState * State, PointerSample * Out)
{
    for (;;)
    {
        uint32_t Before = State->Seq;
        if ((Before & 1u) != 0)
        {
            continue;
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        *Out = State->Sample;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (State->Seq == Before)
        {
            return;
        }
    }
}

inline uint32_t PointerGestureFromName(const char * Name)
{
    if (strcmp(Name, "eyebrows") == 0) return POINTER_GESTURE_EYEBROWS;
    if (strcmp(Name, "head-left") == 0) return POINTER_GESTURE_HEAD_LEFT;
    if (strcmp(Name, "head-right") == 0) return POINTER_GESTURE_HEAD_RIGHT;
    return 0;
}

// Index 0..2 of a single gesture bit, for the GestureLabels table.
inline int PointerGestureIndex(uint32_t Bit)
{
    if (Bit == POINTER_GESTURE_EYEBROWS) return 0;
    if (Bit == POINTER_GESTURE_HEAD_LEFT) return 1;
    return 2;
}
```

- [ ] **Step 4: Write `PointerLayout.h`**

Create `Source/Common/PointerLayout.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// The 4x4 pointer grid, shared by the input plugin (evaluation) and the overlay (drawing)
// so the two can never disagree about where a cell is. Pure functions, no SDL.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

enum
{
    POINTER_ZONE_NONE = -1,
    POINTER_ZONE_CENTRE = 12,
};

// Zone order: top row, then the two side pairs, then the bottom row, then centre.
// Indices 0-11 are the outer cells; 12 is the inner 2x2 block.
inline const char * PointerZoneName(int Zone)
{
    static const char * const kNames[13] = {
        "top1", "top2", "top3", "top4",
        "left2", "right2", "left3", "right3",
        "bottom1", "bottom2", "bottom3", "bottom4",
        "centre",
    };
    return (Zone >= 0 && Zone < 13) ? kNames[Zone] : "";
}

inline int PointerZoneFromName(const char * Name)
{
    for (int i = 0; i < 13; i++)
    {
        if (strcmp(Name, PointerZoneName(i)) == 0) return i;
    }
    return POINTER_ZONE_NONE;
}

inline float PointerStickRadius(int W, int H)
{
    return (float)(W < H ? W : H) / 4.0f;
}

// Cell (Col,Row) of the 4x4 grid -> zone index.
inline int PointerZoneFromCell(int Col, int Row)
{
    if (Row == 0) return Col;                         // top1..top4
    if (Row == 3) return 8 + Col;                     // bottom1..bottom4
    if (Col == 0) return Row == 1 ? 4 : 6;            // left2, left3
    if (Col == 3) return Row == 1 ? 5 : 7;            // right2, right3
    return POINTER_ZONE_CENTRE;
}

// Pixel rectangle of a zone, top-left origin, for the overlay.
inline void PointerZoneRect(int Zone, int W, int H, float * X0, float * Y0, float * X1, float * Y1)
{
    const float CellW = (float)W / 4.0f, CellH = (float)H / 4.0f;
    int Col = 0, Row = 0, Cols = 1, Rows = 1;
    if (Zone == POINTER_ZONE_CENTRE) { Col = 1; Row = 1; Cols = 2; Rows = 2; }
    else if (Zone >= 0 && Zone < 4) { Col = Zone; Row = 0; }
    else if (Zone >= 8 && Zone < 12) { Col = Zone - 8; Row = 3; }
    else if (Zone == 4) { Col = 0; Row = 1; }
    else if (Zone == 5) { Col = 3; Row = 1; }
    else if (Zone == 6) { Col = 0; Row = 2; }
    else if (Zone == 7) { Col = 3; Row = 2; }
    *X0 = Col * CellW;
    *Y0 = Row * CellH;
    *X1 = (Col + Cols) * CellW;
    *Y1 = (Row + Rows) * CellH;
}

struct PointerEval
{
    int Zone;
    int8_t StickX, StickY;
};

// X,Y in window pixels from the top left. Inside false means "not over this window",
// which reads as no zone and a neutral stick.
inline PointerEval PointerLayoutEvaluate(float X, float Y, int W, int H, bool Inside)
{
    PointerEval E = { POINTER_ZONE_NONE, 0, 0 };
    if (!Inside || W <= 0 || H <= 0 || X < 0 || Y < 0 || X >= (float)W || Y >= (float)H)
    {
        return E;
    }
    const int Col = (int)(X / ((float)W / 4.0f));
    const int Row = (int)(Y / ((float)H / 4.0f));
    E.Zone = PointerZoneFromCell(Col > 3 ? 3 : Col, Row > 3 ? 3 : Row);
    if (E.Zone != POINTER_ZONE_CENTRE)
    {
        return E;
    }
    const float R = PointerStickRadius(W, H);
    float Nx = (X - (float)W / 2.0f) / R;
    float Ny = (Y - (float)H / 2.0f) / R;
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
```

- [ ] **Step 5: Run the test and make it pass**

Run: `make pointer-layout-test`
Expected: `ok: pointer layout`. If a `CHECK` on a boundary fails, fix the geometry, not the test; the numbers above are the spec's.

- [ ] **Step 6: Confirm the rest still builds**

Run: `make -j8 all && make test`
Expected: unchanged output; the new headers are not yet included anywhere else.

- [ ] **Step 7: Commit**

```bash
git add Source/Common/PointerState.h Source/Common/PointerLayout.h Source/Project64-sdl/PointerLayoutTest.cpp Makefile
git commit -m "Add the shared pointer state and grid geometry

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Zone, face and pointer-stick bindings in the YAML

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.h` (the `Binding` struct and `InputConfig` class)
- Modify: `Source/Project64-sdl/InputConfig.cpp` (`IsFormKey`, `ParseBinding`, new label helpers)
- Modify: `Source/Project64-sdl/InputConfigTest.cpp`

**Interfaces:**
- Consumes: `PointerZoneFromName`, `PointerGestureFromName`, `PointerGestureIndex`, `POINTER_ZONE_COUNT`, `POINTER_LABEL_SIZE`, `POINTER_GESTURE_COUNT` from Task 1.
- Produces:
  - `Binding::Kind` gains `Zone` (code = zone index), `Face` (code = one `PointerGesture` bit), `Pointer` (Stick only, code unused).
  - `static const char * InputConfig::ControlLabel(N64Control)` returning `"A"`, `"B"`, `"Z"`, `"St"`, `"L"`, `"R"`, `"C^"`, `"Cv"`, `"C<"`, `"C>"`, `"D^"`, `"Dv"`, `"D<"`, `"D>"`, `""` for Stick.
  - `bool InputConfig::UsesPointer() const` — true if any binding is `Zone`, `Face` or `Pointer`.
  - `void InputConfig::PointerLabels(char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE], char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE]) const` — fills each slot with the label of the control bound to it, or `""`.

- [ ] **Step 1: Add the failing parser tests**

In `Source/Project64-sdl/InputConfigTest.cpp`, insert after the `DigitalStick` block (after the line `CHECK(C.Bindings(N64Control::Stick)[0].UpKey == SDL_SCANCODE_UP);`):

```cpp
    const char * Pointer =
        "bindings:\n"
        "  Stick: {stick: pointer}\n"
        "  A: {zone: centre}\n"
        "  Start: {zone: top4}\n"
        "  Z: {face: eyebrows}\n"
        "  B: {face: head-left}\n";
    CHECK(C.Load(WriteTemp(Pointer)));
    CHECK(C.UsesPointer());
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
```

And add these rejections to the block of `CHECK(!C.Load(...))` lines:

```cpp
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {zone: middle}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {face: wink}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {zone: centre}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {face: eyebrows}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {stick: pointer}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {zone: centre, face: eyebrows}\n")));
```

Also add near the end, after the `Config/input.yaml` checks:

```cpp
    CHECK(C.Load("Config/mouse/sm64.yaml"));          // every shipped mouse layout must parse
    CHECK(C.UsesPointer());
    CHECK(C.Load("Config/mouse/goldeneye.yaml"));
    CHECK(C.Load("Config/mouse/mk64.yaml"));
```

Add `#include <Common/PointerLayout.h>` and `#include <Common/PointerState.h>` at the top of the test.

- [ ] **Step 2: Run it to see it fail**

Run: `make input-config-test`
Expected: compile errors for `UsesPointer`, `Kind::Pointer`, `PointerLabels`, `ControlLabel`.

- [ ] **Step 3: Extend the header**

In `Source/Project64-sdl/InputConfig.h`, add after `#include <SDL3/SDL.h>`:

```cpp
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
```

Change the `Binding` struct's `Kind` and comment to:

```cpp
    enum class Kind { Key, Button, Axis, Stick, Keys, Zone, Face, Pointer };

    Kind kind;
    int code;        // Key: SDL_Scancode; Button: SDL_GamepadButton;
                     // Axis: SDL_GamepadAxis; Stick: X axis (Y is code + 1);
                     // Zone: zone index (PointerLayout.h); Face: one PointerGesture bit;
                     // Pointer: unused
```

Add to the public section of `InputConfig`, after `Bindings`:

```cpp
    // Two-character overlay label for a control ("St", "C^", ...); "" for Stick.
    static const char * ControlLabel(N64Control Control);

    // True when any binding is a zone, a face gesture or the pointer stick, which is
    // what turns the overlay on.
    bool UsesPointer() const;

    // Overlay labels: for each zone and each gesture, the label of the control bound to
    // it, or "" when nothing is.
    void PointerLabels(char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE],
                       char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE]) const;
```

- [ ] **Step 4: Implement the parser and helpers**

In `Source/Project64-sdl/InputConfig.cpp`:

Add three constructors after `MakeStickKeys`:

```cpp
static Binding MakeZone(int Zone)
{
    return Binding{ Binding::Kind::Zone, Zone, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeFace(uint32_t Gesture)
{
    return Binding{ Binding::Kind::Face, (int)Gesture, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakePointer()
{
    return Binding{ Binding::Kind::Pointer, 0, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}
```

Change `IsFormKey`:

```cpp
static bool IsFormKey(const std::string & Key)
{
    return Key == "key" || Key == "button" || Key == "axis" || Key == "stick" || Key == "keys"
        || Key == "zone" || Key == "face";
}
```

In `ParseBinding`, change `FormError` to:

```cpp
    static const std::string FormError = "value must be one of {key:}, {button:}, {axis:}, {stick:}, {keys:}, {zone:}, {face:}";
```

Change the `ForStick` guard to include the two new forms:

```cpp
    if ((Form == "key" || Form == "button" || Form == "axis" || Form == "zone" || Form == "face") && ForStick)
    {
        ConfigError(Path, Value[Form], Form + " cannot drive Stick; use {keys:}, {stick: left/right} or {stick: pointer}");
        return false;
    }
```

Insert before `if (Form == "stick")`:

```cpp
    if (Form == "zone")
    {
        const std::string Name = Value[Form].as<std::string>();
        const int Zone = PointerZoneFromName(Name.c_str());
        if (Zone == POINTER_ZONE_NONE) { ConfigError(Path, Value[Form], "unknown zone \"" + Name + "\""); return false; }
        Out = MakeZone(Zone);
        return true;
    }
    if (Form == "face")
    {
        const std::string Name = Value[Form].as<std::string>();
        const uint32_t Gesture = PointerGestureFromName(Name.c_str());
        if (Gesture == 0) { ConfigError(Path, Value[Form], "unknown face gesture \"" + Name + "\""); return false; }
        Out = MakeFace(Gesture);
        return true;
    }
```

In the `Form == "stick"` branch, add the pointer value and update the error text:

```cpp
        if (Name == "left") { Out = MakeStick(SDL_GAMEPAD_AXIS_LEFTX); return true; }
        if (Name == "right") { Out = MakeStick(SDL_GAMEPAD_AXIS_RIGHTX); return true; }
        if (Name == "pointer") { Out = MakePointer(); return true; }
        ConfigError(Path, Value[Form], "stick must be left, right or pointer");
        return false;
```

Add after `InputConfig::Bindings`:

```cpp
const char * InputConfig::ControlLabel(N64Control Control)
{
    static const char * const kLabels[(int)N64Control::Count] = {
        "A", "B", "Z", "St", "L", "R",
        "C^", "Cv", "C<", "C>",
        "D^", "Dv", "D<", "D>",
        "",
    };
    return kLabels[(int)Control];
}

bool InputConfig::UsesPointer() const
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone || B.kind == Binding::Kind::Face || B.kind == Binding::Kind::Pointer)
            {
                return true;
            }
        }
    }
    return false;
}

void InputConfig::PointerLabels(char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE],
                                char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE]) const
{
    memset(Labels, 0, POINTER_ZONE_COUNT * POINTER_LABEL_SIZE);
    memset(GestureLabels, 0, POINTER_GESTURE_COUNT * POINTER_LABEL_SIZE);
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone)
            {
                snprintf(Labels[B.code], POINTER_LABEL_SIZE, "%s", ControlLabel((N64Control)i));
            }
            else if (B.kind == Binding::Kind::Face)
            {
                snprintf(GestureLabels[PointerGestureIndex((uint32_t)B.code)], POINTER_LABEL_SIZE, "%s", ControlLabel((N64Control)i));
            }
        }
    }
}
```

Add `#include <string.h>` to the includes of `InputConfig.cpp`.

- [ ] **Step 5: Create the three shipped layouts**

Create `Config/mouse/sm64.yaml`:

```yaml
# Mouse-and-face layout for Super Mario 64. Select with
#   make run rom=… input=Config/mouse/sm64.yaml face=1
# or PJ64_INPUT_YAML. See README "Playing with a mouse".
#
# Zones are the 4x4 grid drawn over the game: top1-top4, left2, left3, right2, right3,
# bottom1-bottom4, and centre (the stick area; a click there is the centre zone).
# Face gestures: eyebrows, head-left, head-right. Each needs the camera (face=1).

bindings:
  Stick:     {stick: pointer}
  A:         {zone: centre}
  Z:         {face: eyebrows}     # held: crouch; brows + click while tilted = long jump
  B:         {face: head-left}    # punch, and dive while running
  R:         {face: head-right}
  Start:     {zone: top4}
  L:         {zone: top1}
  CUp:       {zone: top2}
  CDown:     {zone: bottom2}
  CLeft:     {zone: left2}
  CRight:    {zone: right2}
  DPadUp:    {zone: top3}
  DPadDown:  {zone: bottom3}
  DPadLeft:  {zone: left3}
  DPadRight: {zone: right3}
```

Create `Config/mouse/goldeneye.yaml`:

```yaml
# Mouse-and-face layout for GoldenEye 007 / Perfect Dark (control style 1.1).
# Select with make run rom=… input=Config/mouse/goldeneye.yaml face=1
# The stick aims and moves; strafe follows your head; fire is the click.

bindings:
  Stick:     {stick: pointer}
  Z:         {zone: centre}       # fire
  R:         {face: eyebrows}     # aim mode, held
  CLeft:     {face: head-left}    # strafe left
  CRight:    {face: head-right}   # strafe right
  A:         {zone: left2}
  B:         {zone: right2}
  Start:     {zone: top4}
  L:         {zone: top1}
  CUp:       {zone: top2}
  CDown:     {zone: bottom2}
  DPadUp:    {zone: top3}
  DPadDown:  {zone: bottom3}
  DPadLeft:  {zone: left3}
  DPadRight: {zone: right3}
```

Create `Config/mouse/mk64.yaml`:

```yaml
# Mouse-and-face layout for Mario Kart 64.
# Select with make run rom=… input=Config/mouse/mk64.yaml face=1
# Hold the click in the centre to accelerate while steering with the cursor.

bindings:
  Stick:     {stick: pointer}
  A:         {zone: centre}       # accelerate, held
  R:         {face: eyebrows}     # hop and drift, held
  Z:         {face: head-left}    # use item
  B:         {face: head-right}   # brake / reverse
  Start:     {zone: top4}
  L:         {zone: top1}
  CUp:       {zone: top2}
  CDown:     {zone: bottom2}
  CLeft:     {zone: left2}
  CRight:    {zone: right2}
  DPadUp:    {zone: top3}
  DPadDown:  {zone: bottom3}
  DPadLeft:  {zone: left3}
  DPadRight: {zone: right3}
```

- [ ] **Step 6: Run the tests**

Run: `make input-config-test`
Expected: `ok: input config`.

- [ ] **Step 7: Build everything**

Run: `make -j8 all && make test`
Expected: green; the plugin compiles the new kinds but does not evaluate them yet.

- [ ] **Step 8: Commit**

```bash
git add Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/InputConfigTest.cpp Config/mouse
git commit -m "Parse zone, face and pointer-stick bindings

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Evaluate pointer bindings in the input plugin

**Files:**
- Modify: `Source/Project64-sdl/PluginInput.cpp` (globals near line 27, `SelftestReport` line 54-64, `GetKeys` line 151-233, `PluginLoaded` line 281-297)

**Interfaces:**
- Consumes: `PointerState`, `PointerSnapshot`, `PJ64_POINTER_ENV`, `PointerLayoutEvaluate`, `POINTER_ZONE_NONE` (Task 1); `Binding::Kind::Zone/Face/Pointer`, `InputConfig::UsesPointer`, `InputConfig::PointerLabels` (Task 2).
- Produces: with `PJ64_POINTER_SELFTEST` set, one stderr line on the first `GetKeys` that sees the button down: `pointer-selftest zone=<n> a=<0|1> start=<0|1>`. `State->LatchedZone`, `State->Labels`, `State->GestureLabels`, `State->OverlayWanted` written for the overlay.

- [ ] **Step 1: Map the shared struct**

In `PluginInput.cpp`, add includes:

```cpp
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
```

Add globals after `g_GridKeysChecked`:

```cpp
static PointerState * g_Pointer = nullptr;
static bool g_PointerChecked = false;
// Latch: the zone under the cursor when the button went down stays pressed until release.
static bool g_PointerPrevButton = false;
static int g_PointerLatched = POINTER_ZONE_NONE;
```

Add after `OpenGridKeys`:

```cpp
// The frontend maps a PointerState and passes its descriptor in PJ64_POINTER_ENV. It is
// mapped read-write: the plugin writes labels and the latched zone back for the overlay.
static void OpenPointerState(void)
{
    if (g_PointerChecked)
    {
        return;
    }
    g_PointerChecked = true;
    const char * FdEnv = getenv(PJ64_POINTER_ENV);
    if (FdEnv == nullptr)
    {
        return;
    }
    void * Mapped = mmap(nullptr, sizeof(PointerState), PROT_READ | PROT_WRITE, MAP_SHARED, atoi(FdEnv), 0);
    if (Mapped == MAP_FAILED)
    {
        fprintf(stderr, "input: could not map %s=%s; pointer bindings are inactive\n", PJ64_POINTER_ENV, FdEnv);
        return;
    }
    g_Pointer = (PointerState *)Mapped;
}
```

- [ ] **Step 2: Publish labels at load**

Replace `PluginLoaded` with:

```cpp
// Copies the resolved layout into the shared struct so the overlay can label cells. Runs
// once at dylib load, before any ROM, so plain stores are enough.
static void PublishPointerLabels(void)
{
    OpenPointerState();
    if (g_Pointer == nullptr)
    {
        return;
    }
    const InputConfig & Config = InputConfig::Get();
    Config.PointerLabels(g_Pointer->Labels, g_Pointer->GestureLabels);
    g_Pointer->LatchedZone.store(POINTER_ZONE_NONE);
    g_Pointer->OverlayWanted.store(Config.UsesPointer() ? 1u : 0u);
}

EXPORT void CALL PluginLoaded(void)
{
    const char * Env = getenv("PJ64_INPUT_YAML");
    if (Env != nullptr && Env[0] != '\0')
    {
        InputConfig::Get().Load(Env);
    }
    else
    {
        char Path[PATH_MAX];
        if (DefaultConfigPath(Path, sizeof(Path)) && access(Path, R_OK) == 0)
        {
            InputConfig::Get().Load(Path);
        }
    }
    PublishPointerLabels();
}
```

- [ ] **Step 3: Evaluate in GetKeys**

In `GetKeys`, insert after the line `SelftestReport(Raw, Keys);` and before `OpenFirstGamepad();`:

```cpp
    OpenPointerState();
    if (g_Pointer != nullptr)
    {
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
        const uint32_t Gestures = g_Pointer->Gestures.load(std::memory_order_relaxed);

        for (int i = 0; i < (int)N64Control::Count; i++)
        {
            for (const Binding & B : Config.Bindings((N64Control)i))
            {
                if (B.kind == Binding::Kind::Zone)
                {
                    if (g_PointerLatched == B.code)
                    {
                        SetControl(Keys, (N64Control)i);
                    }
                }
                else if (B.kind == Binding::Kind::Face)
                {
                    if ((Gestures & (uint32_t)B.code) != 0)
                    {
                        SetControl(Keys, (N64Control)i);
                    }
                }
                else if (B.kind == Binding::Kind::Pointer)
                {
                    Keys->X_AXIS = E.StickX;
                    Keys->Y_AXIS = E.StickY;
                    StickFromKeys = true; // the pointer owns the stick; the gamepad must not overwrite it
                }
            }
        }
        PointerSelftestReport(S.Button, g_PointerLatched, Keys);
    }
```

Add after `SelftestReport`:

```cpp
// Verification only. With PJ64_POINTER_SELFTEST set, report once, on the first frame that
// sees the button down, which zone latched and what the controller produced from it, so
// Scripts/pointer_selftest.sh proves delivery, geometry and mapping together.
static void PointerSelftestReport(bool Button, int Latched, const BUTTONS * Out)
{
    static bool Reported = false;
    if (Reported || !Button || getenv("PJ64_POINTER_SELFTEST") == nullptr)
    {
        return;
    }
    Reported = true;
    fprintf(stderr, "pointer-selftest zone=%d a=%d start=%d\n",
        Latched, Out->A_BUTTON ? 1 : 0, Out->START_BUTTON ? 1 : 0);
}
```

Update the file's header comment (lines 5-8) to add: `The mouse is sampled by the frontend on its main thread (SDL3 documents the mouse state functions as main-thread only) and read here through the shared PointerState.`

- [ ] **Step 4: Build and smoke**

Run: `make -j8 input && make test`
Expected: green. Nothing publishes a pointer yet, so `GetKeys` takes the early return.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-sdl/PluginInput.cpp
git commit -m "Evaluate zone, face and pointer bindings in GetKeys

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Frontend publishes the mouse; end-to-end self-test

**Files:**
- Modify: `Source/Project64-sdl/main.cpp` (includes; arguments at lines 86-119; before `AppInit` at line 177; the loop at lines 200-215; shutdown)
- Create: `Scripts/pointer_selftest.sh`
- Modify: `Makefile` (`config` recipe line 425-433, `run` recipe line 445-447, `.PHONY`, new `pointer-selftest` target)

**Interfaces:**
- Consumes: `PointerState`, `PointerSample`, `PointerPublish`, `PJ64_POINTER_ENV` (Task 1); the plugin's `pointer-selftest` line (Task 3).
- Produces: `PointerState * CreatePointerState(void)` in `main.cpp` (static), the struct's descriptor in `PJ64_POINTER_FD`; `PJ64_POINTER_INJECT=x,y,button` publishes a fixed sample; `make run … input=<yaml> face=1`; `make pointer-selftest rom=…`. The `PointerState *` is passed to Tasks 6 and 7.

- [ ] **Step 1: Write the self-test script (it will fail until the frontend publishes)**

Create `Scripts/pointer_selftest.sh` and `chmod +x` it:

```sh
#!/bin/sh
# Prove the pointer path end to end: the frontend publishes an injected pointer sample, the
# plugin latches the zone under it with Config/mouse/sm64.yaml loaded, and the N64 bits
# come out right. Two runs: a click in the centre must set A; a click in top4 must set
# Start. Design: Docs/superpowers/specs/2026-09-15-mouse-and-face-input-design.md
set -eu

ROM="${1:?usage: pointer_selftest.sh <rom>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64"
YAML="$ROOT/Config/mouse/sm64.yaml"
TIMEOUT=20

[ -x "$BIN" ] || { echo "frontend not built: $BIN" >&2; exit 1; }

# $1 = inject spec, $2 = expected report tail. The window is 640x480.
one_run() {
    LOG="$(mktemp)"
    PJ64_INPUT_YAML="$YAML" PJ64_POINTER_SELFTEST=1 PJ64_POINTER_INJECT="$1" "$BIN" "$ROM" >"$LOG" 2>&1 &
    PID=$!
    I=0
    while [ "$I" -lt "$TIMEOUT" ]; do
        grep -q '^pointer-selftest ' "$LOG" 2>/dev/null && break
        sleep 1
        I=$((I + 1))
    done
    kill -TERM "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    REPORT="$(grep '^pointer-selftest ' "$LOG" || true)"
    if [ "$REPORT" = "pointer-selftest $2" ]; then
        rm -f "$LOG"
        return 0
    fi
    echo "FAIL: inject $1: got '$REPORT', wanted 'pointer-selftest $2' (log: $LOG)" >&2
    return 1
}

FAIL=0
one_run "320,240,1" "zone=12 a=1 start=0" || FAIL=1
one_run "600,20,1" "zone=3 a=0 start=1" || FAIL=1

if [ "$FAIL" -eq 0 ]; then
    echo "ok: pointer path maps centre to A and top4 to Start"
fi
exit "$FAIL"
```

Add to `Makefile` after `grid-selftest`:

```make
pointer-selftest: ## Prove the injected-pointer path maps centre to A and top4 to Start (usage: make pointer-selftest rom=/path/to/game.z64)
	@test -n "$(rom)" || { echo "usage: make pointer-selftest rom=/path/to/game.z64"; exit 1; }
	Scripts/pointer_selftest.sh "$(rom)"
```

Add `pointer-selftest` to `.PHONY`.

Run: `make pointer-selftest rom=<a ROM you have>`
Expected: `FAIL: inject 320,240,1: got ''` — the plugin never sees a pointer.

- [ ] **Step 2: Create and publish the struct in the frontend**

In `main.cpp` add includes:

```cpp
#include <Common/PointerState.h>
#include <fcntl.h>
#include <sys/mman.h>
```

Add before `ConfigurePlugins`:

```cpp
// One mapped PointerState for this process: the main loop writes the mouse sample, the
// face tracker writes gesture bits, the input plugin reads both and writes labels back.
// The plugin is a dylib in this process, so the descriptor is passed by number in the
// environment exactly as the grid passes its key snapshot. The name is unlinked at once.
static PointerState * CreatePointerState(void)
{
    char ShmName[64];
    snprintf(ShmName, sizeof(ShmName), "/pj64ptr-%d", (int)getpid());
    const int Fd = shm_open(ShmName, O_CREAT | O_RDWR, 0600);
    if (Fd < 0 || ftruncate(Fd, (off_t)sizeof(PointerState)) != 0)
    {
        fprintf(stderr, "could not create the pointer state; mouse bindings are inactive\n");
        if (Fd >= 0) { shm_unlink(ShmName); close(Fd); }
        return nullptr;
    }
    void * Mapped = mmap(nullptr, sizeof(PointerState), PROT_READ | PROT_WRITE, MAP_SHARED, Fd, 0);
    shm_unlink(ShmName);
    if (Mapped == MAP_FAILED)
    {
        fprintf(stderr, "could not map the pointer state; mouse bindings are inactive\n");
        close(Fd);
        return nullptr;
    }
    PointerState * State = (PointerState *)Mapped;
    memset(State, 0, sizeof(PointerState));
    State->LatchedZone.store(-1);
    char FdText[16];
    snprintf(FdText, sizeof(FdText), "%d", Fd);
    setenv(PJ64_POINTER_ENV, FdText, 1);
    return State;
}

// PJ64_POINTER_INJECT=x,y,button replaces the sampled mouse with a fixed sample so the
// pointer path can be proven without a mouse (Scripts/pointer_selftest.sh).
static bool ParsePointerInject(PointerSample * Out)
{
    const char * Env = getenv("PJ64_POINTER_INJECT");
    if (Env == nullptr)
    {
        return false;
    }
    int Button = 0;
    if (sscanf(Env, "%f,%f,%d", &Out->X, &Out->Y, &Button) != 3)
    {
        fprintf(stderr, "bad PJ64_POINTER_INJECT: %s (want x,y,button)\n", Env);
        return false;
    }
    Out->Inside = true;
    Out->Button = Button != 0;
    return true;
}

// Main thread only: SDL3 documents SDL_GetMouseState as main-thread only.
static void PublishMouse(PointerState * State, SDL_Window * Window, const PointerSample * Inject)
{
    PointerSample S;
    SDL_GetWindowSize(Window, &S.W, &S.H);
    if (Inject != nullptr)
    {
        S.X = Inject->X;
        S.Y = Inject->Y;
        S.Inside = true;
        S.Button = Inject->Button;
    }
    else
    {
        const SDL_MouseButtonFlags Buttons = SDL_GetMouseState(&S.X, &S.Y);
        S.Inside = SDL_GetMouseFocus() == Window;
        S.Button = (Buttons & SDL_BUTTON_LMASK) != 0;
    }
    PointerPublish(State, S);
}
```

In `main`, immediately before `CSdlNotification notify;` (line 176), add:

```cpp
    PointerState * pointer = CreatePointerState();
    PointerSample inject;
    const bool injecting = ParsePointerInject(&inject);
```

In the main loop, after the inner `while (SDL_PollEvent(&ev)) { ... }` block and before the `g_BaseSystem == nullptr` check, add:

```cpp
        if (pointer != nullptr)
        {
            PublishMouse(pointer, window, injecting ? &inject : nullptr);
        }
```

Also change the file's second header line from `Windowed only; the cursor is never grabbed or hidden.` to `Windowed only; the cursor is never grabbed or hidden. The main loop publishes the mouse for the input plugin (see Common/PointerState.h).`

- [ ] **Step 3: Makefile run arguments and config install**

Change the `run` recipe to:

```make
run: all ## [STEP 8] Run a ROM in a window (usage: make run rom=/path/to/game.z64 [input=Config/mouse/sm64.yaml] [face=1])
	@test -n "$(rom)" || { echo "usage: make run rom=/path/to/game.z64 [input=<yaml>] [face=1]"; exit 1; }
	$(if $(input),PJ64_INPUT_YAML="$(input)") $(if $(face),PJ64_FACE=1) ./$(BIN)/Project64 "$(rom)"
```

In the `config` recipe, after the `cp -n Config/input.yaml` line, add:

```make
	@# -Rn: mouse layouts are user data once installed.
	@cp -Rn Config/mouse $(BIN)/Config/ 2>/dev/null || true
```

- [ ] **Step 4: Build and run the self-test**

Run: `make -j8 all && make pointer-selftest rom=<ROM>`
Expected: `ok: pointer path maps centre to A and top4 to Start`. If the first run reports `zone=-1`, the sample was published with `Inside` false or before the window had a size; check `PublishMouse` runs after `SDL_CreateWindow`.

- [ ] **Step 5: Check the two regressions**

Run: `make test && make input-config-test && make grid-selftest rom=<ROM>`
Expected: all green. The grid tiles each create their own struct and, being not focusable, publish `Inside` false, so the keyboard path is untouched.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-sdl/main.cpp Scripts/pointer_selftest.sh Makefile
git commit -m "Publish the mouse from the frontend and prove the pointer path

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: Gesture classifier

**Files:**
- Create: `Source/Project64-sdl/FaceGestures.h`
- Create: `Source/Project64-sdl/FaceGestures.cpp`
- Create: `Source/Project64-sdl/FaceGesturesTest.cpp`
- Modify: `Makefile` (`FRONTEND_SRC` line 293, `.PHONY`, new `face-gesture-test` target)

**Interfaces:**
- Consumes: `POINTER_GESTURE_*` bits (Task 1).
- Produces:
  - `struct GestureSample { bool FaceFound; float BrowHeight; float Yaw; double Time; }` (Time in seconds, monotonic)
  - `struct GestureThresholds { float Brow; float Yaw; float ReleaseFraction; double BaselineSeconds; int DebounceFrames; double NoFaceSeconds; }` with the constructor defaults 0.035, 0.25, 0.6, 5.0, 2, 0.5
  - `class GestureClassifier { explicit GestureClassifier(const GestureThresholds &); uint32_t Update(const GestureSample &); float BrowBaseline() const; float YawBaseline() const; }`

- [ ] **Step 1: Write the failing classifier tests**

Create `Source/Project64-sdl/FaceGesturesTest.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// Tests for GestureClassifier with synthetic samples. No camera, no SDL.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "FaceGestures.h"

#include <stdio.h>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

static const double kFrame = 1.0 / 30.0;

// Feed N frames of one sample, returning the last result.
static uint32_t Feed(GestureClassifier & C, double & T, int Frames, bool Face, float Brow, float Yaw)
{
    uint32_t Last = 0;
    for (int i = 0; i < Frames; i++)
    {
        T += kFrame;
        Last = C.Update(GestureSample{ Face, Brow, Yaw, T });
    }
    return Last;
}

int main()
{
    GestureThresholds Th;
    CHECK(Th.Brow == 0.035f && Th.Yaw == 0.25f && Th.ReleaseFraction == 0.6f);
    CHECK(Th.BaselineSeconds == 5.0 && Th.DebounceFrames == 2 && Th.NoFaceSeconds == 0.5);

    {
        // Debounce: one raised frame is nothing; the second sets the bit.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);                       // settle: 2 s at rest
        CHECK(C.BrowBaseline() > 0.099f && C.BrowBaseline() < 0.101f);
        CHECK(Feed(C, T, 1, true, 0.15f, 0.0f) == 0);
        CHECK(Feed(C, T, 1, true, 0.15f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        // Hysteresis: dropping to just above the release level keeps it set...
        CHECK(Feed(C, T, 5, true, 0.10f + 0.035f * 0.6f + 0.002f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        // ...and going below it clears after two frames.
        CHECK(Feed(C, T, 1, true, 0.10f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        CHECK(Feed(C, T, 1, true, 0.10f, 0.0f) == 0);
    }
    {
        // The baseline is frozen while the gesture is held: ten seconds raised, still set.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(Feed(C, T, 300, true, 0.15f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        CHECK(C.BrowBaseline() < 0.105f);
    }
    {
        // The baseline follows a slow drift with no gesture firing.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        bool Fired = false;
        for (int i = 0; i < 600; i++)                             // 20 s, 0.10 -> 0.12
        {
            if (Feed(C, T, 1, true, 0.10f + 0.02f * (float)i / 600.0f, 0.0f) != 0) Fired = true;
        }
        CHECK(!Fired);
        CHECK(C.BrowBaseline() > 0.112f);                         // lags the ramp by rate * tau = 0.005
    }
    {
        // Yaw: negative is head-left, positive is head-right; both need debounce.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(Feed(C, T, 2, true, 0.10f, -0.30f) == POINTER_GESTURE_HEAD_LEFT);
        CHECK(Feed(C, T, 2, true, 0.10f, 0.0f) == 0);
        CHECK(Feed(C, T, 2, true, 0.10f, 0.30f) == POINTER_GESTURE_HEAD_RIGHT);
        // Yaw baseline is frozen while a head bit is held.
        Feed(C, T, 300, true, 0.10f, 0.30f);
        CHECK(C.YawBaseline() < 0.02f);
        // Brows and a head turn at once are both reported.
        CHECK(Feed(C, T, 2, true, 0.15f, 0.30f) == (POINTER_GESTURE_EYEBROWS | POINTER_GESTURE_HEAD_RIGHT));
    }
    {
        // No face for longer than the timeout clears everything; a brief dropout does not.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(Feed(C, T, 2, true, 0.15f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        CHECK(Feed(C, T, 5, false, 0.0f, 0.0f) == POINTER_GESTURE_EYEBROWS);   // 0.17 s without a face
        CHECK(Feed(C, T, 15, false, 0.0f, 0.0f) == 0);                          // 0.67 s: cleared
        // The baseline survives the dropout, so the next raise fires at once.
        CHECK(C.BrowBaseline() > 0.099f && C.BrowBaseline() < 0.101f);
        CHECK(Feed(C, T, 2, true, 0.15f, 0.0f) == POINTER_GESTURE_EYEBROWS);
    }
    {
        // A custom threshold set is honoured.
        GestureThresholds Loose;
        Loose.Brow = 0.01f;
        Loose.DebounceFrames = 1;
        GestureClassifier C(Loose);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(Feed(C, T, 1, true, 0.115f, 0.0f) == POINTER_GESTURE_EYEBROWS);
    }

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: face gestures\n");
    return 0;
}
```

Add to `Makefile` after `pointer-layout-test`, and add `face-gesture-test` to `.PHONY`:

```make
face-gesture-test: $(BUILD)/Project64-sdl/FaceGesturesTest.o $(BUILD)/Project64-sdl/FaceGestures.o ## Run the GestureClassifier tests
	$(CXX) $(LDFLAGS) -o $(BUILD)/face-gesture-test $^
	@$(BUILD)/face-gesture-test
```

Run: `make face-gesture-test`
Expected: compile error, `FaceGestures.h` not found.

- [ ] **Step 2: Write the header**

Create `Source/Project64-sdl/FaceGestures.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// Turns two face measures into held gesture bits. Pure C++ so it can be tested without a
// camera; FaceTracker.mm feeds it one sample per frame.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <Common/PointerState.h>
#include <stdint.h>

struct GestureSample
{
    bool FaceFound;
    float BrowHeight;   // eyebrow mean y minus eye mean y, face-box-normalised units
    float Yaw;          // radians; negative is head-left
    double Time;        // seconds, monotonic
};

struct GestureThresholds
{
    float Brow = 0.035f;            // rise above baseline that sets eyebrows
    float Yaw = 0.25f;              // radians from baseline that set a head bit
    float ReleaseFraction = 0.6f;   // release below this fraction of the set threshold
    double BaselineSeconds = 5.0;   // EMA time constant for the rest baseline
    int DebounceFrames = 2;         // consecutive frames to set, and to clear
    double NoFaceSeconds = 0.5;     // clear everything after this long without a face
};

class GestureClassifier
{
public:
    explicit GestureClassifier(const GestureThresholds & Thresholds);

    // Returns the current PointerGesture bits.
    uint32_t Update(const GestureSample & Sample);

    float BrowBaseline() const { return m_BrowBaseline; }
    float YawBaseline() const { return m_YawBaseline; }

private:
    // One held bit with debounce and hysteresis.
    struct Channel
    {
        bool Active = false;
        int Count = 0;
        // Raw true means the measure is beyond the set level (or, while active, still
        // above the release level).
        void Step(bool Raw, int Debounce);
    };

    void Track(float & Baseline, float Value, double Dt, bool Frozen);

    GestureThresholds m_T;
    bool m_HaveBaseline;
    float m_BrowBaseline;
    float m_YawBaseline;
    double m_LastTime;
    double m_LastFaceTime;
    Channel m_Brows, m_Left, m_Right;
};
```

- [ ] **Step 3: Implement it**

Create `Source/Project64-sdl/FaceGestures.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// GestureClassifier: rest baseline, hysteresis, debounce, no-face timeout.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "FaceGestures.h"

GestureClassifier::GestureClassifier(const GestureThresholds & Thresholds) :
    m_T(Thresholds),
    m_HaveBaseline(false),
    m_BrowBaseline(0.0f),
    m_YawBaseline(0.0f),
    m_LastTime(0.0),
    m_LastFaceTime(-1e9)
{
}

void GestureClassifier::Channel::Step(bool Raw, int Debounce)
{
    if (Raw == Active)
    {
        Count = 0;
        return;
    }
    Count++;
    if (Count >= Debounce)
    {
        Active = Raw;
        Count = 0;
    }
}

// Exponential moving average with time constant BaselineSeconds. Frozen while the
// gesture that uses this baseline is held, so a long hold never becomes the new rest.
void GestureClassifier::Track(float & Baseline, float Value, double Dt, bool Frozen)
{
    if (Frozen || Dt <= 0.0)
    {
        return;
    }
    double Alpha = Dt / m_T.BaselineSeconds;
    if (Alpha > 1.0) Alpha = 1.0;
    Baseline = (float)(Baseline + (Value - Baseline) * Alpha);
}

uint32_t GestureClassifier::Update(const GestureSample & S)
{
    const double Dt = m_HaveBaseline ? S.Time - m_LastTime : 0.0;
    m_LastTime = S.Time;

    if (!S.FaceFound)
    {
        if (S.Time - m_LastFaceTime > m_T.NoFaceSeconds)
        {
            m_Brows = Channel();
            m_Left = Channel();
            m_Right = Channel();
        }
        return (m_Brows.Active ? POINTER_GESTURE_EYEBROWS : 0)
             | (m_Left.Active ? POINTER_GESTURE_HEAD_LEFT : 0)
             | (m_Right.Active ? POINTER_GESTURE_HEAD_RIGHT : 0);
    }
    m_LastFaceTime = S.Time;

    if (!m_HaveBaseline)
    {
        m_HaveBaseline = true;
        m_BrowBaseline = S.BrowHeight;
        m_YawBaseline = S.Yaw;
    }

    const float BrowSet = m_T.Brow;
    const float BrowRelease = m_T.Brow * m_T.ReleaseFraction;
    const float BrowDelta = S.BrowHeight - m_BrowBaseline;
    m_Brows.Step(BrowDelta > (m_Brows.Active ? BrowRelease : BrowSet), m_T.DebounceFrames);

    const float YawSet = m_T.Yaw;
    const float YawRelease = m_T.Yaw * m_T.ReleaseFraction;
    const float YawDelta = S.Yaw - m_YawBaseline;
    m_Right.Step(YawDelta > (m_Right.Active ? YawRelease : YawSet), m_T.DebounceFrames);
    m_Left.Step(-YawDelta > (m_Left.Active ? YawRelease : YawSet), m_T.DebounceFrames);

    // Baselines move only at rest. Also hold them while a channel is counting toward a
    // set, so the rise that is about to fire does not get partly absorbed.
    Track(m_BrowBaseline, S.BrowHeight, Dt, m_Brows.Active || m_Brows.Count > 0);
    Track(m_YawBaseline, S.Yaw, Dt, m_Left.Active || m_Right.Active || m_Left.Count > 0 || m_Right.Count > 0);

    return (m_Brows.Active ? POINTER_GESTURE_EYEBROWS : 0)
         | (m_Left.Active ? POINTER_GESTURE_HEAD_LEFT : 0)
         | (m_Right.Active ? POINTER_GESTURE_HEAD_RIGHT : 0);
}
```

Add `FaceGestures.cpp` to `FRONTEND_SRC` in the Makefile:

```make
FRONTEND_SRC = $(addprefix Project64-sdl/, main.cpp SdlNotification.cpp SdlRenderWindow.cpp GridHost.cpp FaceGestures.cpp)
```

- [ ] **Step 4: Run the tests**

Run: `make face-gesture-test`
Expected: `ok: face gestures`. If the drift case fires, the baseline is being frozen too eagerly; if the hold case's baseline creeps, it is not frozen while `Active`. Both are classifier bugs, not test bugs.

- [ ] **Step 5: Build everything and commit**

Run: `make -j8 all && make test`

```bash
git add Source/Project64-sdl/FaceGestures.h Source/Project64-sdl/FaceGestures.cpp Source/Project64-sdl/FaceGesturesTest.cpp Makefile
git commit -m "Add the face gesture classifier

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Face tracker with AVFoundation and Vision

**Files:**
- Create: `Source/Project64-sdl/FaceTracker.h`
- Create: `Source/Project64-sdl/FaceTracker.mm`
- Modify: `Source/Project64-sdl/main.cpp` (argument parsing, start after `CreatePointerState`, stop before `CN64System::CloseSystem()`)
- Modify: `Makefile` (new `FRONTEND_MM_SRC`, `.mm` pattern rule, frontend link line 419-421, `ALL_OBJS`)

**Interfaces:**
- Consumes: `PointerState` (`Gestures`, `Face` atomics), `FaceStatus` (Task 1); `GestureClassifier`, `GestureSample`, `GestureThresholds` (Task 5); `PointerState * pointer` in `main` (Task 4).
- Produces: `bool FaceTrackerStart(PointerState * State)` (false if the camera cannot be used; status already written), `void FaceTrackerStop(void)`; `--face` flag and `PJ64_FACE=1`; env `PJ64_FACE_DEBUG`, `PJ64_FACE_BROW`, `PJ64_FACE_YAW`.

- [ ] **Step 1: Write the header**

Create `Source/Project64-sdl/FaceTracker.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// Webcam face gestures through AVFoundation and Vision. Frontend only; the plugin reads
// the three bits this writes into PointerState. Frames never leave memory.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

struct PointerState;

// Asks for camera access, starts capture, and runs face-landmark detection on a private
// queue. Returns false if capture could not start; State->Face already says why. Safe to
// call once; a second call is ignored.
bool FaceTrackerStart(PointerState * State);

// Stops capture. Safe to call without a prior start.
void FaceTrackerStop(void);
```

- [ ] **Step 2: Write the tracker**

Create `Source/Project64-sdl/FaceTracker.mm`:

```objc
// Project64 - A Nintendo 64 emulator
// Webcam face gestures: AVFoundation capture -> Vision face landmarks -> GestureClassifier.
// Frames are handled in the capture callback and released with it; nothing is stored,
// previewed or written. Only PJ64_FACE_DEBUG prints the two measures, once a second.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#import "FaceTracker.h"
#import "FaceGestures.h"
#import <Common/PointerState.h>

#import <AVFoundation/AVFoundation.h>
#import <Vision/Vision.h>
#import <mach/mach_time.h>
#import <stdio.h>
#import <stdlib.h>

static double MonotonicSeconds(void)
{
    static mach_timebase_info_data_t Info = { 0, 0 };
    if (Info.denom == 0) mach_timebase_info(&Info);
    return (double)mach_absolute_time() * (double)Info.numer / (double)Info.denom / 1e9;
}

static GestureThresholds ThresholdsFromEnv(void)
{
    GestureThresholds T;
    const char * Brow = getenv("PJ64_FACE_BROW");
    if (Brow != nullptr && atof(Brow) > 0.0) T.Brow = (float)atof(Brow);
    const char * Yaw = getenv("PJ64_FACE_YAW");
    if (Yaw != nullptr && atof(Yaw) > 0.0) T.Yaw = (float)atof(Yaw);
    return T;
}

// Mean y of a landmark region in face-box-normalised coordinates (origin bottom-left).
static float RegionMeanY(VNFaceLandmarkRegion2D * Region)
{
    if (Region == nil || Region.pointCount == 0) return 0.0f;
    const CGPoint * Points = Region.normalizedPoints;
    double Sum = 0.0;
    for (NSUInteger i = 0; i < Region.pointCount; i++) Sum += Points[i].y;
    return (float)(Sum / (double)Region.pointCount);
}

@interface PJ64FaceDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
{
    PointerState * m_State;
    GestureClassifier * m_Classifier;
    VNDetectFaceLandmarksRequest * m_Request;
    bool m_Debug;
    double m_LastDebug;
}
- (instancetype)initWithState:(PointerState *)State;
@end

@implementation PJ64FaceDelegate

- (instancetype)initWithState:(PointerState *)State
{
    self = [super init];
    if (self)
    {
        m_State = State;
        m_Classifier = new GestureClassifier(ThresholdsFromEnv());
        m_Request = [[VNDetectFaceLandmarksRequest alloc] init];
        m_Debug = getenv("PJ64_FACE_DEBUG") != nullptr;
        m_LastDebug = 0.0;
    }
    return self;
}

- (void)dealloc
{
    delete m_Classifier;
}

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection
{
    CVPixelBufferRef Pixels = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (Pixels == nullptr) return;

    VNImageRequestHandler * Handler = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:Pixels
                                                                              orientation:kCGImagePropertyOrientationUp
                                                                                  options:@{}];
    NSError * Error = nil;
    GestureSample S = { false, 0.0f, 0.0f, MonotonicSeconds() };
    if ([Handler performRequests:@[ m_Request ] error:&Error])
    {
        VNFaceObservation * Best = nil;
        for (VNFaceObservation * Face in m_Request.results)
        {
            if (Best == nil || Face.boundingBox.size.width > Best.boundingBox.size.width) Best = Face;
        }
        if (Best != nil && Best.landmarks != nil)
        {
            VNFaceLandmarks2D * L = Best.landmarks;
            const float BrowY = 0.5f * (RegionMeanY(L.leftEyebrow) + RegionMeanY(L.rightEyebrow));
            const float EyeY = 0.5f * (RegionMeanY(L.leftEye) + RegionMeanY(L.rightEye));
            S.FaceFound = true;
            S.BrowHeight = BrowY - EyeY;
            // Vision reports yaw for the face as seen by the camera. A front camera is not
            // mirrored, so the player's left turn arrives as a positive yaw; negate so the
            // classifier's "negative is head-left" holds. Confirm on first run (spec Part 6).
            S.Yaw = Best.yaw != nil ? -Best.yaw.floatValue : 0.0f;
        }
    }

    const uint32_t Bits = m_Classifier->Update(S);
    m_State->Gestures.store(Bits, std::memory_order_relaxed);
    m_State->Face.store(S.FaceFound ? FACE_TRACKING : FACE_NO_FACE, std::memory_order_relaxed);

    if (m_Debug && S.Time - m_LastDebug >= 1.0)
    {
        m_LastDebug = S.Time;
        fprintf(stderr, "face: found=%d brow=%.4f (base %.4f) yaw=%.3f (base %.3f) bits=%u\n",
            S.FaceFound ? 1 : 0, S.BrowHeight, m_Classifier->BrowBaseline(),
            S.Yaw, m_Classifier->YawBaseline(), Bits);
    }
}

@end

static AVCaptureSession * g_Session = nil;
static PJ64FaceDelegate * g_Delegate = nil;
static dispatch_queue_t g_Queue = nil;
static bool g_Started = false;

static bool StartSession(PointerState * State)
{
    AVCaptureDevice * Device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (Device == nil)
    {
        fprintf(stderr, "face: no camera found; gestures are off\n");
        State->Face.store(FACE_ERROR);
        return false;
    }
    NSError * Error = nil;
    AVCaptureDeviceInput * Input = [AVCaptureDeviceInput deviceInputWithDevice:Device error:&Error];
    if (Input == nil)
    {
        fprintf(stderr, "face: could not open the camera: %s; gestures are off\n",
            Error.localizedDescription.UTF8String);
        State->Face.store(FACE_ERROR);
        return false;
    }
    g_Session = [[AVCaptureSession alloc] init];
    g_Session.sessionPreset = AVCaptureSessionPreset640x480;
    if (![g_Session canAddInput:Input])
    {
        fprintf(stderr, "face: camera input rejected; gestures are off\n");
        State->Face.store(FACE_ERROR);
        g_Session = nil;
        return false;
    }
    [g_Session addInput:Input];

    AVCaptureVideoDataOutput * Output = [[AVCaptureVideoDataOutput alloc] init];
    Output.alwaysDiscardsLateVideoFrames = YES;
    Output.videoSettings = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
    g_Queue = dispatch_queue_create("pj64.face", DISPATCH_QUEUE_SERIAL);
    g_Delegate = [[PJ64FaceDelegate alloc] initWithState:State];
    [Output setSampleBufferDelegate:g_Delegate queue:g_Queue];
    if (![g_Session canAddOutput:Output])
    {
        fprintf(stderr, "face: camera output rejected; gestures are off\n");
        State->Face.store(FACE_ERROR);
        g_Session = nil;
        return false;
    }
    [g_Session addOutput:Output];
    [g_Session startRunning];
    State->Face.store(FACE_NO_FACE);
    fprintf(stderr, "face: tracking started (%s)\n", Device.localizedName.UTF8String);
    return true;
}

bool FaceTrackerStart(PointerState * State)
{
    if (g_Started || State == nullptr) return false;
    g_Started = true;
    State->Face.store(FACE_STARTING);

    const AVAuthorizationStatus Status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if (Status == AVAuthorizationStatusAuthorized)
    {
        return StartSession(State);
    }
    if (Status == AVAuthorizationStatusDenied || Status == AVAuthorizationStatusRestricted)
    {
        fprintf(stderr, "face: camera access denied for the launching app; allow it in System Settings > Privacy & Security > Camera\n");
        State->Face.store(FACE_DENIED);
        return false;
    }
    // Not determined: macOS prompts, attributed to the terminal or IDE that launched us.
    // The answer arrives on an arbitrary queue; start there, since AVCaptureSession may be
    // started from any thread.
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL Granted) {
        if (Granted)
        {
            StartSession(State);
        }
        else
        {
            fprintf(stderr, "face: camera access denied; gestures are off\n");
            State->Face.store(FACE_DENIED);
        }
    }];
    return true;
}

void FaceTrackerStop(void)
{
    if (g_Session != nil)
    {
        [g_Session stopRunning];
        g_Session = nil;
        fprintf(stderr, "face: tracking stopped\n");
    }
    g_Delegate = nil;
    g_Queue = nil;
}
```

- [ ] **Step 3: Makefile: Objective-C++ rule and frameworks**

In `Makefile`, after `FRONTEND_SRC`, add:

```make
# Objective-C++: the face tracker talks to AVFoundation and Vision. Frontend only.
FRONTEND_MM_SRC = Project64-sdl/FaceTracker.mm
```

After `FRONTEND_OBJS = $(call objs,$(FRONTEND_SRC))` add:

```make
FRONTEND_MM_OBJS = $(call objs,$(FRONTEND_MM_SRC))
```

Add `$(FRONTEND_MM_OBJS)` to the `ALL_OBJS` list (so its `.d` file is included), to the `$(AUDIO_OBJS) $(INPUT_OBJS) $(FRONTEND_OBJS) ...: CPPFLAGS += $(SDL_CFLAGS)` line, and to the `$(FRONTEND_OBJS): WARN = -Wall` line.

Change the frontend link rule to:

```make
$(BIN)/Project64: $(FRONTEND_OBJS) $(FRONTEND_MM_OBJS) $(LIBDIR)/libProject64-core.a $(LIBDIR)/libasmjit.a $(LIBDIR)/libzlib.a $(LIBDIR)/libsoftfloat.a $(LIBDIR)/libCommon.a
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) -framework OpenGL -framework AVFoundation -framework Vision -framework CoreMedia -framework CoreVideo -lpthread
```

Add after the `.cpp` pattern rule (line 477-479):

```make
$(BUILD)/%.o: $(SRC)/%.mm
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fobjc-arc -c $< -o $@
```

- [ ] **Step 4: Wire the flag into main**

In `main.cpp` add `#include "FaceTracker.h"`.

Replace the positional argument handling so `--face` can appear anywhere. Change the block from `bool TileMode = false;` through the `if (RomPath == nullptr)` usage check to:

```cpp
    bool TileMode = false;
    bool FaceFlag = getenv("PJ64_FACE") != nullptr && getenv("PJ64_FACE")[0] != '\0' && strcmp(getenv("PJ64_FACE"), "0") != 0;
    SDL_Rect TileRect = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    const char * RomPath = nullptr;
    // Strip --face wherever it appears; everything else keeps its position.
    int Argc = 0;
    char * Argv[16];
    for (int i = 0; i < argc && Argc < 16; i++)
    {
        if (strcmp(argv[i], "--face") == 0) { FaceFlag = true; continue; }
        Argv[Argc++] = argv[i];
    }
    if (Argc >= 2 && strcmp(Argv[1], "--tile") == 0)
    {
        if (Argc < 5 || strcmp(Argv[3], "--tile-rect") != 0)
        {
            fprintf(stderr, "usage: %s --tile <rom> --tile-rect X,Y,W,H\n", Argv[0]);
            return 2;
        }
        RomPath = Argv[2];
        if (sscanf(Argv[4], "%d,%d,%d,%d", &TileRect.x, &TileRect.y, &TileRect.w, &TileRect.h) != 4
            || TileRect.w <= 0 || TileRect.h <= 0)
        {
            fprintf(stderr, "bad --tile-rect: %s\n", Argv[4]);
            return 2;
        }
        TileMode = true;
    }
    else if (Argc >= 2)
    {
        RomPath = Argv[1];
    }
    if (RomPath == nullptr)
    {
        fprintf(stderr, "usage: %s <rom file> [--face]\n", argv[0]);
        return 2;
    }
```

After the `PointerState * pointer = CreatePointerState();` lines from Task 4, add:

```cpp
    if (FaceFlag && pointer != nullptr && !TileMode)
    {
        FaceTrackerStart(pointer);
    }
```

Before `CN64System::CloseSystem();` add:

```cpp
    FaceTrackerStop();
```

- [ ] **Step 5: Build and check the unit tests and smoke test**

Run: `make -j8 all && make test && make face-gesture-test && make pointer-selftest rom=<ROM>`
Expected: all green. If the `.mm` unit fails on `-std=c++14`, that is the spec's noted risk: add `-std=c++17` to the `.mm` rule only and record it in the rule's comment.

- [ ] **Step 6: Manual camera check and yaw sign**

Run: `PJ64_FACE_DEBUG=1 make run rom=<Super Mario 64> input=Config/mouse/sm64.yaml face=1`
Expected: a camera permission prompt naming your terminal (once), then `face: tracking started (...)` and one `face:` line per second. Raise your brows: `bits=1` appears within two frames and `base` stays put while held. Turn your head to your left: `bits=2` must appear (head-left). If `bits=4` appears instead, remove the negation on `S.Yaw` in `FaceTracker.mm` and rebuild; record the outcome in the comment above that line.

- [ ] **Step 7: Commit**

```bash
git add Source/Project64-sdl/FaceTracker.h Source/Project64-sdl/FaceTracker.mm Source/Project64-sdl/main.cpp Makefile
git commit -m "Add the webcam face tracker behind --face

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Overlay

**Files:**
- Create: `Source/Project64-sdl/Overlay.h`
- Create: `Source/Project64-sdl/Overlay.cpp`
- Modify: `Source/Project64-sdl/SdlRenderWindow.h` (constructor signature, two members)
- Modify: `Source/Project64-sdl/SdlRenderWindow.cpp` (constructor lines 13-42, `SwapWindow` lines 196-216)
- Modify: `Source/Project64-sdl/main.cpp` (the `CSdlRenderWindow renderWindow(...)` construction, line 184)
- Modify: `Makefile` (`FRONTEND_SRC`)

**Interfaces:**
- Consumes: `PointerState` (`OverlayWanted`, `Labels`, `GestureLabels`, `LatchedZone`, `Gestures`, `Face`), `PointerZoneRect`, `PointerStickRadius`, `POINTER_ZONE_CENTRE`, `POINTER_ZONE_COUNT` (Task 1).
- Produces: `void OverlayDraw(const PointerState * State, int Width, int Height)` — draws with fixed-function GL into the current context, leaving all GL state as it found it.

- [ ] **Step 1: Write the overlay header**

Create `Source/Project64-sdl/Overlay.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// The pointer overlay: ring, cells, labels, face status. Drawn by the render window on
// the emulation thread right before each present, with the GL 2.1 compatibility context
// current. Leaves every piece of GL state as it found it.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

struct PointerState;

void OverlayDraw(const PointerState * State, int Width, int Height);
```

- [ ] **Step 2: Write the overlay**

Create `Source/Project64-sdl/Overlay.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// Fixed-function GL overlay for the pointer layout. See Overlay.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Overlay.h"
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <OpenGL/gl.h>
#include <math.h>
#include <string.h>

static const float kDim = 0.35f;     // resting line and label alpha
static const float kBright = 1.0f;   // held zone and active gesture alpha
static const int kScale = 3;         // font pixel size; a glyph is 15x21 window pixels

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

static void DrawFaceStatus(const PointerState * State, int Height)
{
    const uint32_t Face = State->Face.load(std::memory_order_relaxed);
    if (Face == FACE_OFF) return;
    const float Cx = 14.0f, Cy = (float)Height - 14.0f, R = 6.0f;
    if (Face == FACE_TRACKING)
    {
        DrawCircle(Cx, Cy, R, kBright, true);
    }
    else
    {
        DrawCircle(Cx, Cy, R, kDim, false);
        if (Face == FACE_DENIED || Face == FACE_ERROR)
        {
            glBegin(GL_LINES);
            glVertex2f(Cx - R, Cy - R); glVertex2f(Cx + R, Cy + R);
            glVertex2f(Cx - R, Cy + R); glVertex2f(Cx + R, Cy - R);
            glEnd();
        }
    }
    // Active gestures show as their bound labels to the right of the mark.
    const uint32_t Bits = State->Gestures.load(std::memory_order_relaxed);
    float X = Cx + R + 4.0f + 6.0f * kScale;
    const uint32_t kOrder[3] = { POINTER_GESTURE_EYEBROWS, POINTER_GESTURE_HEAD_LEFT, POINTER_GESTURE_HEAD_RIGHT };
    for (int i = 0; i < 3; i++)
    {
        if ((Bits & kOrder[i]) == 0 || State->GestureLabels[i][0] == '\0') continue;
        DrawText(State->GestureLabels[i], X, Cy, kBright);
        X += 14.0f * kScale;
    }
}

void OverlayDraw(const PointerState * State, int Width, int Height)
{
    if (State == nullptr || Width <= 0 || Height <= 0) return;

    // Save what glPushAttrib does not cover: the bound program and the matrices.
    GLint Program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)Width, (double)Height, 0.0, -1.0, 1.0); // y down, like the cursor
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glUseProgram(0);
    glViewport(0, 0, Width, Height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);

    const int Latched = State->LatchedZone.load(std::memory_order_relaxed);
    const float R = PointerStickRadius(Width, Height);
    const float Cx = (float)Width / 2.0f, Cy = (float)Height / 2.0f;

    for (int Zone = 0; Zone < POINTER_ZONE_COUNT; Zone++)
    {
        const float Alpha = Zone == Latched ? kBright : kDim;
        float X0, Y0, X1, Y1;
        PointerZoneRect(Zone, Width, Height, &X0, &Y0, &X1, &Y1);
        if (Zone != POINTER_ZONE_CENTRE)
        {
            DrawRect(X0, Y0, X1, Y1, Alpha);
            DrawText(State->Labels[Zone], (X0 + X1) / 2.0f, (Y0 + Y1) / 2.0f, Alpha);
        }
        else
        {
            DrawCircle(Cx, Cy, R, Alpha, false);
            DrawCircle(Cx, Cy, R * 0.1f, Alpha, false);
            DrawText(State->Labels[Zone], Cx, Cy - R + 7.0f * kScale, Alpha); // just inside the ring's top
        }
    }
    DrawFaceStatus(State, Height);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glUseProgram((GLuint)Program);
}
```

- [ ] **Step 3: Hook it into the render window**

In `SdlRenderWindow.h`, add `struct PointerState;` before the class, change the constructor to:

```cpp
    CSdlRenderWindow(SDL_Window * Window, SDL_GLContext Context, CGLContextObj Cgl, const PointerState * Pointer);
```

and add members after `m_Cgl`:

```cpp
    // The overlay's data, owned by main(); null under a failed shm setup. PJ64_OVERLAY=0
    // hides the overlay for a player who has memorised the layout.
    const PointerState * m_Pointer;
    bool m_OverlayHidden;
```

In `SdlRenderWindow.cpp`, add `#include "Overlay.h"` and `#include <Common/PointerState.h>`, change the constructor signature to match and add to its initialiser list after `m_Cgl(Cgl),`:

```cpp
    m_Pointer(Pointer),
    m_OverlayHidden(false),
```

and in its body:

```cpp
    const char * OverlayEnv = getenv("PJ64_OVERLAY");
    if (OverlayEnv != nullptr && strcmp(OverlayEnv, "0") == 0)
    {
        m_OverlayHidden = true;
    }
```

(add `#include <string.h>`). In `SwapWindow`, after `DumpFrame();` add:

```cpp
    // After the dump so PJ64_FRAME_DUMP measurements are of the game alone.
    if (m_Pointer != nullptr && !m_OverlayHidden && m_Pointer->OverlayWanted.load(std::memory_order_relaxed) != 0)
    {
        GLint Viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, Viewport);
        OverlayDraw(m_Pointer, (int)Viewport[2], (int)Viewport[3]);
    }
```

In `main.cpp`, change the construction to `CSdlRenderWindow renderWindow(window, context, cglContext, pointer);`.

Add `Overlay.cpp` to `FRONTEND_SRC` in the Makefile.

- [ ] **Step 4: Build and verify the overlay is invisible by default**

Run: `make -j8 all && make test`, then:

```sh
PJ64_FRAME_DUMP=/tmp/plain.ppm PJ64_FRAME_DUMP_AT=400 perl -e 'alarm 20; exec @ARGV' -- ./Bin/macOS/Project64 <SM64 ROM> || true
```

Expected: the frame dump works as before (see AGENTS for the 89 % figure) and, with the keyboard file active, no `pointer-selftest` or overlay is involved.

- [ ] **Step 5: Verify the overlay draws with a mouse layout**

Run: `make run rom=<SM64 ROM> input=Config/mouse/sm64.yaml`
Expected, by eye: faint ring and dead-zone circle at the centre labelled `A`, twelve faint cells with labels (`L`, `C^`, `D^`, `St` across the top), the game visible through them. Click in a cell: it turns bright while held. Moving the cursor around the ring moves Mario. Run `PJ64_OVERLAY=0 make run ...` and confirm nothing is drawn but the mouse still works.

- [ ] **Step 6: Verify the face indicator**

Run: `make run rom=<SM64 ROM> input=Config/mouse/sm64.yaml face=1`
Expected: a hollow circle in the bottom-left until your face is found, then filled; raise your brows and `Z` appears beside it.

- [ ] **Step 7: Commit**

```bash
git add Source/Project64-sdl/Overlay.h Source/Project64-sdl/Overlay.cpp Source/Project64-sdl/SdlRenderWindow.h Source/Project64-sdl/SdlRenderWindow.cpp Source/Project64-sdl/main.cpp Makefile
git commit -m "Draw the pointer overlay before each present

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: Documentation and final verification

**Files:**
- Modify: `README.md` (after the "Input mapping" section, line 58-65; the "What works" paragraph, line 69-71)
- Modify: `AGENTS.md` (Commands block, Architecture, Traps)

- [ ] **Step 1: README**

Insert after the "Input mapping" section:

```markdown
## Playing with a mouse

A one-button mouse can drive every N64 control. Pick a layout under `Config/mouse/` and
run with it:

```sh
make run rom=Roms/sm64.z64 input=Config/mouse/sm64.yaml          # mouse only
make run rom=Roms/sm64.z64 input=Config/mouse/sm64.yaml face=1   # plus face gestures
```

The window is a 4x4 grid drawn faintly over the game. The inner block is the stick: the
cursor's distance from the centre is the tilt, and a click there is one button (A in the
Mario layout). The twelve outer cells are buttons: hover and click, hold to hold. The zone
under the cursor when you press stays pressed until you release, so "click A, then tilt"
is a running jump. `PJ64_OVERLAY=0` hides the drawing.

With `face=1` (or `--face`) the webcam adds three held buttons: raising both eyebrows,
turning your head left, and turning it right. macOS asks for camera permission once,
attributed to the terminal or IDE you launched from; if it was denied before, allow it in
System Settings > Privacy & Security > Camera. Frames stay in memory and are never saved,
shown, or logged. `PJ64_FACE_DEBUG=1` prints the two measures once a second, and
`PJ64_FACE_BROW` / `PJ64_FACE_YAW` override the thresholds (defaults 0.035 and 0.25). A
dot in the bottom-left corner shows the tracker: hollow while looking for a face, filled
while tracking, crossed when the camera is unavailable.

Layout files use two more binding forms, `{zone: top4}` (cells `top1`-`top4`, `left2`,
`left3`, `right2`, `right3`, `bottom1`-`bottom4`, `centre`) and
`{face: eyebrows|head-left|head-right}`, plus `{stick: pointer}` for `Stick`. Shipped:
`sm64.yaml`, `goldeneye.yaml`, `mk64.yaml`. Everything without the camera keeps working
when the camera is denied or absent.
```

Change the "What works" sentence `with keyboard input through SDL3; a gamepad works after swapping in the commented block in `Config/input.yaml`.` to `with keyboard input through SDL3; a gamepad works after swapping in the commented block in `Config/input.yaml`, and a one-button mouse with optional face gestures works with a layout from `Config/mouse/` (see "Playing with a mouse").`

- [ ] **Step 2: AGENTS**

In the Commands block add after `make input-config-test`:

```
make pointer-layout-test                 # geometry tests for the pointer grid
make face-gesture-test                   # classifier tests for the face gestures
make pointer-selftest rom=Roms/game.z64  # prove the injected-pointer path end to end
make run rom=Roms/game.z64 input=Config/mouse/sm64.yaml face=1
```

Add to the paragraph after the block: `` `make pointer-layout-test` and `make face-gesture-test` are pure unit tests. `make pointer-selftest` needs a window server and takes ~30 s. ``

In Architecture, after the "Input bindings are data" paragraph, add:

```markdown
**Mouse and face input go through one shared struct.** `Source/Common/PointerState.h` is
a seqlock over `shm_open`, created by the frontend and passed to the input plugin by
descriptor in `PJ64_POINTER_FD`, the same way the grid passes keys. The frontend's main
loop samples the mouse (SDL3's mouse state functions are main-thread only) and publishes
it; `Source/Project64-sdl/FaceTracker.mm` runs AVFoundation and Vision on a private queue
and writes three gesture bits through `FaceGestures.{h,cpp}`; the plugin evaluates zones,
gestures and the pointer stick in `GetKeys` with the pure geometry in
`Source/Common/PointerLayout.h`, and writes labels and the latched zone back for
`Overlay.cpp`, which draws in `CSdlRenderWindow::SwapWindow` before the flush. Layouts are
the `{zone:}`, `{face:}` and `{stick: pointer}` YAML forms in `Config/mouse/`.
```

In Traps, add:

```markdown
- **SDL3 mouse state is main-thread only.** `SDL_GetMouseState` and friends must stay in
  `main.cpp`'s loop; the plugin reads the published `PointerState` instead.
- **The camera prompt is attributed to the launcher.** The binary is not an app bundle, so
  macOS asks for camera access on behalf of the terminal or IDE. A past denial there makes
  the tracker report `denied` without a new prompt; the fix is in System Settings.
- **`Config/input.yaml` must stay keyboard-active.** A mouse block there would replace the
  keyboard bindings under the one-binding rule and break the grid's keyboard broadcast and
  `make grid-selftest`. Mouse layouts live in `Config/mouse/`.
```

- [ ] **Step 3: Full verification pass**

Run, in order, and record the results in the commit message body:

```sh
make clean && make -j8 all
make test
make input-config-test
make pointer-layout-test
make face-gesture-test
make pointer-selftest rom=<ROM>
make grid-selftest rom=<ROM>
PJ64_INPUT_YAML=/tmp/bad.yaml ./Bin/macOS/Project64 --version   # still runs; version only
```

Then the manual checks from the spec's Part 6: Mario 64 (run, jump while running, long jump with brows plus click while tilted, camera rotate from `left2`) and Mario Kart 64 (accelerate while steering, hop on brows). GoldenEye: confirm `Config/mouse/goldeneye.yaml` loads without an error line; playtesting waits on the stall recorded in the black-screen investigation.

- [ ] **Step 4: Commit**

```bash
git add README.md AGENTS.md
git commit -m "Document playing with a mouse and face gestures

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Self-review against the spec

- Part 1 (architecture): Task 1 (struct, geometry), Task 3 (plugin evaluation, labels), Task 4 (frontend sampling, descriptor in the environment). Precedence rule: Task 3 sets `StickFromKeys` so the gamepad cannot overwrite a pointer stick.
- Part 2 (scheme): geometry and constants in Task 1 with boundary tests; latch in Task 3; YAML forms and the three shipped files in Task 2; `input.yaml` untouched.
- Part 3 (tracker): classifier rules in Task 5 with tests for each; capture, measures, permission, privacy, debug print and threshold overrides in Task 6; `--face` and `PJ64_FACE` in Task 6.
- Part 4 (overlay): Task 7, drawn after the frame dump, labels from the plugin, `PJ64_OVERLAY=0`, face status mark and gesture labels.
- Part 5 (errors): YAML rejections in Task 2; unmapped descriptor in Task 3; camera failures in Task 6.
- Part 6 (build, tests, docs): Makefile changes spread across Tasks 1, 4, 5, 6, 7; the three unit test targets, the self-test script, `run` arguments, config install; README and AGENTS in Task 8; the manual checks and the GoldenEye caveat in Task 8.
- Names used across tasks: `PointerState`, `PointerSample`, `PointerPublish`, `PointerSnapshot`, `PointerLayoutEvaluate`, `PointerZoneRect`, `PointerStickRadius`, `PointerZoneName`, `PointerZoneFromName`, `PointerGestureFromName`, `PointerGestureIndex`, `POINTER_ZONE_*`, `POINTER_GESTURE_*`, `FACE_*`, `Binding::Kind::{Zone,Face,Pointer}`, `InputConfig::{ControlLabel,UsesPointer,PointerLabels}`, `GestureSample`, `GestureThresholds`, `GestureClassifier::{Update,BrowBaseline,YawBaseline}`, `FaceTrackerStart`, `FaceTrackerStop`, `OverlayDraw`, `CreatePointerState`, `PublishMouse`, `PointerSelftestReport` — each defined once and used with the same signature everywhere above.
