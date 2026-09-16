# Resizable Game Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The emulator's single-game window can be resized, maximised and taken full screen, with the game and mouse panel scaling and keeping their shape, and panel clicks landing on the right slot at any size.

**Architecture:** The GL surface is pinned at the launch size with `CGLSetParameter(kCGLCPSurfaceBackingSize)`, so the video plugin, overlay and frame dump keep rendering exactly as today and the macOS window server scales the picture. The window gets an aspect lock and a half-size minimum. `PublishMouse` maps the cursor from window points back into launch-size coordinates through a new pure function, `PointerFitToBase`, so every downstream consumer stays in the coordinate space its constants were tuned for.

**Tech Stack:** C++17, SDL 3.4 (`SDL_SetWindowResizable`, `SDL_SetWindowAspectRatio`, `SDL_SetWindowMinimumSize`), CGL (`OpenGL/OpenGL.h`), Markdown.

**Spec:** `Docs/superpowers/specs/2026-09-16-resizable-window-design.md`

## Global Constraints

- Only single-game windows become resizable. Grid tiles (`TileMode`) and the grid's key strip stay fixed.
- The GL surface is pinned at the launch size `TileRect.w` x `TileRect.h` (640x480, or 640x640 with the panel) with `CGLSetParameter(cgl, kCGLCPSurfaceBackingSize, {w, h})` then `CGLEnable(cgl, kCGLCESurfaceBackingSize)`.
- If either CGL call fails, the window stays fixed-size and one stderr line names the call and its `CGLError`; the SDL resize calls happen only after both CGL calls succeed.
- Aspect ratio locked to `w / h` (both limits equal); minimum size `w / 2` x `h / 2`.
- No handler for `SDL_EVENT_WINDOW_RESIZED` is added. No change to `Source/Project64-video/`, `Overlay.cpp`, `SdlRenderWindow.cpp`, or any GL call on either thread.
- `PointerFitToBase` returns true when the cursor is over the picture, edges included; false over a bar or when any size is not positive. On a bar the written position is clamped to the picture's edge.
- An injected sample (`PJ64_POINTER_INJECT`) is already in base coordinates and is published unmapped.
- The screen is never captured. Visual confirmation is a manual check by the human (Task 4).
- Every file touched here is LF (`file` confirms `main.cpp`, `PointerLayout.h`, `PointerLayoutTest.cpp`, `AGENTS.md`, `Docs/UserGuide.md`); check with `file <path>` before editing anything else.
- `make test` output stays one version line plus four `ok:` lines; `make unit-test` still ends `ok: wizard screenshots` then `ok: unit tests`.
- Never run `make clean` (it deletes `Bin/macOS`, including player saves in `Bin/macOS/Save`).
- Commit messages end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
- Runs that load a ROM set `PJ64_FACE=0`. On this machine a Super Mario 64 ROM is at `~/Downloads/N64ROMsPACK/super_mario_64_usa.z64`; a `super_mario_64_usa.yaml` mouse layout sits beside it, so launching it gives the 640x640 panel window.

---

## File structure

| File | Responsibility |
|---|---|
| `Source/Common/PointerLayout.h` | Gains `PointerFitToBase`, the pure window-to-base cursor mapping. |
| `Source/Project64-sdl/PointerLayoutTest.cpp` | Gains the mapping's cases. |
| `Source/Project64-sdl/main.cpp` | Gains `MakeWindowScalable` (called once after context creation, single-game only); `PublishMouse` takes the launch size and maps the cursor. |
| `Docs/UserGuide.md` | Sections 3, 4 and 7 describe the resizable window. |
| `AGENTS.md` | Architecture paragraph and two traps. |

---

### Task 1: The cursor mapping

**Files:**
- Modify: `Source/Common/PointerLayout.h` (append after `PointerGateStick`, the last function)
- Test: `Source/Project64-sdl/PointerLayoutTest.cpp` (insert before the `// Gesture names.` block near the end of `main`)

**Interfaces:**
- Produces, for Task 2: `inline bool PointerFitToBase(float WinW, float WinH, float BaseW, float BaseH, float X, float Y, float * OutX, float * OutY)` in `Source/Common/PointerLayout.h`.

- [ ] **Step 1: Write the failing tests**

In `Source/Project64-sdl/PointerLayoutTest.cpp`, immediately before the line `    // Gesture names.`, insert:

```cpp
    // A window resized or taken full screen shows the launch-size picture scaled to fit,
    // centred, keeping its shape; PointerFitToBase maps the cursor back into that picture.
    {
        float Bx = -1.0f, By = -1.0f;

        // Same size: the identity, inside.
        CHECK(PointerFitToBase(640, 640, 640, 640, 123.0f, 456.0f, &Bx, &By));
        CHECK(Bx == 123.0f && By == 456.0f);

        // Twice the size.
        CHECK(PointerFitToBase(1280, 1280, 640, 640, 640.0f, 1000.0f, &Bx, &By));
        CHECK(Bx == 320.0f && By == 500.0f);

        // Half the size, the minimum.
        CHECK(PointerFitToBase(320, 320, 640, 640, 160.0f, 250.0f, &Bx, &By));
        CHECK(Bx == 320.0f && By == 500.0f);

        // Full screen 1920x1080 over 640x640: scale 1.6875, a 1080-wide picture, bars of 420
        // on each side. The picture's corners are inside, edges included.
        CHECK(PointerFitToBase(1920, 1080, 640, 640, 420.0f, 0.0f, &Bx, &By));
        CHECK(Bx == 0.0f && By == 0.0f);
        CHECK(PointerFitToBase(1920, 1080, 640, 640, 1500.0f, 1080.0f, &Bx, &By));
        CHECK(Bx == 640.0f && By == 640.0f);

        // On the left bar: outside, and clamped to the picture's left edge.
        CHECK(!PointerFitToBase(1920, 1080, 640, 640, 100.0f, 540.0f, &Bx, &By));
        CHECK(Bx == 0.0f && By == 320.0f);

        // On the right bar: outside, clamped to the right edge.
        CHECK(!PointerFitToBase(1920, 1080, 640, 640, 1800.0f, 540.0f, &Bx, &By));
        CHECK(Bx == 640.0f && By == 320.0f);

        // A 640x480 game-only picture in a 1920x1080 screen: scale 2.25, bars of 240.
        CHECK(PointerFitToBase(1920, 1080, 640, 480, 960.0f, 540.0f, &Bx, &By));
        CHECK(Bx == 320.0f && By == 240.0f);

        // Sizes that are not positive map nothing.
        CHECK(!PointerFitToBase(0, 640, 640, 640, 10.0f, 10.0f, &Bx, &By));
        CHECK(!PointerFitToBase(640, -1, 640, 640, 10.0f, 10.0f, &Bx, &By));
        CHECK(!PointerFitToBase(640, 640, 0, 640, 10.0f, 10.0f, &Bx, &By));
        CHECK(!PointerFitToBase(640, 640, 640, -5, 10.0f, 10.0f, &Bx, &By));
    }

```

Every expected value is exact in binary floating point (the scales 2, 0.5, 1.6875 and 2.25 are all exact), so `==` is correct here.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make pointer-layout-test`
Expected: compile error, `use of undeclared identifier 'PointerFitToBase'`.

- [ ] **Step 3: Write the implementation**

Append to the end of `Source/Common/PointerLayout.h`:

```cpp

// The single-game window can be resized or taken full screen, but the GL surface stays at
// the launch size and the window server scales it to fit, centred, keeping its shape: bars
// appear on the two long sides when the window's shape differs, as in full screen. Everything
// above works in launch-size pixels, so the frontend maps the cursor back through this.
//
// Maps (X, Y) in a WinW x WinH window onto the BaseW x BaseH picture. Writes the position
// in base coordinates and returns true when the cursor is over the picture, edges included.
// Over a bar it returns false and writes the position clamped to the picture's edge, so a
// cursor leaving through a bar does not jump. A size that is not positive returns false and
// writes (X, Y) unchanged.
inline bool PointerFitToBase(float WinW, float WinH, float BaseW, float BaseH,
                             float X, float Y, float * OutX, float * OutY)
{
    if (WinW <= 0.0f || WinH <= 0.0f || BaseW <= 0.0f || BaseH <= 0.0f)
    {
        *OutX = X;
        *OutY = Y;
        return false;
    }
    const float ScaleX = WinW / BaseW, ScaleY = WinH / BaseH;
    const float Scale = ScaleX < ScaleY ? ScaleX : ScaleY;
    const float OriginX = (WinW - BaseW * Scale) * 0.5f;
    const float OriginY = (WinH - BaseH * Scale) * 0.5f;
    const float Bx = (X - OriginX) / Scale;
    const float By = (Y - OriginY) / Scale;
    const bool Over = Bx >= 0.0f && Bx <= BaseW && By >= 0.0f && By <= BaseH;
    *OutX = Bx < 0.0f ? 0.0f : (Bx > BaseW ? BaseW : Bx);
    *OutY = By < 0.0f ? 0.0f : (By > BaseH ? BaseH : By);
    return Over;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make pointer-layout-test`
Expected: last line `ok: pointer layout`, no `FAIL` lines, no compiler warnings.

- [ ] **Step 5: Commit**

```bash
git add Source/Common/PointerLayout.h Source/Project64-sdl/PointerLayoutTest.cpp
git commit -m "Add PointerFitToBase: map a cursor in a resized window back onto the launch-size picture

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Resizable window and the mapped cursor

**Files:**
- Modify: `Source/Project64-sdl/main.cpp` — `PublishMouse` (about line 174), a new `MakeWindowScalable` defined just above `PublishMouse`, the call after `CGLContextObj cglContext = CGLGetCurrentContext();` (about line 376), and the `PublishMouse` call in the main loop (about line 432).
- Test: `make frontend`, `make test`, `make unit-test`, `make pointer-selftest`, `make face-selftest`, `make grid-selftest`, a frame-dump run.

**Interfaces:**
- Consumes: `PointerFitToBase` from Task 1 (`Source/Common/PointerLayout.h`, already included by `main.cpp`).
- Produces: nothing other tasks call.

- [ ] **Step 1: Record the baseline**

Run:

```bash
make -j8 all 2>&1 | tail -1
make test
PJ64_FACE=0 make pointer-selftest rom=~/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>&1 | tail -3
```

Expected: `make test` prints one version line and four `ok:` lines; the pointer self-test ends with its `ok:` line. If either fails before any change, stop and report BLOCKED with the output.

- [ ] **Step 2: Add `MakeWindowScalable`**

In `Source/Project64-sdl/main.cpp`, immediately above the comment `// Main thread only: SDL3 documents SDL_GetMouseState as main-thread only.` that precedes `PublishMouse`, insert:

```cpp
// Main thread only, once, right after the GL context is created. Lets the user resize the
// window, maximise it or take it full screen without the renderer ever learning: the GL
// surface is pinned at the launch size (W x H) and the window server scales it to fit,
// centred, keeping its shape. The video plugin, the overlay and the frame dump therefore
// keep working in launch-size pixels, and PublishMouse maps the cursor back into them.
// Nothing here or anywhere reacts to a resize event, and nothing may: resizing the drawable
// or calling the plugin's ChangeSize from the main thread is the deadlock described at
// CSdlRenderWindow::GfxThreadInit. If either CGL call is refused the window stays fixed,
// because a resizable window over an unpinned surface would show a stale picture.
static void MakeWindowScalable(SDL_Window * Window, CGLContextObj Cgl, int W, int H)
{
    if (Cgl == nullptr)
    {
        fprintf(stderr, "window stays fixed-size: no CGL context to pin\n");
        return;
    }
    const GLint Backing[2] = {W, H};
    CGLError Err = CGLSetParameter(Cgl, kCGLCPSurfaceBackingSize, Backing);
    if (Err != kCGLNoError)
    {
        fprintf(stderr, "window stays fixed-size: CGLSetParameter(kCGLCPSurfaceBackingSize): %s\n", CGLErrorString(Err));
        return;
    }
    Err = CGLEnable(Cgl, kCGLCESurfaceBackingSize);
    if (Err != kCGLNoError)
    {
        fprintf(stderr, "window stays fixed-size: CGLEnable(kCGLCESurfaceBackingSize): %s\n", CGLErrorString(Err));
        return;
    }
    const float Aspect = (float)W / (float)H;
    SDL_SetWindowAspectRatio(Window, Aspect, Aspect);
    SDL_SetWindowMinimumSize(Window, W / 2, H / 2);
    SDL_SetWindowResizable(Window, true);
}

```

The limits are set before the window becomes resizable, so no resize can happen without them.

- [ ] **Step 3: Call it for single-game windows**

Find:

```cpp
    CGLContextObj cglContext = CGLGetCurrentContext();
    SDL_GL_MakeCurrent(window, nullptr);
```

and replace with:

```cpp
    CGLContextObj cglContext = CGLGetCurrentContext();
    // Grid tiles are laid out by the grid host and stay fixed; a single game's window scales.
    if (!TileMode)
    {
        MakeWindowScalable(window, cglContext, TileRect.w, TileRect.h);
    }
    SDL_GL_MakeCurrent(window, nullptr);
```

`TileRect` holds the created window's size at this point: the panel height was already added before `SDL_CreateWindow`.

- [ ] **Step 4: Map the cursor in `PublishMouse`**

Replace the whole `PublishMouse` function (the comment line above it stays) with:

```cpp
static void PublishMouse(PointerState * State, SDL_Window * Window, int BaseW, int BaseH, const PointerSample * Inject)
{
    // Everything downstream works in launch-size pixels (see MakeWindowScalable), so the
    // sample always carries the launch size, never the window's current one.
    PointerSample S;
    S.W = BaseW;
    S.H = BaseH;
    if (Inject != nullptr)
    {
        // PJ64_POINTER_INJECT coordinates are already in launch-size pixels.
        S.X = Inject->X;
        S.Y = Inject->Y;
        S.Inside = true;
        S.Button = Inject->Button;
    }
    else
    {
        float WinX = 0.0f, WinY = 0.0f;
        const SDL_MouseButtonFlags Buttons = SDL_GetMouseState(&WinX, &WinY);
        int WinW = 0, WinH = 0;
        SDL_GetWindowSize(Window, &WinW, &WinH);
        // Over a full-screen bar the cursor is outside the picture: no zone, neutral stick.
        const bool OverPicture = PointerFitToBase((float)WinW, (float)WinH, (float)BaseW, (float)BaseH,
                                                  WinX, WinY, &S.X, &S.Y);
        S.Inside = SDL_GetMouseFocus() == Window && OverPicture;
        S.Button = (Buttons & SDL_BUTTON_LMASK) != 0;
    }
    PointerPublish(State, S);
}
```

And in the main loop change:

```cpp
            PublishMouse(pointer, window, injecting ? &inject : nullptr);
```

to:

```cpp
            PublishMouse(pointer, window, TileRect.w, TileRect.h, injecting ? &inject : nullptr);
```

Grid tiles pass their own fixed size as the base, where the mapping is the identity.

- [ ] **Step 5: Build and run the suites**

Run:

```bash
make -j8 all 2>&1 | grep -E 'error|main\.cpp.*warning' ; make -j8 all 2>&1 | tail -1
make test
make unit-test 2>&1 | tail -2
PJ64_FACE=0 make pointer-selftest rom=~/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>&1 | tail -3
PJ64_FACE=0 make face-selftest rom=~/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>&1 | tail -3
PJ64_FACE=0 make grid-selftest rom=~/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>&1 | tail -3
```

Expected: no errors and no new warnings from `main.cpp`; `make test` one version line plus four `ok:` lines; `make unit-test` ends `ok: wizard screenshots` then `ok: unit tests`; each self-test ends with its `ok:` line. The grid self-test takes about 40 s, the pointer one about 30 s.

- [ ] **Step 6: Prove the render path is unchanged and nothing deadlocks**

Run (scratch output under the session scratchpad, `$SP` below; any writable scratch directory outside the repository works):

```bash
SP=/private/tmp/claude-501/-Users-felipe-dos-santos-code-theirs-project64/234f7537-50ac-49e0-96a3-a11f83382bb5/scratchpad
rm -f $SP/frame.ppm
PJ64_FACE=0 PJ64_FRAME_DUMP=$SP/frame.ppm PJ64_FRAME_DUMP_AT=400 \
  perl -e 'alarm 25; exec @ARGV' -- ./Bin/macOS/Project64 ~/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>&1 | grep -E 'input layout|window stays' ; true
python3 - "$SP/frame.ppm" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
parts = d.split(b'\n', 3)
w, h = map(int, parts[1].split())
px = parts[3]
nonblack = sum(1 for i in range(0, w * h * 3, 3) if px[i] or px[i + 1] or px[i + 2])
print(f"{w}x{h} {100.0 * nonblack / (w * h):.1f}% non-black")
EOF
```

Expected: `input layout: …/super_mario_64_usa.yaml`, no `window stays fixed-size` line, and `640x480` with more than 80% non-black (a healthy boot measures about 89%).

- [ ] **Step 7: Commit**

```bash
git add Source/Project64-sdl/main.cpp
git commit -m "Make the single-game window resizable: pin the GL surface, lock the aspect, map the cursor back

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Document the resizable window

**Files:**
- Modify: `Docs/UserGuide.md` (sections 3, 4 and 7)
- Modify: `AGENTS.md` (Architecture, Traps)
- Test: `make wizard-screenshots-check`, a grep

**Interfaces:**
- Consumes: the behaviour from Task 2 (`MakeWindowScalable`, `PublishMouse(State, Window, BaseW, BaseH, Inject)`) and `PointerFitToBase` from Task 1.

- [ ] **Step 1: The guide, section 3**

In `Docs/UserGuide.md`, replace:

```
or, once built, `./Bin/macOS/Project64 Roms/super_mario_64.z64`. The window is 640x480.
Close it to quit. Saves go to `Bin/macOS/Save/`.
```

with:

```
or, once built, `./Bin/macOS/Project64 Roms/super_mario_64.z64`. The window opens at
640x480. Drag a corner to resize it, down to half that size, or use the green button for
full screen. The picture keeps its shape, with black bars at the sides in full screen, and
is scaled up from 640x480 rather than drawn at the larger size, so it looks softer when big.
Close the window to quit. Saves go to `Bin/macOS/Save/`.
```

- [ ] **Step 2: The guide, section 4**

Replace:

```
Each game is its own process, so one crashing cannot take the others down. Per-game
layouts (section 9) are ignored in the grid.
```

with:

```
Each game is its own process, so one crashing cannot take the others down. Per-game
layouts (section 9) are ignored in the grid, and the tiles cannot be resized.
```

- [ ] **Step 3: The guide, section 7**

Replace:

```
The window becomes 640x640: the game in the top 640x480, a panel in the 160 rows below.
```

with:

```
The window opens at 640x640: the game in the top 640x480, a panel in the 160 rows below.
It resizes like any game window (section 3) and the panel scales with the game; the pixel
distances below are at that starting size and grow with the window.
```

- [ ] **Step 4: AGENTS.md, Architecture**

Immediately after the paragraph that begins `**GL belongs to the emulation thread.**` and ends `Do not "simplify" them back.`, insert a blank line and:

```
**The single-game window scales; it never re-renders at a new size.** `MakeWindowScalable`
in `Source/Project64-sdl/main.cpp` pins the GL surface at the launch size with
`CGLSetParameter(kCGLCPSurfaceBackingSize)` and only then makes the window resizable, with
its aspect locked and a half-size minimum; the window server scales the picture to fit,
with bars in full screen. The video plugin, the overlay and `PJ64_FRAME_DUMP` never see the
window's size, and `PublishMouse` maps the cursor back through `PointerFitToBase`
(`Source/Common/PointerLayout.h`) so the published sample is always in launch-size pixels.
Grid tiles stay fixed. Design: `Docs/superpowers/specs/2026-09-16-resizable-window-design.md`.
```

- [ ] **Step 5: AGENTS.md, Traps**

Append these two bullets at the end of the `## Traps` list, after the bullet that begins `- **The user guide's tables are copied facts.**` (the list's last bullet, immediately before `## Design docs`):

```
- **A window resize is not a render event.** Never resize the drawable, call the video
  plugin's `ChangeSize`, or do GL work in response to `SDL_EVENT_WINDOW_RESIZED`: the
  surface is pinned on purpose and the SDL GL calls deadlock (see "GL belongs to the
  emulation thread"). If the backing-size pin is refused, the window stays fixed and stderr
  says `window stays fixed-size`.
- **Window coordinates are not layout coordinates.** Anything that reads the cursor must
  use the published `PointerSample`, which is in launch-size pixels, never
  `SDL_GetWindowSize` or `SDL_GetMouseState` directly: in a resized or full-screen window
  those are scaled and offset by the bars, and the panel's pixel constants would land on
  the wrong slot.
```

- [ ] **Step 6: Check**

Run:

```bash
make wizard-screenshots-check
grep -n 'MakeWindowScalable\|PointerFitToBase' AGENTS.md Source/Project64-sdl/main.cpp Source/Common/PointerLayout.h | head
grep -n '640x480\|640x640' Docs/UserGuide.md
```

Expected: `ok: wizard screenshots`; the names appear in all three files; every remaining `640x480`/`640x640` in the guide is a starting size or the frame dump's size.

- [ ] **Step 7: Commit**

```bash
git add Docs/UserGuide.md AGENTS.md
git commit -m "Document the resizable game window in the user guide and AGENTS.md

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Manual acceptance with the human

**Files:** none modified. This task is run by whoever coordinates the work, not delegated: it needs a person looking at the window, because the screen is never captured.

- [ ] **Step 1: Ask the human to run the mouse layout and resize**

Ask them to run `! PJ64_FACE=0 ./Bin/macOS/Project64 ~/Downloads/N64ROMsPACK/super_mario_64_usa.z64` and to:

1. drag the window to about twice its size, then hover and click a few labelled panel slots and the game image (A in the Mario layout): the lit zone must follow the cursor, and the button that slot is labelled with must press;
2. go full screen with the green button and repeat, including moving over a black bar (no zone lit there);
3. shrink it to the minimum and repeat;
4. launch a game with no layout (for example `! PJ64_FACE=0 PJ64_INPUT_YAML=Config/input.yaml ./Bin/macOS/Project64 ~/Downloads/N64ROMsPACK/super_mario_64_usa.z64`) and confirm it resizes the same way.

- [ ] **Step 2: Record the result**

If all four hold, append to the spec a `## Result` section stating the date, the commits, and that the manual check passed at 2x, full screen and minimum size. Commit it:

```bash
git add Docs/superpowers/specs/2026-09-16-resizable-window-design.md
git commit -m "Record the resizable window's manual check

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

If any fails, record exactly what was seen and treat it as a bug in Task 1 or Task 2, not a documentation change.
