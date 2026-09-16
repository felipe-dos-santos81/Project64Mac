# Binding wizard design

## Goal

A second binary, `Bin/macOS/Project64-wizard`, walks the player through the fifteen N64
controls and writes an input-mapping YAML file the emulator accepts. It reaches every
binding form the reader supports — keyboard, gamepad button, gamepad axis, panel slot,
face gesture, and all six `Stick` forms — by watching what the player does rather than
asking them to type `lefttrigger` or `head-digital`.

## Context

Bindings are read once at startup from a YAML file: `Bin/macOS/Config/input.yaml`, a file
named after the ROM (beside the ROM, then `Config/mouse/`), or `PJ64_INPUT_YAML`. Naming a
control in the file replaces its built-in binding; omitting it keeps the built-in one.
There is no settings UI, so today a mapping is written by hand against the README.

Three vocabularies have to be discoverable, and none of them is guessable:

- SDL scancode and gamepad names, as `SDL_GetScancodeFromName` spells them.
- The fourteen pointer zones — thirteen panel slots plus the game image.
- The eleven face gestures the tracker now classifies.

The wizard exists to make all three reachable without documentation.

## Non-goals

- Editing bindings while a game runs. The wizard never loads a ROM.
- Replacing the shipped layouts. It starts from them; it does not overwrite them.
- A general settings UI. Only the input mapping.
- Any change to the reader's grammar. The wizard is a writer of the existing language.

---

## Part 1 — Architecture

`Source/Project64-wizard/` builds `Bin/macOS/Project64-wizard` as Makefile step 7c,
linking SDL3, yaml-cpp and the Apple frameworks the tracker needs. It does not link
OpenGL: drawing is `SDL_Renderer`, and text is `SDL_RenderDebugText`, SDL3's built-in 8x8
ASCII font, scaled with `SDL_SetRenderScale`. The emulator's overlay font is a
twenty-glyph tag font and cannot spell `Left Shift`; it is not reused.

| File | Responsibility |
|---|---|
| `WizardDraft.{h,cpp}` | the draft mapping, base loading, YAML emission, validation |
| `WizardDraftTest.cpp` | headless tests for all of the above |
| `Screens.{h,cpp}` | draw one screen; turn one SDL event into one draft change |
| `main.cpp` | SDL init, window, event loop, screen dispatch, tracker lifetime |

Four things are reused rather than reimplemented:

- `InputConfig` — built-in defaults, `ControlLabel`, and validation.
- `PointerLayout.h` — `PointerZoneRect`, `PointerZoneName`, `PointerQuadrant`, so the
  clickable panel is the same geometry the game draws.
- `PointerState.h` — `PointerGestureName`, `PointerGestureTag`, `POINTER_GESTURE_COUNT`.
- `FaceTracker` — the wizard owns a plain `PointerState` (no shared memory; nothing else
  reads it) and passes it to `FaceTrackerStart`.

The split is deliberate: everything that can be silently wrong — which bindings the draft
holds and what YAML they become — lives in `WizardDraft`, which has no window, no
renderer and no event loop, and is therefore unit-tested headlessly. `Screens` and
`main.cpp` are a shell that feeds it events.

The window is 720x640, resizable, one screen at a time.

---

## Part 2 — The screens

**Base.** The starting point: built-in defaults, one of the five shipped layouts
(`Config/mouse/super_mario_64_usa.yaml`, `goldeneye_007_u.yaml`, `mario_kart_64_u.yaml`,
`Config/face/super_mario_64_usa.yaml`, `mario_kart_64_u.yaml`), or a path typed in. The
base is copied into the draft; the base file is never written to.

**Fifteen control screens**, in controller order: A, B, Z, Start, L, R, CUp, CDown,
CLeft, CRight, DPadUp, DPadDown, DPadLeft, DPadRight, Stick. Each names the control, says
what the draft binds it to in plain words (`key X`, `zone mid1`, `gesture mouth-open
(Mo)`, `inherited: key X or key Space`), and lists the ways to change it. Enter keeps and
advances; Backspace goes back; Esc jumps to the review; Delete clears to inherited.

**Stick** is the fifteenth screen and has its own six-way chooser (Part 3).

**Review.** All fifteen with their bindings, inherited ones marked. Then the save
destination.

**Save.** Three destinations: `Bin/macOS/Config/input.yaml` (the default mapping, which
`make` installs with `cp -n` and never overwrites), beside a ROM chosen by path so the
per-ROM lookup finds it, or a typed path. A destination under `Config/mouse/` or
`Config/face/` is accepted only after a warning that `make` deletes and recopies both
directories on every build.

---

## Part 3 — Capture

A control screen offers five numbered modes. Modes exist because a bare "press what you
want" capture would make the navigation keys unbindable; inside a mode, input is taken
literally.

| Key | Mode | Records | Emitted as |
|---|---|---|---|
| 1 | keyboard | the next keydown, verbatim, including Escape, Enter and the arrows | `{key: <name>}` |
| 2 | gamepad button | the next button press | `{button: <name>}` |
| 3 | gamepad axis | the first axis past the plugin's `STICK_THRESHOLD` | `{axis: <name>, sign: +}` or `-` |
| 4 | panel slot | a click on the drawn panel: thirteen slots plus the game image | `{zone: <name>}` |
| 5 | face gesture | a row of the eleven-row list | `{face: <name>}` |

Names come from `SDL_GetScancodeName`, `SDL_GetGamepadStringForButton` and
`SDL_GetGamepadStringForAxis`, the inverses of the `…FromString` calls the reader uses.

Leaving a mode: modes 2 to 5 do not capture keys, so Escape cancels them and returns to
the control screen. Mode 1 cannot be cancelled, because the next keydown is the binding by
definition — including Escape. To correct a mistaken key, press 1 and capture again.
Escape on a control screen, outside any mode, jumps to the review. Modes 2 and 3 say so
and record nothing when no gamepad is connected.

**Mode 4** draws the panel at the real `PointerLayout.h` geometry, scaled to fit, with
each slot's current binding drawn in it, so the choice is made against the layout the
player will see in the game.

**Mode 5** lists all eleven gestures as `Tag  name` rows — `Br eyebrows`, `H< head-left`,
`H> head-right`, `H^ head-up`, `Hv head-down`, `T< tilt-left`, `T> tilt-right`,
`Mo mouth-open`, `Sm smile`, `W< wink-left`, `W> wink-right`. When the camera is running,
every gesture currently firing is lit. Arrows move the highlight and Enter takes it;
Space takes the lit gesture when exactly one is firing. The list is fully usable with no
camera — unlit, but pickable.

**Inherited is a state.** Clearing a control omits it from the file, which is not the same
as leaving it unbound: several built-in defaults are *pairs* of keys, and the grammar
allows exactly one input per control, so a paired default can only survive by omission.
The draft tracks explicit-versus-inherited per control and the review shows which is which.

**Stick's six forms:** `{stick: left}` and `{stick: right}` (chosen, or captured by moving
a gamepad stick), `{stick: pointer}`, `{stick: head}`, `{stick: head-digital}`, and
`{keys: {up, down, left, right}}`, whose four keys are captured in sequence.

---

## Part 4 — The draft and the file

`WizardDraft` holds, per control, a `std::vector<Binding>` and an explicit flag.

**Loading a base** takes two reads, because `InputConfig` merges a file over the defaults
and so cannot say which controls the file named. `InputConfig::Load` resolves the values;
a separate yaml-cpp read of the base file's `bindings` map yields the key set, and only
those controls start explicit. Built-in defaults start with nothing explicit.

**Emission** writes a header comment naming the wizard and the base, then `bindings:` and
one line per explicit control in controller order. A value is quoted unless it matches
`[A-Za-z0-9_-]+`, because SDL spells names like `Left Shift` and `Keypad Enter`. Emission
is hand-written rather than yaml-cpp's emitter, so the result reads like the shipped
layouts.

**Validation is a round-trip through the real parser.** The draft writes to a temp file,
calls `InputConfig::Load(path, true)`, and refuses to save when that returns false. The
review screen runs the same round-trip on arrival and, when it fails, shows the reader's
own error line rather than a paraphrase — so a rule such as "`head-left` cannot be bound
while Stick is head" has exactly one statement of itself in the codebase and the wizard
cannot drift from it.

---

## Part 5 — Camera, errors and privacy

The tracker starts lazily, the first time gesture mode is entered, so launching the wizard
never opens a camera. `PJ64_FACE=0` keeps it shut; the gesture list still works. A denied
or absent camera shows in `PointerState::Face`, the list says so, and every gesture stays
pickable. Frames are never stored, shown or written, and nothing about them is logged —
the only thing on screen is which gestures are firing. `FaceTrackerStop` runs on exit.

Failures are one line on stderr and a message on the screen that caused them, never a
crash: a base path that does not exist or does not parse, a save path that cannot be
written, a round-trip the reader rejects. The wizard exits non-zero only when SDL itself
cannot start.

One piece of housekeeping: `Source/Project64-sdl/FaceTracker.h`'s comment still says the
plugin reads "the three bits this writes", which the eleven-gesture work made stale.

---

## Part 6 — Build, tests, docs

- `make wizard` builds it; `make all` includes it. Step 7c, after the frontend.
- `make wizard-draft-test` runs `WizardDraftTest` headlessly: every binding form emits
  text the reader accepts and round-trips back to the same binding; quoting for names with
  spaces; inherited controls stay out of the file; a paired default survives clearing; the
  head-direction rule surfaces the reader's message; each of the five shipped layouts
  loads as a base and re-emits to a file the reader still accepts.
- `make wizard-selftest` runs the binary with `--selftest`, which drives a canned sequence
  of synthetic SDL events through the real screens, writes YAML to a temp path and
  compares it with an expected file. No ROM, no camera, bounded run.
- README gains a section on the wizard; AGENTS.md gains the wizard to its architecture
  notes and one trap: the draft's inherited-versus-explicit distinction, and why a paired
  default cannot be written out.

## Open risks

- `SDL_RenderDebugText` is documented as a debugging convenience with a fixed 8x8 font.
  It is the right tool for a utility screen and wrong for anything that wants to be
  pretty; if the wizard ever needs proportional text, this is the decision to revisit.
- The synthetic-event self-test proves the plumbing, not the feel. Whether five numbered
  modes is the right interaction is a question only a real session answers.
- A gamepad axis capture takes the first axis past the threshold, which on a noisy or
  drifting stick may not be the axis the player meant. The player sees what was recorded
  and can redo it, but the wizard does not debounce beyond the plugin's own threshold.
