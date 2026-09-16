# Face expressions and the head stick — design

Date: 2026-09-15
Status: approved (design), implementation not started
Extends: Part 3 (the face tracker) of `2026-09-15-mouse-and-face-input-design.md`, whose
three gestures, capture path, classifier rules, privacy rules and permission handling all
stand. Nothing here changes the mouse panel of `2026-09-15-mouse-panel-design.md`.

## Goal

Let a player with no usable hands play Super Mario 64 and Mario Kart 64 from the webcam
alone: head pose becomes the analog stick, and eight new facial gestures join the three
existing ones as held buttons. The feature is opt-in through a layout file, and a layout
that binds none of it changes nothing for keyboard, gamepad, mouse or grid users.

## Context

- **Vision on macOS has no expression coefficients.** Apple's blend shapes (jaw open,
  smile, blink and the rest) live in ARKit's `ARFaceAnchor`, which needs the TrueDepth
  camera and runs on iOS only. On macOS, `VNDetectFaceLandmarksRequest` yields a 76-point
  constellation grouped into regions (`leftEye`, `rightEye`, `leftEyebrow`, `rightEyebrow`,
  `nose`, `noseCrest`, `outerLips`, `innerLips`, `faceContour`, `medianLine`, `leftPupil`,
  `rightPupil`) in coordinates normalised to the face bounding box, and `VNFaceObservation`
  carries `roll`, `yaw` and `pitch` in radians. Expressions are therefore derived
  geometrically from those regions, the way the current tracker derives brow height.
- **The tracker already has the right seams.** `Source/Project64-sdl/FaceTracker.mm` turns
  each frame into one `GestureSample`; the pure `GestureClassifier` in
  `Source/Project64-sdl/FaceGestures.{h,cpp}` applies a rest baseline (five-second moving
  average, frozen while a gesture is held), hysteresis (release at 60 % of set), debounce
  (two frames) and a no-face timeout (0.5 s), and returns held bits; the plugin's `GetKeys`
  ORs each bound bit into the N64 buttons; the overlay reads the bits and a label table.
  All of that generalises from two measures to eight without changing shape.
- **The stick has a precedent.** `{stick: pointer}` makes the plugin take the axes from a
  shared struct instead of a device, sets the flag that stops the gamepad overwriting them,
  and stores its quadrant for the guide. A head stick follows the same path.
- **Pupils are out.** Gaze from two pupil points at webcam distance is too noisy to bind a
  button to; the eye regions are used for aperture only.

## Non-goals

- Blend shapes, a CoreML expression model, or any new dependency beyond Vision.
- Gaze, pupils, tongue, cheek or jaw-sideways gestures.
- Toggle (latching) gestures; every gesture is held, like every other binding.
- A calibration UI, a recentre gesture, or a camera preview. The moving baseline is the
  calibration, as it is today.
- Any change to the mouse panel geometry, the core, the plugin ABI, or the video plugin.
- Grid mode. Tiles stay keyboard-broadcast.

## Part 1 — Vocabulary and YAML

**Gestures.** Eleven named held inputs, each bindable with `{face: <name>}` on any control
except `Stick`. The three existing names keep their bits; the eight new ones follow in this
order, which is also the bit order, the label-table order and the overlay's strip order:

| Bit | Name | Measure | Fires when |
|---|---|---|---|
| 0 | `eyebrows` | brow height | rises above the rest baseline by the brow threshold |
| 1 | `head-left` | yaw | turns left of baseline by the yaw threshold |
| 2 | `head-right` | yaw | turns right of baseline by the yaw threshold |
| 3 | `head-up` | pitch | nose rises above baseline by the pitch threshold |
| 4 | `head-down` | pitch | nose drops below baseline by the pitch threshold |
| 5 | `tilt-left` | roll | left ear drops toward the shoulder by the roll threshold |
| 6 | `tilt-right` | roll | right ear drops toward the shoulder by the roll threshold |
| 7 | `mouth-open` | inner-lip gap | opens past baseline by the mouth threshold |
| 8 | `smile` | outer-lip width | widens past baseline by the smile threshold |
| 9 | `wink-left` | eye aperture | the left eye closes by the eye threshold while the right stays open |
| 10 | `wink-right` | eye aperture | the right eye closes by the eye threshold while the left stays open |

Left and right are the player's own, as `head-left` already is. Both eyes closing is a
blink and fires neither wink.

**Head stick.** Two new forms beside `{stick: left}`, `{stick: right}` and
`{stick: pointer}`, valid on `Stick` only:

- `{stick: head}`: X is yaw and Y is pitch, each taken as the angle from its rest baseline
  divided by its full-tilt angle, clamped to the unit disc, with a radial dead zone at 20 %
  of full tilt (inside it the stick is exactly neutral), scaled to ±80 (`N64_AXIS_MAX`)
  with a left turn negative and a nose-up positive. Full tilt defaults to 0.26 rad (15°) of
  yaw and 0.17 rad (10°) of pitch, since a webcam sees less pitch than yaw.
- `{stick: head-digital}`: the same analog value snapped by `PointerQuadrant` (the mouse
  guide's rule: vertical wins ties) to a full tilt on one axis, or neutral inside the dead
  zone.

**One rule.** A file with either head-stick form cannot also bind `head-left`,
`head-right`, `head-up` or `head-down`: yaw and pitch are the stick. The reader rejects the
file with one line in the existing format (`input: <path>:<line>:<col>: head-left cannot
be bound while Stick is head; using built-in defaults`) and, as for every bad file, the
built-in mapping loads whole. `tilt-left`, `tilt-right`, `eyebrows`, `mouth-open`, `smile`
and the winks remain free beside a head stick.

## Part 2 — Tracker and classifier

**Measures.** Per frame, from the largest face, the tracker derives eight numbers. All
landmark measures are in Vision's face-box-normalised coordinates, so they are independent
of distance from the camera:

| `FaceMeasure` | Source | Definition |
|---|---|---|
| `Brow` | eyebrow and eye regions | mean y of both eyebrows minus mean y of both eyes (as today) |
| `Yaw` | `VNFaceObservation.yaw` | negated, as today, so the player's left turn is negative |
| `Pitch` | `VNFaceObservation.pitch` | sign fixed in code after the first manual run so that nose-up is positive |
| `Roll` | `VNFaceObservation.roll` | sign fixed in code after the first manual run so that left-ear-down is negative |
| `Mouth` | `innerLips` | max y minus min y of the region's points |
| `Smile` | `outerLips` | max x minus min x of the region's points |
| `EyeLeft` | `leftEye` | (max y − min y) / (max x − min x) of the region's points |
| `EyeRight` | `rightEye` | the same for `rightEye` |

The eye ratio stays comparable as the head turns, because both extents shrink together. A
region that Vision returns with zero points keeps that measure at its previous frame's
value; the other measures update normally.

**Sample.** `GestureSample` becomes `{ bool FaceFound; float M[FACE_MEASURE_COUNT];
double Time; }`, indexed by the `FaceMeasure` enum above, replacing the two named fields.

**Classifier.** `GestureClassifier` keeps one rest baseline per measure (the existing
moving average, `BaselineSeconds` 5.0) and one `Channel` per gesture, each channel being a
measure index, a sign (+1 fires above baseline, −1 below) and a threshold, run through the
existing `Channel::Step` hysteresis and debounce. The wink channels take one extra input,
whether the other eye is currently below its own release level, and their raw condition is
"this eye closed and the other not", so a blink fires neither. `Update` still returns the
held bits; two accessors `StickX()` and `StickY()` (int8, ±80) return the head stick
computed in the same call from the yaw and pitch baselines by the Part 1 rule. `Update`
gains one parameter, `bool HeadStickInUse`.

**Baseline freezing** keeps today's rule: a measure's baseline holds while any channel on it
is active or counting toward set. One rule is added: while `HeadStickInUse` and the head
stick is outside its dead zone, the yaw and pitch baselines hold too. A layout with no head
stick passes `false` and keeps exactly today's yaw behaviour; this is tested.

**No face** for `NoFaceSeconds` clears every channel and centres the stick, as it clears the
three bits today. Between the last face and the timeout, bits and stick hold their last
values, as today.

**Thresholds.** `GestureThresholds` gains one field per new measure. Defaults are starting
points for the manual runs, each overridable in radians or normalised units by an
environment variable in the existing style:

| Field | Default | Override |
|---|---|---|
| `Brow` | 0.035 | `PJ64_FACE_BROW` (exists) |
| `Yaw` | 0.25 | `PJ64_FACE_YAW` (exists) |
| `Pitch` | 0.20 | `PJ64_FACE_PITCH` |
| `Roll` | 0.25 | `PJ64_FACE_ROLL` |
| `Mouth` | 0.06 | `PJ64_FACE_MOUTH` |
| `Smile` | 0.05 | `PJ64_FACE_SMILE` |
| `Eye` | 0.12 (drop in the aperture ratio) | `PJ64_FACE_EYE` |
| `StickYaw` | 0.26 (full tilt) | `PJ64_FACE_STICK_YAW` |
| `StickPitch` | 0.17 (full tilt) | `PJ64_FACE_STICK_PITCH` |

`ReleaseFraction`, `BaselineSeconds`, `DebounceFrames` and `NoFaceSeconds` are unchanged
and shared by every channel. An override that parses to zero or negative is ignored, as
today.

**Debug.** `PJ64_FACE_DEBUG=1` prints, once a second, all eight measures with their
baselines, the bits and the stick. Nothing else is logged; frames are still never stored,
shown or written.

## Part 3 — Shared state, plugin, overlay

**`PointerState`** (`Source/Common/PointerState.h`) gains:

| Field | Group | Writer | Reader |
|---|---|---|---|
| `std::atomic<int32_t> HeadX, HeadY` | tracker | face tracker queue, every frame, already scaled to ±80 | plugin `GetKeys` |
| `std::atomic<uint32_t> HeadStickWanted` | plugin at load, beside `FaceWanted` | plugin, once, release | tracker, every frame, acquire |

`POINTER_GESTURE_COUNT` goes from 3 to 11 and `GestureLabels` with it. `PointerGesture`
gains the eight bits in Part 1's order; `PointerGestureFromName` learns the eight names;
`PointerGestureIndex` covers all eleven; a new `PointerGestureTag(int Index)` returns the
overlay's two-glyph tag. Both images compile the same header, so the mapped struct stays
consistent, as it does today.

**Plugin.** `Binding::Kind` gains `HeadStick`, with `code` 0 for `head` and 1 for
`head-digital`; `MakeHeadStick(bool Digital)` builds it. In the pointer block of `GetKeys`
(the one that runs when `PJ64_POINTER_FD` mapped), a `HeadStick` binding loads `HeadX` and
`HeadY`; the analog form copies them into the axes, the digital form snaps them through
`PointerQuadrant` to `(0, 80)`, `(80, 0)`, `(0, −80)`, `(−80, 0)` or `(0, 0)`. Either sets
`StickFromKeys` so the gamepad cannot overwrite the stick, and stores its quadrant in
`Quadrant` exactly where the pointer stick does. The face-gesture branch is unchanged: it
tests eleven bits instead of three. `PublishPointerLabels` stores `HeadStickWanted` from a
new `InputConfig::UsesHeadStick()`.

`UsesPointer()` and `UsesFace()` both count a `HeadStick` binding, so a face-only layout
opens the 640x640 window and, with `PJ64_FACE` unset, starts the camera by itself, exactly
as a gesture binding does today. The frontend's quiet pre-load needs no change.

**Config reader** (`InputConfig.cpp`): `stick` accepts `head` and `head-digital`; `face`
accepts the eleven names; after the whole file parses, the head-direction rule of Part 1 is
checked and reported against the offending `face` value's mark. The `Quiet` path is
unchanged.

**Overlay** (`Overlay.cpp`). The tracker dot moves up to the top of the free band under the
middle slots (x 164..484, y from 72 px into the panel), and the strip flows right of it and
wraps onto up to two more rows below. The three labels beside it become a strip of one
entry per *bound* gesture in bit order, each the gesture's two-glyph tag, `=`, and the
bound control's label (`Mo=A`, `W<=C<`, `Br=Z`), drawn with the existing bitmap font at
scale 2 (the slot labels use 3), wrapping within the band so that eleven entries fit,
bright while held and dim otherwise. Tags, in bit order: `Br`, `H<`, `H>`, `H^`, `Hv`,
`T<`, `T>`, `Mo`, `Sm`, `W<`, `W>`. The font gains `=` if it lacks it. The guide over the
game draws unchanged: the lit quadrant and edge arrows follow `Quadrant`, which the head
stick now writes, so no new drawing is needed for it. Nothing new on the emulation thread
reads anything but atomics and the label tables.

## Part 4 — Shipped layouts

Files live under `Config/face/`, installed by `make config` beside `Config/mouse/` with the
same replace-don't-merge rule (`rm -f` then `cp -f`), and are selected explicitly with
`make run rom=… input=Config/face/<file>` or `PJ64_INPUT_YAML`. They are deliberately
outside the per-ROM lookup, which searches beside the ROM and then `Config/mouse/` only, so
a mouse layout and a face layout can share a ROM's base name without one shadowing the
other. They are named after their ROM's base name anyway, for the day the lookup grows.

`Config/face/super_mario_64_usa.yaml`:

```yaml
# Face-only layout for Super Mario 64. Select with
#   make run rom=... input=Config/face/super_mario_64_usa.yaml
# Head pose is the stick; every button here is a held facial gesture. See README
# "Playing with your face". Controls not listed keep their keyboard keys.

bindings:
  Stick:     {stick: head}
  A:         {face: mouth-open}    # hold longer for a higher jump
  B:         {face: smile}         # punch, and dive while running
  Z:         {face: eyebrows}      # crouch; brows + mouth while tilted = long jump
  CLeft:     {face: wink-left}     # camera
  CRight:    {face: wink-right}
  R:         {face: tilt-left}
  Start:     {face: tilt-right}
```

`L`, `CUp`, `CDown` and the D-pad are unbound and keep their built-in keyboard keys.

`Config/face/mario_kart_64_u.yaml`:

```yaml
# Face-only layout for Mario Kart 64. Select with
#   make run rom=... input=Config/face/mario_kart_64_u.yaml
# The analog head stick steers; hold the mouth open to accelerate. See README
# "Playing with your face". Controls not listed keep their keyboard keys.

bindings:
  Stick:     {stick: head}
  A:         {face: mouth-open}    # accelerate (held)
  R:         {face: eyebrows}      # hop, and drift while held
  Z:         {face: smile}         # use item
  B:         {face: tilt-left}     # brake
  Start:     {face: tilt-right}
```

Holding the mouth open to accelerate may prove tiring; the manual run (Part 6) decides,
and moving `A` to another gesture is one line.

## Part 5 — Errors and observability

- YAML errors keep the all-or-nothing rule and the message format
  (`input: <path>:<line>:<col>: <message>; using built-in defaults`). New messages: unknown
  gesture names as today; `stick must be left, right, pointer, head or head-digital`; the
  head-direction rule of Part 1.
- Camera denied, absent or failing: the head stick reads neutral, every bit is clear, one
  stderr line, status in the overlay, play continues. The keyboard keys of unbound controls
  keep working, so a denied camera never strands the player entirely.
- No face for half a second: bits clear and the stick centres.
- Nothing here can block the emulation thread: `GetKeys` reads two more atomics.

## Part 6 — Build, tests, verification, docs

**Build.** No new source files beyond `Config/face/*.yaml` and `Scripts/face_selftest.sh`;
no new frameworks. `make config` installs `Config/face/`. `make run` is unchanged
(`input=` already reaches any file).

**Tests.**

1. `make face-gesture-test`: every new channel sets and releases through synthetic samples;
   a blink fires neither wink; the head stick at rest, inside the dead zone, at full tilt,
   beyond it (clamped to the unit disc), on a diagonal, and with a left turn reading
   negative X and nose-up positive Y; the yaw baseline freezes at the dead zone with
   `HeadStickInUse` true and does not with it false, proving today's behaviour is kept for
   layouts without a head stick; no-face timeout centres the stick.
2. `make input-config-test`: the eight new names bind; `head` and `head-digital` bind on
   `Stick` and are rejected on `A`; `head-left` beside `{stick: head}` rejects the file
   with the Part 1 message and leaves the instance untouched; an unknown gesture still
   rejects; `UsesPointer()`, `UsesFace()` and `UsesHeadStick()` are true for both shipped
   face layouts and `UsesHeadStick()` is false for the three mouse layouts.
3. `make test`, `make pointer-layout-test`, `make pointer-selftest` and
   `make grid-selftest` unchanged and still green.
4. End to end without a camera. `PJ64_FACE_INJECT=<gesture-name>[,<x>,<y>]` makes the
   frontend publish that gesture's bit and that stick into `PointerState` on its main loop
   instead of starting the tracker; with it set the camera is never opened, whatever
   `PJ64_FACE` says. The plugin's `PointerSelftestReport` line gains `x=<n> y=<n>` and is
   also emitted, once, when `PJ64_POINTER_SELFTEST` is set and any face bit or head-stick
   axis is non-zero. `Scripts/face_selftest.sh <rom>` runs Mario with
   `Config/face/super_mario_64_usa.yaml` twice: injecting `mouth-open` must report `a=1`;
   injecting `eyebrows,80,0` must report `a=0 x=80 y=0`, which proves the head stick
   reached the mapped axes and that the layout's `Z`, not `A`, is on brows (the report also
   gains `z=<n>`). Every run sets `PJ64_FACE=0` as well, by policy; the `make face-selftest
   rom=…` target wraps the script like `pointer-selftest`.
5. Manual, Super Mario 64: run and steer by head, jump on mouth-open, crouch on brows,
   camera on winks, long jump from brows plus mouth while tilted. The pitch and roll signs
   are confirmed here and fixed in code. Default thresholds are tuned here.
6. Manual, Mario Kart 64: steer a lap with the analog head stick, hop on brows, and judge
   whether holding the mouth open to accelerate is bearable.

**Docs.** README gains "Playing with your face": the gesture table, the two stick forms,
the two files, the strip and its tags, the overrides, and the note that unbound controls
keep their keys. AGENTS gains the measure-channel architecture note (eight measures, one
baseline each, one channel per gesture, head stick from the classifier, two atomics to the
plugin) and three traps: `Config/face/` is outside the per-ROM lookup on purpose; the pitch
and roll signs are fixed in code after the first manual run, like yaw, so a sign that looks
wrong is a code fix, not a YAML one; and the yaw baseline freezes at the dead zone only
when a head stick is bound, which is what `HeadStickWanted` exists for.

## Open risks

- **Smile width under yaw.** Turning the head narrows the apparent lip width, so a held
  smile can release while steering hard with the head stick. The face box narrows too, which
  partly cancels it. If it bothers play, normalise width by the `faceContour` extent; not in
  this design.
- **Sustained gestures tire.** A held mouth or smile is harder to keep than a held key. The
  layouts put the most-held control (accelerate) on the mouth because it is the most
  reliable measure; the Kart run decides whether that stands.
- **Rest pose drifts while steering.** The yaw and pitch baselines hold while the stick is
  tilted, so a player who shifts posture mid-lap must sit still inside the dead zone for a
  few seconds to recentre. Accepted; a recentre gesture is the follow-up if needed.
- **Pitch and roll signs.** Unknown until the first manual run, as yaw's was; the code has
  one place per sign and the test pins the chosen convention.
- **Eye aperture with glasses or low light.** Vision's eye points can jitter; debounce and
  the release fraction absorb some of it, `PJ64_FACE_EYE` the rest.
- **No automated camera test.** The classifier and the injection path are tested; capture
  and Vision remain manual only, by the standing rule that automated runs never start the
  camera.
