# Clickable wizard design

## Goal

A player with only a pointer and its left button can open a game's layout from the
launcher, see its panel as the game shows it, change what each slot and face gesture does
— including toggle slots, the stick's hold and the menu — and save it beside the ROM, with
no keyboard.

This is sub-project 4 of four that together make the emulator usable with no keyboard, from
launch to quit. Sub-project 1 is one-button mouse play
(`Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md`: toggle slots, the stick
hold), sub-project 2 the emulator actions menu
(`Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md`), sub-project 3 the
launcher (`Docs/superpowers/specs/2026-09-24-launcher-design.md`), which this one extends
with an `Edit` button. The player is the same: limited hand use, a pointer and a reliable
left click, no keyboard, no right button, no scroll; the face is optional.

## Context

- **The wizard is keyboard-driven.** `Source/Project64-wizard/` walks the fifteen controls
  one screen at a time: arrows and Enter move and choose, `1`-`5` pick a binding kind,
  Backspace, Delete, Escape and `S` do the rest, and a path is typed. Its one mouse action is
  clicking a slot on a small panel picture in panel-slot mode (`CaptureZone`).
- **The draft already carries the one-button forms.** `WizardDraft` keeps a base's toggle
  flags, stick hold and `Menu:` key and writes them back (`HoldZone()`, `MenuZone()`), but
  nothing in the wizard can create or move them.
- **The panel geometry is shared.** `PointerZoneRect` (`Source/Common/PointerLayout.h`)
  places the thirteen 48x48 slots (the middle five 56 wide) in a 640x160 panel below the
  game image; the overlay, the plugin and the wizard all use it.
- **The added menu has one rule.** `InputConfig::ApplyAutoMenu` puts `==` on the first free
  of `mid5`…`mid1`, else on `pad-down` (or `pad-up` when `pad-down` is the hold), taken from
  its control. The launcher sets `PJ64_MENU_AUTO=1` for every game, so every game the player
  starts has a menu.
- **A layout is found per ROM.** `GameConfigPath` looks for `<rom>.yaml` beside the ROM, then
  `Config/mouse/<rom>.yaml`; the launcher starts a game with neither on
  `Config/mouse/default.yaml`. The pack's 274 layouts beside the ROMs were generated outside
  this repo.
- **The launcher runs children.** It starts a game with `posix_spawn`, hides, polls
  `waitpid` and comes back; `LauncherChildEnv` builds the environment, with `PJ64_FACE=0`
  when its Face button is off.
- **The wizard's pictures are checked.** `--screenshots` renders a fixed tour with no window
  into `Docs/img/wizard/`, and `make unit-test` fails if a fresh render differs byte for
  byte; the tour must never read a machine-specific path.

## Non-goals

- Binding keyboard keys or gamepad inputs in the editor. The fifteen-step walk does that
  and stays as it is, for a helper.
- A "Save and play" button, undo beyond Cancel, and restoring the `.orig` backup from the
  screen.
- Editing `Config/mouse/` layouts or the generic `default.yaml` themselves, or more than one
  layout per game.
- Making the fifteen-step walk clickable.

## Design

### The flow

1. Each launcher row gets an `Edit` button at its right end. A click starts
   `<emulator dir>/Project64-wizard --edit <rom>` as a child, the way a game starts: the
   launcher hides, waits and comes back.
2. `--edit` opens the wizard straight on the **panel editor**, loaded with what the game uses
   now: `GameConfigPath`'s file (beside the ROM, then `Config/mouse/`), else
   `Config/mouse/default.yaml` beside the wizard.
3. **What you see is what you play.** A loaded layout with no `Menu:` gets one at once, by the
   play-time rule (`ApplyAutoMenu`'s, taking `pad-down` or `pad-up` from its control when every
   middle slot is used), and the status line says where it went. A save writes it out.
4. `Save` validates, writes `<rom folder>/<rom name without its extension>.yaml` and exits;
   `Cancel` exits without writing.
5. The launcher rescans, so a game that now has its own layout loses its `generic` tag.
   Editing does not change the recent games.

### The editor screen

The window is the wizard's 800x640. Text is the debug font at twice its size; the screen
has no keyboard handling, and every target acts on the left button's release inside the
target it was pressed in, as in the launcher.

```
┌────────────────────────────────────────────────────────────┐
│ Layout for super_mario_64_usa.z64                          │  title (the ROM's file name)
│ [Stick: pointer]  [Gestures]              [Save] [Cancel]  │  top bar
│   ┌──────────────────────────────────────────────────┐     │
│   │                        A                         │     │  the game picture: one
│   │                                                  │     │  target, "game" (640x320)
│   └──────────────────────────────────────────────────┘     │
│   ┌──────────────────────────────────────────────────┐     │
│   │   [D^]      [St][ Z ][ B ][ R ][Ho]      [C^]    │     │  the panel at its real
│   │[D<]  [D>]                              [C<] [C>] │     │  size (640x160), slots
│   │   [==]                                    [Cv]   │     │  from PointerZoneRect
│   └──────────────────────────────────────────────────┘     │
│ Not placed: L  D^  Dv                                      │
│ mid2 is Z, a toggle                                        │  status line
└────────────────────────────────────────────────────────────┘
```

- **The panel is at its real size**, laid out by `PointerZoneRect` for a 640x640 window, so
  the slot the player clicks here is the slot they click in the game. Each slot shows its
  label as the game's overlay does (`St`, `C^`, `Ho`, `==`, …); a toggle slot is marked.
- **The chooser.** Clicking a slot or the picture opens a chooser over the picture area: a
  header naming the place and what it holds (`mid2: Z, toggle`, or `mid2: Z and L` for a
  shared slot), then a grid of large buttons — the fourteen controls other than Stick
  (`A B Z St L R C^ Cv C< C> D^ Dv D< D>`), `Menu`, `Hold`, `Nothing`, `Toggle: on/off` (dimmed
  until a control sits there), and `Back`, which closes it without a change. The current
  choice is lit; a choice not allowed there is dimmed and says why on the status line when
  clicked (the picture takes neither `Menu` nor `Hold`; `Hold` needs the stick to be the
  pointer).
- **Gestures.** `Gestures` swaps the picture and the panel for a list of the eleven gestures,
  each with what it holds and, while the camera runs, lit when the player makes it. A row
  opens the same chooser without `Menu`, `Hold` and `Toggle`. `Back to panel` returns.
- **The stick.** `Stick: pointer`, `Stick: head` or `Stick: head-digital` opens a three-way
  chooser. A base with any other stick form (a gamepad stick, four keys) reads
  `Stick: other` and keeps its form until the player picks one of the three.
- **Not placed.** One line lists every control that has neither a slot nor a gesture.

### The rules

They live in `WizardDraft`, which stays pure and unit-tested; the screen only turns clicks
into calls on it.

- **One thing per slot.** A slot holds a control, the menu, the hold or nothing. Placing a
  control moves it — its old slot or gesture empties, because a control has one binding — and
  displaces whatever held the target place. A displaced control becomes not placed: it leaves
  the file and keeps its built-in key and gamepad pair. A shared slot from a loaded layout
  (the reader allows several controls on one slot when their toggle flags agree) displaces
  all its controls.
- **Toggle** is a flag on the control in a slot and goes when that control leaves it. The
  picture can be a toggle, as in play.
- **Hold** exists only while the stick is the pointer. Placing it elsewhere moves it;
  `Nothing` on its slot removes it.
- **The menu always has a place.** `Menu` on a slot moves it there. Anything else placed on
  the menu's slot, `Nothing` included, first tries the play-time order (`mid5`…`mid1`, then
  `pad-down`, or `pad-up` when `pad-down` is the hold — one function in `InputConfig.h`, used
  by `ApplyAutoMenu` and by the draft, so play and editing cannot disagree); when that slot is
  itself taken, editing (unlike play, which would take it from its control) falls back to the
  first free slot on the panel in zone order, never the picture, and the status line says
  where. Only when every one of the thirteen panel slots is taken is the click refused: `The
  panel is full: free a slot for the menu first`. A loaded layout whose menu is a gesture
  shows it as `==` in the gesture list; replacing it moves the menu to a slot by the same
  rule.
- **Gestures** hold one control or nothing. While the stick is `head` or `head-digital`, the
  four head-direction gestures are dimmed (the head moves the stick). Switching the stick to a
  head form clears what those four held and removes the hold; the status line names what was
  cleared.

### Saving

- **Checked first.** `Save` runs the draft through the real reader (`WizardDraft::Validate`);
  a rejection, which the rules should make impossible, shows the reader's own line and writes
  nothing.
- **Where.** `<rom folder>/<rom name without its extension>.yaml`, the name `GameConfigPath`
  tries first, so the saved layout wins at once.
- **The backup.** If that file exists and `<name>.yaml.orig` does not, the file is copied to
  `.orig` first. Later saves never touch the `.orig`. The layout itself is written to a
  temporary file and renamed into place.
- **A failed write** (a read-only folder, say) keeps the editor open with `Cannot save beside
  the ROM: <reason>`.
- **A layout that will not load.** A malformed game layout starts the editor from the generic
  layout with `Your layout could not be read (<reason>); starting from the generic layout`;
  saving then replaces it, keeping it as `.orig`.
- **Leaving.** `Cancel`, or the window's close button, leaves at once when nothing changed;
  with changes, it needs a second click: `Cancel again to discard your changes`. Exit status:
  0 after a save or a cancel, 1 on an error the editor cannot recover from (no window, say), 2
  on bad arguments.
- **The camera** starts the first time the gesture list opens, unless `PJ64_FACE=0`, which
  the launcher passes when its Face button is off; the list is then editable without the
  lighting. The editor ignores an inherited `PJ64_INPUT_YAML`: it always edits the game's own
  layout.

### The launcher's side

- **Row targets.** A row splits into the title, which starts the game, and an `Edit` button
  84 points wide at its right end; `generic` sits just left of `Edit`. Titles fit 41
  characters, or 33 beside `generic`. Recent rows get `Edit` too. `Edit` is dimmed when no
  `Project64-wizard` sits beside the emulator.
- **The editor child** gets the same environment a game does (`LauncherChildEnv`), so Face
  off means `PJ64_FACE=0`. The launcher prints `launcher: editing <rom>` and
  `launcher: editor ended (exit N)`. Exit 0 rescans quietly; any other exit sets the status
  line to `<title>: the layout editor ended with an error (exit N)`. Recent games are not
  touched.

## Errors and observability

The editor's stderr: `wizard: editing <rom> from <layout path>` (or `from the generic
layout`), `wizard: saved <path>`, `wizard: kept the original as <path>.orig`,
`wizard: cannot save <path>: <reason>`, and the load failure above. The launcher's two lines
are above. `PJ64_EDIT_SELFTEST=1`, a test hook, makes `--edit` run a scripted edit (below)
with no window and exit.

## Tests

- **`wizard-draft` area, extended:** placing a control moves it and displaces the target's
  occupant, a shared slot displaces all, toggle follows its control, the picture takes a
  toggle but not the menu or the hold, hold only with the pointer, the menu moved by the
  play-time order and refused on a full panel, a gesture menu shown and moved to a slot, a
  head stick clearing the four head gestures and the hold, a base without a menu getting one
  on load (including the full-panel case taking it from a control), and the save beside the
  ROM: the path, `.orig` made only on the first save over an existing file, and a read-only
  folder failing without writing.
- **New `wizard-edit` area:** the editor's rectangles (inside the window, no overlaps except
  the chooser over the picture), hit-testing, which chooser buttons are enabled where, and
  what a click on each does — pure functions, registered like any area (AGENTS.md).
- **`launcher` area, extended:** the `Edit` rectangle beside the title with no overlap,
  `LauncherAct` on it, and its dimming without a wizard.
- **`make wizard-selftest`, second run:** `Project64-wizard --edit <tmp>/game.z64` with
  `PJ64_EDIT_SELFTEST=1` drives the real editor screen with scripted clicks and no window,
  starting from the generic layout (which already has Z as a toggle on `mid2`, the hold on
  `mid5` and A on the picture): L onto `mid4` as a toggle, the hold onto `pad-up`, R onto
  `pad-down` so the menu moves to `mid5`, Z onto the picture, save. The saved file passes the
  real reader (Save validates first) and the script compares its bindings block with the
  expected one; a second run over the existing file must make `.orig`, and a third must leave
  it alone.
- **`make launcher-selftest`, third step:** after the two games, `Edit` on the first row
  (`nolayout.z64`) with `PJ64_EDIT_SELFTEST=1` passed through the environment. The script
  requires `launcher: editor ended (exit 0)`, a `nolayout.yaml` beside the ROM link with a
  `Menu:` line, and — after the rescan — that row no longer generic
  (`launcher: selftest ok` checks it).
- **Screenshots:** the tour adds `12-edit-panel.png`, `13-edit-chooser.png` and
  `14-edit-gestures.png`, built from the built-in bindings plus scripted clicks and a neutral
  ROM path under `/Users/you/`, never from a file on disk.
- **By hand**, recorded in the Result: from the launcher, `Edit` a game, move a control,
  make a toggle, set the hold, move the menu, save, play it and find the changes; edit again
  and cancel with changes (two clicks).

## Documentation

- `Docs/UserGuide.md` section 6: a subsection, "Editing a game's layout by clicking", with
  the three pictures, the rules, where the file goes, the `.orig` backup and how to restore it
  (rename it back). Section 3's launcher subsection gains `Edit`. Section 11's test-hooks row
  gains `PJ64_EDIT_SELFTEST`.
- `AGENTS.md`: the wizard paragraph gains `--edit` and the panel editor; the unit-test list
  gains `wizard-edit`; the screenshot count becomes fourteen; a trap: an editor-made layout
  always has a menu, and the first save beside a ROM keeps `.orig`, which a helper restores by
  renaming.

## Open risks

- **Slot size for the player.** The 48-point slots are the game's own; if they prove small
  to hit in the editor, the chooser's buttons are larger, but the panel keeps its real size
  on purpose (the same muscle memory as play). The manual check decides.
- **Two windows in turn.** The launcher hides and the wizard opens; focus should follow the
  new window. The same click-through hint the launcher sets goes into the wizard's editor.
