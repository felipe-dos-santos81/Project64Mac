# AGENTS.md

Guidance for coding agents working in this repository.

## What this is

A permanent fork of [Project64](https://github.com/project64/project64) reduced to one
platform: macOS on Apple Silicon, built by a hand-written `Makefile` with SDL3 and
OpenGL.

`origin` is upstream Project64, **not** a fork of it. Never push. `develop` is the
default branch and is deliberately far ahead of `origin/develop` — never `git pull` on
it, that would drag upstream's Windows-era history back in.

## Commands

```sh
make -j8 all              # the normal build; a few minutes from clean
make test                 # smoke test
make run rom=Roms/game.z64
make clean                # removes build/macos, Bin/macOS, generated Version.h files
make help                 # every target with its stage number
```

`make test` is the whole automated suite: the frontend must run `--version`, and each
of the four plugin dylibs must export `GetDllInfo` (checked with `nm`). Passing output
is one version line plus four `ok:` lines. There is no unit-test framework, so no
single-test command exists.

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
- **`Config/` and `Lang/` are tracked runtime data**, not build inputs. Never delete
  from them.
- **Line endings are mixed.** 132 tracked files are CRLF (including
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

## Design docs

Specs and plans live in `Docs/superpowers/specs/` and `Docs/superpowers/plans/`, and
record decisions with their reasoning — including a Result section describing what the
cleanup actually did. Read the relevant one before reopening a decision it settles.
