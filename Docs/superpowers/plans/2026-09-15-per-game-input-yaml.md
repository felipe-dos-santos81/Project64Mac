# Per-game input YAML Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When a ROM loads, a YAML file named after it (beside the ROM, else under `Config/mouse/`) becomes that game's input layout, and the camera starts by itself when that layout binds a face gesture.

**Architecture:** The frontend (`main.cpp`) resolves the per-game path with a new pure function before any plugin loads and exports it as `PJ64_INPUT_YAML`, which the input plugin already honours. The plugin, which is the only parser of the YAML, publishes one more bit (`FaceWanted`) into the shared `PointerState`; the frontend's main loop starts the face tracker when it sees that bit and `PJ64_FACE` is unset.

**Tech Stack:** C++14, POSIX (`access`, `setenv`), GNU Make, `std::atomic`. No new dependencies; the frontend still does not link yaml-cpp.

**Spec:** `Docs/superpowers/specs/2026-09-15-per-game-input-yaml-design.md`

## Global Constraints

- Build with the existing `Makefile` only: `-std=c++14`, `-I$(SRC)`; test binaries follow the `pointer-layout-test` pattern (`$(CXX) $(LDFLAGS) -o $(BUILD)/<name> $^`, then `@$(BUILD)/<name>`).
- The frontend must not link yaml-cpp or `InputConfig.cpp`. The plugin is the only YAML parser.
- Explicit `PJ64_INPUT_YAML` (non-empty) always wins over the lookup; `--tile` mode skips the lookup and never starts the camera.
- `PJ64_FACE`: unset or empty = auto; `0` = never; any other non-empty value or `--face` = start at once.
- New files carry the four-line header used across `Source/Project64-sdl/` (title, purpose, licence). Check `file <path>` before editing a tracked file and preserve its line endings (all files touched here are LF).
- Never capture the screen. Windowed runs are wrapped in `perl -e 'alarm N; exec @ARGV' -- <cmd> || true`. Never run with the camera enabled except where a step says so.
- Commit after each task with the message given; end every commit message with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- Test ROM: `/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`.

---

### Task 1: `GameConfigPath` and its unit test

**Files:**
- Create: `Source/Project64-sdl/GameConfig.h`
- Create: `Source/Project64-sdl/GameConfig.cpp`
- Create: `Source/Project64-sdl/GameConfigTest.cpp`
- Modify: `Makefile:293` (`FRONTEND_SRC`), `Makefile:344` (`.PHONY`), after the `face-gesture-test` target (~line 477)

**Interfaces:**
- Consumes: nothing new.
- Produces: `bool GameConfigPath(const char * RomPath, const char * ExeDir, char * Out, size_t Size);` in `GameConfig.h`, compiled into the frontend. Task 3 calls it.

- [ ] **Step 1: Write the header**

`Source/Project64-sdl/GameConfig.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// Per-game input layout: a YAML named after the ROM, beside it or under Config/mouse/.
// Design: Docs/superpowers/specs/2026-09-15-per-game-input-yaml-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#include <stddef.h>

// Writes the per-game YAML path for RomPath into Out and returns true, or returns false
// and leaves Out alone when neither candidate is readable. Candidates, in order:
// <rom dir>/<base>.yaml, then <ExeDir>/Config/mouse/<base>.yaml, where base is the ROM
// file name without its last extension. Reads the file system only; touches no
// environment variable, so main.cpp owns the PJ64_INPUT_YAML decision.
bool GameConfigPath(const char * RomPath, const char * ExeDir, char * Out, size_t Size);

#endif
```

- [ ] **Step 2: Write the failing test**

`Source/Project64-sdl/GameConfigTest.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// Tests for GameConfigPath. Builds a temp tree; no window and no SDL init.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "GameConfig.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

static void Touch(const std::string & Path)
{
    FILE * F = fopen(Path.c_str(), "w");
    if (F == nullptr) { perror(Path.c_str()); exit(2); }
    fclose(F);
}

static void MakeDir(const std::string & Path)
{
    if (mkdir(Path.c_str(), 0700) != 0) { perror(Path.c_str()); exit(2); }
}

int main()
{
    char Root[] = "/tmp/pj64-gamecfg-XXXXXX";
    if (mkdtemp(Root) == nullptr) { perror("mkdtemp"); return 2; }
    const std::string Roms = std::string(Root) + "/roms";
    const std::string Exe = std::string(Root) + "/bin";
    MakeDir(Roms);
    MakeDir(Exe);
    MakeDir(Exe + "/Config");
    MakeDir(Exe + "/Config/mouse");
    const std::string Rom = Roms + "/game.z64";   // the ROM itself need not exist
    char Out[PATH_MAX];

    // Neither candidate: false, and Out is left alone.
    strcpy(Out, "untouched");
    CHECK(!GameConfigPath(Rom.c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(strcmp(Out, "untouched") == 0);

    // Only the installed copy under Config/mouse.
    Touch(Exe + "/Config/mouse/game.yaml");
    CHECK(GameConfigPath(Rom.c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Exe + "/Config/mouse/game.yaml");

    // The sibling wins when both exist.
    Touch(Roms + "/game.yaml");
    CHECK(GameConfigPath(Rom.c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Roms + "/game.yaml");

    // Only the last extension is stripped, so a .zip finds the same sibling.
    CHECK(GameConfigPath((Roms + "/game.zip").c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Roms + "/game.yaml");

    // A ROM with no extension uses its whole name.
    Touch(Roms + "/plain.yaml");
    CHECK(GameConfigPath((Roms + "/plain").c_str(), Exe.c_str(), Out, sizeof(Out)));
    CHECK(std::string(Out) == Roms + "/plain.yaml");

    // A different ROM in the same folder does not pick up game.yaml.
    CHECK(!GameConfigPath((Roms + "/other.z64").c_str(), Exe.c_str(), Out, sizeof(Out)));

    // A path ending in a slash names no ROM.
    CHECK(!GameConfigPath((Roms + "/").c_str(), Exe.c_str(), Out, sizeof(Out)));

    // No directory component: the sibling is looked up in the working directory.
    if (chdir(Roms.c_str()) != 0) { perror("chdir"); return 2; }
    CHECK(GameConfigPath("game.z64", Exe.c_str(), Out, sizeof(Out)));
    CHECK(strcmp(Out, "./game.yaml") == 0);

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: game config\n");
    return 0;
}
```

- [ ] **Step 3: Add the Makefile target and source, then run the test to see it fail to link**

In `Makefile`, add `GameConfig.cpp` to `FRONTEND_SRC` (line 293):

```make
FRONTEND_SRC = $(addprefix Project64-sdl/, main.cpp SdlNotification.cpp SdlRenderWindow.cpp GridHost.cpp FaceGestures.cpp Overlay.cpp GameConfig.cpp)
```

Append `game-config-test` to the `.PHONY` line (line 344), after `face-gesture-test`.

After the `face-gesture-test` target, add:

```make
game-config-test: $(BUILD)/Project64-sdl/GameConfigTest.o $(BUILD)/Project64-sdl/GameConfig.o ## Run the GameConfigPath lookup tests
	$(CXX) $(LDFLAGS) -o $(BUILD)/game-config-test $^
	@$(BUILD)/game-config-test
```

(The recipe lines are tab-indented, as every recipe in this file is.)

Run: `make game-config-test`
Expected: fails because `Source/Project64-sdl/GameConfig.cpp` does not exist yet ("No rule to make target").

- [ ] **Step 4: Write the implementation**

`Source/Project64-sdl/GameConfig.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// Per-game input layout lookup. See GameConfig.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "GameConfig.h"

#include <stdio.h>
#include <string>
#include <unistd.h>

bool GameConfigPath(const char * RomPath, const char * ExeDir, char * Out, size_t Size)
{
    const std::string Rom = RomPath;
    const size_t Slash = Rom.find_last_of('/');
    const std::string Dir = Slash == std::string::npos ? "." : Rom.substr(0, Slash);
    std::string Base = Slash == std::string::npos ? Rom : Rom.substr(Slash + 1);
    const size_t Dot = Base.find_last_of('.');
    if (Dot != std::string::npos && Dot > 0)   // ".hidden" keeps its name whole
    {
        Base.erase(Dot);
    }
    if (Base.empty())
    {
        return false;
    }
    const std::string Candidates[2] = {
        Dir + "/" + Base + ".yaml",
        std::string(ExeDir) + "/Config/mouse/" + Base + ".yaml",
    };
    for (const std::string & Path : Candidates)
    {
        if (access(Path.c_str(), R_OK) == 0)
        {
            snprintf(Out, Size, "%s", Path.c_str());
            return true;
        }
    }
    return false;
}
```

Note for a ROM at the file-system root (`/game.z64`): `Dir` is the empty string and the sibling candidate is `/game.yaml`, which is right.

- [ ] **Step 5: Run the test to verify it passes**

Run: `make game-config-test`
Expected: last line `ok: game config`, exit 0.

- [ ] **Step 6: Confirm the frontend still builds with the new source and the smoke test passes**

Run: `make -j8 all && make test`
Expected: `all` links `Bin/macOS/Project64`; `make test` prints the version line and four `ok:` lines.

- [ ] **Step 7: Commit**

```bash
git add Source/Project64-sdl/GameConfig.h Source/Project64-sdl/GameConfig.cpp Source/Project64-sdl/GameConfigTest.cpp Makefile
git commit -m "Add GameConfigPath: the per-game input YAML lookup

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: `UsesFace()` and the `FaceWanted` signal

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.h:51-53` (after `UsesPointer`)
- Modify: `Source/Project64-sdl/InputConfig.cpp:40-53` (after `UsesPointer`)
- Modify: `Source/Project64-sdl/InputConfigTest.cpp:75`, `:96`, `:129`
- Modify: `Source/Common/PointerState.h:53-54`
- Modify: `Source/Project64-sdl/PluginInput.cpp:380-391` (`PublishPointerLabels`)

**Interfaces:**
- Consumes: `Binding::Kind::Face` from `InputConfig.h`; `PointerState::OverlayWanted` pattern.
- Produces: `bool InputConfig::UsesFace() const;` and `std::atomic<uint32_t> PointerState::FaceWanted` (1 when the loaded layout binds any face gesture, stored with `std::memory_order_release` at dylib load). Task 3 reads `FaceWanted` with `std::memory_order_acquire`.

- [ ] **Step 1: Write the failing tests**

In `Source/Project64-sdl/InputConfigTest.cpp`:

After line 75 (`CHECK(C.UsesPointer());` in the `Pointer` block) add:

```cpp
    CHECK(C.UsesFace());                              // Z and B are gestures
```

After line 96 (`CHECK(!C.UsesPointer());                          // keyboard-only file: no overlay`) add:

```cpp
    CHECK(!C.UsesFace());

    const char * ZonesOnly =
        "bindings:\n"
        "  Stick: {stick: pointer}\n"
        "  A: {zone: centre}\n";
    CHECK(C.Load(WriteTemp(ZonesOnly)));
    CHECK(C.UsesPointer());
    CHECK(!C.UsesFace());                             // pointer without gestures: no camera
```

After line 129 (`CHECK(C.UsesPointer());` following the `sm64.yaml` load) add:

```cpp
    CHECK(C.UsesFace());                              // sm64.yaml binds Z, B and R to gestures
```

- [ ] **Step 2: Run the test to verify it fails to compile**

Run: `make input-config-test`
Expected: compile error, `no member named 'UsesFace' in 'InputConfig'`.

- [ ] **Step 3: Declare and implement `UsesFace`**

In `Source/Project64-sdl/InputConfig.h`, after the `UsesPointer` declaration (line 53), add:

```cpp
    // True when any binding is a face gesture, which is what starts the camera when
    // PJ64_FACE is unset.
    bool UsesFace() const;
```

In `Source/Project64-sdl/InputConfig.cpp`, after `UsesPointer` (line 53), add:

```cpp
bool InputConfig::UsesFace() const
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Face)
            {
                return true;
            }
        }
    }
    return false;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make input-config-test`
Expected: `ok: input config`, exit 0.

- [ ] **Step 5: Add `FaceWanted` to the shared struct**

In `Source/Common/PointerState.h`, replace lines 53-54:

```cpp
    // Plugin at load -> overlay. Written before the ROM opens; plain storage.
    std::atomic<uint32_t> OverlayWanted;
```

with:

```cpp
    // Plugin at load -> overlay and frontend. Written before the ROM opens.
    std::atomic<uint32_t> OverlayWanted;
    std::atomic<uint32_t> FaceWanted;    // 1 when the layout binds a face gesture
```

The frontend zeroes the whole struct in `CreatePointerState` (`main.cpp:105`), so the field starts at 0 without further change.

- [ ] **Step 6: Publish it from the plugin**

In `Source/Project64-sdl/PluginInput.cpp`, in `PublishPointerLabels`, after

```cpp
    g_Pointer->OverlayWanted.store(Config.UsesPointer() ? 1u : 0u);
```

add:

```cpp
    // The frontend's main loop polls this to start the camera; it may run on another
    // thread than the one loading the dylib, hence release here and acquire there.
    g_Pointer->FaceWanted.store(Config.UsesFace() ? 1u : 0u, std::memory_order_release);
```

- [ ] **Step 7: Rebuild everything that includes the header and run the smoke test**

Run: `make -j8 all && make test && make input-config-test && make pointer-layout-test`
Expected: all green. The struct changed size, so the frontend, the plugin and the overlay must all rebuild; `make all` does that through the dependency files.

- [ ] **Step 8: Commit**

```bash
git add Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/InputConfigTest.cpp Source/Common/PointerState.h Source/Project64-sdl/PluginInput.cpp
git commit -m "Publish FaceWanted from the input plugin when the layout binds a gesture

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Frontend lookup, three-state `PJ64_FACE`, lazy tracker start, end-to-end proof

**Files:**
- Modify: `Source/Project64-sdl/main.cpp:5-26` (includes), `:164-211` (argument parsing), `:263-267` (tracker start), `:306-309` (main loop)
- Modify: `Makefile:450-452` (`run` target)
- Modify: `Scripts/pointer_selftest.sh`

**Interfaces:**
- Consumes: `GameConfigPath` (Task 1), `PointerState::FaceWanted` (Task 2), existing `FaceTrackerStart(PointerState *)`, `ExecutableDirectory()` in `main.cpp`.
- Produces: the stderr line `input layout: <path>`; `PJ64_FACE=0` as the camera opt-out; `make run face=0`.

- [ ] **Step 1: Extend the selftest script first, so the end-to-end check exists before the code**

Replace the whole of `Scripts/pointer_selftest.sh` with:

```sh
#!/bin/sh
# Prove the pointer path end to end: the frontend publishes an injected pointer sample, the
# plugin latches the zone under it with Config/mouse/sm64.yaml loaded, and the N64 bits
# come out right. Three runs: a click in the centre must set A; a click in top4 must set
# Start; and with no PJ64_INPUT_YAML at all, a copy of the layout named after the ROM and
# sitting beside it must be found by the frontend's own lookup (centre must set A again).
# Design: Docs/superpowers/specs/2026-09-15-mouse-and-face-input-design.md and
# Docs/superpowers/specs/2026-09-15-per-game-input-yaml-design.md
set -eu

ROM="${1:?usage: pointer_selftest.sh <rom>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64"
YAML="$ROOT/Config/mouse/sm64.yaml"
TIMEOUT=20

[ -x "$BIN" ] || { echo "frontend not built: $BIN" >&2; exit 1; }

# The third run must see the frontend's own lookup, not a value inherited from the caller.
unset PJ64_INPUT_YAML

# $1 = inject spec, $2 = expected report tail, $3 = ROM path, $4 = PJ64_INPUT_YAML value,
# or "" to leave it unset. PJ64_FACE=0 keeps the camera prompt out of a test. The window
# is 640x480.
one_run() {
    LOG="$(mktemp)"
    if [ -n "$4" ]; then
        PJ64_INPUT_YAML="$4" PJ64_FACE=0 PJ64_POINTER_SELFTEST=1 PJ64_POINTER_INJECT="$1" "$BIN" "$3" >"$LOG" 2>&1 &
    else
        PJ64_FACE=0 PJ64_POINTER_SELFTEST=1 PJ64_POINTER_INJECT="$1" "$BIN" "$3" >"$LOG" 2>&1 &
    fi
    PID=$!
    I=0
    while [ "$I" -lt "$TIMEOUT" ]; do
        grep -q '^pointer-selftest ' "$LOG" 2>/dev/null && break
        sleep 1
        I=$((I + 1))
    done
    kill -TERM "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    REPORT="$(grep '^pointer-selftest ' "$LOG" || true)"
    if [ "$REPORT" = "pointer-selftest $2" ]; then
        rm -f "$LOG"
        return 0
    fi
    echo "FAIL: inject $1: got '$REPORT', wanted 'pointer-selftest $2' (log: $LOG)" >&2
    return 1
}

FAIL=0
one_run "320,240,1" "zone=12 a=1 start=0" "$ROM" "$YAML" || FAIL=1
one_run "600,20,1" "zone=3 a=0 start=1" "$ROM" "$YAML" || FAIL=1

# Third run: symlink the ROM into a temp folder under its own name and extension, with the
# layout beside it as <base>.yaml. Without the lookup the keyboard mapping loads and a
# centre click cannot set A, so a=1 here proves the sibling file was used.
TMP="$(mktemp -d)"
NAME="$(basename "$ROM")"
BASE="${NAME%.*}"
ln -s "$(cd "$(dirname "$ROM")" && pwd)/$NAME" "$TMP/$NAME"
cp "$YAML" "$TMP/$BASE.yaml"
one_run "320,240,1" "zone=12 a=1 start=0" "$TMP/$NAME" "" || FAIL=1
rm -rf "$TMP"

if [ "$FAIL" -eq 0 ]; then
    echo "ok: pointer path maps centre to A and top4 to Start, and finds a layout named after the ROM"
fi
exit "$FAIL"
```

- [ ] **Step 2: Run the selftest to verify the third run fails**

Run: `make pointer-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: the first two runs pass; the third prints `FAIL: inject 320,240,1: got ...` with either an empty report or `a=0`, because the frontend does not look for the sibling yet and the keyboard mapping loads instead. Exit 1. (Takes about 60 s: three bounded windowed runs.)

- [ ] **Step 3: Add the lookup to `main.cpp`**

Add two includes next to the existing ones (after `#include "FaceTracker.h"` on line 8, and after `#include <fcntl.h>` on line 18):

```cpp
#include "GameConfig.h"
```

```cpp
#include <limits.h>
```

Immediately after the usage check that ends on line 211 (`return 2; }` for `RomPath == nullptr`), before `SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");`, add:

```cpp
    // A YAML named after the ROM, beside it or under Config/mouse/, is that game's layout.
    // A non-empty PJ64_INPUT_YAML already in the environment wins, and tiles skip the
    // lookup: a mouse layout would replace the keyboard bindings the grid strip broadcasts.
    const char * ExplicitLayout = getenv("PJ64_INPUT_YAML");
    if (!TileMode && (ExplicitLayout == nullptr || ExplicitLayout[0] == '\0'))
    {
        char Layout[PATH_MAX];
        if (GameConfigPath(RomPath, ExecutableDirectory().c_str(), Layout, sizeof(Layout)))
        {
            setenv("PJ64_INPUT_YAML", Layout, 1);
            fprintf(stderr, "input layout: %s\n", Layout);
        }
    }
```

- [ ] **Step 4: Replace the face flag with a three-state mode**

Above `main` (after `PublishMouse`, before `ConfigurePlugins` at line 154), add:

```cpp
// PJ64_FACE: unset or empty means "start the camera once the plugin reports that the
// layout binds a gesture"; 0 means never; anything else, or --face, means start at once.
enum class FaceMode { Auto, On, Off };

static FaceMode FaceModeFromEnv(void)
{
    const char * Env = getenv("PJ64_FACE");
    if (Env == nullptr || Env[0] == '\0') return FaceMode::Auto;
    return strcmp(Env, "0") == 0 ? FaceMode::Off : FaceMode::On;
}
```

Replace line 176:

```cpp
    bool FaceFlag = getenv("PJ64_FACE") != nullptr && getenv("PJ64_FACE")[0] != '\0' && strcmp(getenv("PJ64_FACE"), "0") != 0;
```

with:

```cpp
    FaceMode Face = FaceModeFromEnv();
```

In the argv loop (line 184) replace:

```cpp
        if (strcmp(argv[i], "--face") == 0) { FaceFlag = true; continue; }
```

with:

```cpp
        if (strcmp(argv[i], "--face") == 0) { Face = FaceMode::On; continue; }
```

Replace lines 263-267:

```cpp
    PointerState * pointer = CreatePointerState();
    if (FaceFlag && pointer != nullptr && !TileMode)
    {
        FaceTrackerStart(pointer);
    }
```

with:

```cpp
    PointerState * pointer = CreatePointerState();
    bool FaceStarted = false;
    if (Face == FaceMode::On && pointer != nullptr && !TileMode)
    {
        FaceTrackerStart(pointer);
        FaceStarted = true;
    }
```

- [ ] **Step 5: Start the tracker lazily from the main loop**

Replace lines 306-309:

```cpp
        if (pointer != nullptr)
        {
            PublishMouse(pointer, window, injecting ? &inject : nullptr);
        }
```

with:

```cpp
        if (pointer != nullptr)
        {
            PublishMouse(pointer, window, injecting ? &inject : nullptr);
            // The plugin stores FaceWanted at dylib load, on whichever thread loads it.
            // FaceTrackerStop at the bottom is safe whether or not this ever fires.
            if (!FaceStarted && Face == FaceMode::Auto && !TileMode
                && pointer->FaceWanted.load(std::memory_order_acquire) != 0)
            {
                FaceTrackerStart(pointer);
                FaceStarted = true;
            }
        }
```

- [ ] **Step 6: Let `make run` pass `face=0` through**

In `Makefile`, replace the `run` target (lines 450-452) with:

```make
run: all ## [STEP 8] Run a ROM in a window (usage: make run rom=/path/to/game.z64 [input=Config/mouse/sm64.yaml] [face=1|face=0])
	@test -n "$(rom)" || { echo "usage: make run rom=/path/to/game.z64 [input=<yaml>] [face=1|face=0]"; exit 1; }
	$(if $(input),PJ64_INPUT_YAML="$(input)") $(if $(face),PJ64_FACE=$(face)) ./$(BIN)/Project64 "$(rom)"
```

`face=1` still means "start at once"; `face=0` is the new opt-out; omitting it is auto.

- [ ] **Step 7: Build and run the selftest to verify all three runs pass**

Run: `make -j8 all && make test && make pointer-selftest rom=/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: `ok: pointer path maps centre to A and top4 to Start, and finds a layout named after the ROM`, exit 0.

- [ ] **Step 8: Prove the explicit variable wins and tiles skip the lookup**

Build a temp folder with a deliberately broken sibling and check that an explicit file still loads:

```sh
S=/private/tmp/claude-501/-Users-felipe-dos-santos-code-theirs-project64--claude-worktrees-mouse-and-face-input/1991f63e-c078-4add-938c-3cb69e2845a3/scratchpad
mkdir -p "$S/pg" && ln -sf /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 "$S/pg/super_mario_64_usa.z64"
printf 'bindings:\n  A: {key: NoSuchKey}\n' > "$S/pg/super_mario_64_usa.yaml"
PJ64_INPUT_YAML=Config/mouse/sm64.yaml PJ64_FACE=0 PJ64_POINTER_SELFTEST=1 PJ64_POINTER_INJECT=320,240,1 \
  perl -e 'alarm 12; exec @ARGV' -- ./Bin/macOS/Project64 "$S/pg/super_mario_64_usa.z64" 2>&1 | grep -E '^(input layout|pointer-selftest|input)' || true
```

Expected: no `input layout:` line (the explicit variable suppressed the lookup) and `pointer-selftest zone=12 a=1 start=0`. Then the tile path:

```sh
PJ64_FACE=0 PJ64_AUDIO_MUTE=1 perl -e 'alarm 8; exec @ARGV' -- ./Bin/macOS/Project64 --tile "$S/pg/super_mario_64_usa.z64" --tile-rect 0,0,320,240 2>&1 | grep -c '^input layout:' || true
```

Expected: `0` (a tile never prints the line, even with a sibling present). If the broken sibling had been loaded, the plugin would have printed its one-line rejection; that must not appear in either run. Finish with `rm -rf "$S/pg"`.

- [ ] **Step 9: Prove auto-start and the opt-out**

The camera on this machine is already authorised for the terminal, so `FaceTrackerStart` prints `face: tracking started (...)` synchronously. Two bounded runs with the sibling in place:

```sh
mkdir -p "$S/pg" && ln -sf /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 "$S/pg/super_mario_64_usa.z64"
cp Config/mouse/sm64.yaml "$S/pg/super_mario_64_usa.yaml"
perl -e 'alarm 8; exec @ARGV' -- ./Bin/macOS/Project64 "$S/pg/super_mario_64_usa.z64" 2>&1 | grep -E '^(input layout|face):' || true
PJ64_FACE=0 perl -e 'alarm 8; exec @ARGV' -- ./Bin/macOS/Project64 "$S/pg/super_mario_64_usa.z64" 2>&1 | grep -E '^(input layout|face):' || true
rm -rf "$S/pg"
```

Expected, first run: `input layout: .../super_mario_64_usa.yaml` then `face: tracking started (...)`, and `face: tracking stopped` may follow when the alarm ends it. Second run: only the `input layout:` line, no `face:` line at all. (This is the one step that opens the camera; no frames are stored or shown.)

- [ ] **Step 10: Commit**

```bash
git add Source/Project64-sdl/main.cpp Makefile Scripts/pointer_selftest.sh
git commit -m "Load a YAML named after the ROM and start the camera when it binds gestures

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Documentation

**Files:**
- Modify: `README.md:60-67` (Input mapping), `:69-103` (Playing with a mouse), `:20-26` (Build block)
- Modify: `AGENTS.md:19-32` (Commands), `:90-104` (Architecture), `:120-154` (Traps)
- Modify: `Docs/superpowers/specs/2026-09-15-per-game-input-yaml-design.md:4` (Status)

**Interfaces:**
- Consumes: the behaviour shipped by Tasks 1-3. No code.

- [ ] **Step 1: README, Build block**

After the `make face-gesture-test` line in the Build code block add:

```sh
make game-config-test     # lookup tests for the per-game YAML
```

- [ ] **Step 2: README, Input mapping**

Replace the first sentence of the "Input mapping" section:

```
Bindings are read once at startup from `Bin/macOS/Config/input.yaml`
(`PJ64_INPUT_YAML=/path` overrides it); delete the file for the built-in mapping.
```

with:

```
Bindings are read once at startup from `Bin/macOS/Config/input.yaml`; a YAML named after
the ROM takes its place (see "Playing with a mouse"), and `PJ64_INPUT_YAML=/path` overrides
both. Delete the file for the built-in mapping.
```

- [ ] **Step 3: README, Playing with a mouse**

Replace the two `make run` lines in the code block with:

```sh
make run rom=Roms/sm64.z64 input=Config/mouse/sm64.yaml          # camera starts: the layout binds gestures
make run rom=Roms/sm64.z64 input=Config/mouse/sm64.yaml face=0   # mouse only; Z, B and R are unavailable
```

Replace the sentence that opens the webcam paragraph:

```
With `face=1` (or `--face`) the webcam adds three held buttons: raising both eyebrows,
turning your head left, and turning it right.
```

with:

```
When the layout binds a face gesture the webcam starts by itself (`face=1` or `--face`
forces it, `face=0` or `PJ64_FACE=0` keeps it off) and adds three held buttons: raising
both eyebrows, turning your head left, and turning it right.
```

After the paragraph that lists the shipped layouts (ending "...mapped only to face gestures are unavailable.") add:

```
Name a layout after the ROM and it loads without `input=`: `Roms/sm64.yaml` beside
`Roms/sm64.z64`, or `Config/mouse/sm64.yaml` under the binary. The first that exists
wins, the frontend prints `input layout: <path>` when it picks one, and an explicit
`input=` beats both. Grid tiles ignore per-game files.
```

- [ ] **Step 4: AGENTS.md, Commands**

After the `make face-gesture-test` line add:

```sh
make game-config-test                    # lookup tests for the per-game YAML
```

Replace `make run rom=Roms/game.z64 input=Config/mouse/sm64.yaml face=1` with:

```sh
make run rom=Roms/game.z64 input=Config/mouse/sm64.yaml  # camera starts by itself; face=0 stops it
```

In the paragraph after the block, change "`make pointer-layout-test` and `make face-gesture-test` are pure unit tests" to "`make pointer-layout-test`, `make face-gesture-test` and `make game-config-test` are pure unit tests".

- [ ] **Step 5: AGENTS.md, Architecture**

In the "**Input bindings are data.**" paragraph, replace
"`PluginLoaded` loads `Config/input.yaml` once and `GetKeys` only evaluates the resolved table."
with:

```
`PluginLoaded` loads `PJ64_INPUT_YAML` if set, else `Config/input.yaml`, once, and
`GetKeys` only evaluates the resolved table. The frontend sets that variable itself when a
YAML named after the ROM exists (`Source/Project64-sdl/GameConfig.cpp`: beside the ROM,
then `Config/mouse/`), unless it was already set or the process is a grid tile.
```

In the "**Mouse and face input go through one shared struct.**" paragraph, after "and writes labels and the latched zone back for `Overlay.cpp`," insert "plus `FaceWanted`, which the frontend's main loop polls to start the camera when `PJ64_FACE` is unset (`0` never, anything else at once),".

- [ ] **Step 6: AGENTS.md, Traps**

After the "**`Config/input.yaml` must stay keyboard-active.**" entry add:

```
- **A YAML beside a ROM silently changes that game's bindings.** `<rom>.yaml` next to
  `<rom>.z64`, or `Config/mouse/<rom>.yaml`, is loaded instead of `input.yaml`, and the
  camera starts if it binds a gesture. The stderr line `input layout: <path>` is the tell;
  `PJ64_INPUT_YAML` and `PJ64_FACE=0` override it.
```

- [ ] **Step 7: Mark the spec implemented**

In `Docs/superpowers/specs/2026-09-15-per-game-input-yaml-design.md` change line 4 to:

```
Status: implemented
```

- [ ] **Step 8: Check every command the docs name still runs**

Run: `make game-config-test && make input-config-test && make test`
Expected: all green. Then `git diff --stat` shows only the four files above.

- [ ] **Step 9: Commit**

```bash
git add README.md AGENTS.md Docs/superpowers/specs/2026-09-15-per-game-input-yaml-design.md
git commit -m "Document the per-game input YAML and the three-state PJ64_FACE

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```
