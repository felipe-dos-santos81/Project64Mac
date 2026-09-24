# Emulator actions menu design

## Goal

A player with only a pointer and its left button can pause, resume, soft-reset, save and
load a state, toggle full screen, recentre the face tracker and quit, without a keyboard.
All of it is reached from one panel slot that opens a menu.

This is sub-project 2 of four that together make the emulator usable with no keyboard,
from launch to quit (sub-project 1, one-button mouse play, is
`Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md`). The player is the same:
limited hand use, a pointer and a reliable left click, no keyboard, no right button, no
scroll; the face is optional. Sub-project 3 is the launcher, which Quit will later return
to; sub-project 4 is the clickable wizard.

## Context

- **The core already has the actions.** `CN64System::ExternalEvent` queues
  `SysEvent_PauseCPU_FromMenu`, `SysEvent_ResetCPU_Soft`, `SysEvent_SaveMachineState` and
  `SysEvent_LoadMachineState` for the CPU thread, and `SysEvent_ResumeCPU_FromMenu`
  triggers the pause event directly. `SaveState()` and `LoadState()` use the slot in
  `Game_CurrentSaveState`. `GameRunning_CPU_Paused` reports whether the CPU is paused.
- **A paused game stops everything on the emulation thread.** `CN64System::Pause` blocks
  in `m_hPauseEvent.IsTriggered(INFINITE)`. The input plugin's `GetKeys` and the overlay,
  drawn in `CSdlRenderWindow::SwapWindow` when the game presents a frame, both run on that
  thread, so a paused game neither reads clicks nor redraws the panel.
- **The frontend's main loop keeps running.** `main.cpp` pumps SDL events every 10 ms,
  publishes the mouse into `PointerState`, starts the camera, and ends when the window
  closes. It is the one place that can see a click while the game is paused, and the one
  thread allowed to change the window (`SDL_SetWindowFullscreen`).
- **GL belongs to the emulation thread** (AGENTS.md). The main thread may not draw.
- **The panel is full.** The shipped mouse layouts use all thirteen slots except the
  D-pad's, which none of the three games needs.
- **Carried from sub-project 1:** `RomClosed` resets the click state with the button
  marked up, so a button held through a hard reset reads as a new press and can flip a
  toggle. This sub-project makes resets reachable by mouse, so it fixes that.

## Non-goals

- A menu that stays live while paused (redrawing from the main thread, or holding the
  emulation thread inside the swap). Rejected during design; see "The one-frame step".
- More than one save slot, a slot picker, or hard reset.
- Picking menu items by face gesture: a gesture may open and close the menu, but items
  are clicked.
- Menus in grid tiles, which never have a pointer layout.
- Returning to a game list on Quit (sub-project 3) and authoring `Menu:` in the wizard
  (sub-project 4).

## Design

### Behaviour

**Opening.** A layout binds `Menu: {zone: <slot>}` or `Menu: {face: <gesture>}`. The
slot's label is `==`. A press on it, or the gesture turning on, opens the menu: every
panel label is replaced by the menu's, the game gets no input, and once the overlay has
drawn the menu the game pauses.

**The menu.** One item per slot; every other slot is blank.

| Slot | Item | Label | Click |
|---|---|---|---|
| `mid3`, and the menu's own slot | Resume | `Go` | once |
| `mid1` | Full screen on/off | `Fs` | once; the menu stays open |
| `mid2` | Recentre the face | `Fc` | once; the menu stays open; shown only while the camera runs |
| `mid4` | Save, slot 0 | `Sv` | twice |
| `mid5` | Load, slot 0 | `Ld` | twice |
| `pad-left` | Reset, soft | `Rs` | twice |
| `c-right` | Quit | `Qt` | twice |

If the menu's own slot is one of the item slots above, the item wins there and Resume
stays on `mid3`. "The camera runs" means `PointerState::Face` is `FACE_STARTING`,
`FACE_TRACKING` or `FACE_NO_FACE`.

**Press twice.** The first click on a guarded item arms it: its slot lights. A second
click on the same slot does it. A click on any other slot disarms it (and, on another
item, does that item's own first click); a click on a blank slot or outside the panel
only disarms.

**What each item does.**
- *Resume* closes the menu and the game continues.
- *Save* and *Load* set `Game_CurrentSaveState` to 0, queue the save or load, close the
  menu and resume; the core runs the queued event straight away.
- *Reset* queues a soft reset, clears the toggles, the hold and the rest (sub-project 1's
  settle), closes the menu and resumes.
- *Full screen* toggles `SDL_WINDOW_FULLSCREEN` on the game window; the resizable-window
  work already scales the pinned surface into it.
- *Recentre* makes the face tracker forget its resting pose; the next frames become the
  new rest, as at start-up.
- *Quit* ends the emulator exactly as the window's close button does.
- The menu gesture, while the menu is open, is Resume.

**The one-frame step.** A paused game cannot redraw the panel, so after every menu click
that leaves the menu open (arming, disarming, Full screen, Recentre) the frontend resumes
the game until the overlay has drawn one frame, then pauses it again. The game gets no
input during the step, so the character stands still; the game advances by that one
frame, or a few if the queued pause lands late.

**No stray input.** While the menu is open, `GetKeys` returns all zeros from every
source: mouse, face, keyboard and gamepad. The click that closes the menu is still down
when the game resumes; it counts only after a release, so it never lands as B or A.
Toggles that were on when the menu opened are on again after Resume.

### Grammar

```yaml
bindings:
  Menu:  {zone: pad-down}     # or {face: tilt-right}
  Stick: {stick: pointer, hold: mid5}
```

- `Menu` is a key of `bindings` but not an N64 control: it is stored apart from the
  fifteen controls and never presses anything.
- It takes exactly `{zone: <slot>}` or `{face: <gesture>}`.
- Across the file, after every key is read: the menu slot is not `game`, not the stick's
  hold slot and no control's slot; the menu gesture is no control's gesture; and the
  existing head-direction rule applies to it (no `head-*` gesture beside a head stick).
- A menu gesture counts for `UsesFace`, so it starts the camera; a menu slot counts for
  `UsesPointer`, so it gives the window its panel.

### Components

**`Source/Common/PointerMenu.h`** (new, pure; no SDL, no core):

- `enum PointerMenuItem` (`MENU_ITEM_NONE`, `MENU_ITEM_RESUME`, `MENU_ITEM_FULLSCREEN`,
  `MENU_ITEM_RECENTRE`, `MENU_ITEM_SAVE`, `MENU_ITEM_LOAD`, `MENU_ITEM_RESET`,
  `MENU_ITEM_QUIT`); `PointerMenuItemAt(int Zone, int MenuZone, bool FaceOn)`, the table
  above; `PointerMenuLabel(PointerMenuItem)`; `PointerMenuGuarded(PointerMenuItem)`.
- `struct PointerMenu { bool Open = false; int Armed = POINTER_ZONE_NONE; bool PrevButton
  = false; bool PrevGesture = false; }`.
- `enum PointerMenuAction` (`MENU_ACTION_NONE`, `OPEN`, `REPAINT`, `RESUME`, `FULLSCREEN`,
  `RECENTRE`, `SAVE`, `LOAD`, `RESET`, `QUIT`) and
  `PointerMenuAction PointerMenuStep(PointerMenu *, bool Button, int Zone, bool Gesture,
  int MenuZone, bool FaceOn)`: the opening rule, press-twice, disarming, and which
  actions close the menu (`RESUME`, `SAVE`, `LOAD`, `RESET`, `QUIT` set `Open` false).
  `REPAINT` means "the menu's picture changed and it stays open"; `FULLSCREEN` and
  `RECENTRE` also leave it open and need the same repaint.

**`Source/Common/PointerState.h`** gains:

| Field | Writer | Readers |
|---|---|---|
| `MenuZone` (int32, `POINTER_ZONE_NONE` when none) | plugin, once at load | overlay |
| `MenuOpen` (uint32) | frontend | plugin, overlay |
| `MenuArmed` (int32 zone) | frontend | overlay |
| `OverlayFrames` (uint32, +1 per `OverlayDraw`) | overlay | frontend |
| `ClearClicks` (uint32 counter) | frontend | plugin |
| `RecentreFace` (uint32 flag) | frontend | face tracker (clears it) |

**`Source/Project64-sdl/InputConfig.{h,cpp}`**: `ParseBinding`'s caller recognises
`Menu` before looking up a control and stores a `Binding` of kind `Zone` or `Face` in a
new member; `MenuZone()` returns its slot or `POINTER_ZONE_NONE`, `MenuGesture()` its
gesture bit or 0. `CheckSlots` gains the menu rules; `PointerLabels` writes `==` on the
menu slot; `UsesPointer` and `UsesFace` count the menu.

**`Source/Project64-sdl/MenuHost.{h,cpp}`** (new; frontend, main thread only). Created by
`main.cpp` when the process has a `PointerState` and is not a grid tile; `main.cpp`'s loop
calls it once per pass and ends when it returns "quit". Each pass it snapshots the
published `PointerSample` (launch-size pixels, as AGENTS.md requires), evaluates the zone,
reads the menu gesture's bit and the face status, and runs `PointerMenuStep` whenever it
is closed or paused. Its phases:

1. *Closed.* `OPEN` → set `MenuOpen`, note `OverlayFrames`, go to Drawing. Also heals a
   late pause: if `GameRunning_CPU_Paused` is true with `GameRunning_CPU_PausedType` ==
   `PauseType_FromMenu`, it resumes (a resume sent before a queued pause lands is wiped
   by `Pause()`).
2. *Drawing.* Heals a late pause as Closed does, then waits for `OverlayFrames` to move by
   two frames — a frame already in flight when the menu opens may finish without it, so
   only the second one certainly drew the menu — or 2 s, then queues `PauseCPU_FromMenu`
   and goes to Pausing.
3. *Pausing.* When `GameRunning_CPU_Paused` is true, go to Paused. After 1 s, print one
   stderr line and go to Paused anyway (the game keeps running; every item still works).
4. *Paused.* `REPAINT`, `FULLSCREEN`, `RECENTRE` → act, publish `MenuArmed`, resume, note
   `OverlayFrames`, go to Stepping. A closing action → queue its event (after setting
   `Game_CurrentSaveState` to 0 for Save and Load; after bumping `ClearClicks` for
   Reset), clear `MenuOpen` and `MenuArmed`, resume, go to Closed. `QUIT` → report quit.
5. *Stepping.* Waits for `OverlayFrames` to move (the game drew a frame), then queues the
   next pause and goes to Pausing. Only if 500 ms pass without a frame does it look at the
   paused flag: false means the CPU is running, so it queues the pause and goes to
   Pausing; still true means the resume was lost, so it resumes again and restarts the
   wait.

Presses during Drawing, Pausing and Stepping are ignored, but the button's state is still
tracked, so a press that began then needs a release before it counts. With
`PJ64_MENU_SELFTEST` set, each phase change prints `menu: <phase>` on stderr.

**The input plugin** (`PluginInput.cpp`): `PublishPointerLabels` stores `MenuZone`.
`GetKeys` returns all zeros while `MenuOpen` is set, before reading any source, and marks
the button as already down (`PrevButton = true`). When `ClearClicks` differs from the value
it last saw, it resets the click state and the rest, keeping the button marked down.
`RomClosed` does the same, which fixes the carried finding.

**`Overlay.cpp`**: while `MenuOpen` is set, `DrawPanel` draws each slot's menu label
(`PointerMenuLabel(PointerMenuItemAt(...))`, blank for none), lights `MenuArmed`, and draws
no corner marks; `DrawGuide` is skipped. `OverlayDraw` adds one to `OverlayFrames` every
time it runs, menu or not. The 5x7 font gains `G`, `F`, `s`, `c`, `d` and `Q`.

**The face tracker**: `GestureClassifier::Rebaseline()` forgets the resting pose
(`m_HaveBaseline = false`), so the next sample becomes the new rest.
`FaceTracker.mm`'s frame handler, on its queue, calls it when
`RecentreFace.exchange(0)` is non-zero.

**The wizard, round trip only**: `WizardDraft` keeps the loaded base's `Menu` and emits
it after the controls (`Menu:      {zone: pad-down}`); `SetZone` or `SetGesture` onto the
menu's slot or gesture takes it off the menu, as `SetZone` already does for the hold slot;
the zone screen labels the menu slot `==`. The screenshot tour loads base row 0, the
built-in bindings, so the pictures do not change.

### Layouts

The three `Config/mouse/` layouts replace `DPadDown: {zone: pad-down}` with
`Menu: {zone: pad-down}`; D-pad down keeps its keyboard key. Their comments name `==`
and say what the menu holds.

## Errors and observability

- The reader rejects a bad file with one line and changes nothing, as today:
  `menu must be {zone:} or {face:}`; `the menu cannot be game`;
  `<slot> is the menu slot and cannot also be bound` (a control's slot or the hold slot);
  `<gesture> is the menu's gesture and cannot also be bound`.
- The overlay not drawing within 2 s of opening, or within 500 ms of each step (a game
  presenting no frames on some screen, as some do for over a second while booting): the
  host pauses anyway; the menu may look stale until the game presents a frame.
- The pause not confirmed within 1 s: one stderr line, `menu: the game did not pause`;
  the menu stays usable with the game running.
- Save or Load failing, or Load finding no save: the core reports it on stderr through
  `g_Notify`, as it does for any save; the menu closes regardless.
- Quit while paused: `CN64System::CloseSystem` ends a paused CPU itself (`CloseCpu` sets
  `m_EndEmulation` and triggers the pause event), so Quit needs no resume of its own.
- With `PJ64_MENU_SELFTEST` set, each phase change prints `menu: <phase>` on stderr, and
  four more lines mark the cases above: `menu: drawn` and `menu: draw timed out` (Drawing's
  two ways out), `menu: healed a late pause` (Closed or Drawing undoing a pause that lost
  its race with a resume), and `menu: resume retried` (Stepping resending a resume that has not
  taken after 500 ms).

## Tests

Following AGENTS.md: one unit-test program, existing checks extended before new ones
added.

- New `pointer-menu` area (`Source/Project64-sdl/PointerMenuTest.cpp`,
  `RunPointerMenuTests()`), registered in `UnitTestMain.cpp` and `UNIT_TEST_OBJS`:
  opening by a press on the menu slot and by the gesture's rising edge (holding either
  does not re-open); Resume from `mid3` and from the menu slot; each guarded item arms
  then fires; a click elsewhere disarms, and on another guarded item arms that one; a
  blank slot or a click outside the panel only disarms; `Fc` is blank without the camera;
  the item on the menu slot wins over Resume there; a press held when the menu opens
  counts only after a release; the menu gesture while open resumes; every item has a
  two-character label.
- `input-config` area: the two `Menu` forms, each of the four errors, the head-direction
  rule on a menu gesture, `==` on the menu slot, `UsesPointer`/`UsesFace` for a menu, and
  `MenuZone() == 1` for each shipped mouse layout.
- `face-gestures` area: after `Rebaseline()` the next sample is the rest, so a gesture
  held across it reads as off.
- `wizard-draft` area: `Menu` round-trips; `SetZone` onto the menu slot takes it off.
- `make pointer-selftest` gains a fifth run: the shipped Super Mario 64 layout, a static
  inject pressed on `pad-down` (80,608) and `PJ64_MENU_SELFTEST=1`; stderr must reach
  `menu: drawn` and `menu: paused`, which proves open → draw → pause end to end, and the
  run fails on `menu: draw timed out` or `menu: the game did not pause`.
- `make wizard-screenshots-check` passes with the pictures unchanged.
- Manual, once, each game: every item, press-twice, the one-frame steps, Quit while
  paused.

## Documentation

- `Docs/UserGuide.md` section 7: "The menu" — opening it, the item table, press-twice,
  the one-frame step, and that D-pad down moved to its keyboard key in the shipped
  layouts. Section 11: `PJ64_MENU_SELFTEST`.
- `AGENTS.md`: `MenuHost` and the overlay-frame handshake in the mouse paragraph, and a
  trap: a paused game never redraws the panel; every menu change is shown by stepping one
  frame, never by drawing from the main thread.

## Open risks

- **The step's length.** A queued pause lands at the core's next event check, so a step
  may run a few frames rather than one, still with no input.
- **Games that stop presenting frames** on some screens make every menu change wait for
  the 500 ms timeout and look stale until then.
- **Soft reset closes and reopens the plugins late.** The core runs every plugin's
  `RomClosed` and `RomOpen` about a second of game time after a soft reset, while the game
  runs; the menu's Reset is the first way to reach that in this port. `ClearClicks` clears
  the clicks at once rather than waiting for it.
