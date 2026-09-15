# YAML Input Mapping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Correction during execution:** the spec was revised so gamepad buttons use SDL-native
> names (`a`/`b`/`x`/`y`, not `south`/`west`) and the axis `sign` is a sibling key of `axis`
> rather than nested inside it. See
> `Docs/superpowers/specs/2026-09-14-input-yaml-mapping-design.md`.

**Goal:** Let the SDL3 input plugin read its N64-controller bindings from `Config/input.yaml` instead of hardcoded tables, without changing behavior when the file is absent.

**Architecture:** A new standalone module `InputConfig` (inside the input plugin) owns the N64 control set, the built-in default table (today's keyboard + gamepad pairings), YAML loading and validation. `PluginLoaded` loads the file once, off the emulation thread; `GetKeys` only evaluates the resolved table.

**Tech Stack:** C++14, Apple clang, arm64; SDL3 3.4.16 via `pkg-config sdl3`; yaml-cpp 0.9.0 via `pkg-config yaml-cpp` (Homebrew); hand-written Makefile; `sh` verification scripts.

## Global Constraints

- Build is C++14 (`Makefile:31`) with Apple clang (`/usr/bin/clang++`), `-arch arm64`; `Source/` is an include root (`Makefile:23`).
- The input plugin stays a standalone dylib: it links **only** SDL3 and yaml-cpp, never the core libraries (`Makefile:406-409`).
- A control named in the file gets **exactly one** input; the built-in defaults keep today's **two** sources (keyboard *and* gamepad) per control.
- File semantics: absent file = built-in defaults; a control the file omits keeps its default; loading **replaces** only the controls named. `Load` always starts from the built-in defaults, never from the current state.
- Discovery: `PJ64_INPUT_YAML` (non-empty) wins; otherwise `Config/input.yaml` beside the binary, resolved from the plugin dylib's own path. Empty env value = unset. Absent default file is silent; a set-but-broken env path is logged.
- Errors are **all-or-nothing**: any bad control, duplicate control, unknown source name, wrong shape, or `stick`/`keys` on a non-`Stick` control rejects the whole file, logs **one** `stderr` line as `input: <path>[:line:col]: <reason>; using built-in defaults`, and leaves the instance unchanged. Never crash, never exit.
- Fixed values copied from the spec: `STICK_DEAD_ZONE = 4000`, `STICK_THRESHOLD = 16000`, `N64_AXIS_MAX = 80`, Y inverted for the analog stick.
- New files use LF. `Source/Project64-sdl/PluginInput.cpp` and `Makefile` are LF; do not introduce CRLF.
- Do not add comments to code beyond the file's existing style (these files are prose-commented; match that).
- Never overwrite user data: install `Config/input.yaml` with `cp -n`.

---

## File Structure

- `Source/Project64-sdl/InputConfig.h` — **create.** N64 control enum, `Binding`, `InputConfig` (defaults + `Load` + read).
- `Source/Project64-sdl/InputConfig.cpp` — **create.** Default table, YAML parse/validate, `dladdr` path resolution.
- `Source/Project64-sdl/InputConfigTest.cpp` — **create.** Failing-fast `main()` over `Load`; no window, no SDL init.
- `Source/Project64-sdl/PluginInput.cpp` — **modify.** `GetKeys` evaluates via `InputConfig`; `SelftestReport` reports evaluated bits; `PluginLoaded` loads once.
- `Makefile` — **modify.** yaml-cpp flags/deps, `InputConfig.cpp` in `INPUT_SRC`, input link, `input-config-test` target, `config` installs `input.yaml`.
- `Config/input.yaml` — **create.** Shipped mapping (keyboard active, gamepad commented).
- `README.md`, `AGENTS.md` — **modify.** Prerequisites, Input mapping section, architecture/trap notes.

---

### Task 1: Bring yaml-cpp into the build

**Files:**
- Modify: `Makefile` (flags near `:21-22`, `INPUT_SRC` at `:290`, per-component flags at `:336`, input link at `:406-409`, `deps` at `:350-354`)
- Create: `Source/Project64-sdl/InputConfig.h`, `Source/Project64-sdl/InputConfig.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `pkg-config yaml-cpp` wired into the input plugin; `InputConfig.cpp` compiled into the input dylib.

- [ ] **Step 1: Install the dependency**

Run: `brew install yaml-cpp && pkg-config --exists yaml-cpp && pkg-config --modversion yaml-cpp`
Expected: prints a version (0.9.0) — this confirms the module name `yaml-cpp`.

- [ ] **Step 2: Add the flags and the dep check to the Makefile**

After `SDL_LIBS   := ...` (`Makefile:22`) add:

```make
YAML_CFLAGS := $(shell pkg-config --cflags yaml-cpp 2>/dev/null)
YAML_LIBS   := $(shell pkg-config --libs yaml-cpp 2>/dev/null)
```

Change `INPUT_SRC` (`Makefile:290`) to:

```make
INPUT_SRC = Project64-sdl/PluginInput.cpp Project64-sdl/InputConfig.cpp
```

Change the per-component flags line (`Makefile:336`) to add the input objects' yaml cflags:

```make
$(AUDIO_OBJS) $(INPUT_OBJS) $(FRONTEND_OBJS): CPPFLAGS += $(SDL_CFLAGS)
$(INPUT_OBJS) $(BUILD)/Project64-sdl/InputConfigTest.o: CPPFLAGS += $(YAML_CFLAGS)
```

Change the input dylib link (`Makefile:409`) to:

```make
	$(CXX) $(LDFLAGS) -dynamiclib -o $@ $^ $(SDL_LIBS) $(YAML_LIBS)
```

In `deps` (`Makefile:353`), after the SDL3 check add:

```make
	@pkg-config --exists yaml-cpp || { echo "yaml-cpp not found: brew install yaml-cpp"; exit 1; }
```

Change the deps success echo (`Makefile:354`) to mention both:

```make
	@echo "deps ok: SDL3 $$(pkg-config --modversion sdl3), yaml-cpp $$(pkg-config --modversion yaml-cpp), $$($(CXX) --version | head -1)"
```

- [ ] **Step 3: Create the module with a compile probe**

`Source/Project64-sdl/InputConfig.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// N64 control bindings, loaded from Config/input.yaml.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
//
// The input plugin is the only consumer. The file is read once, in PluginLoaded;
// GetKeys only reads the resolved table.
#ifndef INPUT_CONFIG_H
#define INPUT_CONFIG_H

#include <SDL3/SDL.h>
#include <vector>

enum class N64Control
{
    A, B, Z, Start, L, R,
    CUp, CDown, CLeft, CRight,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    Stick, Count
};

struct Binding
{
    enum class Kind { Key, Button, Axis, Stick, Keys };

    Kind kind;
    int code;        // Key: SDL_Scancode; Button: SDL_GamepadButton;
                     // Axis: SDL_GamepadAxis; Stick: X axis (Y is code + 1)
    bool positive;   // Axis: true fires on +, false on -
    SDL_Scancode UpKey, DownKey, LeftKey, RightKey;   // Keys only
};

class InputConfig
{
public:
    static InputConfig & Get();

    // Apply a file over the built-in defaults. On any error returns false, logs one
    // line, and leaves the instance exactly as it was.
    bool Load(const char * Path);

    const std::vector<Binding> & Bindings(N64Control Control) const;

private:
    InputConfig();

    static void DefaultBindings(std::vector<Binding> * Out);

    std::vector<Binding> m_Bindings[(int)N64Control::Count];
};

#endif
```

`Source/Project64-sdl/InputConfig.cpp` (probe only; the table and parser arrive in Tasks 3–5):

```cpp
// Project64 - A Nintendo 64 emulator
// N64 control bindings, loaded from Config/input.yaml.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "InputConfig.h"

#include <yaml-cpp/yaml.h>

InputConfig::InputConfig()
{
    DefaultBindings(m_Bindings);
}

InputConfig & InputConfig::Get()
{
    static InputConfig Instance;
    return Instance;
}

const std::vector<Binding> & InputConfig::Bindings(N64Control Control) const
{
    return m_Bindings[(int)Control];
}

bool InputConfig::Load(const char * Path)
{
    YAML::Node Root = YAML::LoadFile(Path);
    return Root.IsDefined();
}

void InputConfig::DefaultBindings(std::vector<Binding> * Out)
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        Out[i].clear();
    }
}
```

- [ ] **Step 4: Build and confirm the link**

Run: `make deps && make input && make test`
Expected: `deps ok: … yaml-cpp 0.9.0 …`, the input dylib builds, then one version line and four `ok:` lines.

- [ ] **Step 5: Commit**

```bash
git add Makefile Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp
git commit -m "Add yaml-cpp to the input plugin build"
```

---

### Task 2: Lock today's mapping with an evaluated-bits selftest

Verify the current hardcoded mapping **before** refactoring it, so the refactor in Task 3 is proven equivalent.

**Files:**
- Modify: `Source/Project64-sdl/PluginInput.cpp:52-64` (`SelftestReport`), `:132-164` (`GetKeys` keyboard block)
- Modify: `Scripts/grid_selftest.sh:43-46`

**Interfaces:**
- Consumes: `BUTTONS` from the zilmar spec.
- Produces: report line `grid-selftest pid=<pid> recv=<0|1> a=<0|1> start=<0|1>` where `a`/`start` are the **evaluated** `A_BUTTON`/`START_BUTTON` bits and `recv` is the raw snapshot receiving X and Return.

- [ ] **Step 1: Change the report to evaluated bits**

Replace `SelftestReport` (`PluginInput.cpp:52-64`) with:

```cpp
// Verification only. With PJ64_GRID_SELFTEST set, report once both that the snapshot
// carried the strip's pattern (recv) and what the controller produced from it (a, start),
// so Scripts/grid_selftest.sh proves delivery and mapping together.
static void SelftestReport(const bool * Raw, const BUTTONS * Out)
{
    static bool Reported = false;
    if (Reported || getenv("PJ64_GRID_SELFTEST") == nullptr)
    {
        return;
    }
    Reported = true;
    const int Recv = (Raw[SDL_SCANCODE_X] && Raw[SDL_SCANCODE_RETURN]) ? 1 : 0;
    fprintf(stderr, "grid-selftest pid=%d recv=%d a=%d start=%d\n",
        (int)getpid(), Recv, Out->A_BUTTON ? 1 : 0, Out->START_BUTTON ? 1 : 0);
}
```

- [ ] **Step 2: Call it after the keyboard mapping is applied**

In `GetKeys`, the keyboard block ends at `Keys->Y_AXIS = (int8_t)y;` (`PluginInput.cpp:163`). Move the `SelftestReport(Snapshot);` call out of the `if (g_GridKeys != nullptr)` block (`:135-140`) so it runs once, and place this immediately after the keyboard block's closing brace (after `:164`):

```cpp
    bool Raw[SDL_SCANCODE_COUNT] = { false };
    if (g_GridKeys != nullptr)
    {
        GridKeysSnapshot(g_GridKeys, Snapshot);
        memcpy(Raw, Snapshot, sizeof(Raw));
    }
    SelftestReport(Raw, Keys);
```

Add `#include <string.h>` next to the existing includes (`PluginInput.cpp:12-15`).

- [ ] **Step 3: Assert both delivery and mapping in the script**

Replace the check at `Scripts/grid_selftest.sh:43-46` with:

```sh
if printf '%s\n' "$REPORTS" | grep '^grid-selftest ' | grep -v 'recv=1 a=1 start=1' | grep -q .; then
    echo "FAIL: a tile did not deliver or map the key pattern" >&2
    FAIL=1
fi
```

- [ ] **Step 4: Run the proof against the hardcoded mapping**

Run: `make input && make grid-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: `ok: 4 tiles received the key pattern`. This goes through the current hardcoded X→A and Return→Start tables.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-sdl/PluginInput.cpp Scripts/grid_selftest.sh
git commit -m "Assert evaluated buttons in the grid selftest"
```

---

### Task 3: InputConfig defaults, and GetKeys evaluates through it

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.cpp` (add constants, `DefaultBindings`)
- Modify: `Source/Project64-sdl/PluginInput.cpp:124-197` (`GetKeys` and helpers)

**Interfaces:**
- Consumes: `InputConfig::Get()`, `InputConfig::Bindings(N64Control)`.
- Produces: unchanged `GetKeys(int32_t, BUTTONS *)` behavior; `Binding` kinds `Key/Button/Axis/Stick/Keys` are now evaluated.

- [ ] **Step 1: Write the built-in default table**

Replace `DefaultBindings` in `InputConfig.cpp` with (keeping the includes and constructor from Task 1, and adding the constants below them):

```cpp
// N64 stick range is -80..80; the SDL axis range is -32768..32767.
static const int16_t STICK_DEAD_ZONE = 4000;
static const int16_t STICK_THRESHOLD = 16000;
static const int N64_AXIS_MAX = 80;

static Binding MakeKey(SDL_Scancode Sc)
{
    return Binding{ Binding::Kind::Key, (int)Sc, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeButton(SDL_GamepadButton Button)
{
    return Binding{ Binding::Kind::Button, (int)Button, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeAxis(SDL_GamepadAxis Axis, bool Positive)
{
    return Binding{ Binding::Kind::Axis, (int)Axis, Positive, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeStick(SDL_GamepadAxis XAxis)
{
    return Binding{ Binding::Kind::Stick, (int)XAxis, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeStickKeys(SDL_Scancode Up, SDL_Scancode Down, SDL_Scancode Left, SDL_Scancode Right)
{
    return Binding{ Binding::Kind::Keys, 0, true, Up, Down, Left, Right };
}

void InputConfig::DefaultBindings(std::vector<Binding> * Out)
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        Out[i].clear();
    }
    auto add = [Out](N64Control Control, const Binding & B) { Out[(int)Control].push_back(B); };

    add(N64Control::A, MakeKey(SDL_SCANCODE_X));
    add(N64Control::A, MakeButton(SDL_GAMEPAD_BUTTON_SOUTH));
    add(N64Control::B, MakeKey(SDL_SCANCODE_C));
    add(N64Control::B, MakeButton(SDL_GAMEPAD_BUTTON_WEST));
    add(N64Control::Z, MakeKey(SDL_SCANCODE_Z));
    add(N64Control::Z, MakeAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true));
    add(N64Control::Start, MakeKey(SDL_SCANCODE_RETURN));
    add(N64Control::Start, MakeButton(SDL_GAMEPAD_BUTTON_START));
    add(N64Control::L, MakeKey(SDL_SCANCODE_Q));
    add(N64Control::L, MakeButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    add(N64Control::R, MakeKey(SDL_SCANCODE_E));
    add(N64Control::R, MakeButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    add(N64Control::CUp, MakeKey(SDL_SCANCODE_W));
    add(N64Control::CUp, MakeAxis(SDL_GAMEPAD_AXIS_RIGHTY, false));
    add(N64Control::CDown, MakeKey(SDL_SCANCODE_S));
    add(N64Control::CDown, MakeAxis(SDL_GAMEPAD_AXIS_RIGHTY, true));
    add(N64Control::CLeft, MakeKey(SDL_SCANCODE_A));
    add(N64Control::CLeft, MakeAxis(SDL_GAMEPAD_AXIS_RIGHTX, false));
    add(N64Control::CRight, MakeKey(SDL_SCANCODE_D));
    add(N64Control::CRight, MakeAxis(SDL_GAMEPAD_AXIS_RIGHTX, true));
    add(N64Control::DPadUp, MakeKey(SDL_SCANCODE_I));
    add(N64Control::DPadUp, MakeButton(SDL_GAMEPAD_BUTTON_DPAD_UP));
    add(N64Control::DPadDown, MakeKey(SDL_SCANCODE_K));
    add(N64Control::DPadDown, MakeButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    add(N64Control::DPadLeft, MakeKey(SDL_SCANCODE_J));
    add(N64Control::DPadLeft, MakeButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    add(N64Control::DPadRight, MakeKey(SDL_SCANCODE_L));
    add(N64Control::DPadRight, MakeButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    add(N64Control::Stick, MakeStickKeys(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT));
    add(N64Control::Stick, MakeStick(SDL_GAMEPAD_AXIS_LEFTX));
}
```

- [ ] **Step 2: Replace the hardcoded keyboard and gamepad blocks in GetKeys**

Replace `PluginInput.cpp:141-196` (from `if (k != nullptr)` through the gamepad `}`), keeping `OpenGridKeys`, `SelftestReport` and `OpenFirstGamepad` calls, with:

```cpp
    const InputConfig & Config = InputConfig::Get();
    bool StickFromKeys = false;

    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : Config.Bindings((N64Control)i))
        {
            if (B.kind == Binding::Kind::Key)
            {
                if (k != nullptr && k[B.code])
                {
                    SetControl(Keys, (N64Control)i);
                }
            }
            else if (B.kind == Binding::Kind::Keys && k != nullptr)
            {
                int x = 0, y = 0;
                if (k[B.LeftKey]) x -= N64_AXIS_MAX;
                if (k[B.RightKey]) x += N64_AXIS_MAX;
                if (k[B.DownKey]) y -= N64_AXIS_MAX;
                if (k[B.UpKey]) y += N64_AXIS_MAX;
                Keys->X_AXIS = (int8_t)x;
                Keys->Y_AXIS = (int8_t)y;
                if (x != 0 || y != 0)
                {
                    StickFromKeys = true;
                }
            }
        }
    }

    SetSelftestReport(Raw, Keys);

    OpenFirstGamepad();
    if (g_Gamepad != nullptr)
    {
        SDL_UpdateGamepads();
        if (!SDL_GamepadConnected(g_Gamepad))
        {
            CloseGamepad();
            return;
        }
        for (int i = 0; i < (int)N64Control::Count; i++)
        {
            for (const Binding & B : Config.Bindings((N64Control)i))
            {
                if (B.kind == Binding::Kind::Button)
                {
                    if (SDL_GetGamepadButton(g_Gamepad, (SDL_GamepadButton)B.code))
                    {
                        SetControl(Keys, (N64Control)i);
                    }
                }
                else if (B.kind == Binding::Kind::Axis)
                {
                    const int16_t v = SDL_GetGamepadAxis(g_Gamepad, (SDL_GamepadAxis)B.code);
                    if (B.positive ? v > STICK_THRESHOLD : v < -STICK_THRESHOLD)
                    {
                        SetControl(Keys, (N64Control)i);
                    }
                }
                else if (B.kind == Binding::Kind::Stick && !StickFromKeys)
                {
                    Keys->X_AXIS = AxisToN64(SDL_GetGamepadAxis(g_Gamepad, (SDL_GamepadAxis)B.code));
                    Keys->Y_AXIS = (int8_t)-AxisToN64(SDL_GetGamepadAxis(g_Gamepad, (SDL_GamepadAxis)(B.code + 1)));
                }
            }
        }
    }
```

Rename the existing `SelftestReport(Snapshot);` call from Task 2 to `SetSelftestReport(Raw, Keys);` and keep the `Raw` copy in place. Add above `GetKeys`:

```cpp
static void SetControl(BUTTONS * Keys, N64Control Control)
{
    switch (Control)
    {
    case N64Control::A: Keys->A_BUTTON = 1; break;
    case N64Control::B: Keys->B_BUTTON = 1; break;
    case N64Control::Z: Keys->Z_TRIG = 1; break;
    case N64Control::Start: Keys->START_BUTTON = 1; break;
    case N64Control::L: Keys->L_TRIG = 1; break;
    case N64Control::R: Keys->R_TRIG = 1; break;
    case N64Control::CUp: Keys->U_CBUTTON = 1; break;
    case N64Control::CDown: Keys->D_CBUTTON = 1; break;
    case N64Control::CLeft: Keys->L_CBUTTON = 1; break;
    case N64Control::CRight: Keys->R_CBUTTON = 1; break;
    case N64Control::DPadUp: Keys->U_DPAD = 1; break;
    case N64Control::DPadDown: Keys->D_DPAD = 1; break;
    case N64Control::DPadLeft: Keys->L_DPAD = 1; break;
    case N64Control::DPadRight: Keys->R_DPAD = 1; break;
    case N64Control::Stick: break;
    default: break;
    }
}
```

Keep `AxisToN64` as it is (`PluginInput.cpp:93-100`). Add `#include "InputConfig.h"` to `PluginInput.cpp`.

- [ ] **Step 3: Prove the refactor is equivalent**

Run: `make input && make grid-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: `ok: 4 tiles received the key pattern` — the same proof as Task 2, now served by `InputConfig`'s default table.

- [ ] **Step 4: Commit**

```bash
git add Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/PluginInput.cpp
git commit -m "Drive GetKeys from the InputConfig default table"
```

---

### Task 4: Load a valid YAML file

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.cpp`
- Create: `Source/Project64-sdl/InputConfigTest.cpp`
- Modify: `Makefile` (add the test target near `test`, `:453`)

**Interfaces:**
- Consumes: `InputConfig::DefaultBindings`, `Binding`.
- Produces: `bool InputConfig::Load(const char *)` accepts a file that sets some controls; `make input-config-test` runs the checks and exits non-zero on failure.

- [ ] **Step 1: Write the failing test**

`Source/Project64-sdl/InputConfigTest.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// Tests for InputConfig::Load. No window and no SDL init; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "InputConfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

static const char * WriteTemp(const char * Text)
{
    static char Path[64];
    snprintf(Path, sizeof(Path), "/tmp/pj64-input-XXXXXX");
    int Fd = mkstemp(Path);
    if (Fd < 0) { perror("mkstemp"); exit(2); }
    write(Fd, Text, strlen(Text));
    close(Fd);
    return Path;
}

int main()
{
    InputConfig & C = InputConfig::Get();

    const char * Valid =
        "bindings:\n"
        "  A: {key: Y}\n"
        "  Start: {key: Return}\n";
    CHECK(C.Load(WriteTemp(Valid)));
    CHECK(C.Bindings(N64Control::A).size() == 1);
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Key);
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_Y);
    CHECK(C.Bindings(N64Control::B).size() == 2);     // omitted control keeps its default pair

    const char * CommentsOnly = "# nothing here\n";
    CHECK(C.Load(WriteTemp(CommentsOnly)));
    CHECK(C.Bindings(N64Control::A).size() == 2);     // defaults restored

    const char * Gamepad =
        "bindings:\n"
        "  A: {button: south}\n"
        "  Z: {axis: lefttrigger, sign: +}\n"
        "  CUp: {axis: righty, sign: -}\n"
        "  Stick: {stick: left}\n";
    CHECK(C.Load(WriteTemp(Gamepad)));
    CHECK(C.Bindings(N64Control::A)[0].kind == Binding::Kind::Button);
    CHECK(C.Bindings(N64Control::Z)[0].kind == Binding::Kind::Axis);
    CHECK(C.Bindings(N64Control::CUp)[0].positive == false);
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Stick);

    const char * DigitalStick =
        "bindings:\n"
        "  Stick: {keys: {up: Up, down: Down, left: Left, right: Right}}\n";
    CHECK(C.Load(WriteTemp(DigitalStick)));
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Keys);
    CHECK(C.Bindings(N64Control::Stick)[0].UpKey == SDL_SCANCODE_UP);

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: input config\n");
    return 0;
}
```

- [ ] **Step 2: Add the test target**

In `Makefile`, before `test:` (`Makefile:453`) add:

```make
input-config-test: $(BUILD)/Project64-sdl/InputConfigTest.o $(BUILD)/Project64-sdl/InputConfig.o ## Run the InputConfig parser tests
	$(CXX) $(LDFLAGS) -o $(BUILD)/input-config-test $^ $(SDL_LIBS) $(YAML_LIBS)
	@$(BUILD)/input-config-test
```

- [ ] **Step 3: Run it to confirm it fails**

Run: `make input-config-test`
Expected: FAIL — `Load` currently only parses and returns, so `C.Load(Valid)` succeeds but `A` keeps 2 bindings.

- [ ] **Step 4: Implement parsing for the five forms**

Replace `InputConfig::Load` in `InputConfig.cpp` with the following, and add the helpers above it:

```cpp
static int ControlFromName(const std::string & Name)
{
    static const char * kNames[] = {
        "A", "B", "Z", "Start", "L", "R",
        "CUp", "CDown", "CLeft", "CRight",
        "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
        "Stick"
    };
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (Name == kNames[i]) return i;
    }
    return -1;
}

static void ConfigError(const char * Path, const YAML::Node & Node, const std::string & Message)
{
    const YAML::Mark Mark = Node.Mark();
    if (Mark.is_null())
    {
        fprintf(stderr, "input: %s: %s; using built-in defaults\n", Path, Message.c_str());
    }
    else
    {
        fprintf(stderr, "input: %s:%d:%d: %s; using built-in defaults\n",
            Path, (int)Mark.line + 1, (int)Mark.column + 1, Message.c_str());
    }
}

static bool ParseBinding(const char * Path, const YAML::Node & Value, N64Control Control, Binding & Out)
{
    if (!Value.IsMap() || Value.size() != 1)
    {
        ConfigError(Path, Value, "value must be one of {key:}, {button:}, {axis:}, {stick:}, {keys:}");
        return false;
    }
    const std::string Form = Value.begin()->first.as<std::string>();
    const YAML::Node Arg = Value.begin()->second;
    const bool ForStick = (Control == N64Control::Stick);

    if (Form == "key" || Form == "button" || Form == "axis")
    {
        if (ForStick)
        {
            ConfigError(Path, Arg, Form + " cannot drive Stick; use {keys:} or {stick:}");
            return false;
        }
    }
    if (Form == "key")
    {
        const std::string Name = Arg.as<std::string>();
        const SDL_Scancode Sc = SDL_GetScancodeFromName(Name.c_str());
        if (Sc == SDL_SCANCODE_UNKNOWN) { ConfigError(Path, Arg, "unknown key \"" + Name + "\""); return false; }
        Out = MakeKey(Sc);
        return true;
    }
    if (Form == "button")
    {
        const std::string Name = Arg.as<std::string>();
        const SDL_GamepadButton B = SDL_GetGamepadButtonFromString(Name.c_str());
        if (B == SDL_GAMEPAD_BUTTON_INVALID) { ConfigError(Path, Arg, "unknown gamepad button \"" + Name + "\""); return false; }
        Out = MakeButton(B);
        return true;
    }
    if (Form == "axis")
    {
        std::string Name;
        bool Positive = true;
        if (Arg.IsMap())
        {
            if (!Arg["axis"] || Arg.size() > 2) { ConfigError(Path, Arg, "axis takes {axis: name, sign: +/-}"); return false; }
            Name = Arg["axis"].as<std::string>();
            if (Arg["sign"]) { const std::string S = Arg["sign"].as<std::string>();
                if (S == "+") Positive = true; else if (S == "-") Positive = false;
                else { ConfigError(Path, Arg, "sign must be + or -"); return false; } }
        }
        else { Name = Arg.as<std::string>(); }
        const SDL_GamepadAxis A = SDL_GetGamepadAxisFromString(Name.c_str());
        if (A == SDL_GAMEPAD_AXIS_INVALID) { ConfigError(Path, Arg, "unknown gamepad axis \"" + Name + "\""); return false; }
        Out = MakeAxis(A, Positive);
        return true;
    }
    if (Form == "stick")
    {
        if (!ForStick) { ConfigError(Path, Arg, "stick is only valid on Stick"); return false; }
        const std::string Name = Arg.as<std::string>();
        if (Name == "left") { Out = MakeStick(SDL_GAMEPAD_AXIS_LEFTX); return true; }
        if (Name == "right") { Out = MakeStick(SDL_GAMEPAD_AXIS_RIGHTX); return true; }
        ConfigError(Path, Arg, "stick must be left or right");
        return false;
    }
    if (Form == "keys")
    {
        if (!ForStick) { ConfigError(Path, Arg, "keys is only valid on Stick"); return false; }
        if (!Arg.IsMap() || !Arg["up"] || !Arg["down"] || !Arg["left"] || !Arg["right"])
        {
            ConfigError(Path, Arg, "keys needs up, down, left, right");
            return false;
        }
        Out = MakeStickKeys(SDL_GetScancodeFromName(Arg["up"].as<std::string>().c_str()),
                            SDL_GetScancodeFromName(Arg["down"].as<std::string>().c_str()),
                            SDL_GetScancodeFromName(Arg["left"].as<std::string>().c_str()),
                            SDL_GetScancodeFromName(Arg["right"].as<std::string>().c_str()));
        return true;
    }
    ConfigError(Path, Value, "unknown form \"" + Form + "\"");
    return false;
}

bool InputConfig::Load(const char * Path)
{
    YAML::Node Root;
    try
    {
        Root = YAML::LoadFile(Path);
    }
    catch (const YAML::Exception & e)
    {
        fprintf(stderr, "input: %s: %s; using built-in defaults\n", Path, e.what());
        return false;
    }

    std::vector<Binding> Next[(int)N64Control::Count];
    DefaultBindings(Next);

    const YAML::Node Bindings = Root["bindings"];
    if (Bindings)
    {
        if (!Bindings.IsMap()) { ConfigError(Path, Bindings, "'bindings' must be a map"); return false; }
        for (const auto & Entry : Bindings)
        {
            const int Index = ControlFromName(Entry.first.as<std::string>());
            if (Index < 0) { ConfigError(Path, Entry.first, "unknown control \"" + Entry.first.as<std::string>() + "\""); return false; }
            Binding B;
            if (!ParseBinding(Path, Entry.second, (N64Control)Index, B)) return false;
            Next[Index].clear();
            Next[Index].push_back(B);
        }
    }

    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        m_Bindings[i] = Next[i];
    }
    return true;
}
```

Wrap the `Entry.first.as<std::string>()`, `Arg.as<std::string>()` and `Arg["sign"].as<std::string>()` conversions so a wrong YAML type cannot throw out of `Load`: catch `const YAML::Exception &` around the body's map walk and treat it as a generic `ConfigError(Path, Root, "malformed value")`. (Task 5 tests this path.)

- [ ] **Step 5: Run the test to verify it passes**

Run: `make input-config-test`
Expected: `ok: input config`

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/InputConfigTest.cpp Makefile
git commit -m "Parse input bindings from YAML"
```

---

### Task 5: Reject bad files whole

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.cpp`
- Modify: `Source/Project64-sdl/InputConfigTest.cpp`

**Interfaces:**
- Consumes: `Load` from Task 4.
- Produces: `Load` returns `false` and leaves the instance unchanged for every invalid input; one `stderr` line each.

- [ ] **Step 1: Write the failing tests**

Append to `main()` in `InputConfigTest.cpp`, before the failure check:

```cpp
    CHECK(C.Load(WriteTemp(Valid)));                  // establish a known good state
    const size_t ABefore = C.Bindings(N64Control::A).size();

    CHECK(!C.Load(WriteTemp("bindings:\n  Nope: {key: X}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: NoSuchKey}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {button: sout}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: X}\n  A: {key: Y}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: X, button: south}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {stick: middle}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {stick: left}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {keys: {up: Up}}\n")));
    CHECK(!C.Load(WriteTemp("bindings: [1, 2]\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {key: [X]}\n")));
    CHECK(!C.Load("/tmp/pj64-does-not-exist.yaml"));

    CHECK(C.Bindings(N64Control::A).size() == ABefore);   // failed loads changed nothing
    CHECK(C.Bindings(N64Control::A)[0].code == SDL_SCANCODE_Y);
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `make input-config-test`
Expected: FAIL on at least the duplicate-control and wrong-type cases, which Task 4 does not yet reject.

- [ ] **Step 3: Add duplicate detection and a catch-all**

In `Load`, after `DefaultBindings(Next);` add a `bool Seen[(int)N64Control::Count] = { false };`, and inside the loop, before parsing:

```cpp
            if (Seen[Index]) { ConfigError(Path, Entry.first, "control named twice"); return false; }
            Seen[Index] = true;
```

Wrap the whole `if (Bindings) { ... }` body in a `try { ... } catch (const YAML::Exception &) { ConfigError(Path, Root, "malformed value"); return false; }` so a bad type (a sequence where a scalar belongs) cannot escape as an exception.

- [ ] **Step 4: Run the test to verify it passes**

Run: `make input-config-test`
Expected: `ok: input config` and exactly one `input: …` line per bad file, all ending `; using built-in defaults`.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/InputConfigTest.cpp
git commit -m "Reject invalid input configs whole"
```

---

### Task 6: Find and load the file once

**Files:**
- Modify: `Source/Project64-sdl/PluginInput.cpp:241-243` (`PluginLoaded`), includes at `:9-15`
- Modify: `Source/Project64-sdl/InputConfig.cpp` (add `DefaultConfigPath`)

**Interfaces:**
- Consumes: `InputConfig::Load`.
- Produces: `bool DefaultConfigPath(char * Out, size_t Size)`; `PluginLoaded` loads the resolved file.

- [ ] **Step 1: Add path resolution**

In `InputConfig.cpp`, add `#include <dlfcn.h>`, `#include <limits.h>` and:

```cpp
// The plugin lives at <bin>/Plugin/Input/<name>.dylib, so the binary's Config/
// directory is two levels up. Resolving from the dylib keeps the result independent of
// the working directory, which "make run" and "make grid" both rely on.
bool DefaultConfigPath(char * Out, size_t Size)
{
    Dl_info Info;
    if (dladdr((void *)&DefaultConfigPath, &Info) == 0 || Info.dli_fname == nullptr)
    {
        return false;
    }
    std::string Dir = Info.dli_fname;
    for (int i = 0; i < 3; i++)
    {
        const size_t Slash = Dir.find_last_of('/');
        if (Slash == std::string::npos) return false;
        Dir.erase(Slash);
    }
    const std::string Path = Dir + "/Config/input.yaml";
    snprintf(Out, Size, "%s", Path.c_str());
    return true;
}
```

Declare it in `InputConfig.h` below the class:

```cpp
bool DefaultConfigPath(char * Out, size_t Size);
```

- [ ] **Step 2: Load it in PluginLoaded**

Replace the empty `PluginLoaded` (`PluginInput.cpp:241-243`) with:

```cpp
EXPORT void CALL PluginLoaded(void)
{
    const char * Env = getenv("PJ64_INPUT_YAML");
    if (Env != nullptr && Env[0] != '\0')
    {
        InputConfig::Get().Load(Env);
        return;
    }
    char Path[PATH_MAX];
    if (DefaultConfigPath(Path, sizeof(Path)) && access(Path, R_OK) == 0)
    {
        InputConfig::Get().Load(Path);
    }
}
```

Add `#include <limits.h>` to `PluginInput.cpp` (it already includes `<unistd.h>`).

- [ ] **Step 3: Prove the shipped-path behaviour by hand**

Run:
```sh
printf 'bindings:\n  A: {key: Y}\n' > /tmp/pj64-good.yaml
printf 'bindings:\n  A: {key: NoSuchKey}\n' > /tmp/pj64-bad.yaml
PJ64_INPUT_YAML=/tmp/pj64-good.yaml make grid-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64
```
Expected: `ok: 4 tiles received the key pattern` — A is now bound to `Y`, and the strip sends X, so this **fails**. That failure is the proof the env path was honoured; replace `key: Y` with `key: X` and re-run to see it pass.

- [ ] **Step 4: Prove the error path is loud and harmless**

Run:
```sh
PJ64_INPUT_YAML=/tmp/pj64-bad.yaml perl -e 'alarm 8; exec @ARGV' -- ./Bin/macOS/Project64 /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>/tmp/pj64-err.txt || true
grep -c '^input: ' /tmp/pj64-err.txt
```
Expected: `1`, naming `/tmp/pj64-bad.yaml` and the bad key, and the emulator started (no crash).

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/PluginInput.cpp
git commit -m "Load the input config once in PluginLoaded"
```

---

### Task 7: Ship Config/input.yaml

**Files:**
- Create: `Config/input.yaml`
- Modify: `Makefile` (`config` target, `:425-431`)

**Interfaces:**
- Consumes: the grammar from Tasks 4–5.
- Produces: `Bin/macOS/Config/input.yaml` after `make config`, never overwriting an existing file.

- [ ] **Step 1: Write the shipped file**

`Config/input.yaml` (keyboard active; the gamepad mapping is commented and ready to swap):

```yaml
# Input bindings for the SDL3 input plugin. Optional.
#
# A control named here gets exactly one input, replacing its built-in binding (which is
# a keyboard key and a gamepad input at once). An omitted control keeps its built-in
# binding. Delete this file to restore every built-in binding.
#
# Forms:
#   {key: <name>}                         SDL scancode name: X, Return, Space, Up, Left
#   {button: <name>}                      SDL gamepad name: south, west, start, leftshoulder, dpup
#   {axis: <name>} or {axis: <name>, sign: +/-}   SDL gamepad axis: lefttrigger, rightx, righty
#   {stick: left} / {stick: right}        a whole stick (Stick only)
#   {keys: {up:…, down:…, left:…, right:…}}       four keys as a digital stick (Stick only)

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

# Gamepad alternative: replace the `bindings:` block above with this one.
# bindings:
#   A:         {button: south}
#   B:         {button: west}
#   Z:         {axis: lefttrigger}
#   Start:     {button: start}
#   L:         {button: leftshoulder}
#   R:         {button: rightshoulder}
#   CUp:       {axis: righty, sign: -}
#   CDown:     {axis: righty, sign: +}
#   CLeft:     {axis: rightx, sign: -}
#   CRight:    {axis: rightx, sign: +}
#   DPadUp:    {button: dpup}
#   DPadDown:  {button: dpdown}
#   DPadLeft:  {button: dpleft}
#   DPadRight: {button: dpright}
#   Stick:     {stick: left}
```

- [ ] **Step 2: Install it without clobbering edits**

In the `config` target (`Makefile:425-431`), after the cheats/enhancements line add:

```make
	@# -n: the mapping is user data once installed.
	@cp -n Config/input.yaml $(BIN)/Config/ 2>/dev/null || true
```

- [ ] **Step 3: Verify parse and install**

Run: `make config && make grid-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: `ok: 4 tiles received the key pattern` — the installed keyboard file maps X→A and Return→Start, so the evaluated assertion holds.

- [ ] **Step 4: Verify a user edit survives**

Run: `printf '\n# user note\n' >> Bin/macOS/Config/input.yaml && make config && tail -1 Bin/macOS/Config/input.yaml`
Expected: `# user note` — `cp -n` preserved the edit. Then restore with `git checkout -- Config/input.yaml` does not apply (the edit was to the installed copy); remove the installed copy before the final run if desired.

- [ ] **Step 5: Commit**

```bash
git add Config/input.yaml Makefile
git commit -m "Ship the default input mapping"
```

---

### Task 8: Document it, then verify end to end

**Files:**
- Modify: `README.md` (prerequisites `:17-18`, *What works* `:57-59`, add an *Input mapping* section after *Grid mode* `:53`)
- Modify: `AGENTS.md` (Architecture `:52-82`, Traps `:83-106`)

**Interfaces:**
- Consumes: everything above.
- Produces: documentation that matches behavior.

- [ ] **Step 1: README prerequisites and section**

Change the prerequisites line (`README.md:17-18`) to:

```markdown
Prerequisites: Xcode command line tools plus Homebrew SDL3 and yaml-cpp
(`brew install sdl3 pkg-config yaml-cpp`).
```

Add after the Grid mode section (`README.md:53`):

```markdown
## Input mapping

Bindings are read once at startup from `Bin/macOS/Config/input.yaml`
(`PJ64_INPUT_YAML=/path` overrides it); delete the file for the built-in mapping. Each
control takes exactly one input — `{key: X}`, `{button: south}`, `{axis: rightx, sign: -}`,
and for `Stick` also `{stick: left}` or `{keys: {up: Up, …}}` — so naming a control replaces
its built-in binding. The shipped file maps the **keyboard**; its commented block is the
full gamepad alternative. A file with a mistake is ignored whole, with one line on stderr.
```

Change the *What works* sentence (`README.md:57-58`) so it no longer promises gamepad by default:

```markdown
Super Mario 64 renders, plays audio and runs at full speed, with keyboard input through
SDL3; a gamepad works after swapping in the commented block in `Config/input.yaml`. The
window is fixed at 640x480 and the mouse cursor is never captured.
```

- [ ] **Step 2: AGENTS architecture and trap**

Add to Architecture, after the Grid mode paragraph (`AGENTS.md:69-77`):

```markdown
**Input bindings are data.** `Source/Project64-sdl/InputConfig.{h,cpp}` owns the N64 control
set, the built-in default table (each control's keyboard *and* gamepad source) and the YAML
reader; `PluginLoaded` loads `Config/input.yaml` once and `GetKeys` only evaluates the
resolved table. yaml-cpp is a declared Homebrew dependency, linked into the input dylib.
```

Add to Traps (`AGENTS.md:83-106`):

```markdown
- **A listed input control loses its other source.** The shipped `Config/input.yaml` maps the
  keyboard, so a fresh build's gamepad does nothing until the commented gamepad block is
  swapped in. The built-in keyboard+gamepad pairing returns when the file is deleted.
```

- [ ] **Step 3: Full verification**

Run, in order:

```sh
make -j8 all
make test
make input-config-test
make grid-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64
printf 'bindings:\n  A: {key: NoSuchKey}\n' > /tmp/pj64-bad.yaml
PJ64_INPUT_YAML=/tmp/pj64-bad.yaml perl -e 'alarm 8; exec @ARGV' -- ./Bin/macOS/Project64 /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>/tmp/pj64-err.txt || true
grep -c '^input: ' /tmp/pj64-err.txt
```

Expected: `make test` one version line + four `ok:`; `ok: input config`; `ok: 4 tiles received the key pattern`; `1`.

- [ ] **Step 4: Manual gamepad check (documented, not automated)**

Swap the commented block into `Bin/macOS/Config/input.yaml`, launch a ROM, and confirm the
gamepad drives the game. Note the result in the commit message.

- [ ] **Step 5: Commit**

```bash
git add README.md AGENTS.md
git commit -m "Document YAML input mapping"
```

---

## Self-Review

**Spec coverage**

- Part 1 (module, internal list, load once, discovery, interface) → Tasks 1, 3, 6.
- Part 2 (grammar, five forms, control names, thresholds, shipped file) → Tasks 3, 4, 7; thresholds are copied verbatim into Task 3.
- Part 3 (all-or-nothing, one stderr line, absent vs broken, no core `Trace`) → Tasks 4, 5, 6.
- Part 4 (Makefile, install, docs, verification 1–4) → Tasks 1, 6, 7, 8. Verification 2 (evaluated bits) → Task 2, proven first against the hardcoded mapping and again after Task 3.
- Non-goals (no UI, no hot reload, no profiles, one controller, one input per listed control, fixed deadzone) → unchanged by design; nothing in the plan adds them.

**Placeholder scan:** no `TBD`/`TODO`; every code step carries its code and every run step its command and expected output.

**Type consistency:** `InputConfig::Get/Bindings/Load`, `N64Control`, `Binding::Kind` (`Key/Button/Axis/Stick/Keys`), `DefaultConfigPath`, `SetControl`, `SelftestReport(Raw, Out)` are used with the same names and signatures in every task. Task 2's `SelftestReport(const bool *, const BUTTONS *)` matches Task 3's call. Task 4's `MakeKey/MakeButton/MakeAxis/MakeStick/MakeStickKeys` are defined in Task 3 and reused. Task 1's test-target cflags line is the line Task 4's target relies on.

**Known gap:** `InputConfigTest` checks the parser and the resolved table, not live gamepad evaluation; the grid proof covers keyboard evaluation end to end. This matches the spec's stated verification.
