# Resizable game window design

## Goal

The emulator's single-game window can be resized, maximised and taken full screen. The game,
and the mouse panel below it when a layout uses one, scale with the window and keep their
shape. Clicking a panel slot or steering with the game image works at any size.

## Context

The video plugin chooses its render size once, when the ROM opens, and never learns the
window's size afterwards: `ChangeSize()` in `Source/Project64-video/Main.cpp` reads
`PJ64_TILE_SIZE` or the fixed resolution table, and the viewport offset from
`PJ64_VIEWPORT_OFFSET`. Presentation bypasses SDL: `CSdlRenderWindow::SwapWindow` calls
`CGLFlushDrawable` on the emulation thread, and nothing ever sends the GL context the
update a resized view needs. `AGENTS.md` records why the SDL equivalents stay out of that
path: they are main-thread only and deadlock the first swap.

The panel is already parametric. `PublishMouse` in `Source/Project64-sdl/main.cpp` publishes
the live `SDL_GetWindowSize` with every sample, and every function in
`Source/Common/PointerLayout.h` takes `W,H`. But its geometry is in window pixels with fixed
constants (`POINTER_PANEL_HEIGHT` 160, the slot sizes, the 160 px full-tilt radius, the
24 px flick gate), so it is only right in the launch-size coordinate space. The overlay
finds the game rectangle from the GL viewport, and the frame dump sizes itself from the
viewport; both see the render size, not the window's.

A spike on 2026-09-16 pinned the GL surface of a running Super Mario 64 (with its mouse
layout, 640x640) at the launch size with `CGLSetParameter(kCGLCPSurfaceBackingSize)` and
`CGLEnable(kCGLCESurfaceBackingSize)`, made the window resizable and locked its aspect
ratio. Both calls returned `kCGLNoError`, the frame dump still read 640x480 at frame 400,
nothing deadlocked, and a manual run showed the picture scaling smoothly while dragging a
corner and, in full screen, filling the height with black bars at the sides.

## Non-goals

- Rendering at the window's resolution. The picture is scaled by the window server from the
  launch size, so it is soft when enlarged. Native re-rendering needs a resize signal from
  the main thread into the emulation thread's `ChangeSize`/`InitGfx`, touches the plugin,
  the overlay's origin check and the panel's pixel constants, and reopens the deadlock the
  presentation path was built to avoid.
- High-density (Retina) rendering. The surface stays point-sized, as today.
- Grid tiles and the grid's key strip. The grid host lays tiles out; they stay fixed.
- The binding wizard window, whose fixed 800x640 layout is a separate, documented decision.
- A remembered window size or position between runs.

## Design

### Window setup

In `Source/Project64-sdl/main.cpp`, only when the process is not a grid tile, immediately
after the GL context is created and its `CGLContextObj` captured on the main thread:

- Pin the surface at the launch size `TileRect.w` x `TileRect.h` (640x480, or 640x640 with
  the panel): `CGLSetParameter(cgl, kCGLCPSurfaceBackingSize, {w, h})` then
  `CGLEnable(cgl, kCGLCESurfaceBackingSize)`.
- Make the window resizable (`SDL_SetWindowResizable`), lock its aspect ratio to
  `w / h` (`SDL_SetWindowAspectRatio` with both limits equal), and set a minimum size of
  half the launch size (`SDL_SetWindowMinimumSize(w / 2, h / 2)`).

If either CGL call fails, the window stays fixed-size and one stderr line names the call
and its `CGLError`; the game runs exactly as today. A resizable window over an unpinned surface would
show a stale or distorted picture, so the three SDL calls happen only after both CGL calls
succeed.

Nothing else in the render path changes. The plugin, the overlay, `SwapWindow` and the
frame dump keep working in the launch size, which they cannot tell apart from today.
No handler for `SDL_EVENT_WINDOW_RESIZED` is added: there is nothing to tell.

### Mouse mapping

A pure function joins `Source/Common/PointerLayout.h`:

```cpp
// Maps a cursor position in a window of WinW x WinH points onto a picture of
// BaseW x BaseH that the window server scales to fit the window, centred, keeping its
// shape (bars on the two long sides when the shapes differ, as in full screen).
// Writes the position in base coordinates and returns true when the cursor is over the
// picture, edges included; returns false over a bar or when any size is not positive.
inline bool PointerFitToBase(float WinW, float WinH, float BaseW, float BaseH,
                             float X, float Y, float * OutX, float * OutY);
```

Scale is `min(WinW / BaseW, WinH / BaseH)`; the picture's origin is
`((WinW - BaseW * Scale) / 2, (WinH - BaseH * Scale) / 2)`; the result is
`(X - OriginX) / Scale`, `(Y - OriginY) / Scale`. On the bars the mapped position is still
written, clamped to the picture's edge, so a cursor leaving through a bar does not jump.

`PublishMouse` keeps the launch size (the `TileRect` it was created with, passed in) and
publishes `W,H` as that size. For a real cursor it maps `SDL_GetMouseState`'s position
through `PointerFitToBase` against the live `SDL_GetWindowSize`, and `Inside` becomes
"the window has mouse focus and the cursor is over the picture". An injected sample
(`PJ64_POINTER_INJECT`) is already in base coordinates and is published unmapped. Every
consumer downstream, the plugin's `GetKeys`, the flick gate and the overlay's lit zone,
keeps working in launch-size pixels, where its constants were tuned.

Grid tiles, which are not resizable, go through the same code with window and base sizes
equal, where the mapping is the identity.

## Testing

- `make pointer-layout-test` (`Source/Project64-sdl/PointerLayoutTest.cpp`) gains cases for
  `PointerFitToBase`: equal sizes map identically; a 1280x1280 window maps (640, 1000) to
  (320, 500); a full-screen 1920x1080 window over a 640x640 base puts bars of 420 points on
  each side, so (420, 0) maps to (0, 0) inside, (1500, 1080) to (640, 640) inside, and
  (100, 540) is outside and clamps to (0, 320); a 320x320 window maps (160, 250) to
  (320, 500); zero or negative sizes return false.
- `make pointer-selftest`, `make face-selftest` and `make grid-selftest` must still pass
  unchanged: injection bypasses the mapping, and tiles map identically.
- `make test` and the frame-dump check still hold: a Super Mario 64 dump at frame 400 is
  640x480 with more than 80 percent non-black pixels.
- One manual check, because the screen is never captured: with the Super Mario 64 mouse
  layout, drag the window to about twice its size and click the A slot, then go full screen
  and click it again. The A button must press both times, and the lit zone must follow the
  cursor. Also confirm a plain keyboard game (no layout) resizes the same way.

## Documentation

- `Docs/UserGuide.md` section 3: the window starts at 640x480 and can be resized or taken
  full screen; the picture keeps its shape (bars in full screen) and is scaled, not
  rendered at the larger size. Section 7: the 640x640 figure is the starting size; the
  panel scales with the game.
- `AGENTS.md`, architecture: one paragraph saying the single-game window's GL surface is
  pinned at the launch size and the window server scales it, and that `PublishMouse` maps
  the cursor back into launch-size coordinates. Traps: never resize the drawable, call the
  plugin's `ChangeSize`, or add GL work in response to a window resize event; and any new
  consumer of window coordinates must use the published base-size sample, not
  `SDL_GetWindowSize`.
- The wizard's screenshots do not change.

## Open risks

- `kCGLCPSurfaceBackingSize` is an old CGL parameter. It works on the installed macOS
  today; if a future macOS drops the scaling, the fallback above keeps the window fixed
  rather than broken only if the call reports failure, not if it silently stops scaling.
- The mapping assumes the window server centres the picture and scales it uniformly.
  The spike's full-screen bars match that; the manual click check is what proves it.

## Result

Shipped on 2026-09-16 in commits c1a91bc (`PointerFitToBase` and its tests), 3128cad and
acc8c8e (`MakeWindowScalable`, the mapped `PublishMouse`, and a scoped pragma for the
deprecated CGL calls), e42ddc8 (guide and `AGENTS.md`) and d8a5c32 (comment and guide fixes
from the final review).

`make test`, `make unit-test`, `make pointer-selftest`, `make face-selftest` and
`make grid-selftest` pass, and a Super Mario 64 frame dump at frame 400 is still 640x480 at
90 percent non-black.

The manual check passed on the checks first asked for: with the Super Mario 64 mouse layout
the lit zone followed the cursor and clicks pressed their slot's button at about twice the
size, in full screen and at the minimum size, and a game with no layout resized the same
way. The final review asked for a stricter version afterwards, and these parts of it were
not separately confirmed: clicking the outermost slots (pad-left, c-right) in full screen,
the lit zone turning off exactly at a bar's edge, a maximised (Zoom or Window > Fill) window,
and moving between displays of different density. Those are what would expose a stretched
rather than fitted picture, so the spec's second open risk is reduced, not closed.
