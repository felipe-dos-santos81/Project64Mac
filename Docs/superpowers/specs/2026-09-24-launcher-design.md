# Launcher design

## Goal

A player with only a pointer and its left button can start the emulator from Finder or the
Dock, pick a game from a folder of several hundred, play it, quit back to the list, pick
another, and close the launcher, without a keyboard.

This is sub-project 3 of four that together make the emulator usable with no keyboard, from
launch to quit. Sub-project 1 is one-button mouse play
(`Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md`); sub-project 2 is the
emulator actions menu (`Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md`),
whose Quit this launcher returns from; sub-project 4 is the clickable wizard. The player is
the same: limited hand use, a pointer and a reliable left click, no keyboard, no right
button, no scroll; the face is optional.

## Context

- **A game is one process.** `main.cpp` runs one ROM and exits when the window closes, the
  menu's Quit runs, or emulation ends. The core and the four plugins hold process-global
  state, which is why the grid runs one process per tile (`GridHost.cpp`: `fork`, `execv`,
  `waitpid`); a second game in the same process is not safe.
- **The emulator's base directory is its executable's directory.** `AppInit` receives
  `ExecutableDirectory()`, and `Cmd_BaseDirectory` roots `Config/Project64.cfg`, `Save/`,
  `Screenshots/`, `Logs/`, the `.rdb` files and `Lang/`. The read-only data and the
  player's saves share `Bin/macOS`.
- **A mouse layout is found per ROM.** `GameConfigPath` (`GameConfig.cpp`) looks for
  `<rom>.yaml` beside the ROM, then `Config/mouse/<rom>.yaml`; with neither, the game gets
  `Config/input.yaml`, which is keyboard-only and unplayable for this player.
- **The ROM pack the player has** (`~/Downloads/N64ROMsPACK`) holds 296 games (`.z64`,
  `.n64`, `.v64`) and 274 layouts beside them, generated outside this repo. 22 games have
  no layout. None of the 274 has a `Menu:`; 267 have `mid5` free and 7 use every slot. 267
  bind face gestures, so each of those starts the camera.
- **The wizard already draws a clickable-enough window without GL**: `SDL_Renderer`, SDL's
  8x8 debug font scaled with `SDL_SetRenderScale`, and synthetic events
  (`SyntheticEvents.h`) that drive its real screens in `--selftest`.
- **The binary is not an app bundle**, so macOS attributes the camera prompt to the
  terminal or IDE that started it (AGENTS.md trap).

## Non-goals

- Starting the grid from the launcher, box art, search, or more than one ROM folder.
- A self-contained app that can be moved or installed in `/Applications`. That needs the
  emulator's writable files moved out of its base directory, a first-run copy of its data,
  and a decision about existing saves; it can be its own later step.
- Editing or creating layouts (sub-project 4). The launcher only chooses which layout a
  game starts with.
- Changing the pack's layout files, or the generator that made them.

## Design

### Pieces

- **`Source/Project64-launcher/`** builds `Bin/macOS/Project64-launcher`, a third binary.
  - `LauncherModel.{h,cpp}` is pure: no SDL. It scans a folder, makes titles, sorts,
    paginates, maps letters to pages, keeps the recent games, reads and writes the
    settings, and hit-tests the screen's fixed rectangles.
  - `Screens.cpp` draws with `SDL_Renderer` and the debug font at twice its size, from the
    model's rectangles.
  - `main.cpp` owns the window, the event loop, the folder picker, and starting and waiting
    for the game.
  - It links SDL3, yaml-cpp and `Project64-sdl/GameConfig.o`. It links no core, no plugin,
    no OpenGL.
- **`Bin/macOS/Project64.app`**, a thin bundle built by `make app`: `Contents/Info.plist`
  and `Contents/MacOS/Project64-launcher`, a copy of the launcher. It holds no emulator; it
  finds the one beside it in `Bin/macOS`.
- **`PJ64_MENU_AUTO=1`** in the emulator: add a menu to a layout that lacks one.
- **`Config/mouse/default.yaml`**: the generic one-button layout.

The emulator run from a terminal is unchanged: nothing reads `PJ64_MENU_AUTO` unless it is
set, and the default layout is only chosen by the launcher.

### The flow

1. The launcher starts, finds the emulator (below), and reads its settings. With no folder
   remembered it opens the macOS folder picker (`SDL_ShowOpenFolderDialog`).
2. It lists the folder's games. The player clicks one.
3. It builds the child's environment from its own:
   - `PJ64_INPUT_YAML=<emulator dir>/Config/mouse/default.yaml` when `GameConfigPath`
     finds no layout for the ROM. An inherited `PJ64_INPUT_YAML` is left alone and wins, as
     the frontend's own rule says; a Finder launch never has one.
   - `PJ64_MENU_AUTO=1`.
   - `PJ64_FACE=0` when Face is off; `PJ64_FACE` removed when Face is on, so the layout
     decides as it does today.
4. It hides its window and starts `<emulator dir>/Project64 <rom>` with `posix_spawn`,
   handing it the environment built in step 3 and the emulator dir as its working
   directory, then keeps pumping events, checking `waitpid(…, WNOHANG)` every 50 ms. Not
   `fork` then `setenv` as `GridHost` does: SDL has started threads by then, and `setenv`
   between `fork` and `exec` is not safe in a threaded process.
5. When the child exits, the launcher shows and raises its window on the view the player
   left, moves the game to the front of the recent games, saves the settings, and reports
   the exit (below).

If the launcher is asked to quit (Cmd-Q from the Dock) while a game runs, it sends the child
`SIGTERM`, waits up to 5 seconds for it, then sends `SIGKILL` and waits for that, and exits.

**Finding the emulator.** From its executable's own path (`_NSGetExecutablePath`; not
`SDL_GetBasePath`, which returns `Contents/Resources/` inside a bundle), the launcher looks
for an executable `Project64` in its own directory (the bare `Bin/macOS/Project64-launcher`), then three levels up (from
`Bin/macOS/Project64.app/Contents/MacOS/`). With neither, the window shows
`Project64 not found beside the app` and only the Quit button.

**Exit reporting.** Exit 0, which Quit and the close button both give, returns to the list
with the status line cleared. Any other exit status, or a signal, sets the status line to
`<title> ended with an error (exit N)` or `(signal N)`. A game that fails to start, such as a
bad ROM, reaches the launcher the same way.

### The screen

The window is 800x640 points, fixed size, titled `Project64`. Text is the debug font at
twice its size (16 px glyphs). Every target is a rectangle that lights under the pointer and
acts on **release inside the same target it was pressed in**; a press that drifts off does
nothing. There is no keyboard handling.

```
┌────────────────────────────────────────────────────────┐
│ Project64          [Face: off]  [Folder]   [Quit]      │  top bar
├────────────────────────────────────────────────────────┤
│ [Recent] [A] [B] [C] [D] [E] [F] [G] [H] [I] [J] [K] [L]│  letter strip,
│ [M] [N] [O] [P] [Q] [R] [S] [T] [U] [V] [W] [X] [Y] [Z] │  two rows
├────────────────────────────────────────────────────────┤
│  Aerofighter S Assault U                               │
│  Aerogauge U                               generic     │  10 rows, 40 px each
│  Aidyn Chronicles The First Mage U                     │
│  …                                                     │
├────────────────────────────────────────────────────────┤
│  [   <   ]          page 2 of 30           [   >   ]   │  bottom bar
│  Face is off: gesture controls do nothing              │  status line
└────────────────────────────────────────────────────────┘
```

- **Games.** Every regular file in the folder (not its subfolders) whose extension is
  `.z64`, `.n64` or `.v64` in any case. Hidden files are skipped. The title is the file name
  without its extension, `_` turned into spaces, each word's first letter uppercased:
  `banjo_kazooie_u.z64` is `Banjo Kazooie U`, `007_goldeneye.v64` is `007 Goldeneye`, as in
  the pack's README. Titles sort case-insensitively, file name breaking ties.
- **Rows.** Ten per page. A title wider than the row is cut and ends in `...`. A game with no
  layout of its own (`GameConfigPath` false) shows `generic` at the right.
- **Pages.** Page 1 is the first ten titles. `<` and `>` step one page; `>` on the last page
  and `<` on page 1 with no recent games are dimmed and do nothing. The bar shows
  `page P of N`.
- **Recent.** The five games most recently started, newest first, stored as absolute paths.
  A path whose file no longer exists is dropped when the settings are read. The Recent view
  is the page before page 1: `<` on page 1 goes there, and the `Recent` cell jumps there. The
  launcher opens on Recent when it has any recent games, else on page 1. The bar reads
  `recent games`. Recent games from a folder chosen earlier stay while their files exist.
- **Letters.** A letter jumps to the page holding the first title whose first character,
  uppercased, is that letter. `A` also takes titles that start with a digit or anything that
  is not a letter, since they sort before it. A letter no title starts with is dimmed and does
  nothing.
- **Face** flips between `Face: on` and `Face: off` and saves at once. It starts **off**, so
  the camera never opens until someone clicks it. While it is off the status line reads
  `Face is off: gesture controls do nothing`, unless a game's exit error is showing.
- **Folder** opens the folder picker. Cancelling keeps the current folder. Choosing one
  saves it, rescans, and goes to page 1.
- **Quit** exits the launcher at once; nothing is lost.
- **Empty.** A folder with no games, a remembered folder that no longer exists, or a
  cancelled picker on first run shows `No games in <folder>` (or `No folder chosen`) and a
  large `[Choose a folder]` button in place of the rows.

The folder picker's callback may run on another thread; it stores the chosen path, or a
cancel, under a mutex, and the main loop applies it.

### Settings

`launcher.yaml` in `SDL_GetPrefPath("", "Project64")`, which is
`~/Library/Application Support/Project64/`, or in `$PJ64_LAUNCHER_HOME` when that is set:

```yaml
folder: /Users/you/Downloads/N64ROMsPACK
face: false
recent:
  - /Users/you/Downloads/N64ROMsPACK/super_mario_64_usa.z64
```

It is written through a temporary file and a rename, so a crash never leaves half a file. A
missing file gives the defaults (no folder, Face off, no recent games); an unreadable or
malformed one does too, with one stderr line, and is replaced on the next save. It lives
outside `Bin/macOS`, so `make clean` keeps it, and outside the bundle, which is never
written to.

### The added menu (`PJ64_MENU_AUTO=1`)

The pack's layouts have no `Menu:`, so without this a player can start those games but
never reach Quit, Save or Load.

`InputConfig::ApplyAutoMenu()` runs after a successful `Load`, and only when
`PJ64_MENU_AUTO` is `1`. Two callers apply it, each to its own `InputConfig` instance: the
frontend after parsing the layout for sizing (so `MenuHost` gets the zone), and the input
plugin's `PluginLoaded` (so the plugin publishes the same `MenuZone` and suppresses the same
slot). Both read the same file and the same rule, so they agree. It is not in `Load` itself,
so the wizard, which also loads layouts, never adds a menu to a file it writes.

The rule, in `ApplyAutoMenu`, which returns the slot it added (or `POINTER_ZONE_NONE`) so
the `input-config` area can test it without an environment; `AutoMenuWanted()`, beside
`MenuSlotOf` in `InputConfig.h`, is the one reader of the variable:

- Do nothing if the layout has a `Menu:` or does not use the pointer (a keyboard-only layout
  has no panel).
- Otherwise take the first of `mid5`, `mid4`, `mid3`, `mid2`, `mid1` that no zone binding,
  toggle or stick hold uses (`hold: mid5` counts as used).
- If all five are used, take `pad-down`, or `pad-up` when `pad-down` is the stick's hold slot
  (`CheckSlots` forbids a file from sharing the hold slot, so the menu must not either). A
  control bound to that fallback slot loses that binding for this run and does nothing. A
  control has one binding, so it has no other source.
- Print one stderr line: `menu: added on mid5`, `menu: took pad-down from DPadDown`, or, when
  the fallback is `pad-up`, `menu: added on pad-up` / `menu: took pad-up from <Control>`.

The rest follows sub-project 2 unchanged: the slot's label is `==`, and the menu's Quit ends
the process with exit 0, which takes the player back to the list.

### `Config/mouse/default.yaml`

The generic one-button layout for any game without its own, shaped like the three shipped
ones and using no face gestures, so it never starts the camera:

| Control | Binding |
| --- | --- |
| Stick | `{stick: pointer, hold: mid5}` |
| A | `{zone: game}` |
| Start | `{zone: mid1}` |
| Z | `{zone: mid2, toggle: true}` |
| B | `{zone: mid3}` |
| R | `{zone: mid4}` |
| L | `{zone: pad-up}` |
| CUp, CDown, CLeft, CRight | `{zone: c-up}`, `c-down`, `c-left`, `c-right` |
| DPadLeft, DPadRight | `{zone: pad-left}`, `{zone: pad-right}` |
| Menu | `{zone: pad-down}` |

DPadUp and DPadDown are omitted and keep their built-in keys. A header comment says what the
file is for. `make config` installs it with the other mouse layouts. The per-ROM lookup would
pick it only for a ROM named `default.z64`; that is accepted.

### The bundle

`make app`, part of `make all`, writes:

- `Bin/macOS/Project64.app/Contents/MacOS/Project64-launcher`, a copy of the launcher.
- `Bin/macOS/Project64.app/Contents/Info.plist` from the tracked
  `Source/Project64-launcher/Info.plist.in`, with `CFBundleExecutable` `Project64-launcher`,
  `CFBundleIdentifier` `io.github.felipe-dos-santos81.Project64Mac`, `CFBundleName`
  `Project64`, `CFBundlePackageType` `APPL`, `NSHighResolutionCapable` true, and
  `NSCameraUsageDescription` ("Project64 uses the camera to read face gestures when a game's
  layout binds them."). `CFBundleShortVersionString` is `MAJOR.MINOR.REVISION` from
  `Source/Project64-core/Version.h.in`, filled in at build time.
- An ad-hoc signature (`codesign --force -s -`) over the bundle.

The bundle has no icon. The launcher never uses the camera itself; the usage string is
there because macOS attributes the child's camera request to the app that started it.

## Errors and observability

The launcher prints to stderr, which a Finder launch discards and a terminal run shows:

- `launcher: emulator <path>` at start, or `launcher: Project64 not found beside the app`.
- `launcher: folder <path> (<n> games)` after each scan, or `launcher: cannot read folder
  <path>` when `opendir` itself fails (the folder was removed, or lost its permissions).
- `launcher: started <rom>`, `launcher: started <rom> with the generic layout`, or
  `launcher: started <rom> with $PJ64_INPUT_YAML` when the launcher's own environment
  already names a layout (`LauncherChildEnv`'s inherited case, which wins over the generic
  layout for every game, not only a generic one), and `launcher: game ended (exit N)` or
  `(signal N)`. SDL3 turns `SIGTERM` into a quit event
  (`SDL_events.h`: a signal-generated quit event "will be delivered to the application at
  the next event poll"), so a game the launcher stops — the self-test's timer, or the
  launcher quitting while a game runs — shuts down through its own event loop and exits 0,
  not by the raw signal; `launcher: game ended (exit 0)` is what a clean stop looks like.
- `menu: added on <slot>` (`mid5`, `mid4`, `mid3`, `mid2`, `mid1`, `pad-down` or `pad-up`),
  or `menu: took pad-down from <Control>` / `menu: took pad-up from <Control>`, printed once
  by `ApplyAutoMenu` when `PJ64_MENU_AUTO=1` adds a menu slot (see "The added menu" above).
- `launcher: window back` once the window is shown again.
- `launcher: settings unreadable, using defaults: <path>` for a bad `launcher.yaml`.
- On screen, `<title> could not start: <reason>` when `posix_spawn` fails.
- `launcher: cannot start <exe>: <reason>` for that same failure, on stderr.
- On screen, `<title> ended with an error (exit N)` or `(signal N)` when a game that did
  start exits non-zero or dies by a signal; Quit and the close button, both a clean exit 0,
  clear the status line instead.
- `launcher: cannot write <settings path>` when `launcher.yaml` cannot be saved.
- `launcher: folder picker failed: <SDL error>` when the folder dialog's callback gets no
  files back.
- `launcher: cannot open a window: <SDL error>` and `SDL_Init failed: <SDL error>` for the
  two ways startup itself can fail.
- `launcher: game did not stop, killed it` when quitting while a game runs (Cmd-Q from the
  Dock) has to fall back to `SIGKILL` because the child did not exit within 5 seconds of
  `SIGTERM`.

`PJ64_LAUNCHER_SELFTEST=<seconds>` makes the launcher send the child `SIGTERM` that many
seconds after starting it. Only the self-test sets it.

## Tests

- **Unit tests, new area `launcher`** (`Source/Project64-launcher/LauncherModelTest.cpp`,
  registered in `UnitTestMain.cpp` and `UNIT_TEST_OBJS`, per the AGENTS.md rule):
  - the scan: the three extensions in any case count, other files, hidden files and
    subfolders do not;
  - titles from file names, and the sort, including a digit-led title and a tie;
  - page count for 0, 1, 10, 11 and 296 games;
  - letter pages: a present letter, a dimmed letter, `A` taking a digit-led title;
  - recent games: pushing a new game, pushing one already present, the cap of five, a
    missing file dropped on read;
  - settings: a write read back, a missing file and a malformed file each giving the
    defaults (written under a temporary `PJ64_LAUNCHER_HOME`, restored afterwards);
  - hit-testing: one point inside each target, the gaps between them, a dimmed target, and
    the `<` rule on page 1 with and without recent games.
- **Unit tests, `input-config` area, extended**: the added menu's first free slot, `mid5`
  skipped for `hold: mid5`, a full layout taking `pad-down` from its control, an existing
  `Menu:` left alone, a keyboard-only layout left alone, and nothing added without the
  variable.
- **`make launcher-selftest rom=<path>`** (`Scripts/launcher_selftest.sh`): builds a
  temporary folder holding two links to the ROM (`withlayout.z64`, beside a minimal
  `withlayout.yaml` binding only `Stick: {stick: pointer}` and `A: {zone: game}`, and `nolayout.z64`),
  writes a `launcher.yaml` there with Face off, and runs the bundle's copy,
  `Bin/macOS/Project64.app/Contents/MacOS/Project64-launcher --selftest`, so the run also
  proves the emulator is found three levels up, with `PJ64_LAUNCHER_HOME` and
  `PJ64_LAUNCHER_SELFTEST=6`. `--selftest` drives the real
  screens with synthetic clicks, as the wizard's does: it clicks the first row
  (`Nolayout`), waits for the window to come back, clicks the second (`Withlayout`), waits
  again, and checks that the recent games are `withlayout.z64` then `nolayout.z64`. The script requires, in the combined stderr:
  `launcher: started …nolayout.z64 with the generic layout`,
  `launcher: started …withlayout.z64`, `input layout: …withlayout.yaml`, exactly one
  `menu: added on mid5` (the generic layout has its own menu), two
  `launcher: game ended (exit 0)` — SDL3 turns the self-test's `SIGTERM` into a quit event,
  so the child exits cleanly rather than dying by the signal — two `launcher: window back`,
  and `launcher: selftest ok`. The camera is never opened. It needs a window server.
- **By hand**, recorded in the Result:
  - double-click `Bin/macOS/Project64.app` in Finder, and start it from the Dock;
  - Face on, start a face layout's game, and confirm the camera prompt names Project64;
  - a full round trip with the mouse only: choose the folder, jump by letter, start a game,
    play, open the menu, Quit, start another from Recent, quit the launcher.

## Documentation

- `Docs/UserGuide.md`: a new subsection of section 3, "Starting from the launcher" (a
  subsection, so no section number the guide and AGENTS.md cite moves), covering `make app`, starting it
  from Finder or the Dock, the screen, what `generic` means, the menu the launcher adds and
  where it goes, Face on and off, and where the settings live. Section 11 gains rows
  for `PJ64_MENU_AUTO` and `PJ64_LAUNCHER_HOME`, and `PJ64_LAUNCHER_SELFTEST` joins the
  test-hooks row.
- `AGENTS.md`: `make app`, `make run-launcher` and `make launcher-selftest` in Commands; a
  launcher paragraph in Architecture; the `launcher` area in the unit-test list; two traps:
  the app finds the emulator by its place in `Bin/macOS` and breaks if moved, and
  `make clean` deletes the app but not `launcher.yaml`.

## Open risks

- **The camera prompt's attribution.** macOS normally attributes a child's camera request
  to the app that started it. If the manual check shows the prompt naming something else,
  that is fixed in its own change; the launcher works either way, and Face defaults to off.
- **The folder picker on a window that is not key.** `SDL_ShowOpenFolderDialog` is given the
  launcher's window as parent. If the sheet does not appear on first run, it is shown with no
  parent.
- **Debug-font titles.** Only ASCII glyphs exist; the pack's names are ASCII. A non-ASCII
  file name draws its unknown characters as the font's fallback.
