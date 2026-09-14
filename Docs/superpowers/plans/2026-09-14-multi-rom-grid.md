# Multi-ROM Grid Frontend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `Project64 --grid a.z64 b.z64 …` opens 1–16 games in a near-square grid of windows, all driven by one keyboard, each tile running the real single-ROM emulator path.

**Architecture:** The core is process-global (`g_BaseSystem`, `g_Plugins`, `g_Settings`) and the plugin dylibs hold singleton state, so one game per process is the only reuse-maximizing design. `--grid` runs an orchestrator in the same binary: it owns a small focusable control-strip window (the only focusable window), computes a 4:3 tile size, spawns one child per ROM in tile mode, publishes the keyboard into a shared snapshot, and kills the children on quit. Each child is today's single-ROM path plus window geometry, a non-focusable window, muted audio, and input read from the snapshot.

**Tech Stack:** C++ (SDL3 3.4.16 / OpenGL frontend and plugins), POSIX (`fork`/`execv`, `shm_open`+`mmap`, signals), GNU Make, POSIX `sh`.

**Spec:** `Docs/superpowers/specs/2026-09-14-multi-rom-grid-design.md`

## Global Constraints

- macOS on Apple Silicon only; the frontend is interpreter-only and windowed.
- `make test` must stay green: one version line plus four `ok:` lines.
- With every new environment variable unset, behavior must be exactly as today: one ROM, a 640×480 window, the audio device opened normally.
- Line endings are mixed. `Source/Project64-video/Main.cpp` is CRLF; `Source/Project64-audio/Driver/SdlAudio.cpp`, `Source/Project64-sdl/*.cpp`, `Makefile` and `README.md` are LF. Run `file <path>` before editing and preserve what is there. New files are LF. `Makefile` is UTF-8 with box-drawing characters: preserve encoding and use real tabs for recipe lines.
- Exact environment variable names: `PJ64_TILE_SIZE`, `PJ64_AUDIO_MUTE`, `PJ64_GRID_KEYS_FD`, `PJ64_GRID_SELFTEST`.
- Exact limits: at most 16 tiles; the strip is 28 px tall; a tile is at least 320×240.
- SDL scancodes used by the self-test: `SDL_SCANCODE_X` = 27, `SDL_SCANCODE_RETURN` = 40.
- Never commit ROM files, frame dumps, or test logs; the ROM pack lives outside the repo.
- Shared non-black rule is unchanged (any R/G/B channel > 16).

---

### Task 1: Per-tile size override in the video plugin

**Files:**
- Modify: `Source/Project64-video/Main.cpp` (`ChangeSize()`, around lines 90–97; CRLF)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `PJ64_TILE_SIZE=WxH`, read by the video plugin in `ChangeSize()` to set `g_width`/`g_height` directly, bypassing the `ScreenRes` list. Unset means unchanged.

- [ ] **Step 1: Confirm the file and read `ChangeSize()`**

Run: `file Source/Project64-video/Main.cpp`
Expected: `... with CRLF line terminators`. Read `ChangeSize()`; the non-Android branch sets `g_width`/`g_height` from `GetScreenResWidth/Height(g_settings->ScreenRes())`.

- [ ] **Step 2: Apply the override, preserving CRLF**

`Main.cpp` is CRLF, so edit it with a byte-preserving script rather than a line-based tool. Run:

```sh
python3 - <<'PY'
p = "Source/Project64-video/Main.cpp"
data = open(p, "rb").read()
crlf = b"\r\n" in data
text = data.decode("utf-8")
old = """    g_width = ev_fullscreen ? GetFullScreenResWidth(g_settings->FullScreenRes()) : GetScreenResWidth(g_settings->ScreenRes());
    g_height = ev_fullscreen ? GetFullScreenResHeight(g_settings->FullScreenRes()) : GetScreenResHeight(g_settings->ScreenRes());"""
new = """    int TileW = 0, TileH = 0;
    const char * TileSize = getenv("PJ64_TILE_SIZE");
    if (TileSize != nullptr && sscanf(TileSize, "%dx%d", &TileW, &TileH) == 2 && TileW > 0 && TileH > 0)
    {
        g_width = TileW;
        g_height = TileH;
    }
    else
    {
        g_width = ev_fullscreen ? GetFullScreenResWidth(g_settings->FullScreenRes()) : GetScreenResWidth(g_settings->ScreenRes());
        g_height = ev_fullscreen ? GetFullScreenResHeight(g_settings->FullScreenRes()) : GetScreenResHeight(g_settings->ScreenRes());
    }"""
assert text.count(old) == 1, "expected one match, got %d" % text.count(old)
text = text.replace(old, new)
out = text.encode("utf-8")
if crlf:
    out = out.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
open(p, "wb").write(out)
print("crlf" if crlf else "lf")
PY
```

`getenv`, `sscanf`, `atoi` are already available: `Gfx_1.3.h` includes `<stdio.h>` and `<stdlib.h>`.

- [ ] **Step 3: Verify the file is still CRLF and builds**

Run: `file Source/Project64-video/Main.cpp && make video`
Expected: still `with CRLF line terminators`; the video dylib builds with no new warnings.

- [ ] **Step 4: Regression — unset means 640×480**

Run:
```sh
rm -f /tmp/g_tile_off.ppm
PJ64_FRAME_DUMP=/tmp/g_tile_off.ppm PJ64_FRAME_DUMP_AT=120 \
  perl -e 'alarm 40; exec @ARGV' -- ./Bin/macOS/Project64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 || true
head -2 /tmp/g_tile_off.ppm
```
Expected: `P6` then `640 480`.

- [ ] **Step 5: Override takes effect**

Run:
```sh
rm -f /tmp/g_tile_on.ppm
PJ64_TILE_SIZE=800x600 PJ64_FRAME_DUMP=/tmp/g_tile_on.ppm PJ64_FRAME_DUMP_AT=120 \
  perl -e 'alarm 40; exec @ARGV' -- ./Bin/macOS/Project64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 || true
head -2 /tmp/g_tile_on.ppm
```
Expected: `P6` then `800 600` — the plugin rendered at the overridden size.

- [ ] **Step 6: Smoke test**

Run: `make test`
Expected: one `Project64 macOS SDL3 frontend` line plus four `ok:` lines.

- [ ] **Step 7: Commit**

```bash
git add Source/Project64-video/Main.cpp
git commit -m "Honor PJ64_TILE_SIZE in the video plugin"
```

---

### Task 2: Per-tile mute in the audio plugin

**Files:**
- Modify: `Source/Project64-audio/Driver/SdlAudio.cpp` (constructor, around lines 10–23; LF)

**Interfaces:**
- Consumes: the existing `m_DeviceUnavailable` flag and the existing silence-drain path (`StartSilenceDrain`, `SdlAudio.cpp:129`).
- Produces: `PJ64_AUDIO_MUTE` — when set, no audio device is opened and the existing silent drain runs, so audio emulation continues without sound.

- [ ] **Step 1: Read the file**

Read `Source/Project64-audio/Driver/SdlAudio.cpp`. Note the constructor calls `SDL_InitSubSystem(SDL_INIT_AUDIO)` and `OpenStream()` starts by checking `m_DeviceUnavailable` and routing to `StartSilenceDrain()`.

- [ ] **Step 2: Add the include and the flag**

Add `#include <stdlib.h>` after `#include <string.h>` (line 7).

In the constructor, after the `SDL_InitSubSystem(SDL_INIT_AUDIO)` block (after line 21, before the closing brace), add:

```cpp
    // A grid tile sets this so no device is opened. OpenStream then takes the existing
    // silence-drain path, keeping audio emulation running without sound.
    if (getenv("PJ64_AUDIO_MUTE") != nullptr)
    {
        m_DeviceUnavailable = true;
    }
```

- [ ] **Step 3: Build**

Run: `make audio`
Expected: the audio dylib builds with no new warnings.

- [ ] **Step 4: Unset opens a device**

Run:
```sh
PJ64_TRACE=AudioInitShutdown \
  perl -e 'alarm 20; exec @ARGV' -- ./Bin/macOS/Project64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>&1 | grep -i "audio" | head
```
Expected: a line containing `Opened SDL audio stream`.

- [ ] **Step 5: Set opens no device and drains**

Run:
```sh
PJ64_AUDIO_MUTE=1 PJ64_TRACE=AudioInitShutdown \
  perl -e 'alarm 20; exec @ARGV' -- ./Bin/macOS/Project64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>&1 | grep -i "audio" | head
```
Expected: `No audio device; draining silently so emulation continues`, and no `Opened SDL audio stream` line.

- [ ] **Step 6: Smoke test**

Run: `make test`
Expected: version line plus four `ok:` lines.

- [ ] **Step 7: Commit**

```bash
git add Source/Project64-audio/Driver/SdlAudio.cpp
git commit -m "Honor PJ64_AUDIO_MUTE in the audio plugin"
```

---

### Task 3: Key snapshot contract and the input plugin's alternate source

**Files:**
- Create: `Source/Common/GridKeys.h` (LF)
- Modify: `Source/Project64-sdl/PluginInput.cpp` (LF)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `#define PJ64_GRID_KEYS_ENV "PJ64_GRID_KEYS_FD"`
  - `struct GridKeys { volatile uint32_t Seq; bool Keys[SDL_SCANCODE_COUNT]; };`
  - `void GridKeysPublish(GridKeys * Keys, const bool * State)`
  - `void GridKeysSnapshot(const GridKeys * Keys, bool * Out)`
  - The input plugin reads the snapshot when `PJ64_GRID_KEYS_FD` is set, and with `PJ64_GRID_SELFTEST` set it prints once to stderr: `grid-selftest pid=<pid> a=<0|1> start=<0|1>`.

- [ ] **Step 1: Create the header**

Create `Source/Common/GridKeys.h` with exactly:

```cpp
// Project64 - A Nintendo 64 emulator
// Shared by the grid orchestrator (only writer) and the input plugin (readers).
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <SDL3/SDL.h>
#include <stdint.h>
#include <string.h>

// Environment variable holding the descriptor number of a mapped GridKeys. The
// orchestrator sets it on each tile; it is absent for a normal single-ROM run.
#define PJ64_GRID_KEYS_ENV "PJ64_GRID_KEYS_FD"

// Seq is a seqlock: odd while a write is in flight, even when Keys is stable.
struct GridKeys
{
    volatile uint32_t Seq;
    bool Keys[SDL_SCANCODE_COUNT];
};

inline void GridKeysPublish(GridKeys * Keys, const bool * State)
{
    Keys->Seq = Keys->Seq + 1; // odd: a write is in flight
    memcpy(Keys->Keys, State, sizeof Keys->Keys);
    Keys->Seq = Keys->Seq + 1; // even: stable
}

inline void GridKeysSnapshot(const GridKeys * Keys, bool * Out)
{
    for (;;)
    {
        uint32_t Before = Keys->Seq;
        if ((Before & 1u) != 0)
        {
            continue; // a writer is mid-publish
        }
        memcpy(Out, Keys->Keys, sizeof Keys->Keys);
        if (Keys->Seq == Before)
        {
            return;
        }
    }
}
```

- [ ] **Step 2: Include it and add the mapping helpers to the input plugin**

In `Source/Project64-sdl/PluginInput.cpp`, replace the includes block (lines 9–11) with:

```cpp
#include <Project64-plugin-spec/Input.h>
#include <Common/GridKeys.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
```

After `static SDL_Gamepad * g_Gamepad = nullptr;` (line 20), add:

```cpp
static GridKeys * g_GridKeys = nullptr;
static bool g_GridKeysChecked = false;

// The orchestrator maps a GridKeys and passes its descriptor in PJ64_GRID_KEYS_ENV.
// Reading it here means every tile sees the one keyboard owned by the control strip.
// Without the variable this is a normal single-ROM run and SDL's own state is used.
static void OpenGridKeys(void)
{
    if (g_GridKeysChecked)
    {
        return;
    }
    g_GridKeysChecked = true;
    const char * FdEnv = getenv(PJ64_GRID_KEYS_ENV);
    if (FdEnv == nullptr)
    {
        return;
    }
    void * Mapped = mmap(nullptr, sizeof(GridKeys), PROT_READ, MAP_SHARED, atoi(FdEnv), 0);
    if (Mapped == MAP_FAILED)
    {
        fprintf(stderr, "input: could not map %s=%s\n", PJ64_GRID_KEYS_ENV, FdEnv);
        return;
    }
    g_GridKeys = (GridKeys *)Mapped;
}

// Verification only. With PJ64_GRID_SELFTEST set, report once which of two known keys
// the snapshot carried, so Scripts/grid_selftest.sh can prove the broadcast.
static void SelftestReport(const bool * Keys)
{
    static bool Reported = false;
    if (Reported || getenv("PJ64_GRID_SELFTEST") == nullptr)
    {
        return;
    }
    Reported = true;
    fprintf(stderr, "grid-selftest pid=%d a=%d start=%d\n",
        (int)getpid(), Keys[SDL_SCANCODE_X] ? 1 : 0, Keys[SDL_SCANCODE_RETURN] ? 1 : 0);
}
```

- [ ] **Step 3: Read from the snapshot in `GetKeys`**

In `GetKeys`, replace:

```cpp
    const bool * k = SDL_GetKeyboardState(nullptr);
    if (k != nullptr)
```

with:

```cpp
    bool Snapshot[SDL_SCANCODE_COUNT];
    const bool * k = SDL_GetKeyboardState(nullptr);
    OpenGridKeys();
    if (g_GridKeys != nullptr)
    {
        GridKeysSnapshot(g_GridKeys, Snapshot);
        SelftestReport(Snapshot);
        k = Snapshot;
    }
    if (k != nullptr)
```

The scancode-to-N64 mapping below and the gamepad block are unchanged.

- [ ] **Step 4: Build and keep the exports**

Run: `make input && make test`
Expected: the input dylib builds; `make test` shows four `ok:` lines including `Project64-input-sdl.dylib`.

- [ ] **Step 5: Prove the reader end to end with a prepared snapshot**

This writes the snapshot from Python and launches a single ROM with its descriptor, which exercises the exact path the orchestrator will use.

Run:
```sh
python3 - <<'PY'
import mmap, os, struct, subprocess, sys, tempfile, time
SCAN_COUNT = 512
KEY_X, KEY_RETURN = 27, 40
f = tempfile.TemporaryFile()
f.truncate(4 + SCAN_COUNT)
m = mmap.mmap(f.fileno(), 4 + SCAN_COUNT)
m[4 + KEY_X] = 1
m[4 + KEY_RETURN] = 1
m[0:4] = struct.pack("<I", 2)   # even seq: stable
m.flush()
log = open("/tmp/g_keys.log", "wb")
env = dict(os.environ, PJ64_GRID_KEYS_FD=str(f.fileno()), PJ64_GRID_SELFTEST="1")
p = subprocess.Popen(
    ["./Bin/macOS/Project64",
     "/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64"],
    env=env, pass_fds=(f.fileno(),), stdout=subprocess.DEVNULL, stderr=log)
time.sleep(15)
p.terminate()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()
log.close()
print(open("/tmp/g_keys.log").read().strip())
PY
grep -c '^grid-selftest pid=[0-9]* a=1 start=1$' /tmp/g_keys.log
```
Expected: the printed log contains `grid-selftest pid=... a=1 start=1`, and the final `grep -c` prints `1`. The keyboard is untouched, so `a=1 start=1` can only come from the snapshot.

- [ ] **Step 6: Control — the snapshot path is required for the marker**

Run:
```sh
python3 - <<'PY'
import os, subprocess, time
log = open("/tmp/g_keys_control.log", "wb")
env = dict(os.environ, PJ64_GRID_SELFTEST="1")   # no PJ64_GRID_KEYS_FD
p = subprocess.Popen(
    ["./Bin/macOS/Project64",
     "/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64"],
    env=env, stdout=subprocess.DEVNULL, stderr=log)
time.sleep(10)
p.terminate()
try:
    p.wait(timeout=10)
except subprocess.TimeoutExpired:
    p.kill()
log.close()
PY
grep -c 'grid-selftest' /tmp/g_keys_control.log || true
```
Expected: `0` — no snapshot source, so no marker.

- [ ] **Step 7: Commit**

```bash
git add Source/Common/GridKeys.h Source/Project64-sdl/PluginInput.cpp
git commit -m "Read broadcast keys from a shared snapshot"
```

---

### Task 4: Tile mode in the frontend

**Files:**
- Modify: `Source/Project64-sdl/main.cpp` (LF)

**Interfaces:**
- Consumes: `PJ64_TILE_SIZE` semantics from Task 1.
- Produces: `Project64 --tile <rom> --tile-rect X,Y,W,H` — a non-focusable, non-resizable window at the given rectangle that renders at that same size, plus a watcher thread that exits the process when reparented. Plain `Project64 <rom>` is unchanged.

- [ ] **Step 1: Add includes and the watcher**

In `Source/Project64-sdl/main.cpp`, after `#include <mach-o/dyld.h>` (line 14), add:

```cpp
#include <sys/types.h>
#include <unistd.h>
```

After `ShutdownSdl` (before `ConfigurePlugins`), add:

```cpp
// A tile is a child of the orchestrator. macOS has no PDEATHSIG, so if the orchestrator is
// killed hard the tile is reparented; exit then rather than lingering as a stray emulator.
static int SDLCALL ParentWatchThread(void * /*data*/)
{
    pid_t Parent = getppid();
    for (;;)
    {
        SDL_Delay(1000);
        if (getppid() != Parent)
        {
            _exit(0);
        }
    }
    return 0;
}
```

- [ ] **Step 2: Parse tile arguments**

Replace the existing argument block (from `if (argc < 2)` through the corresponding closing brace, lines 74–78) with:

```cpp
    bool TileMode = false;
    SDL_Rect TileRect = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    const char * RomPath = nullptr;
    if (argc >= 2 && strcmp(argv[1], "--tile") == 0)
    {
        if (argc < 5 || strcmp(argv[3], "--tile-rect") != 0)
        {
            fprintf(stderr, "usage: %s --tile <rom> --tile-rect X,Y,W,H\n", argv[0]);
            return 2;
        }
        RomPath = argv[2];
        if (sscanf(argv[4], "%d,%d,%d,%d", &TileRect.x, &TileRect.y, &TileRect.w, &TileRect.h) != 4
            || TileRect.w <= 0 || TileRect.h <= 0)
        {
            fprintf(stderr, "bad --tile-rect: %s\n", argv[4]);
            return 2;
        }
        TileMode = true;
    }
    else if (argc >= 2)
    {
        RomPath = argv[1];
    }
    if (RomPath == nullptr)
    {
        fprintf(stderr, "usage: %s <rom file>\n", argv[0]);
        return 2;
    }
```

`sscanf` needs `<stdio.h>`, already included.

- [ ] **Step 3: Create the window with tile flags and size**

Replace the window creation (lines 98–103) with:

```cpp
    Uint64 WindowFlags = SDL_WINDOW_OPENGL;
    if (TileMode)
    {
        WindowFlags |= SDL_WINDOW_NOT_FOCUSABLE;
    }
    SDL_Window * window = SDL_CreateWindow("Project64", TileRect.w, TileRect.h, WindowFlags);
    if (window == nullptr)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return ShutdownSdl(nullptr, nullptr, 1);
    }
    if (TileMode)
    {
        SDL_SetWindowPosition(window, TileRect.x, TileRect.y);
        // The video plugin renders at PJ64_TILE_SIZE; keep it equal to the window so the
        // game cannot land unscaled in a corner.
        char Size[32];
        snprintf(Size, sizeof(Size), "%dx%d", TileRect.w, TileRect.h);
        setenv("PJ64_TILE_SIZE", Size, 1);
    }
```

- [ ] **Step 4: Use `RomPath` and start the watcher**

Replace `CN64System::RunFileImage(argv[1])` and its error line with:

```cpp
    if (!CN64System::RunFileImage(RomPath))
    {
        fprintf(stderr, "Failed to load ROM: %s\n", RomPath);
```

And immediately after that block (before `bool running = true;`), add:

```cpp
    if (TileMode)
    {
        SDL_CreateThread(ParentWatchThread, "pj64-parent-watch", nullptr);
    }
```

- [ ] **Step 5: Build and smoke test**

Run: `make frontend && make test`
Expected: builds; version line plus four `ok:` lines.

- [ ] **Step 6: Tile geometry drives both window and render size**

Run:
```sh
rm -f /tmp/g_tile.ppm
PJ64_FRAME_DUMP=/tmp/g_tile.ppm PJ64_FRAME_DUMP_AT=120 \
  perl -e 'alarm 40; exec @ARGV' -- ./Bin/macOS/Project64 \
  --tile /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 \
  --tile-rect 100,100,800,600 || true
head -2 /tmp/g_tile.ppm
```
Expected: `P6` then `800 600`.

- [ ] **Step 7: Single-ROM path untouched**

Run:
```sh
rm -f /tmp/g_single.ppm
PJ64_FRAME_DUMP=/tmp/g_single.ppm PJ64_FRAME_DUMP_AT=120 \
  perl -e 'alarm 40; exec @ARGV' -- ./Bin/macOS/Project64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 || true
head -2 /tmp/g_single.ppm
```
Expected: `P6` then `640 480`.

- [ ] **Step 8: The watcher exits an orphaned tile**

Run:
```sh
python3 - <<'PY'
import subprocess, time
p = subprocess.Popen(
    ["./Bin/macOS/Project64", "--tile",
     "/Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64",
     "--tile-rect", "100,100,640,480"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("child started pid", p.pid)
time.sleep(4)   # parent exits here; the child is reparented
PY
sleep 3
pgrep -f 'Bin/macOS/Project64 --tile' && echo "STILL RUNNING (bad)" || echo "tile is gone (good)"
```
Expected: `tile is gone (good)` — the watcher exited the reparented tile.

- [ ] **Step 9: Commit**

```bash
git add Source/Project64-sdl/main.cpp
git commit -m "Add tile mode to the frontend"
```

---

### Task 5: Grid orchestrator

**Files:**
- Create: `Source/Project64-sdl/GridHost.h` (LF)
- Create: `Source/Project64-sdl/GridHost.cpp` (LF)
- Modify: `Source/Project64-sdl/main.cpp` (LF)
- Modify: `Makefile` (line 291, `FRONTEND_SRC`)

**Interfaces:**
- Consumes: `--tile` (Task 4), `PJ64_TILE_SIZE` (Task 1), `PJ64_AUDIO_MUTE` (Task 2).
- Produces: `int GridHostRun(int argc, char ** argv)` and the `--grid` command. This task lays out and supervises the grid; key broadcast arrives in Task 6.

- [ ] **Step 1: Create the header**

Create `Source/Project64-sdl/GridHost.h`:

```cpp
#pragma once

// Runs `Project64 --grid rom1 .. rom16`: lays the ROMs out in a near-square grid of
// windows with a focusable control strip, and cleans every tile up on quit.
int GridHostRun(int argc, char ** argv);
```

- [ ] **Step 2: Create the orchestrator**

Create `Source/Project64-sdl/GridHost.cpp` with exactly:

```cpp
// Project64 - A Nintendo 64 emulator
// Grid orchestrator: one child process per ROM, laid out near-square, with a control
// strip that owns the keyboard. See Docs/superpowers/specs/2026-09-14-multi-rom-grid-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "GridHost.h"
#include <SDL3/SDL.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>

static const int MAX_TILES = 16;
static const int STRIP_HEIGHT = 28;
static const int MIN_TILE_W = 320;
static const int MIN_TILE_H = 240;

static volatile sig_atomic_t g_StopRequested = 0;

static void HandleStopSignal(int /*sig*/)
{
    g_StopRequested = 1;
}

static std::string ExecutablePath(void)
{
    char Buf[4096];
    uint32_t Size = sizeof(Buf);
    if (_NSGetExecutablePath(Buf, &Size) != 0)
    {
        return "";
    }
    char Resolved[4096];
    if (realpath(Buf, Resolved) == nullptr)
    {
        return "";
    }
    return std::string(Resolved);
}

int GridHostRun(int argc, char ** argv)
{
    const int RomCount = argc - 2;
    char ** Roms = argv + 2;
    if (RomCount < 1 || RomCount > MAX_TILES)
    {
        fprintf(stderr, "usage: %s --grid rom1 .. rom%d (got %d)\n", argv[0], MAX_TILES, RomCount);
        return 2;
    }
    for (int i = 0; i < RomCount; i++)
    {
        if (access(Roms[i], R_OK) != 0)
        {
            fprintf(stderr, "cannot read %s\n", Roms[i]);
            return 2;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Rect Area;
    if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &Area))
    {
        fprintf(stderr, "SDL_GetDisplayUsableBounds failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Near-square grid. Integer sqrt so no floating point is involved.
    int Cols = 1;
    while (Cols * Cols < RomCount)
    {
        Cols += 1;
    }
    const int Rows = (RomCount + Cols - 1) / Cols;
    const int CellW = Area.w / Cols;
    const int CellH = (Area.h - STRIP_HEIGHT) / Rows;
    int TileH = CellH < CellW * 3 / 4 ? CellH : CellW * 3 / 4;
    if (TileH < MIN_TILE_H)
    {
        TileH = MIN_TILE_H;
    }
    int TileW = TileH * 4 / 3;
    if (TileW < MIN_TILE_W)
    {
        TileW = MIN_TILE_W;
    }
    const int GridW = Cols * TileW;
    const int GridH = Rows * TileH;
    const int OriginX = Area.x + (Area.w - GridW) / 2;
    const int OriginY = Area.y + (Area.h - STRIP_HEIGHT - GridH) / 2;

    SDL_Window * Strip = SDL_CreateWindow("Project64 grid - Esc to quit", Area.w, STRIP_HEIGHT,
        SDL_WINDOW_ALWAYS_ON_TOP);
    if (Strip == nullptr)
    {
        fprintf(stderr, "SDL_CreateWindow (strip) failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowPosition(Strip, Area.x, Area.y + Area.h - STRIP_HEIGHT);
    SDL_RaiseWindow(Strip);

    const std::string Exe = ExecutablePath();
    if (Exe.empty())
    {
        fprintf(stderr, "could not resolve the executable path\n");
        SDL_DestroyWindow(Strip);
        SDL_Quit();
        return 1;
    }

    std::vector<pid_t> Children;
    for (int i = 0; i < RomCount; i++)
    {
        const int Row = i / Cols;
        const int Col = i % Cols;
        const int X = OriginX + Col * TileW + (CellW - TileW) / 2;
        const int Y = OriginY + Row * TileH + (CellH - TileH) / 2;
        const pid_t Pid = fork();
        if (Pid == 0)
        {
            char Rect[64];
            snprintf(Rect, sizeof(Rect), "%d,%d,%d,%d", X, Y, TileW, TileH);
            setenv("PJ64_AUDIO_MUTE", "1", 1);
            char * ChildArgv[] = {
                const_cast<char *>(Exe.c_str()),
                const_cast<char *>("--tile"),
                Roms[i],
                const_cast<char *>("--tile-rect"),
                Rect,
                nullptr,
            };
            execv(Exe.c_str(), ChildArgv);
            _exit(127);
        }
        if (Pid < 0)
        {
            fprintf(stderr, "fork failed for %s\n", Roms[i]);
            continue;
        }
        Children.push_back(Pid);
    }
    if (Children.empty())
    {
        fprintf(stderr, "no tiles started\n");
        SDL_DestroyWindow(Strip);
        SDL_Quit();
        return 1;
    }

    signal(SIGINT, HandleStopSignal);
    signal(SIGTERM, HandleStopSignal);

    while (!g_StopRequested)
    {
        SDL_Event Ev;
        while (SDL_PollEvent(&Ev))
        {
            if (Ev.type == SDL_EVENT_QUIT || Ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                g_StopRequested = 1;
            }
            if (Ev.type == SDL_EVENT_KEY_DOWN && Ev.key.key == SDLK_ESCAPE)
            {
                g_StopRequested = 1;
            }
        }
        int Status = 0;
        pid_t Done = 0;
        while ((Done = waitpid(-1, &Status, WNOHANG)) > 0)
        {
            fprintf(stderr, "tile pid %d exited (status %d)\n", (int)Done, Status);
        }
        SDL_Delay(10);
    }

    for (size_t i = 0; i < Children.size(); i++)
    {
        kill(Children[i], SIGTERM);
    }
    for (int Pass = 0; Pass < 10; Pass++)
    {
        bool AnyAlive = false;
        for (size_t i = 0; i < Children.size(); i++)
        {
            if (waitpid(Children[i], nullptr, WNOHANG) == 0)
            {
                AnyAlive = true;
            }
        }
        if (!AnyAlive)
        {
            break;
        }
        SDL_Delay(100);
    }
    for (size_t i = 0; i < Children.size(); i++)
    {
        kill(Children[i], SIGKILL);
    }
    for (size_t i = 0; i < Children.size(); i++)
    {
        waitpid(Children[i], nullptr, 0);
    }

    SDL_DestroyWindow(Strip);
    SDL_Quit();
    return 0;
}
```

- [ ] **Step 3: Wire `--grid` into `main`**

In `Source/Project64-sdl/main.cpp`, add after `#include "SdlRenderWindow.h"` (line 5):

```cpp
#include "GridHost.h"
```

And after the `--version` block (line 73), add:

```cpp
    if (argc >= 2 && strcmp(argv[1], "--grid") == 0)
    {
        return GridHostRun(argc, argv);
    }
```

- [ ] **Step 4: Add the source to the build**

In `Makefile` line 291, change:

```make
FRONTEND_SRC = $(addprefix Project64-sdl/, main.cpp SdlNotification.cpp SdlRenderWindow.cpp)
```

to:

```make
FRONTEND_SRC = $(addprefix Project64-sdl/, main.cpp SdlNotification.cpp SdlRenderWindow.cpp GridHost.cpp)
```

Use a real tab only for recipe lines; this is a variable assignment, so spaces are correct.

- [ ] **Step 5: Build and smoke test**

Run: `make frontend && make test`
Expected: builds; version line plus four `ok:` lines.

- [ ] **Step 6: Single-tile grid renders a 4:3 tile**

Run:
```sh
rm -f /tmp/g_grid1.ppm
PJ64_FRAME_DUMP=/tmp/g_grid1.ppm PJ64_FRAME_DUMP_AT=120 \
  perl -e 'alarm 25; exec @ARGV' -- ./Bin/macOS/Project64 --grid \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 || true
python3 - <<'PY'
d = open('/tmp/g_grid1.ppm','rb').read().split(b'\n',3)
w,h = map(int, d[1].split())
print(w, h, round(w/h, 3))
assert w >= 320 and h >= 240, "below the tile floor"
assert abs(w/h - 4/3) < 0.02, "tile is not 4:3"
print("ok: 4:3 tile")
PY
```
Expected: a size with ratio ~1.333 and `ok: 4:3 tile`. (The `alarm` kills the grid after 25 s; that is the force-close path.)

- [ ] **Step 7: Four tiles start, and SIGTERM leaves none behind**

Run:
```sh
./Bin/macOS/Project64 --grid \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 \
  >/dev/null 2>&1 &
GRID=$!
sleep 8
echo "tiles: $(pgrep -f 'Bin/macOS/Project64 --tile' | wc -l | tr -d ' ')"
kill -TERM "$GRID"
wait "$GRID" 2>/dev/null || true
sleep 2
echo "after quit: $(pgrep -f 'Bin/macOS/Project64 --tile' | wc -l | tr -d ' ')"
```
Expected: `tiles: 4` then `after quit: 0`. Visually, four windows fill a 2×2 grid with a strip along the bottom.

- [ ] **Step 8: Bad input is rejected**

Run:
```sh
./Bin/macOS/Project64 --grid; echo "exit=$?"
./Bin/macOS/Project64 --grid /no/such/rom.z64; echo "exit=$?"
```
Expected: a usage line then `exit=2`; a cannot-read line then `exit=2`.

- [ ] **Step 9: Commit**

```bash
git add Source/Project64-sdl/GridHost.h Source/Project64-sdl/GridHost.cpp Source/Project64-sdl/main.cpp Makefile
git commit -m "Add the grid orchestrator and --grid"
```

---

### Task 6: Key broadcast, self-test, and README

**Files:**
- Modify: `Source/Project64-sdl/GridHost.cpp` (LF)
- Create: `Scripts/grid_selftest.sh` (LF)
- Modify: `README.md` (LF)

**Interfaces:**
- Consumes: `GridKeys`/`GridKeysPublish` and `PJ64_GRID_KEYS_ENV` (Task 3); the tile path (Task 4); the orchestrator (Task 5).
- Produces: every tile receives the control strip's keystrokes; `PJ64_GRID_SELFTEST=1` makes the strip publish a fixed pattern instead; `Scripts/grid_selftest.sh <rom>` proves it.

- [ ] **Step 1: Create and publish the snapshot in the orchestrator**

In `Source/Project64-sdl/GridHost.cpp`, extend the includes (after `#include "GridHost.h"`):

```cpp
#include <Common/GridKeys.h>
#include <fcntl.h>
#include <sys/mman.h>
```

Immediately after the `Exe.empty()` check, add:

```cpp
    // One mapped snapshot, one writer (this process), many readers (the tiles). The name is
    // unlinked at once: children inherit the descriptor, never the name, so a crash here
    // leaves nothing behind.
    char ShmName[64];
    snprintf(ShmName, sizeof(ShmName), "/pj64grid-%d", (int)getpid());
    const int KeyFd = shm_open(ShmName, O_CREAT | O_RDWR, 0600);
    GridKeys * Keys = nullptr;
    if (KeyFd < 0 || ftruncate(KeyFd, (off_t)sizeof(GridKeys)) != 0)
    {
        fprintf(stderr, "could not create the key snapshot\n");
        SDL_DestroyWindow(Strip);
        SDL_Quit();
        return 1;
    }
    void * MappedKeys = mmap(nullptr, sizeof(GridKeys), PROT_READ | PROT_WRITE, MAP_SHARED, KeyFd, 0);
    if (MappedKeys == MAP_FAILED)
    {
        fprintf(stderr, "could not map the key snapshot\n");
        close(KeyFd);
        SDL_DestroyWindow(Strip);
        SDL_Quit();
        return 1;
    }
    Keys = (GridKeys *)MappedKeys;
    memset(Keys, 0, sizeof(GridKeys));
    shm_unlink(ShmName);
    fcntl(KeyFd, F_SETFD, 0); // shm_open sets FD_CLOEXEC; the children need it open
```

In the child branch, directly before building `ChildArgv`, add:

```cpp
            char FdText[16];
            snprintf(FdText, sizeof(FdText), "%d", KeyFd);
            setenv(PJ64_GRID_KEYS_ENV, FdText, 1);
```

- [ ] **Step 2: Publish each frame, with the self-test pattern**

In `GridHost.cpp`, just before the spawn loop (after the `Keys` setup above), add:

```cpp
    const bool Selftest = getenv("PJ64_GRID_SELFTEST") != nullptr;
    bool Pattern[SDL_SCANCODE_COUNT];
    memset(Pattern, 0, sizeof(Pattern));
    Pattern[SDL_SCANCODE_X] = true;
    Pattern[SDL_SCANCODE_RETURN] = true;
    // In self-test mode, publish before any tile starts so a tile can never read the
    // zero-filled mapping first and report a false negative. A normal run starts with
    // nothing pressed, and the mapping is already zero.
    if (Selftest)
    {
        GridKeysPublish(Keys, Pattern);
    }
```

Replace the body of the main loop's `SDL_Delay(10);` line so the iteration ends with publishing, i.e. change:

```cpp
        SDL_Delay(10);
    }
```

to:

```cpp
        const bool * State = SDL_GetKeyboardState(nullptr);
        if (Selftest)
        {
            GridKeysPublish(Keys, Pattern);
        }
        else if (State != nullptr)
        {
            GridKeysPublish(Keys, State);
        }
        SDL_Delay(10);
    }
```

And before the final `SDL_DestroyWindow(Strip);`, add:

```cpp
    munmap(Keys, sizeof(GridKeys));
    close(KeyFd);
```

- [ ] **Step 3: Build**

Run: `make frontend`
Expected: builds with no new warnings.

- [ ] **Step 4: Create the self-test script**

Create `Scripts/grid_selftest.sh` with exactly:

```sh
#!/bin/sh
# Prove key broadcast: run one ROM in four tiles, have the strip publish a fixed key
# pattern (PJ64_GRID_SELFTEST), and check that every tile's input plugin read it.
# Design: Docs/superpowers/specs/2026-09-14-multi-rom-grid-design.md
set -eu

ROM="${1:?usage: grid_selftest.sh <rom>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64"
LOG="$(mktemp)"
TILES=4
TIMEOUT=40

[ -x "$BIN" ] || { echo "frontend not built: $BIN" >&2; exit 1; }

PJ64_GRID_SELFTEST=1 "$BIN" --grid "$ROM" "$ROM" "$ROM" "$ROM" >"$LOG" 2>&1 &
GRID=$!

I=0
while [ "$I" -lt "$TIMEOUT" ]; do
    COUNT=$(grep -c '^grid-selftest ' "$LOG" 2>/dev/null || true)
    [ "$COUNT" -ge "$TILES" ] && break
    sleep 1
    I=$((I + 1))
done

kill -TERM "$GRID" 2>/dev/null || true
wait "$GRID" 2>/dev/null || true

FAIL=0
REPORTS=$(grep '^grid-selftest ' "$LOG" 2>/dev/null || true)
COUNT=$(printf '%s\n' "$REPORTS" | grep -c '^grid-selftest ' || true)
PIDS=$(printf '%s\n' "$REPORTS" | sed -n 's/^grid-selftest pid=\([0-9]*\).*/\1/p' | sort -u | wc -l | tr -d ' ')

if [ "$COUNT" -lt "$TILES" ]; then
    echo "FAIL: expected $TILES tile reports, got $COUNT" >&2
    FAIL=1
fi
if [ "$PIDS" -lt "$TILES" ]; then
    echo "FAIL: expected $TILES distinct tile pids, got $PIDS" >&2
    FAIL=1
fi
if printf '%s\n' "$REPORTS" | grep '^grid-selftest ' | grep -v 'a=1 start=1' | grep -q .; then
    echo "FAIL: a tile did not receive the key pattern" >&2
    FAIL=1
fi

sleep 2
if pgrep -f "$BIN --tile" >/dev/null 2>&1; then
    echo "FAIL: tiles survived the orchestrator" >&2
    FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
    echo "ok: $COUNT tiles received the key pattern"
else
    echo "log: $LOG" >&2
fi
rm -f "$LOG"
exit "$FAIL"
```

Then: `chmod +x Scripts/grid_selftest.sh`

- [ ] **Step 5: Run the self-test**

Run: `Scripts/grid_selftest.sh /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64`
Expected: `ok: 4 tiles received the key pattern` (exit 0).

- [ ] **Step 6: Document `--grid`**

In `README.md`, after the "Any path works" paragraph (line 44) and before `## What works`, add:

```markdown
## Grid mode

`./Bin/macOS/Project64 --grid a.z64 b.z64 c.z64 …` opens 1–16 games side by side in a
near-square grid and sends every keystroke to all of them. A small always-on-top strip
along the bottom owns the keyboard (the tiles never take focus); Esc or closing the strip
quits everything. Tiles render at the largest 4:3 size that fits their cell and are muted.
Each game is a separate process running the normal single-ROM path, so one game cannot take
down the others.
```

- [ ] **Step 7: Full verification**

Run:
```sh
make test
Scripts/grid_selftest.sh /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64
rm -f /tmp/g_reg.ppm
PJ64_FRAME_DUMP=/tmp/g_reg.ppm PJ64_FRAME_DUMP_AT=120 \
  perl -e 'alarm 40; exec @ARGV' -- ./Bin/macOS/Project64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 || true
head -2 /tmp/g_reg.ppm
git status --short
```
Expected: `make test` green; the self-test prints `ok`; the single-ROM dump is `640 480`; `git status` shows only the files this task changed.

- [ ] **Step 8: Commit**

```bash
git add Source/Project64-sdl/GridHost.cpp Scripts/grid_selftest.sh README.md
git commit -m "Broadcast the keyboard to every tile"
```
