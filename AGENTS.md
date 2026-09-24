# AGENTS.md

Guidance for coding agents working in this repository.

## What this is

A permanent fork of [Project64](https://github.com/project64/project64) reduced to one
platform: macOS on Apple Silicon, built by a hand-written `Makefile` with SDL3 and
OpenGL.

`origin` is this fork's own repository (`felipe-dos-santos81/Project64Mac`), **not**
upstream Project64 — upstream is not configured as a remote at all. `main` is the
default and only long-lived branch, and its history was squashed to a single root
commit, so upstream's Windows-era history is not reachable from here and cannot be
pulled back in by accident.

## Commands

```sh
make -j8 all                             # the normal build; a few minutes from clean
make test                                # smoke test
make unit-test                           # every headless unit-test area, then the screenshot drift check
make unit-test only=input-config         # one area, without the drift check
make pointer-selftest rom=Roms/game.z64  # prove the injected-pointer path end to end
make face-selftest rom=Roms/a.z64          # face path end to end, camera never opened
make wizard-selftest                     # the wizard's screens, driven by synthetic events
make wizard-screenshots                  # render the user guide's wizard pictures into Docs/img/wizard
make wizard-screenshots-check            # fail if those pictures no longer match what the wizard draws
make run-wizard                          # launch the binding wizard window
make run rom=Roms/game.z64 input=Config/mouse/super_mario_64_usa.yaml  # one-button mouse; no camera
make run rom=Roms/game.z64
make grid roms="Roms/a.z64 Roms/b.z64"   # 1-16 ROMs, one window each
make grid-selftest rom=Roms/game.z64     # prove key broadcast across four tiles
make rom-test                            # sweep a ROM pack, screenshot each game (see --help)
make clean                               # removes build/macos, Bin/macOS, generated Version.h files
make help                                # user-facing targets; build stages are hidden
```

`make test` is the smoke suite: the frontend must run `--version`, and each of the four
plugin dylibs must export `GetDllInfo` (checked with `nm`). Passing output is one version
line plus four `ok:` lines.

`make unit-test` builds and runs one program, `build/macos/unit-tests`, which needs no
window. Its areas, in the order they run: `pointer-layout` (the mouse panel's geometry
and the one-button rules), `pointer-menu` (the emulator actions menu's rules),
`face-gestures` (the gesture classifier), `game-config` (the per-game YAML lookup),
`input-config` (the YAML reader) and `wizard-draft` (the wizard's draft and the YAML it
writes). It prints `ok: <area>` for each; `only=<area>` runs one. There is no other
unit-test command. `make pointer-selftest` needs a window server and takes ~30 s.

Stages build individually — `deps`, `version`, `common`, `core`, `rsp`, `video`,
`audio`, `input`, `frontend`, `config`. Run `make core` after touching the core rather
than rebuilding everything.

## Verifying a change

`make test` proves the binaries link and export the right symbols. It does not prove
the emulator renders. For that, read pixels from inside the program — never capture the
screen:

```sh
PJ64_FRAME_DUMP=/tmp/frame.ppm PJ64_FRAME_DUMP_AT=400 \
  perl -e 'alarm 20; exec @ARGV' -- ./Bin/macOS/Project64 /path/to/game.z64 || true
```

That writes one 640x480 binary PPM from the back buffer. A healthy Super Mario 64 boot
measures ~89% non-black pixels; below 80% something broke. `PJ64_TRACE` raises trace
levels (see `Docs/UserGuide.md` section 11). The core and each plugin hold their own trace
state, so one value covers all of them and each ignores names it does not know.

`make grid-selftest rom=…` is the grid's end-to-end proof: one ROM in four tiles, each of
which must report the strip's key pattern. It needs a window server and takes ~40 s.

`make rom-test` (`Scripts/run_rom_pack.py --help`) runs the same frame-dump check across
every ROM in a folder, in parallel, and reports each as a screenshot or a black/hang/crash
failure — the broad-compatibility sweep version of the single-ROM check above.

Touching `Source/Project64-wizard/Screens.cpp` changes the pictures in `Docs/UserGuide.md`.
So can touching what it draws from without touching that file itself: `DefaultBindings` in
`Source/Project64-sdl/InputConfig.cpp:160` sets what an inherited binding reads as;
`WizardControlName` and `WizardDraft::Describe` in `Source/Project64-wizard/WizardDraft.cpp`
turn a control and its binding into the "now:" line and the review; and `PointerGestureName`
and `PointerGestureTag` in `Source/Common/PointerState.h` label the gesture list. Any of
those can change pictures 02, 07, 08 or 09. Run `make wizard-screenshots` and commit
`Docs/img/wizard/` with the change, or `make unit-test` fails on the drift check. The check
compares bytes, so a Homebrew SDL upgrade that changes the debug font can fail it too;
regenerate and commit in that case.

## Architecture

**Four dylibs and a frontend across a C ABI.** The core is a static library linked into
`Bin/macOS/Project64`; the video, audio, RSP and input plugins are separate `.dylib`s
under `Bin/macOS/Plugin/`, loaded by `Source/Common/DynamicLibrary.cpp` (`dlopen`) and
called through the zilmar spec in `Source/Project64-plugin-spec/`. Each plugin exports
`GetDllInfo`, `RomOpen`, `RomClosed`, `CloseDLL`, `PluginLoaded` and its own `Initiate*`.
Separate dylibs are deliberate — a monolithic link previously produced a null
`g_InputPlugin`. Exports are resolved by name in
`Source/Project64-core/Plugins/PluginBase.cpp` and its per-type subclasses.

**Frontend flow** (`Source/Project64-sdl/main.cpp`): create window and GL context on the
main thread, release the context, capture the underlying `CGLContextObj`, `AppInit`,
point the four `Plugin_*_Current` settings at the dylib paths, then
`CN64System::RunFileImage(argv[1])`. After that the main thread only pumps SDL events
and watches for `g_BaseSystem` going null.

`Bin/macOS/Project64-wizard` is a second binary from `Source/Project64-wizard/`. It shares
`InputConfig.o`, `FaceGestures.o` and `FaceTracker.o` with the frontend but links no
OpenGL: it draws with `SDL_Renderer` and SDL's 8x8 debug font, because the overlay's font
has twenty-one glyphs and cannot spell a scancode name. `WizardDraft` holds the mapping
and emits the YAML with no SDL window in sight, which is why the `wizard-draft` unit-test
area can test it headlessly; `Screens.cpp` turns one event into one call on the draft, which is why
`--selftest <path>` can drive the real screens with synthetic events — a flag on the
binary itself, which `Scripts/wizard_selftest.sh` (what `make wizard-selftest` runs) calls
but does not own.

`--screenshots <dir>` (`Screenshots.cpp`) walks a second fixed tour through the same
screens and writes one PNG per stop through `SDL_CreateSoftwareRenderer` and
`SDL_SavePNG`, with no window and no `SDL_Init`; the user guide references those eleven
files by name and `Scripts/wizard_screenshots_check.sh` compares a fresh render with the
committed `Docs/img/wizard/` byte for byte.

**Grid mode runs one process per ROM.** `Source/Project64-sdl/GridHost.cpp` turns
`--grid a b c` into an orchestrator that lays out 4:3 tiles, spawns
`Project64 --tile <rom> --tile-rect …` per ROM, and kills them on quit; each child is the
ordinary single-ROM path. The core and the plugins hold process-global singleton state, so
a tile cannot be a thread. The strip's keyboard reaches every tile through a `GridKeys`
seqlock in `Source/Common/GridKeys.h` over an inherited `shm_open` descriptor, and
`PJ64_TILE_SIZE` / `PJ64_AUDIO_MUTE` give each tile its render size and silence. All of it
is opt-in: with no `--grid` and those variables unset, behavior is unchanged.

**Input bindings are data.** `Source/Project64-sdl/InputConfig.{h,cpp}` owns the N64 control
set, the built-in default table (each control's keyboard *and* gamepad source) and the YAML
reader; `PluginLoaded` loads `PJ64_INPUT_YAML` if set, else `Config/input.yaml`, once, and
`GetKeys` only evaluates the resolved table. The frontend sets that variable itself when a
YAML named after the ROM exists (`Source/Project64-sdl/GameConfig.cpp`: beside the ROM,
then `Config/mouse/`), unless it was already set or the process is a grid tile.
yaml-cpp is a declared Homebrew dependency, linked into the input dylib.

**Mouse and face input go through one shared struct.** `Source/Common/PointerState.h` is
a seqlock over `shm_open`, created by the frontend and passed to the input plugin by
descriptor in `PJ64_POINTER_FD`, the same way the grid passes keys.

*Sizing happens first.* The frontend parses the layout itself, before the window exists —
`InputConfig.cpp` is compiled into both the frontend and the input dylib, each with its
own singleton. A layout that uses the pointer makes the window 640x640 and sets
`PJ64_VIEWPORT_OFFSET=160`, which the video plugin's `ChangeSize` stores in the renderer's
dormant status-bar offset `g_viewport_offset`, so the game renders in the top 640x480 and
the panel owns the rows below.

*Then each frame.* The frontend's main loop samples the mouse (SDL3's mouse state
functions are main-thread only) and publishes it, and polls `FaceWanted` to start the
camera when `PJ64_FACE` is unset (`0` never, anything else at once).
`Source/Project64-sdl/FaceTracker.mm` runs AVFoundation and Vision on a private queue,
derives eight measures from the landmark regions (brow height, yaw, pitch, roll, inner-lip
gap, outer-lip width, each eye's aperture) and feeds them to the pure classifier in
`FaceGestures.{h,cpp}`: one rest baseline per measure, one hysteresis-and-debounce channel
per gesture bit (eleven, in `PointerState.h`'s order), and the head stick from the yaw and
pitch baselines. The tracker writes the bits and the stick (`HeadX`, `HeadY`) into the
struct. The plugin's `GetKeys` evaluates slots, the flick gate, the rest and the one button
(`PointerSettleStep`, `PointerClickStep`: latch, toggle slots, the stick hold), gestures,
the pointer stick and the head stick (`{stick: head}` copies, `head-digital` snaps by
quadrant) using the pure geometry in `Source/Common/PointerLayout.h`, then writes the
labels, the latched zone and the lit quadrant back for `Overlay.cpp`, which reads the GL
viewport to find the game rectangle and paints the panel below it in
`CSdlRenderWindow::SwapWindow` before the flush.

*The menu.* A `Menu:` key in a layout gives the panel an `==` slot (or a gesture) that opens
the emulator actions menu. `Source/Project64-sdl/MenuHost.cpp` runs it on the main thread,
because a paused game runs neither `GetKeys` nor the overlay: it reads clicks from the
published `PointerSample`, applies the pure rules in `Source/Common/PointerMenu.h`, pauses
and resumes the core with `ExternalEvent`, and waits on `PointerState::OverlayFrames` so it
pauses only once the menu is on screen. While `MenuOpen` is set the plugin returns no input.

Layouts are the `{zone:}`, `{face:}`, `{stick: pointer}` and `{stick: head|head-digital}`
YAML forms, in `Config/mouse/` and `Config/face/`. `PJ64_FACE_INJECT=<gesture>[,<x>,<y>]`
stands in for the tracker so `Scripts/face_selftest.sh` proves the path without a camera.

**GL belongs to the emulation thread.** `CSdlRenderWindow` binds the context with
`CGLSetCurrentContext` and presents with `CGLFlushDrawable`. The SDL equivalents are
main-thread-only on macOS and marshal there, which deadlocks on the first swap because
the main thread is itself waiting on the emulation thread. Do not "simplify" them back.

**The single-game window scales; it never re-renders at a new size.** `MakeWindowScalable`
in `Source/Project64-sdl/main.cpp` pins the GL surface at the launch size with
`CGLSetParameter(kCGLCPSurfaceBackingSize)` and only then makes the window resizable, with
its aspect locked and a half-size minimum; the window server scales the picture to fit,
with bars in full screen. The video plugin, the overlay and `PJ64_FRAME_DUMP` never see the
window's size, and `PublishMouse` maps the cursor back through `PointerFitToBase`
(`Source/Common/PointerLayout.h`) so the published sample is always in launch-size pixels.
Grid tiles stay fixed. Design: `Docs/superpowers/specs/2026-09-16-resizable-window-design.md`.

**Interpreter only.** `ConfigurePlugins()` sets `Setting_ForceInterpreterCPU`, and
`Source/Common/MemoryManagement.cpp` drops `PROT_EXEC` because Apple Silicon refuses
writable-and-executable mappings. The recompiler still compiles but is unreachable: on
arm64 `CodeBlock.cpp` calls `g_Notify->BreakPoint` instead of constructing backend ops.
Only the `Aarch64` backend directory survives.

**Settings** are typed handlers registered in `Source/Project64-core/Settings.cpp`; each
`Setting_*` ID maps to a `CSettingType*` backed by the ini files in `Config/`.

## Traps

- **Data files must sit beside the binary.** `make config` copies `Config/*.rdb`,
  cheats, enhancements and `Lang/` into `Bin/macOS/`. Without them the core writes empty
  databases, every game falls back to defaults, and the RSP reports "uCode crc not found
  in INI" and never runs the graphics task. `make all` includes this step; a hand-rolled
  build sequence must not skip it.
- **A listed input control loses its other source.** The shipped `Config/input.yaml` maps the
  keyboard, so a fresh build's gamepad does nothing until the commented gamepad block is
  swapped in. The built-in keyboard+gamepad pairing returns when the file is deleted.
- **`Config/` and `Lang/` are tracked runtime data**, not build inputs. Never delete
  from them.
- **Unit tests live in one program.** A new area is a `Run<Area>Tests()` in its own
  `*Test.cpp`, registered in `Source/Project64-sdl/UnitTestMain.cpp` and added to
  `UNIT_TEST_OBJS` in the Makefile. Never add a test program or a `make` target. Before
  adding a check, look for one that already covers the behaviour and extend it; shared
  helpers go in `Source/Project64-sdl/UnitTest.h`, not a copy per file. Every area runs in
  the same process, so it must leave process-wide state as it found it: the working
  directory (`game-config` `chdir`s and changes back), the environment, and the
  `InputConfig` instance, which `input-config` and `wizard-draft` share.
- **Line endings are mixed.** Many tracked files are CRLF (including
  `Source/Common/Trace.{h,cpp}` and `Source/Project64-video/Renderer/glitchmain.h`).
  Run `file <path>` before editing and preserve what is there. BSD `sed` will not help;
  a Python read/replace/write that detects `\r\n` will. A CRLF `.gitignore` also gives
  every pattern a trailing `\r`, which git strips — but a blank line becomes a pattern
  that matches directories, so keep that file LF.
- **Platform conditionals still exist.** ~285 `_WIN32` directives across ~69 compiled
  files. Code inside them is dead here, but removing it is a deliberate later phase with
  its own spec — not incidental cleanup.
- **`Version.h` files are generated** from `Version.h.in` and git-ignored. Never edit or
  commit one.
- **`Source/3rdParty/` is trimmed to exactly what the Makefile compiles.** Adding a
  third-party source means adding it to the Makefile's list; only softfloat, zlib and
  asmjit use wildcards, and softfloat's list is hand-picked for a reason the Makefile
  comment explains.
- **SDL3 mouse state is main-thread only.** `SDL_GetMouseState` and friends must stay in
  `main.cpp`'s loop; the plugin reads the published `PointerState` instead.
- **The cursor is never captured, confined or warped.** One-button play (toggle slots and
  the stick hold, `Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md`) exists so
  that no mouse mode is needed: the player reaches the panel with a free cursor. Relative
  mouse mode, a grab or a warp would take the panel away from a player who has nothing else.
- **A paused game never redraws the panel.** The overlay is drawn by the emulation thread
  when the game presents a frame, and GL never leaves that thread. Every change to the menu
  is therefore shown by resuming the game until `OverlayFrames` moves, then pausing again;
  never by drawing from the main thread. A soft reset reaches the plugins' `RomClosed` only
  when the core's reset timer finishes, about a second of game time later, so the menu's
  Reset also bumps `ClearClicks` to clear the clicks at once.
- **The camera prompt is attributed to the launcher.** The binary is not an app bundle, so
  macOS asks for camera access on behalf of the terminal or IDE. A past denial there makes
  the tracker report `denied` without a new prompt; the fix is in System Settings.
- **`Config/input.yaml` must stay keyboard-active.** A mouse block there would replace the
  keyboard bindings under the one-binding rule and break the grid's keyboard broadcast and
  `make grid-selftest`. Mouse layouts live in `Config/mouse/`. The wizard warns before
  saving a zone, face, pointer or head binding here and asks for a second Enter, but never
  refuses: the reader accepts such a file, so this stays a trap and not a rule.
- **A YAML beside a ROM silently changes that game's bindings.** `<rom>.yaml` next to
  `<rom>.z64`, or `Config/mouse/<rom>.yaml`, is loaded instead of `input.yaml`, the window
  becomes 640x640 with the panel, and the camera starts if it binds a gesture. The stderr
  line `input layout: <path>` is the tell; `PJ64_INPUT_YAML` and `PJ64_FACE=0` override it.
  Any unattended launcher over a ROM folder must set `PJ64_FACE=0` itself, the way
  `Scripts/run_rom_pack.py` does — otherwise a matching layout can open the camera with
  nobody watching.
- **The frontend owns `PJ64_VIEWPORT_OFFSET`.** It sets the variable to the panel height,
  or clears it, before the plugins load, from the layout it parsed itself. Never set it by
  hand: a value the video plugin honours without the taller window pushes the game off the
  top, and a stale video dylib that ignores it shows the game at the bottom of a tall
  window with no panel, since the overlay draws the panel wherever the viewport is not.
  `PJ64_TRACE=info` with `PJ64_FRAME_DUMP` reports the viewport origin, so the lift can be
  checked rather than assumed.
- **Installed mouse layouts are replaced by `make config`.** `Bin/macOS/Config/mouse/` is
  wiped and re-copied from the tracked examples every build, so renaming an example cannot
  leave the old name behind for the per-ROM lookup to find and the reader to reject. Put a
  player's own layout beside the ROM as `<rom>.yaml`, never there. `Config/input.yaml` is
  still copied once.
- **`Config/face/` is outside the per-ROM lookup on purpose.** `GameConfigPath` searches
  beside the ROM and then `Config/mouse/` only, so a face layout is never picked by ROM
  name and cannot shadow the mouse layout of the same base name; select it with `input=`.
  `make config` replaces the installed copies like the mouse ones.
- **Pitch and roll signs are fixed in code, not YAML.** `kPitchSign` and `kRollSign` in
  `FaceTracker.mm` are the one place each is flipped, decided by the first manual run of
  a face layout the way the yaw negation was. A tilt or nod that reads backwards is a code
  fix there; never swap gesture names in a layout to compensate.
- **The yaw baseline freezes differently once a head stick is bound.** With
  `{stick: head}` the classifier holds the yaw and pitch baselines whenever the stick is
  outside its dead zone, not only past the head-turn threshold; the plugin publishes
  `HeadStickWanted` so a mouse layout keeps the old behaviour. Read
  `FaceGesturesTest.cpp`'s two freeze cases before touching `Track`.
- **Vision's `leftEye`/`rightEye` may be named for the observer, not the player.** The
  two winks can come out mirrored; the fix is the two `EyeAperture` calls in
  `FaceTracker.mm`, and only a manual run of a face layout decides whether to swap them.
- **A control that inherits its built-in binding must stay out of the file.** The grammar
  allows exactly one input per control, and several built-in bindings are a *pair* (a key
  and a gamepad input), so a pair can only survive by the control being omitted. This is
  why `WizardDraft` tracks explicit-versus-inherited instead of just writing all fifteen
  lines, and why loading a base reads the file twice — `InputConfig` merges over the
  defaults and cannot say which controls a file named.
- **The screenshot tour must stay machine-independent.** The save screen's first choice
  prints `SDL_GetBasePath()` — the checkout's absolute path — as the "to:" line. The base
  screen's shipped-layout rows (every row but the first and the last; the last is "a file I
  will type the path of", not a layout) print no path on screen, but choosing one resolves
  its file through `SDL_GetBasePath()` to load it, and that resolved path only exists where
  `make config` has run. A tour stop that shows the save screen's first choice, or picks a
  shipped-layout row, bakes one machine's state into a committed PNG and the drift check
  fails everywhere else; the tour only ever picks base row 0, the built-in bindings. Row N
  is `WizardBaseFile(N - 1)`, so row 0 is not `WizardBaseFile(0)`, and editing a shipped
  layout never changes the pictures. Typed paths in the tour live under `/Users/you/`.
- **The user guide's tables are copied facts.** `Docs/UserGuide.md` lists the built-in
  bindings (section 3) and every `PJ64_*` variable (section 11) by hand. A change to
  `DefaultBindings` in `Source/Project64-sdl/InputConfig.cpp:160` does fail
  `make wizard-screenshots-check` — it changes what pictures 02, 07, 08 or 09 show — but that
  only catches the wizard's own on-screen text. Nothing checks the guide's written-out
  markdown table, and a new `getenv` is invisible to the check entirely: both still need a
  matching guide edit by hand.
- **A window resize is not a render event.** Never resize the drawable, call the video
  plugin's `ChangeSize`, or do GL work in response to `SDL_EVENT_WINDOW_RESIZED`: the
  surface is pinned on purpose and the SDL GL calls deadlock (see "GL belongs to the
  emulation thread"). If the backing-size pin is refused, the window stays fixed and stderr
  says `window stays fixed-size`.
- **Window coordinates are not layout coordinates.** Anything that reads the cursor must
  use the published `PointerSample`, which is in launch-size pixels, never
  `SDL_GetWindowSize` or `SDL_GetMouseState` directly, outside `PublishMouse` — the one
  place that maps them: in a resized or full-screen window those are scaled and offset by
  the bars, and the panel's pixel constants would land on the wrong slot.

## Design docs

Specs and plans live in `Docs/superpowers/specs/` and `Docs/superpowers/plans/`, and
record decisions with their reasoning — including a Result section describing what the
cleanup actually did. Read the relevant one before reopening a decision it settles.
