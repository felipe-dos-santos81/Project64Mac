# Per-game input YAML — design

Date: 2026-09-15
Status: approved (design), implementation not started

## Goal

When a ROM is loaded, a YAML mapping file named after it is used as that game's input
layout without any flag, and the camera starts by itself when that layout binds a face
gesture. Explicit settings keep winning, and nothing changes for a ROM that has no such
file.

## Context

- **The plugin already takes its file from one variable.** `PluginLoaded` in
  `Source/Project64-sdl/PluginInput.cpp` loads `PJ64_INPUT_YAML` when set, else
  `<bin>/Config/input.yaml`, else the built-in table. The frontend knows the ROM path
  before any plugin loads (`main.cpp` resolves `RomPath` before `AppInit`), so it can set
  that variable and the plugin needs no change to its loading logic.
- **`Config/mouse/` is installed beside the binary.** `make config` copies it to
  `Bin/macOS/Config/mouse/`, the same way it installs `input.yaml`, so a lookup relative
  to the executable directory finds it regardless of the working directory.
- **The frontend does not link the YAML parser.** yaml-cpp and `InputConfig.cpp` are
  compiled into the input dylib only. The plugin already publishes one fact about the
  layout to the frontend side through `PointerState::OverlayWanted`
  (`Source/Common/PointerState.h`), set in `PublishPointerLabels` at dylib load.
- **The face tracker starts from the main thread** (`FaceTrackerStart` in `main.cpp`,
  before `AppInit`) and stops before `CloseSystem`. Its classifier baselines over five
  seconds, so a start a few frames later is invisible to the player.
- **Grid tiles must stay keyboard-driven.** A mouse layout in a tile replaces the
  keyboard bindings and breaks the strip's broadcast (AGENTS.md trap, "`Config/input.yaml`
  must stay keyboard-active").

## Non-goals

- Merging a per-game file over `input.yaml`; the file replaces it whole, as any file does.
- A ROM-header or ROM-database key. The base name of the file on disk is the key.
- Hot reload when the file changes, or a per-game file in grid mode.
- Renaming the shipped layouts in `Config/mouse/`. They keep their short names.

## Part 1 — Resolution

**Rule.** Let `base` be the ROM's file name with its last extension removed
(`super_mario_64_usa.z64` and `super_mario_64_usa.zip` both give `super_mario_64_usa`).
Before `AppInit`, the frontend checks, in order:

1. `<rom dir>/<base>.yaml`
2. `<executable dir>/Config/mouse/<base>.yaml`

The first path that is readable is exported as `PJ64_INPUT_YAML` with overwrite off
(`setenv(..., 0)`), so a variable already in the environment, including the Makefile's
`input=`, always wins. One line goes to stderr, `input layout: <path>`, whenever the
frontend chooses a file, so a changed binding set is never silent.

**Precedence** for the plugin's file, highest first: `PJ64_INPUT_YAML` set by the user,
sibling YAML, `Config/mouse/<base>.yaml`, `<bin>/Config/input.yaml`, built-in table. The
plugin sees only the first two as one variable and is unchanged.

**Tile mode** (`--tile`) skips the lookup. `--grid` children therefore behave exactly as
today. A grid user who wants a per-game keyboard remap sets `PJ64_INPUT_YAML` explicitly.

**Unit.** `Source/Project64-sdl/GameConfig.{h,cpp}` holds one function:

```cpp
// Path of the per-game YAML for RomPath, or false when neither candidate exists.
// ExeDir is the directory holding the frontend binary. Pure: reads the file system,
// touches no environment variable.
bool GameConfigPath(const char * RomPath, const char * ExeDir, char * Out, size_t Size);
```

`main.cpp` calls it once, then does the `setenv` and the stderr line. Keeping the
environment out of the function is what makes it testable.

## Part 2 — Face auto-start

**Signal.** `PointerState` gains `std::atomic<uint32_t> FaceWanted` next to
`OverlayWanted`. `InputConfig` gains `bool UsesFace() const`, true when any binding is
`Binding::Kind::Face`. `PublishPointerLabels` stores `UsesFace() ? 1 : 0` into
`FaceWanted` at dylib load, before any ROM opens, exactly as it does for the overlay.

**Three states of `PJ64_FACE`.**

| Setting | Camera |
|---|---|
| `--face`, or `PJ64_FACE` set to a non-empty value other than `0` | starts before `AppInit`, as today |
| `PJ64_FACE=0` | never starts |
| unset or empty | starts when the plugin publishes `FaceWanted` |

The Makefile's `face=1` maps to the first row unchanged. Tile mode never starts the
tracker, in any row.

**Mechanism.** The main loop already runs every 10 ms. It keeps a `FaceStarted` flag; on
each pass, if the flag is clear, the face setting is "unset", the pointer exists, tile
mode is off, and `FaceWanted.load(std::memory_order_acquire)` is non-zero, it calls
`FaceTrackerStart(pointer)` and sets the flag. `FaceTrackerStop` at shutdown is already
unconditional and safe when nothing started. The store side uses
`std::memory_order_release`.

Auto-start applies equally to a file chosen by `input=`: `make run input=Config/mouse/sm64.yaml`
now opens the camera without `face=1`. That is intended; `face=0` is the opt-out.

## Part 3 — Testing

- **`make game-config-test`** (`Source/Project64-sdl/GameConfigTest.cpp`): builds a temp
  tree and checks `GameConfigPath` for: sibling present (returns sibling even when the
  mouse copy also exists); only `Config/mouse/<base>.yaml` present; neither present
  (returns false); a `.zip` ROM; a ROM with no extension; a ROM path with no directory
  component (sibling resolves in the current directory).
- **`make input-config-test`** gains: a file with a `{face:}` binding gives
  `UsesFace()` true; the keyboard-only file and a zone-only file give false.
- **`make pointer-selftest`** gains a third run. It symlinks the ROM into a temp directory,
  copies `Config/mouse/sm64.yaml` beside it as `<base>.yaml`, runs the frontend with
  `PJ64_INPUT_YAML` unset and `PJ64_FACE=0`, and expects the same `zone=12 a=1 start=0`
  report as the explicit centre-click run. `PJ64_FACE=0` keeps the camera prompt out of a
  test. That run is the end-to-end proof of the sibling lookup.
- **`make test`** stays the smoke suite and is unaffected.

## Part 4 — Documentation

- `README.md`, "Playing with a mouse": one paragraph on naming a YAML after the ROM, the
  two locations, that the camera then starts by itself, and `PJ64_FACE=0` to stop it.
- `AGENTS.md`: the lookup order and the three-state flag in the input-bindings
  paragraph; a trap entry that a YAML beside a ROM changes that game's bindings, with the
  stderr line as the tell.
- `Makefile`: the `run` target's usage comment shows `face=0` as well as `face=1`.

## Open risks

- **A stray YAML beside a ROM.** Any `<base>.yaml` in a ROM folder is picked up. The
  stderr line and the README paragraph are the mitigation; there is no allow-list.
- **Symlinked ROMs.** The lookup uses the path as given, not the resolved target, so a
  symlink's own directory is where the sibling is expected. The selftest relies on that.
