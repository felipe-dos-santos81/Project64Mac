# Mouse panel and direction quadrants — design

Date: 2026-09-15
Status: approved (design), implementation not started
Supersedes: Part 2 (the 4x4 grid) and Part 4 (the overlay) of
`2026-09-15-mouse-and-face-input-design.md`. Everything else in that spec stands.

## Goal

Move the mouse buttons out of the game image into a controller-shaped panel below it, and
make the whole game image the stick, with four direction quadrants drawn as a guide. The
player still needs only one mouse button and the three face gestures. The game itself
renders exactly as before, unscaled, in the top of a taller window.

## Context

- **The grid shares the game image with the buttons.** Today the window is a 4x4 grid: the
  inner 2x2 block is the stick, the twelve outer cells are buttons, and all of it is drawn
  over the game. The stick ring is small (radius 120 px), the cells hide the edges of the
  picture, and every cell is a label that must be read.
- **The renderer already has a dormant viewport offset.** `g_viewport_offset` in
  `Source/Project64-video/Renderer/OGLglitchmain.cpp` was set by the Windows build to the
  height of its status bar. Every on-screen `glViewport`, `glScissor`, `glCopyTexSubImage2D`
  and `glReadPixels` in the renderer adds it; the render-to-texture paths (`glViewport(0, 0,
  …)` inside the FBO branches of `gfxTextureBufferExt`) do not, correctly, because they draw
  into offscreen buffers. On macOS it is always 0. `ChangeSize` in
  `Source/Project64-video/Main.cpp` already reads one macOS-specific environment variable,
  `PJ64_TILE_SIZE`, and runs from `InitGfx` before the first frame and again on every
  resolution change.
- **Window points equal drawable pixels.** The frontend creates its window without
  `SDL_WINDOW_HIGH_PIXEL_DENSITY`; the SDL3 high-DPI notes say SDL then makes the pixel size
  match the window size. The plugin evaluates the cursor in window points and the overlay
  draws in drawable pixels, so the two coordinate systems coincide and stay coincident in the
  taller window.
- **Geometry is one pure header.** `Source/Common/PointerLayout.h` is shared by the plugin
  (evaluation), the overlay (drawing) and `make pointer-layout-test`. The YAML reader takes
  zone names from it through `PointerZoneFromName`, so a new zone table is one change.
- **The frontend already resolves the layout file.** `main.cpp` sets `PJ64_INPUT_YAML` from
  `GameConfigPath` when a YAML named after the ROM exists, and prints `input layout: <path>`.
  It does not read the file; the plugin does, in `PluginLoaded`, after the window exists.
- **The overlay runs on the emulation thread** in `CSdlRenderWindow::SwapWindow`, where SDL
  window functions must not be called. It learns the render size from `GL_VIEWPORT`.

## Non-goals

- A resizable window, full screen, or any size other than 640x640 with a mouse layout.
- A second mouse button, scroll wheel, keyboard chords, or more than one controller.
- New face gestures; the classifier and the tracker are untouched.
- Per-layout panel arrangements. The panel's shape is fixed; a layout only says which control
  sits in which slot.
- A camera-less shipped layout. The panel can hold one, but none ships.
- Grid mode. Tiles never get a panel.
- Any change to the core, the plugin ABI, audio, or RSP.

## Part 1 — Geometry

All coordinates are window pixels, origin top-left, for the 640x640 window. The header
functions still take the window size `(W, H)`; the game image is `W x (H − 160)` and the
panel is the bottom 160 rows. With `H ≤ 160` everything reads as no zone and neutral.

**Constants** (in `PointerLayout.h`): `POINTER_PANEL_HEIGHT = 160`, `POINTER_ZONE_COUNT = 14`,
`POINTER_ZONE_GAME = 13`, `POINTER_FLICK_PX = 24`.

**Zones.** Index order is the table below; names are the YAML `{zone: …}` values.

| Index | Name | Rectangle (x, y, w, h) |
|---|---|---|
| 0 | `pad-up` | (56, 488, 48, 48) |
| 1 | `pad-down` | (56, 584, 48, 48) |
| 2 | `pad-left` | (8, 536, 48, 48) |
| 3 | `pad-right` | (104, 536, 48, 48) |
| 4 | `c-up` | (W − 104, 488, 48, 48) |
| 5 | `c-down` | (W − 104, 584, 48, 48) |
| 6 | `c-left` | (W − 152, 536, 48, 48) |
| 7 | `c-right` | (W − 56, 536, 48, 48) |
| 8–12 | `mid1`–`mid5` | (164 + 64·(i − 1), 488, 56, 48) for i = 1..5 |
| 13 | `game` | (0, 0, W, H − 160): the whole game image |

The `y` values above are `H − 160 + 8`, `H − 160 + 56` and `H − 160 + 104`; the left cross is
anchored to the left edge, the right cross to the right edge, and the middle slots to the
left edge (they are symmetric at W = 640). Gaps between slots and the empty centre cell of
each cross are no zone. The names say
where a slot sits, after its natural occupant; any control may bind to any slot. The old
names (`top1`…`bottom4`, `left2`, `left3`, `right2`, `right3`, `centre`) are gone and are
rejected by the reader like any unknown zone.

```
 y=480 ┌────────────────────────────────────────────────────────────────────┐
       │      ┌──────┐       ┌────┬────┬────┬────┬────┐        ┌──────┐     │
       │      │pad-up│       │mid1│mid2│mid3│mid4│mid5│        │ c-up │     │
       │ ┌────┼──────┼────┐  └────┴────┴────┴────┴────┘   ┌────┼──────┼────┐│
       │ │pad-│      │pad-│                               │ c- │      │ c- ││
       │ │left│      │rght│  ● Z B R  tracker, gestures   │left│      │rght││
       │ └────┼──────┼────┘                               └────┼──────┼────┘│
       │      │ pad- │                                         │  c-  │     │
       │      │ down │                                         │ down │     │
       │      └──────┘                                         └──────┘     │
 y=640 └────────────────────────────────────────────────────────────────────┘
```

**Stick.** Centre `(W/2, (H − 160)/2)`, full-tilt radius `R = (H − 160)/3` (160 px at
640x480), dead zone `0.1 R`. Tilt is `(cursor − centre)/R`, clamped to unit length, scaled to
±80 with Y inverted, exactly the current rule with the larger R. Cursor in the panel, outside
the window, or with the window unfocused reads neutral.

**Quadrant** is a property of the tilt the game receives, not of the cursor: for a
non-neutral `(x, y)` in N64 units, `|y| ≥ |x|` is up (`y > 0`) or down, otherwise right
(`x > 0`) or left; neutral is none. Indices: 0 up, 1 right, 2 down, 3 left, −1 none. Pure
function `PointerQuadrant(int8_t StickX, int8_t StickY)`.

**Guide lines** are the four rays from the centre at 45°, which meet the top and bottom edges
of the game image at `cx ± (H − 160)/2` (80 px and 560 px in from the left at 640x480). The
quadrant wedges are: up = centre, (80, 0), (560, 0); down = centre, (80, 480), (560, 480);
left = centre, (80, 0), (0, 0), (0, 480), (80, 480); right mirrored. Because the lines are at
45° they agree with the quadrant rule above, which the 4:3 corner diagonals (37°) would not.

**Click in the game.** The whole game image is one zone, `game`; the latch rule is unchanged:
the zone under the cursor when the button goes down stays pressed until release. "Click,
then tilt" is still a held A while running.

**Flick gate.** A flick from the game image to a panel slot crosses the down quadrant for a
few controller polls, which would read as a brief backward tilt. Rule, applied by the plugin
once per `GetKeys` after `PointerLayoutEvaluate`:

1. If the cursor is not in the game image (zone is not `game`), the stick is neutral.
2. Else if there is a previous poll and the cursor moved more than `Threshold` px since it,
   the stick keeps the previous poll's tilt.
3. Else the stick is the geometry tilt.

The previous poll's position and tilt are stored every poll, whichever rule fired, so a
flick that spans several polls stays gated and the gate releases the first poll the cursor
moves less than the threshold. `Threshold` is `POINTER_FLICK_PX` (24) unless
`PJ64_POINTER_FLICK=<px>` overrides it; `0` disables the gate. The first poll has no
previous position and is never gated, and the self-test's injected sample never moves. Pure
function, in `PointerLayout.h`:

```cpp
struct PointerGate { bool HavePrev; float PrevX, PrevY; int8_t PrevStickX, PrevStickY; };
// Applies the three rules to E (already evaluated at X,Y) and records this poll in G.
inline void PointerGateStick(PointerGate * G, PointerEval * E, float X, float Y, float Threshold);
```

## Part 2 — Components

**Frontend (`Source/Project64-sdl/main.cpp`).** After the layout lookup and before
`SDL_CreateWindow`, and only when not a tile: take the layout path (`PJ64_INPUT_YAML` if
non-empty, else `<executable dir>/Config/input.yaml`), load it with `InputConfig::Get().Load(path, /*Quiet=*/true)`,
and if `UsesPointer()` make the window `640 x (480 + POINTER_PANEL_HEIGHT)` and
`setenv("PJ64_VIEWPORT_OFFSET", "160", 1)`. Otherwise `unsetenv("PJ64_VIEWPORT_OFFSET")`, so
a value inherited from the caller can never lift the game in a keyboard run or a tile.
`Load` gains the `Quiet` flag (default false) so a bad file is still reported once, by the
plugin, in the existing format. `InputConfig.cpp` joins the frontend's sources; the frontend
and the input dylib each have their own copy and their own singleton, which read the same
file with the same code and cannot disagree. `PublishMouse` is unchanged: it publishes the
window size, now 640x640, and the cursor in window points.

**Video plugin (`Source/Project64-video/Main.cpp`, `ChangeSize`).** After the
`PJ64_TILE_SIZE` block: read `PJ64_VIEWPORT_OFFSET`, parse it as a non-negative integer,
store it in `g_viewport_offset` (0 when unset or malformed). `ChangeSize` runs before the
first frame, so the first `glViewport` already carries the offset, and every later run
stores the same value. The game's clears stay inside its scissor rectangle, so the panel
rows are never touched by the renderer. Nothing else in the plugin changes.

**Shared headers (`Source/Common/`).** `PointerLayout.h`: the constants, the zone table,
`PointerZoneRect` for the thirteen slots and the game image, `PointerLayoutEvaluate` with the
new geometry, `PointerQuadrant`, `PointerGate` and `PointerGateStick`. `PointerState.h`:
`POINTER_ZONE_COUNT` becomes 14, and one atomic joins `LatchedZone`:
`std::atomic<int32_t> Quadrant` (−1..3), plugin-written, overlay-read.

**Input plugin (`Source/Project64-sdl/PluginInput.cpp`).** Per poll: snapshot, evaluate,
`PointerGateStick` (one static `PointerGate`, the threshold read once from the environment),
latch on the button edge as today, store `LatchedZone` and `Quadrant` (from the gated tilt),
then evaluate bindings unchanged. `PointerSelftestReport` is unchanged; the zone indices it
prints follow the new table.

**Overlay (`Source/Project64-sdl/Overlay.{h,cpp}`).** Signature becomes
`OverlayDraw(const PointerState *, const int Viewport[4], bool GuideHidden)`. From the
viewport `(vx, vy, vw, vh)` the window is `(vx + vw) x (vy + vh)`, the game image is the top
`vh` rows and the panel is the `vy` rows below it, so the overlay never calls SDL from the
emulation thread and never assumes the panel height; with an offset of 0 the panel has no
height and nothing is drawn below the game. Each frame, in this order:

1. Panel: an opaque fill (grey 0.12) over the panel rows; each slot's outline and label at
   `kDim`, the latched slot at `kBright`; the tracker mark at `(178, H − 40)` with the three
   gesture labels to its right at `(206, 242, 278)`, drawn as today (hollow, filled, crossed;
   labels bright while their gesture is active). The corner mark over the game goes away.
2. Guide, skipped when `GuideHidden`: the four rays, the ring at `R`, the dead-zone circle,
   the `game` label just inside the ring's top, the four arrow glyphs (`^` at `(cx, 24)`, `v`
   at `(cx, gh − 24)`, `<` at `(24, cy)`, `>` at `(W − 24, cy)`) at `kDim`, the current
   quadrant's wedge filled at alpha 0.08 and its arrow at `kBright`.

`CSdlRenderWindow::SwapWindow` calls it whenever `OverlayWanted` is set and passes
`m_OverlayHidden` (`PJ64_OVERLAY=0`) as `GuideHidden`; the panel is the controls and is
never hidden. `ReadBackBuffer` reads at `(vx, vy)` instead of `(0, 0)`, so `PJ64_FRAME_DUMP`
still writes the 640x480 game alone.

**YAML reader (`Source/Project64-sdl/InputConfig.{h,cpp}`).** The `Quiet` flag on `Load`.
Zone names, the label table size and `PointerLabels` follow the header. No new forms.

**Unchanged.** `FaceTracker.mm`, `FaceGestures.{h,cpp}`, `GameConfig.{h,cpp}`,
`GridHost.cpp`, `GridKeys.h`, the seqlock, `PJ64_POINTER_INJECT`, the core and the other
plugins.

## Part 3 — Layouts and installation

`Config/mouse/super_mario_64_usa.yaml`:

```yaml
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

`mario_kart_64_u.yaml`: the same slots, with `A` on `game` (hold to accelerate while
steering), `R` on `eyebrows` (hop and drift), `Z` on `head-left` (item), `B` on `head-right`
(brake).

`goldeneye_007_u.yaml`: `Z` on `game` (fire), `R` on `eyebrows` (aim, held), `CLeft` and
`CRight` on `head-left` and `head-right` (strafe the way you turn), `A` `mid1`, `B` `mid2`,
`Start` `mid3`, `L` `mid4`, `CUp` `c-up`, `CDown` `c-down`, D-pad on the left cross.
`c-left`, `c-right` and `mid5` are free.

Each file's header comment names the slots and the gestures. Thirteen slots plus the game
click cover all fourteen buttons, so a camera-less layout is one file away; none ships.

**Installed copies are refreshed.** `make config` currently copies `Config/mouse/` with
`cp -n`, treating installed layouts as user data. A stale installed copy would now fail with
`unknown zone "top4"` and the game would get keyboard defaults with only one stderr line to
say why. So the shipped layouts become examples: `make config` always overwrites
`Bin/macOS/Config/mouse/*.yaml` with the tracked files. A player's own layout belongs beside
the ROM as `<rom>.yaml`, which the lookup checks first; `Config/input.yaml` keeps its `cp -n`.

## Part 4 — Errors and observability

- Bad or missing layout: the frontend's silent load fails, the window stays 640x480, no
  offset is set; the plugin's own load prints the one existing line and uses defaults.
- Plugin cannot map `PJ64_POINTER_FD` (foreign frontend only): no panel is painted because
  nothing sets `OverlayWanted`; the window may be tall, and the game still runs.
- Video plugin ignoring the offset (a stale dylib): the viewport reports `vy = 0`, the overlay
  draws no panel and nothing overlaps the game; the taller window shows the game at its
  bottom. `make all` rebuilds both, so this cannot happen from a normal build.
- Camera denied or absent: the panel works, the gesture-bound buttons are unavailable, the
  tracker mark in the panel says so, exactly as the corner mark did.
- The existing tells stay: `input layout: <path>` on stderr, `PJ64_FACE_DEBUG=1`, and the
  self-test report line. `PJ64_POINTER_FLICK` is the one new variable, documented with the
  face thresholds.

## Part 5 — Build and tests

**Makefile.** `InputConfig.cpp` joins `FRONTEND_SRC` (its object is already built with the
SDL and yaml-cpp flags for the input dylib) and `$(YAML_LIBS)` joins the frontend link.
`config` overwrites the mouse layouts. Help text for `run` and `pointer-selftest` names the
new slots.

**Tests.**

1. `make pointer-layout-test` (rewritten): the fourteen names round-trip; each slot at its
   centre; the pixel either side of `pad-left`'s right edge (`x = 55` is `pad-left`, `x = 56`
   is the cross's empty centre); a gap between `mid1` and `mid2` and the panel's empty
   corner read no zone; any point in the
   game image, including `(0, 0)` and `(639, 479)`, is `game`; the stick at the centre, at
   `0.1 R`, at `R`, beyond `R`, in the panel, outside the window and unfocused; a tilt at 44°
   and 46° from vertical lands in different quadrants and a neutral tilt in none; slot
   rectangles lie inside the panel and none overlap; the gate over three sequences (a 300 px
   jump keeps the previous tilt, a 5 px move follows the cursor, a jump that lands in the
   panel reads neutral, and `Threshold = 0` never gates).
2. `make input-config-test`: `{zone: game}`, `{zone: mid1}` and `{zone: pad-up}` parse to
   their indices; `{zone: top4}` and `{zone: centre}` are rejected; the three shipped files
   parse with `UsesPointer()` true and `UsesFace()` true; the label table puts `A` at
   `POINTER_ZONE_GAME` and `St` at index 8 for the Mario file.
3. `make pointer-selftest rom=…`: inject `320,240,1`, expect `zone=13 a=1 start=0`; inject
   `192,512,1` (the centre of `mid1`), expect `zone=8 a=0 start=1`; then the sibling-lookup
   run with the centre click as today. The second run can only pass if the window is 640
   tall and the offset is in force, so it proves the window and the plugin hook together.
4. `make test`: unchanged. `make rom-test`: unchanged expectations; the dump must still be
   640x480 and Super Mario 64's non-black share must stay where it is, which proves the
   read-back origin.
5. Manual, Super Mario 64: run, jump while running, long jump, rotate the camera from a C
   slot, and flick from the centre to a C slot without Mario turning around. Tune
   `PJ64_POINTER_FLICK` if a deliberate move ever feels late. Manual, Mario Kart 64: steer
   and hop. GoldenEye: the file loads; play waits on the stall recorded in
   `goldeneye-black-screen-investigation`.

## Part 6 — Documentation

- README "Playing with a mouse": the taller window, the panel and its slots, the quadrant
  guide, `game`, the flick gate and `PJ64_POINTER_FLICK`, `PJ64_OVERLAY=0` hiding the guide
  only, and that shipped layouts are refreshed while a player's own goes beside the ROM.
- AGENTS: the architecture paragraph gains the frontend's own parse, `PJ64_VIEWPORT_OFFSET`,
  and the overlay reading the viewport; the traps gain "the frontend owns
  `PJ64_VIEWPORT_OFFSET`" and the installed-layout policy, and the existing sibling-YAML
  trap notes the window becomes 640x640.
- `2026-09-15-mouse-and-face-input-design.md` gets a one-line status note under its header
  pointing here for Parts 2 and 4, so nobody reopens the grid decision from the stale text.

## Open risks

- **Flick threshold.** 24 px per poll separates a 300 px flick over a few frames from
  deliberate steering at a few pixels per frame, but it is a feel judgement; the override
  exists for that.
- **Renderer offset on macOS.** The mechanism is the Windows status bar's and every
  on-screen path honours it, but the first manual run is the proof that nothing smears into
  the panel rows.
- **Two parses of one file.** The frontend and the plugin each parse the layout. They share
  the code, so they cannot disagree, at the cost of one extra read at startup.
- **Selftest coordinates are literal.** `192,512` is `mid1`'s centre for the fixed panel; the
  script comments say so, and the layout test guards the geometry it depends on.
