# Input mapping from a YAML file — design

Date: 2026-09-14
Status: approved (design), implementation not started

## Goal

Let the SDL3 input plugin read its N64-controller bindings from a YAML file instead of
hardcoded tables, so a user can map any N64 control to a keyboard key or a gamepad input
without recompiling. The file is optional: without it, behavior is exactly what it is today.

## Context

Five facts force the shape of this feature.

- **Both bindings are hardcoded today.** `Source/Project64-sdl/PluginInput.cpp` maps a
  keyboard scancode to each N64 button at lines 143–163, then ORs a gamepad button or axis
  onto the same bits at lines 175–195. Nothing reads a config file.
- **The input plugin is a standalone dylib.** The Makefile links it against SDL3 only
  (`Makefile:407`) and gives it no core libraries, so it does not use `CSettings` or the
  core's `Trace`. It communicates with the frontend by environment variable already — the
  grid key fd (`PluginInput.cpp:31-50`). A config module here must stay self-contained.
- **A control's two sources are both live.** Every digital control is currently driven by a
  key *and* a gamepad button at the same time (they are OR'd, not alternatives). Any
  "one input per control" file format cannot describe that default, so the default has to
  live inside the plugin, not in the shipped file.
- **Grid mode is keyboard-only by contract.** `GridHost` broadcasts the strip's keyboard to
  every tile through `GridKeys`; the gamepad is polled per tile. A config that binds controls
  to the gamepad therefore makes the grid ignore the shared keyboard, and
  `Scripts/grid_selftest.sh` would not notice, because it checks that the snapshot reached
  the tile (`PluginInput.cpp:54-64`), not that a control consumed it.
- **SDL3 already names its inputs.** `SDL_GetScancodeFromName` (`SDL_keyboard.h:313`),
  `SDL_GetGamepadButtonFromString` (`SDL_gamepad.h:1294`) and
  `SDL_GetGamepadAxisFromString` (`SDL_gamepad.h:1211`) turn names into codes and return an
  INVALID sentinel on a miss (all since SDL 3.2.0; the gamepad two are thread-safe, the
  scancode one is documented not thread-safe). No name tables need to be written.

Also relevant: the core calls each plugin's `PluginLoaded` when it loads the dylib
(`Source/Project64-core/Plugins/PluginBase.cpp:69`), which is off the emulation thread and
before any ROM. `make config` installs runtime data beside the binary and uses `cp -n` for
files that are user data once installed (`Makefile:425-431`). yaml-cpp 0.9.0 is available
from Homebrew as a bottle; it is not installed, and SDL3 is the only third-party dependency
the build declares today.

## Non-goals

- No in-app remapping UI, no hot reload, no per-ROM or per-tile profiles.
- No support for >1 controller, and no change to which gamepad is opened (still the first).
- No multi-input bindings in the file: a control named in the file gets exactly one input.
- No configurable deadzone, threshold, range or axis inversion. The stick keeps today's
  fixed deadzone (4000) and built-in Y inversion.
- No change to the core, the frontend, `GridKeys`, or the plugin ABI.
- No generated or versioned config; the shipped file is hand-written.

## Part 1 — Architecture

**One new module owns bindings.** `Source/Project64-sdl/InputConfig.{h,cpp}` is the only
code that knows the N64 control set, the built-in default table, how to read the YAML, and
how to resolve a control to its input(s). It joins `INPUT_SRC` in the Makefile. The input
plugin is the right layer: it already owns both tables and is their only consumer, so the
core, the frontend and the grid need no change.

**Internal model: a short list per control.** A control resolves to zero or more bindings, so
the built-in defaults can express today's "key AND gamepad" pairing without loss. A binding
is a tagged union over the five source kinds (`Key`, `Button`, `Axis`, `Stick`, `Keys`).
The file may set at most one binding per control; loading a file *replaces* that control's
list with the one binding given. Controls the file does not mention keep their list. With no
file, every list is the built-in default, so the no-file path behaves exactly as today.

**`PluginInput.cpp` shrinks to evaluation.** `GetKeys` keeps its shape: read the keyboard
snapshot (grid fd or SDL), then for each N64 control OR in the state of every binding the
config holds for it. The gamepad open/close lifecycle, the grid fd mapping and the selftest
report stay where they are.

**Load once, off the hot path.** `PluginLoaded` (currently empty at `PluginInput.cpp:241`)
loads the file once and publishes the resolved table. `GetKeys` runs on the emulation thread
every frame and must not do file I/O, so it only reads the published table; if a load never
happened, it uses the built-in defaults. `PluginLoaded` runs on the main thread at dylib load,
which is also what makes the non-thread-safe `SDL_GetScancodeFromName` safe to call.

**File discovery.** `PJ64_INPUT_YAML`, when set and non-empty, is the path. Otherwise the
default is `Config/input.yaml` beside the binary, resolved relative to the plugin dylib's own
location (the dylib is `Bin/macOS/Plugin/Input/Project64-input-sdl.dylib`, so two directories
up is `Bin/macOS`). Resolving from the dylib means the result does not depend on the working
directory, which `make run` and `make grid` need. An empty `PJ64_INPUT_YAML` is treated as
unset.

Sketch of the interface (final names may shift in the plan):

```cpp
enum class N64Control { A, B, Z, Start, L, R, CUp, CDown, CLeft, CRight,
                        DPadUp, DPadDown, DPadLeft, DPadRight, Stick, Count };

struct Binding
{
    enum class Kind { Key, Button, Axis, Stick, Keys } kind;
    int  code;       // SDL_Scancode / SDL_GamepadButton / SDL_GamepadAxis
    bool positive;   // axis direction; true for +, false for -
    int  up, down, left, right;   // the four keys of a digital Stick
};

class InputConfig
{
public:
    // Process-wide instance. Starts as the built-in defaults.
    static InputConfig & Get();
    // Apply a file on top of the defaults. On any error returns false, logs once,
    // and leaves the instance exactly as it was.
    bool Load(const char * path);
    const Binding * Begin(N64Control c) const;
    const Binding * End(N64Control c) const;
};
```

## Part 2 — The YAML file

Top level is a `bindings:` mapping of control name to exactly one input. Each value is a
flow mapping on one line with exactly one recognised key:

```yaml
bindings:
  A:         {key: X}
  B:         {key: C}
  Z:         {key: Z}
  Start:     {key: Return}
  L:         {key: Q}
  R:         {key: E}
  CUp:       {key: W}
  CDown:     {key: S}
  CLeft:     {key: A}
  CRight:    {key: D}
  DPadUp:    {key: I}
  DPadDown:  {key: K}
  DPadLeft:  {key: J}
  DPadRight: {key: L}
  Stick:     {keys: {up: Up, down: Down, left: Left, right: Right}}
```

| Form | Meaning | Allowed on |
|---|---|---|
| `{key: <name>}` | Keyboard key, named by SDL scancode name (`X`, `Return`, `Space`, `Up`, `Left`). | any control except `Stick` |
| `{button: <name>}` | Gamepad button, named by SDL gamepad name (`south`, `west`, `start`, `leftshoulder`, `dpup`). | any control except `Stick` |
| `{axis: <name>}` and optional `sign: +`/`-` | Gamepad axis crossing the button threshold in that direction; `sign` defaults to `+`. | any control except `Stick` |
| `{stick: left}` or `{stick: right}` | A whole gamepad stick, analog, fixed deadzone, Y inverted. | `Stick` only |
| `{keys: {up:…, down:…, left:…, right:…}}` | Four keys acting as a digital stick. | `Stick` only |

- **Control names** are the friendly ones above, mapped to the plugin's `BUTTONS` fields:
  `A`→`A_BUTTON`, `B`→`B_BUTTON`, `Z`→`Z_TRIG`, `Start`→`START_BUTTON`, `L`→`L_TRIG`,
  `R`→`R_TRIG`, `CUp`→`U_CBUTTON`, `CDown`→`D_CBUTTON`, `CLeft`→`L_CBUTTON`,
  `CRight`→`R_CBUTTON`, `DPadUp`→`U_DPAD`, `DPadDown`→`D_DPAD`, `DPadLeft`→`L_DPAD`,
  `DPadRight`→`R_DPAD`, and `Stick`→`X_AXIS`/`Y_AXIS`.
- **Source names are SDL's own**, passed to the three lookup functions above, so the
  vocabulary is whatever SDL accepts and nothing is duplicated here.
- **Thresholds and ranges are unchanged:** a digital control driven by an `axis` fires above
  `STICK_THRESHOLD` (16000); a `stick` uses `STICK_DEAD_ZONE` (4000) and scales to
  ±`N64_AXIS_MAX` (80), with Y inverted exactly as today.
- **A file with no `bindings:` key is valid** and means "defaults", so the shipped template
  can be comments plus one active block.

**Shipped file.** `Config/input.yaml` is committed and installed by `make config` with
`cp -n`. It carries the keyboard mapping as the active `bindings:` block and the full
gamepad mapping as a commented second block, ready to swap in. Because listing a control
replaces its default, this file's active block makes the **keyboard** the out-of-box
mapping: keyboard and grid work exactly as today, and a gamepad does nothing until the
commented block is swapped in. README and AGENTS must state that plainly. The built-in
gamepad defaults are still reachable by deleting the file.

## Part 3 — Errors and observability

- **All-or-nothing.** Any error means the whole file is ignored and the built-in behavior
  remains: an unknown control, a control named twice, an unknown key/button/axis name, a
  value that is not one recognised form, or `stick`/`keys` on anything but `Stick`. No
  partial application, so there is no half-configured controller.
- One `stderr` line names the path, the line/column yaml-cpp reports (its exceptions carry a
  `Mark`), and the offending control or source, e.g.
  `input: Config/input.yaml:12: unknown gamepad button "sout"; using built-in defaults`.
- **Absent and broken are different.** No file at the default path and no `PJ64_INPUT_YAML`
  is silent — the feature is opt-in and existing users should see no new output. If
  `PJ64_INPUT_YAML` is set but the file cannot be opened or parsed, that is an explicit
  request failing, so it is logged.
- Logging matches the plugin's existing `fprintf(stderr, "input: …")` style; the plugin gains
  no dependency on the core's `Trace`. It never crashes and never exits.

## Part 4 — Build, install, verification

**Makefile.**

- `InputConfig.cpp` joins `INPUT_SRC`.
- `YAML_CFLAGS`/`YAML_LIBS` come from `pkg-config --cflags/--libs yaml-cpp`; `INPUT_OBJS` get
  the cflags, and the input dylib link adds the libs.
- `deps` gains `pkg-config --exists yaml-cpp || { echo "yaml-cpp not found: brew install yaml-cpp"; exit 1; }`.
- `config` copies `Config/input.yaml` into `Bin/macOS/Config/` with `cp -n`.

**Docs.** README gains `yaml-cpp` in the prerequisites and a short **Input mapping** section
(path, `PJ64_INPUT_YAML`, the value forms, and the keyboard-default/gamepad-edit fact). The
"keyboard and gamepad" line in *What works* is corrected to match. AGENTS gains an
architecture note (InputConfig owns bindings, loaded once in `PluginLoaded`; yaml-cpp is a
declared dependency) and a trap (listing a control replaces its built-in dual source; the
shipped file is keyboard-active so gamepad needs the edit).

**Verification.**

1. `make test` is green — the input plugin links against yaml-cpp and still exports its five
   symbols.
2. Extend the existing proof so `make grid-selftest` covers mapping, not just delivery:
   `SelftestReport` additionally prints the *evaluated* N64 bits, and
   `Scripts/grid_selftest.sh` runs with `PJ64_INPUT_YAML` pointed at the tracked
   `Config/input.yaml` and asserts that pressing X and Return sets `A_BUTTON` and
   `START_BUTTON`.
3. Error path: run with `PJ64_INPUT_YAML=/tmp/bad.yaml` (malformed) and confirm the frontend
   still runs, the selftest still passes on the built-in defaults, and exactly one stderr
   line names the file and line.
4. `PJ64_INPUT_YAML` pointed at a gamepad-block file starts and does not crash. Since there
   is no synthetic gamepad, actual gamepad control stays a documented manual check.

## Open risks

- **yaml-cpp 0.9.0 under `-std=c++14`.** The library only requires C++11 (since 0.6.0) and
  its internals feature-detect C++14/17, so this should build. Confirm in the first
  implementation step; the fallback is to raise just this translation unit to C++17.
- **`pkg-config` module name.** Homebrew is expected to install `yaml-cpp.pc` under the name
  `yaml-cpp`; confirm with `pkg-config --exists yaml-cpp` after `brew install yaml-cpp`.
- **Gamepad is off by default.** The accepted consequence of an active shipped keyboard
  block under the one-input rule. It is documented, and deleting the file restores the
  built-in keyboard+gamepad pairing.
- **Gamepad path unproven by automation.** No synthetic gamepad exists, so the gamepad
  bindings are covered by the same evaluation code as the keyboard but only exercised by
  hand.
