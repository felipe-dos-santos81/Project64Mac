# Mouse-and-face input — design

Date: 2026-09-15
Status: approved (design), implementation not started

## Goal

Let a player who can use only a standard mouse with one working button play N64 games
in this port, with Apple's Vision framework turning three facial gestures into extra
buttons. The pointer grid's thirteen zones plus the stick reach fourteen of the fifteen
N64 controls from the mouse alone; the face gestures cover the rest and make the
most-used ones faster and simultaneous. The feature is opt-in and changes nothing for
keyboard, gamepad, or grid users.

## Context

- **The N64 pad is an analog stick plus fourteen digital inputs.** The available inputs
  are two continuous mouse axes, one mouse button, and three face gestures (eyebrow raise,
  head turn left, head turn right). Motion becomes the stick, the four binary inputs can be
  buttons directly, and the remaining ten buttons need another route.
- **Bindings are already data.** `Source/Project64-sdl/InputConfig.{h,cpp}` owns the N64
  control set, the default table and the YAML reader; `GetKeys` in
  `Source/Project64-sdl/PluginInput.cpp` only evaluates the resolved table. A control named
  in the file has exactly one binding, and any error rejects the whole file
  (`Docs/superpowers/specs/2026-09-14-input-yaml-mapping-design.md`).
- **SDL3 mouse state is main-thread only.** `SDL_GetMouseState` and
  `SDL_GetRelativeMouseState` are documented "should only be called on the main thread",
  unlike `SDL_GetKeyboardState`, which the plugin reads from the emulation thread today.
  So the frontend must sample the mouse and hand it to the plugin.
- **A cross-thread, cross-dylib channel already exists.** The grid orchestrator publishes
  keyboard state through the `GridKeys` seqlock in `Source/Common/GridKeys.h` over a
  `shm_open` descriptor passed in an environment variable; the plugin maps it in
  `OpenGridKeys`. The same mechanism works in-process.
- **Vision gives what the gestures need.** `VNDetectFaceLandmarksRequest` yields
  `VNFaceLandmarks2D` with `leftEyebrow`, `rightEyebrow`, `leftEye`, `rightEye` regions in
  coordinates normalised to the face bounding box, and `VNFaceObservation` carries `roll`,
  `yaw` and `pitch` in radians. Eyebrow height and head yaw are therefore cheap to derive
  and independent of distance from the camera.
- **The render window presents on the emulation thread** with a GL 2.1 compatibility
  context current (`main.cpp:138-140`, `SdlRenderWindow.cpp:199-208`), so fixed-function
  GL drawn right before `CGLFlushDrawable` is a valid overlay path.
- **The binary is not an app bundle.** macOS attributes its camera permission prompt to
  the launching terminal or IDE.

## Non-goals

- Grid mode: tiles stay keyboard-broadcast. Pointer bindings are single-window only.
- More than one controller, a second mouse button, scroll wheel, or trackpad gestures.
- Mouth, pitch, roll, blink or gaze gestures. Only eyebrows and yaw.
- In-game remapping, hot reload, calibration UI, or a camera preview.
- Any change to the core, the plugin ABI, or the video plugin.
- Multi-input bindings in the file; the one-binding-per-control rule stands.

## Part 1 — Architecture

Three new pieces share one struct.

**`PointerState`** (`Source/Common/PointerState.h`). A shared struct with one writer per
field group:

| Field group | Writer | Reader | Sync |
|---|---|---|---|
| Cursor x, y (window pixels), window w, h, `focused_inside` bit, left button bit | frontend main loop | plugin `GetKeys` | seqlock, as `GridKeys` |
| Gesture bits (eyebrows, head-left, head-right) and tracker status | face tracker queue | plugin, overlay | one 32-bit atomic |
| Label table (13 cells × up to 2 chars), `overlay_wanted` | plugin, once at load | overlay | plain, written before the ROM opens |
| Latched zone index, active gesture labels | plugin `GetKeys` | overlay | atomics |

The frontend creates it with `shm_open` before `AppInit`, sets `PJ64_POINTER_FD` to the
descriptor, and the plugin maps it in `PluginLoaded` (labels) and lazily in `GetKeys`
(state), mirroring `OpenGridKeys`. Same process, same mechanism, and the plugin needs no
link against the frontend.

**Frontend sampling.** The existing loop in `main.cpp` (`while (running)`) already pumps
events every ten milliseconds. After `SDL_PollEvent` it calls `SDL_GetMouseState`,
`SDL_GetWindowSize`, checks `SDL_GetMouseFocus() == window`, and publishes one snapshot.

**Plugin evaluation.** Two binding kinds join `Key`, `Button`, `Axis`, `Stick`, `Keys`:

- `Zone`: a grid cell (Part 2). Fires while latched by a click that began in that cell.
- `Face`: one gesture. Fires while its bit is set.
- `Stick` gains a `pointer` variant: tilt from the cursor's offset to the window centre.

`A` in the default layout is `{zone: centre}`; the click in the stick area is simply a
thirteenth zone name, so it needs no special case.

**Geometry in one place.** `Source/Common/PointerLayout.h` holds a pure function
`PointerLayoutEvaluate(x, y, w, h, inside) -> {zone, stickX, stickY}` used by the plugin to
evaluate and by the overlay to draw, so the two cannot disagree. Being pure and SDL-free,
it is unit-tested without a window.

**Precedence.** Pointer bindings OR into the same `BUTTONS` bits as keyboard and gamepad
bindings under the existing rule: a control listed in the file has exactly the binding
given. If `Stick` is `{stick: pointer}`, that is its only source; the keys-over-gamepad
stick priority in `GetKeys` is untouched.

## Part 2 — The control scheme

**Grid.** The window is a 4×4 grid of equal cells. The twelve outer cells are zones named
by edge and column or row index: `top1`–`top4`, `bottom1`–`bottom4`, `left2`, `left3`,
`right2`, `right3`. The inner 2×2 block is the stick area; a click there is the zone
`centre`.

**Stick.** Radius `R = min(w, h) / 4`, so the ring fits inside the inner block. Tilt is
`(cursor − centre) / R`, clamped to unit length, with a dead zone of `0.1 R`, scaled to
±80 (`N64_AXIS_MAX`) and Y inverted (screen down is stick down). Cursor in an outer cell,
outside the window, or with the window unfocused reads neutral. A brief full tilt while the
cursor crosses the ring on its way to a zone is accepted; speed-gating it is a possible
follow-up, not part of this design.

**Latch on press.** The zone under the cursor when the button goes down is the pressed
zone and stays pressed until the button is released, wherever the cursor moves. This makes
"click in the centre, then tilt" a held A while running, and stops slides across cells from
pressing other buttons. The plugin keeps one static previous-button state for the edge.

**Face gestures** are three named held inputs: `eyebrows`, `head-left`, `head-right`.

**YAML forms**, added to the existing five:

| Form | Meaning | Allowed on |
|---|---|---|
| `{zone: <name>}` | The named cell, or `centre`; pressed while latched. | any control except `Stick` |
| `{face: <gesture>}` | One of `eyebrows`, `head-left`, `head-right`. | any control except `Stick` |
| `{stick: pointer}` | Cursor offset from the window centre. | `Stick` only |

`stick` already exists with `left`/`right`; `pointer` is a third value. Unknown zone or
gesture names, `pointer` outside `Stick`, or a zone or face on `Stick` reject the file.

**Shipped layouts.** `Config/input.yaml` stays keyboard-active: under the one-binding rule
an active mouse block would replace the keyboard bindings and break grid mode's keyboard
broadcast and `Scripts/grid_selftest.sh`. Mouse layouts ship as separate files under
`Config/mouse/`, installed by `make config` with `cp -n`, and selected with
`PJ64_INPUT_YAML` (or `make run … input=Config/mouse/sm64.yaml`).

`Config/mouse/sm64.yaml` (Super Mario 64):

```yaml
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

`bottom1` and `bottom4` are free.

`Config/mouse/goldeneye.yaml`: `Stick` pointer, `Z` centre (fire), `R` eyebrows (aim,
held), `CLeft`/`CRight` on `head-left`/`head-right` (strafe the way you turn), `A` `left2`,
`B` `right2`, `Start` `top4`, `CUp` `top2`, `CDown` `bottom2`, `L` `top1`, D-pad on
`top3`, `bottom3`, `left3`, `right3`.

`Config/mouse/mk64.yaml`: `Stick` pointer, `A` centre (hold to accelerate while steering),
`R` eyebrows (hop and drift, held), `Z` `head-left` (item), `B` `head-right` (brake),
`Start` `top4`, C buttons and L and D-pad on the remaining cells as in the Mario file.

These are starting points; each is one line to change.

## Part 3 — The face tracker

**Module.** `Source/Project64-sdl/FaceTracker.mm` (Objective-C++, frontend only) with a
C++ header exposing `FaceTrackerStart(PointerState *)` and `FaceTrackerStop()`. The pure
classifier is `Source/Project64-sdl/FaceGestures.{h,cpp}`, C++ only. The plugin never
touches the camera; it reads three bits.

**Opt-in.** `--face` on the frontend, or `PJ64_FACE=1` (`make run … face=1`). Without it
nothing camera-related is initialised and no permission prompt appears.

**Capture and detection.** `AVCaptureSession` on the default video device, 640×480
preset, about 30 fps, `AVCaptureVideoDataOutput` on a private serial dispatch queue with
`alwaysDiscardsLateVideoFrames`. Each frame's `CVPixelBuffer` goes to a
`VNImageRequestHandler` running one `VNDetectFaceLandmarksRequest`; the largest face
observation wins.

**Measures.**

- Eyebrow height: mean y of `leftEyebrow` and `rightEyebrow` points minus mean y of
  `leftEye` and `rightEye` points, in face-box-normalised units. Sets `eyebrows` when it
  exceeds its baseline by the brow threshold (default 0.035).
- Head yaw: the observation's `yaw` in radians against its baseline. Beyond the yaw
  threshold (default 0.25 rad) to one side sets `head-left`, the other `head-right`. Which
  sign is "left" for a front camera is confirmed in the first manual test and fixed in
  code, not in the YAML.

**Classifier rules** (all in `GestureClassifier`, fed one sample per frame with a
timestamp):

- Baseline per measure is an exponential moving average with a five-second time constant,
  frozen while that gesture is active, so it tracks face and posture but never chases a
  held gesture.
- Hysteresis: release at 60 % of the set threshold above baseline.
- Debounce: two consecutive qualifying frames to set, two to clear (about 70 ms).
- No face for 0.5 s clears every bit and sets status `no-face`.

**Status** (in the atomic, for the overlay): `off`, `starting`, `tracking`, `no-face`,
`denied`, `error`.

**Privacy.** No preview, no frame written anywhere, nothing retained past the callback, no
landmark values logged by default. One stderr line on start, stop, and denial.
`PJ64_FACE_DEBUG=1` prints the two measures and their baselines once a second for tuning.
`PJ64_FACE_BROW` and `PJ64_FACE_YAW` override the two thresholds.

**Permission.** `AVCaptureDevice requestAccessForMediaType:` at start. Denied, absent, or a
session that fails to start: one stderr line, status `denied` or `error`, play continues.

**Threading.** Vision runs on the capture queue; the only cross-thread traffic is the
atomic write into `PointerState`. `FaceTrackerStop` runs `stopRunning` before SDL quits.

## Part 4 — The overlay

**Where.** `Source/Project64-sdl/Overlay.{h,cpp}`, called from
`CSdlRenderWindow::SwapWindow` on the emulation thread immediately before
`CGLFlushDrawable`, after the frame-dump readback so `PJ64_FRAME_DUMP` metrics are
unchanged. It pushes GL attributes, sets an orthographic projection in window pixels
(current viewport size each frame), draws with fixed-function calls, and pops.

**What**, in white lines at about one-third opacity:

- The stick ring (radius `R`) and the dead-zone circle at the window centre.
- The twelve outer cell outlines.
- One label per cell and one in the ring, at most two characters, from a built-in 5×7
  bitmap font covering exactly the glyphs needed: `A`, `B`, `Z`, `St`, `L`, `R`, `C^`,
  `Cv`, `C<`, `C>`, `D^`, `Dv`, `D<`, `D>`. An unbound cell has no label.
- The latched zone drawn at full opacity while held.
- A face status mark in the bottom-left corner: nothing when `off`; a hollow circle for
  `starting` or `no-face`; a filled circle for `tracking`; a crossed circle for `denied` or
  `error`. Active gestures show their bound label beside it while held.

The system cursor stays visible; no custom pointer is drawn.

**Labels come from the plugin**, the only reader of the YAML: at load it writes the label
table and `overlay_wanted` (true if any zone, face or pointer-stick binding is present),
and each `GetKeys` writes the latched zone and active gesture labels.

**Switches.** On whenever `overlay_wanted`; otherwise nothing is drawn and keyboard and
gamepad users see no change. `PJ64_OVERLAY=0` hides it.

## Part 5 — Errors and observability

- YAML errors keep the all-or-nothing rule and the existing message format
  (`input: <path>:<line>:<col>: <message>; using built-in defaults`).
- Plugin cannot map `PJ64_POINTER_FD`: pointer bindings evaluate released and stick
  neutral, one stderr line. The frontend always creates the struct, so this only occurs
  under a foreign frontend.
- Camera denied, absent, or failing: one stderr line, status in the overlay, play
  continues.
- Nothing here can crash or block the emulation thread: `GetKeys` does no I/O, and the
  tracker only writes an atomic.

## Part 6 — Build, install, verification

**Makefile.**

- `PointerState.h`, `PointerLayout.h` in `Source/Common` (header-only).
- `FaceTracker.mm`, `FaceGestures.cpp`, `Overlay.cpp` join `FRONTEND_SRC`; a `.mm`
  pattern rule compiles Objective-C++ with `-fobjc-arc`; the frontend link adds
  `-framework AVFoundation -framework Vision -framework CoreMedia -framework CoreVideo`.
- `InputConfig.cpp` and `PluginInput.cpp` gain the new kinds; no new plugin dependencies.
- `make config` installs `Config/mouse/*.yaml` with `cp -n`.
- `make run` accepts `input=<yaml>` and `face=1`, setting `PJ64_INPUT_YAML` and
  `PJ64_FACE`.

**Tests.**

1. `make input-config-test` gains the three new forms and their rejections (unknown zone,
   unknown gesture, `pointer` on `A`, `zone` on `Stick`).
2. `make pointer-layout-test`: zone lookup at cell centres and borders; stick tilt at the
   centre, at the dead-zone edge, at the ring edge, beyond the ring, in an outer cell, and
   outside the window.
3. `make face-gesture-test`: synthetic sample sequences prove baseline adaptation,
   hysteresis, debounce, freeze-while-active, and the no-face timeout.
4. `make test` still passes: five input exports, `--version` on a frontend now linked
   against Vision.
5. End-to-end without a mouse: `PJ64_POINTER_INJECT=x,y,button` makes the frontend publish
   that snapshot instead of sampling. `Scripts/pointer_selftest.sh` runs Mario with
   `Config/mouse/sm64.yaml`, injects a click at the window centre and one in `top4`, and
   asserts via the existing `SelftestReport` line that `A_BUTTON` and `START_BUTTON` were
   set. Needs a window server, like the grid self-test.
6. Manual, Super Mario 64: `make run rom=… input=Config/mouse/sm64.yaml face=1`. Success:
   run, jump while running, long jump with brows plus click while tilted, camera rotate
   from a side cell. The yaw sign is confirmed here.
7. Manual, Mario Kart 64: accelerate while steering, and a hop on brows.
8. GoldenEye: its file ships and loads, but playtesting waits on the stall recorded in the
   black-screen investigation.

**Docs.** README gains "Playing with a mouse": the grid, the files, `input=` and `face=1`,
the camera prompt, and the privacy statement. AGENTS gains the architecture note (shared
`PointerState`, frontend samples the mouse, tracker in the frontend, plugin evaluates)
and three traps: SDL3 mouse APIs are main-thread only; the camera prompt is attributed to
the launching terminal; `Config/input.yaml` must stay keyboard-active.

## Open risks

- **Ring-crossing tilt.** Reaching a zone drags the cursor across the ring edge for a few
  frames, read as a brief full tilt. Accepted; a speed gate is the follow-up if it bothers
  play.
- **Yaw sign and thresholds.** Defaults are guesses until the first manual run; the debug
  print and the two overrides exist to fix them without a rebuild, and the file changes if
  the sign is reversed.
- **Camera permission attribution.** From a non-bundled binary, macOS prompts for the
  terminal or IDE. If a launcher has been denied in the past, the prompt does not reappear
  and the tracker reports `denied`; the fix is in System Settings, and the README says so.
- **Vision under `-std=c++14` Objective-C++.** The frameworks are Objective-C; the `.mm`
  unit is expected to compile with the existing flags plus ARC. Confirm in the first
  implementation step.
- **No automated camera test.** The classifier is unit-tested; capture and Vision are
  manual only.
