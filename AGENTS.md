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
make input-config-test                   # parser tests for the YAML input mapping
make pointer-layout-test                 # geometry tests for the pointer grid
make face-gesture-test                   # classifier tests for the face gestures
make pointer-selftest rom=Roms/game.z64  # prove the injected-pointer path end to end
make run rom=Roms/game.z64 input=Config/mouse/sm64.yaml face=1
make run rom=Roms/game.z64
make grid roms="Roms/a.z64 Roms/b.z64"   # 1-16 ROMs, one window each
make grid-selftest rom=Roms/game.z64     # prove key broadcast across four tiles
make clean                               # removes build/macos, Bin/macOS, generated Version.h files
make help                                # every target with its stage number
```

`make test` is the smoke suite: the frontend must run `--version`, and each of the four
plugin dylibs must export `GetDllInfo` (checked with `nm`). Passing output is one version
line plus four `ok:` lines. `make input-config-test` runs the YAML input-mapping parser
tests and needs no window. There is no generic unit-test framework, so no other
single-test command exists. `make pointer-layout-test` and `make face-gesture-test` are
pure unit tests. `make pointer-selftest` needs a window server and takes ~30 s.

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
levels (see README). The core and each plugin hold their own trace state, so one value
covers all of them and each ignores names it does not know.

`make grid-selftest rom=…` is the grid's end-to-end proof: one ROM in four tiles, each of
which must report the strip's key pattern. It needs a window server and takes ~40 s.

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
reader; `PluginLoaded` loads `Config/input.yaml` once and `GetKeys` only evaluates the
resolved table. yaml-cpp is a declared Homebrew dependency, linked into the input dylib.

**Mouse and face input go through one shared struct.** `Source/Common/PointerState.h` is
a seqlock over `shm_open`, created by the frontend and passed to the input plugin by
descriptor in `PJ64_POINTER_FD`, the same way the grid passes keys. The frontend's main
loop samples the mouse (SDL3's mouse state functions are main-thread only) and publishes
it; `Source/Project64-sdl/FaceTracker.mm` runs AVFoundation and Vision on a private queue
and writes three gesture bits through `FaceGestures.{h,cpp}`; the plugin evaluates zones,
gestures and the pointer stick in `GetKeys` with the pure geometry in
`Source/Common/PointerLayout.h`, and writes labels and the latched zone back for
`Overlay.cpp`, which draws in `CSdlRenderWindow::SwapWindow` before the flush. Layouts are
the `{zone:}`, `{face:}` and `{stick: pointer}` YAML forms in `Config/mouse/`.

**GL belongs to the emulation thread.** `CSdlRenderWindow` binds the context with
`CGLSetCurrentContext` and presents with `CGLFlushDrawable`. The SDL equivalents are
main-thread-only on macOS and marshal there, which deadlocks on the first swap because
the main thread is itself waiting on the emulation thread. Do not "simplify" them back.

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
- **The camera prompt is attributed to the launcher.** The binary is not an app bundle, so
  macOS asks for camera access on behalf of the terminal or IDE. A past denial there makes
  the tracker report `denied` without a new prompt; the fix is in System Settings.
- **`Config/input.yaml` must stay keyboard-active.** A mouse block there would replace the
  keyboard bindings under the one-binding rule and break the grid's keyboard broadcast and
  `make grid-selftest`. Mouse layouts live in `Config/mouse/`.

## Design docs

Specs and plans live in `Docs/superpowers/specs/` and `Docs/superpowers/plans/`, and
record decisions with their reasoning — including a Result section describing what the
cleanup actually did. Read the relevant one before reopening a decision it settles.
