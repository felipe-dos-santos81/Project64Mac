# One-button mouse play design

## Goal

A player who can move a pointer and press its one (left) button, and who cannot use a
keyboard, can play the shipped mouse games with the mouse alone: holding a button while
pressing another, and keeping the stick tilted while reaching for a panel button. No face
gesture and no keyboard is required, and no new latency is added to any click.

This is the first of four sub-projects that together make the emulator usable with no
keyboard at all, from launch to quit. The player, agreed on 2026-09-24: limited hand use,
a pointer and a reliable left click, no reliable right button, middle button or scroll.
The four, in order, each with its own spec, plan and implementation:

1. **One-button mouse play** — this spec.
2. **Emulator actions** — pause, reset, save and load state, full screen, recentre the
   face, quit, bindable to panel slots or gestures.
3. **Launcher** — a window that lists the ROMs and layouts and starts a game by click;
   quitting a game returns to it; likely a double-clickable `.app`.
4. **Clickable wizard** — every binding-wizard screen operable by mouse, including
   authoring the toggle slots and the stick hold this spec introduces.

Success for the whole series is getting from a Finder double-click, through play and a game
change, to quitting, with no keyboard. This spec alone does not reach that: until the
launcher lands, starting a game still needs someone at a terminal.

## Context

`PublishMouse` in `Source/Project64-sdl/main.cpp` publishes the cursor in launch-size pixels
and one bit, `SDL_BUTTON_LMASK`, into `PointerSample::Button`. `GetKeys` in
`Source/Project64-sdl/PluginInput.cpp` evaluates the sample with `PointerLayoutEvaluate`,
applies the flick gate (`PointerGateStick`), and on the press edge latches the zone under
the cursor into `g_PointerLatched` until the button is released. A zone binding fires while
its zone is the latched one; the pointer stick copies the gated stick.

That gives a one-button player two limits:

- **One button at a time.** A single latched zone means holding Z on the panel leaves no way
  to also press A. Today the shipped `Config/mouse/` layouts put two or three buttons on
  face gestures for exactly this reason.
- **The panel releases the stick.** `PointerGateStick` forces a neutral stick whenever the
  cursor is outside the game image, so "run, then press B to dive" cannot be done by mouse.
  The flick gate only bridges a single fast jump; a slow move to the panel crosses the game
  image's bottom edge, which reads as a full backward tilt.

The reader (`InputConfig::Load`) allows two controls on the same zone — both fire — and
every form except `{axis:}` is a one-key map. `Binding` is built by aggregate
initialisation both in `InputConfig.cpp` (`Binding{kind, code, positive, …}`) and in the
wizard (`Binding B = {}`); the Makefile compiles C++14, so a default member initialiser
keeps both forms valid. The wizard (`Source/Project64-wizard/WizardDraft.cpp`) loads a
base layout into a draft, lets controls share an input ("also bound elsewhere"), and emits
YAML from each binding's facts.

## Non-goals

- Right button, middle button, side buttons and the scroll wheel. The player cannot use them
  reliably; only the left button is read.
- Double-click, long-press or drag as distinct inputs. Telling them from a click delays the
  click, which the timing-sensitive games this fork targets cannot afford.
- Dwell (hover to press). It serves a player who cannot click, which this player can.
- Capturing, confining or warping the cursor. The cursor is never captured, as today.
- Authoring toggles or the hold in the binding wizard. The wizard keeps them when it loads a
  base and writes them back; creating them is sub-project 4.
- Emulator actions, the launcher and the clickable wizard (sub-projects 2-4).

## Design

### Behaviour

**Toggle slots.** A zone binding may carry `toggle: true`. A press of the button over a
toggle slot flips its controls on or off; the release does nothing, and the press is not
latched, so the button is immediately free for anything else. With Z toggled on, a click in
the game image gives Z and A together. Any zone may be a toggle, `game` included (a click
in the game image then flips its control, and the stick keeps following the cursor). Every
toggle clears when the ROM closes, so a new game never starts with a button held.

**The stick hold.** `Stick: {stick: pointer, hold: <slot>}` makes one panel slot the hold
slot. It belongs to no N64 control. A press over it turns the hold on or off. While the
hold is on, the game's stick is the **last settled tilt** (below), wherever the cursor is.
The hold ends on a second press over the hold slot, or when the cursor **settles in the
game image** — so the stick stays held while the cursor travels back up through the
image's bottom edge, a click in the image on the way is A with the held tilt, and once the
cursor rests the stick follows it again from wherever the player put it. The hold clears
when the ROM closes.

**Last settled tilt.** The cursor settles when it stays within 8 px of an anchor point for
9 consecutive polls inside the game image (a poll is one of the game's controller reads:
about 150 ms at 60 a second, 300 ms at 30). At the poll where that count is reached, the
gated stick is recorded as the settled tilt; the count then keeps rising without recording
again until the cursor leaves the 8 px radius, which moves the anchor there and restarts
the count. Leaving the game image resets the anchor and the count but keeps the recorded
tilt. A slow drag to the panel never stays within 8 px for 9 polls, so it never records
the backward tilt; a flick does not either, and the flick gate already holds the stick
during the jump. Before the first settle since the ROM opened the settled tilt is neutral.

`PJ64_POINTER_SETTLE=<px>,<polls>` sets the radius and the count for players whose hands
move more or less. `0` turns settling off: the hold then always gives neutral and ends only
on a second press. A value that is not `0` and not two positive numbers keeps the defaults
and prints one stderr line naming the variable.

**The panel.** A toggle slot and the hold slot draw a small mark in one corner, so the
player can tell them from momentary slots before pressing. A slot is lit while it is
latched (as today), while its toggle is on, or, for the hold slot, while holding. The hold
slot's label is `Ho` (the overlay font has `H` and `o`). A toggled-on `game` zone lights
the guide over the game image the way a held click does.

**Unchanged.** A layout with no `toggle:` and no `hold:` behaves exactly as today, byte for
byte in what `GetKeys` returns. The cursor is never captured, confined or warped. Only the
left button is read. `main.cpp` is not touched.

### Grammar

```yaml
bindings:
  Stick: {stick: pointer, hold: mid5}   # mid5 is the hold slot
  Z:     {zone: mid2, toggle: true}     # one press on, the next off
  A:     {zone: game}                   # momentary, as today
```

- `toggle:` is allowed only in `{zone:}`, must be `true` or `false`, and `false` is the same
  as leaving it out.
- `hold:` is allowed only in `{stick: pointer}` and names a panel slot (any zone but
  `game`).
- Across the file, after every control is read (the way the head-direction rule is
  checked): every zone binding on one slot must agree on `toggle`, and the hold slot must
  not be any control's zone.

### Components

**`Source/Common/PointerLayout.h`** gains the pure logic, so `make pointer-layout-test`
covers it without a window:

- `struct PointerSettle` (anchor, count, `HaveSettled`, `SettledX`, `SettledY`,
  `JustSettled`) and `PointerSettleStep(PointerSettle *, const PointerEval &, float X,
  float Y, float Radius, int Polls)`, fed after `PointerGateStick` each poll. `JustSettled`
  is true only on the poll the count reaches `Polls` inside the game image. `Radius <= 0`
  never settles.
- `struct PointerClicks` (`PrevButton`, `Latched`, `Toggled` — one bit per zone —
  `Holding`, `HeldX`, `HeldY`) and `PointerClickStep(PointerClicks *, bool Button, int
  Zone, uint32_t ToggleMask, int HoldZone, const PointerSettle &)`. On the press edge: a
  zone in `ToggleMask` flips its bit and nothing is latched; the hold zone flips `Holding`
  and, when turning on, copies the settled tilt (or neutral) into `HeldX/HeldY`; any other
  zone is latched, as today. With the button up, nothing is latched. While holding, a
  `JustSettled` ends the hold. This one function replaces the latch lines in `GetKeys`, so
  the old rule and the new ones are tested together.
- `POINTER_SETTLE_PX` (8) and `POINTER_SETTLE_POLLS` (9) beside `POINTER_FLICK_PX`.

**`Source/Common/PointerState.h`** gains two fields: `ToggleZones` (the toggle slots and
the hold slot, one bit per zone, written once when the plugin loads the layout, before the
ROM opens, like `Labels`) and `ToggledZones` (the slots that are on, written every
`GetKeys`). The frontend and the plugin compile this one header, so the shared layout stays
in step.

**`Source/Project64-sdl/InputConfig.{h,cpp}`**:

- `Binding` gains `bool Toggle = false;` and `int Hold = POINTER_ZONE_NONE;`. `Hold` is a
  field of its own rather than the pointer binding's unused `code`, because every existing
  constructor writes `code = 0`, which is the `pad-up` zone.
- `ParseBinding` accepts `toggle:` beside `zone:` and `hold:` beside `stick: pointer`,
  with the errors below; the other forms stay one-key maps.
- `Load` runs the two cross-binding checks after the loop.
- `PointerLabels` writes `Ho` for the hold slot; `PointerToggleZones()` returns the toggle
  and hold mask; `PointerHoldZone()` returns the hold slot or `POINTER_ZONE_NONE`.

**`Source/Project64-sdl/PluginInput.cpp`**: `OpenPointerState` parses
`PJ64_POINTER_SETTLE` beside `PJ64_POINTER_FLICK`. `PluginLoaded` publishes
`ToggleZones`. `GetKeys` runs gate, settle and click step; presses every control whose
zone is latched or toggled on; sets the pointer stick to `HeldX/HeldY` while holding and to
the gated stick otherwise; and publishes `LatchedZone`, `ToggledZones` (toggle bits, plus
the hold slot's bit while holding) and `Quadrant` (of the stick the game got).
`RomClosed` resets the settle and click state.

**`Source/Project64-sdl/Overlay.cpp`**: `DrawPanel` lights a slot when it is latched or its
`ToggledZones` bit is set, and draws the corner mark on `ToggleZones` slots. `DrawGuide`
treats a toggled-on `game` bit like a latched `game`.

**The wizard — round trip only.**

- `WizardDraft` keeps `Toggle` and `Hold` from a loaded base and emits them:
  `{zone: mid2, toggle: true}`, `{stick: pointer, hold: mid5}`.
- `Describe` shows them: `zone mid2, toggle`; `stick pointer, hold mid5`.
- The draft never writes a file the reader rejects. `SetZone` onto a slot that another
  control holds as a toggle makes the new binding a toggle too; `SetZone` onto the hold
  slot clears the stick's hold; `SetStickPointer` starts with no hold.
- The zone screen labels the hold slot `Ho`, so the player sees it is taken.
- The screenshot tour loads base row 0, which has no toggles and no hold, so the committed
  pictures do not change.

### Layouts

The three `Config/mouse/` layouts become mouse-only: every button moves off face gestures
onto a slot, so the camera no longer starts for them. Face play stays available through
`Config/face/` and any custom layout. Their comments explain the toggles and the hold.

| Control | Super Mario 64 | GoldenEye 007 | Mario Kart 64 |
|---|---|---|---|
| Stick | pointer, hold `mid5` | pointer, hold `mid5` | pointer, hold `mid5` |
| A | `game` | `mid1` | `game`, toggle (accelerate) |
| B | `mid3` (punch, dive) | `mid2` | `mid4` (brake) |
| Z | `mid2`, toggle (crouch) | `game` (fire) | `mid3` (item) |
| R | `mid4` (camera) | `mid4`, toggle (aim) | `mid2`, toggle (hop, drift) |
| Start | `mid1` | `mid3` | `mid1` |
| L | not listed | not listed | not listed |
| C-up/down/left/right | the C cross | the C cross | the C cross |
| D-pad | the pad cross | the pad cross | the pad cross |

L is left out of all three because none of the three games needs it and the five middle
slots are taken; it keeps its built-in keyboard binding. Two walkthroughs in the guide show
the point:

- **Mario's dive:** run with the cursor, press Hold, press B.
- **Mario's long jump:** run, press Hold, press the Z toggle, move up into the game image,
  click (A); press Z again afterwards to stand.

## Errors and observability

The reader rejects a bad file with one line and leaves the bindings as they were, as today:

- `toggle must be true or false`
- `toggle only applies to {zone:}`
- `hold only applies to {stick: pointer}`
- `hold must name a panel slot` (an unknown name, or `game`)
- `<slot> is the stick's hold slot and cannot also be bound`
- `<slot> is a toggle for <control> but not for <control>`

`PJ64_POINTER_SELFTEST`'s report is unchanged: it already prints `zone=` and `z=`, and a
toggle shows as `zone=-1` (nothing latched) with the toggled control on.

## Tests

No new framework; each suite already exists.

- `make pointer-layout-test`:
  - `PointerSettleStep`: a rest settles once at the ninth poll, a slow drag of 2 px a poll
    never settles, a flick does not move the settled tilt, leaving the image resets the
    count but keeps the tilt, it is neutral before any settle, and a radius of 0 never
    settles.
  - `PointerClickStep`: a toggle flips on the press edge only and ignores the release;
    pressing a toggle latches nothing; a toggle and a latched zone are on together; the hold
    turns on and off by press and copies the settled tilt or neutral; a settle in the game
    image ends the hold; a layout with no toggles and no hold latches exactly as the old
    code did.
- `make input-config-test`: each new form parses, each error above fires, and a file
  without the new keys produces the same table as before.
- `make wizard-draft-test`: a base with a toggle and a hold round-trips through load and
  emit; `Describe` shows both; `SetZone` onto a toggle slot inherits the toggle; `SetZone`
  onto the hold slot clears the hold.
- `make pointer-selftest` gains one case: a test layout with `Z: {zone: mid2, toggle:
  true}` and a static inject pressed over `mid2`. A static inject is one press edge, so the
  report must read `zone=-1` with `z=1`. The hold needs motion, which the pure tests cover.
- `make wizard-screenshots-check` passes with the pictures unchanged.
- Manual, once: each rewritten layout with its game, face off, playing the table's moves;
  the dive and the long jump in Super Mario 64 as described.

## Documentation

- `Docs/UserGuide.md` section 7: a subsection "Toggle slots and the stick hold" with the
  forms, the settle rule, the panel marks, and the dive and long-jump walkthroughs; the
  layouts' description updated now that they are mouse-only and do not start the camera.
  Section 9's note that a matching layout can start the camera stays true of custom layouts.
- Section 11: `PJ64_POINTER_SETTLE`.
- `AGENTS.md`: the mouse paragraph names `PointerSettleStep` and `PointerClickStep`, and a
  trap records that the cursor is never captured, confined or warped.

## Open risks

- **The settle constants are a guess.** 8 px and 9 polls come from reasoning, not a
  player, and a poll's length follows the game's controller rate, so the same setting is a
  different time in different games. A tremor may never settle (the hold then only ends on
  a press, which is still safe), and a very slow, deliberate move may settle on the way to
  the panel. `PJ64_POINTER_SETTLE` exists for this; the manual run should try both a slow
  and a fast reach.
- **Game knowledge in the table.** The GoldenEye and Mario Kart assignments assume L is
  unused and that R is the button worth toggling; the manual run confirms or moves them.
