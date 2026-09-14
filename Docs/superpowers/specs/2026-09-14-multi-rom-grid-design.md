# Multi-ROM grid frontend — design

Date: 2026-09-14
Status: approved (design), implementation not started

## Goal

Let one invocation of the frontend open several games side by side in a grid, driven by a
single keyboard: `Project64 --grid a.z64 b.z64 c.z64 …`. It is a demo/showcase tool — a
person presents a wall of games and drives all of them at once — and because every tile is
the real emulator, running it is also a test of the emulator.

## Context

Three facts force the shape of this feature.

- **The core is single-instance by construction.** `g_BaseSystem`, `g_Plugins` and
  `g_Settings` are process globals (`Source/Project64-core/N64System/SystemGlobals.cpp:6`,
  `SystemGlobals.h:4-21`), `g_Plugins->SetRenderWindows` installs one render window for
  the process (`main.cpp:126`), and the zilmar plugin dylibs export free functions over
  their own globals. Several games in one process would require rewriting the core and
  the plugin ABI, so each game runs in its own process and the feature is an orchestrator
  over today's single-ROM path. This is also what keeps every tile on the real emulator
  code, which is what makes the tool useful as a test.
- **A tile's window size must equal the video plugin's render resolution.** The plugin
  draws the game scaled into a viewport of `g_width`×`g_height`, taken from its
  `resolution` setting (`Source/Project64-video/Main.cpp:95`,
  `Renderer/OGLglitchmain.cpp:715,1026`). They are plain ints and the FBOs/textures
  allocate at whatever they hold, so an arbitrary size is already supported — the list in
  `ScreenResolution.cpp:46-72` is only a UI convenience. If the window and the viewport
  disagree the image lands unscaled in the window's bottom-left.
- **A silent audio path already exists.** When no device opens, `SdlAudioDriver` drains
  the ring silently and emulation continues (`Driver/SdlAudio.cpp:45,129-143`). Muting a
  tile is forcing that existing path, not adding a feature; audio still runs, so the tile
  still exercises the emulator.

Also relevant: SDL3 3.4.16 from Homebrew (`pkg-config sdl3`) provides
`SDL_WINDOW_NOT_FOCUSABLE`, `SDL_SetWindowFocusable`, `SDL_GetDisplayUsableBounds` and
`SDL_SCANCODE_COUNT` (512). `Source/` is an include root (`Makefile:23`), so
`Source/Common/` is reachable from both the frontend and the input plugin.

## Non-goals

- No per-tile settings, no per-tile audio solo or unmute, no individual (non-broadcast)
  input. Every tile always receives every key.
- No re-layout when the display changes, no saved layouts.
- No watchdog for a hung tile; no global event tap (which would need macOS Accessibility
  permission); no window-size scaling to decouple render size from tile size.
- No core refactor for in-process instances and no plugin-ABI change.
- More than 16 tiles.

## Part 1 — Architecture

Two roles, one binary.

**Orchestrator.** Runs when `--grid` is present. Owns argument validation, the display
work area, layout and tile-size computation, the **control strip** (the only focusable
window), the shared key snapshot, child spawning and supervision, and quitting.

**Tile.** A child process running today's single-ROM path plus: window geometry and
non-focusable/non-resizable flags from arguments, muted audio, and input read from the
shared snapshot instead of SDL's own keyboard state (which is always empty for a
non-focusable window).

The orchestrator never touches emulation or GL; children never talk to each other; the key
snapshot is the only shared state — one writer, many readers. Isolation is a property of
the process boundary: a crashing or hung game cannot take down the grid.

**Files.**

| File | Change |
| --- | --- |
| `Source/Project64-sdl/GridHost.{h,cpp}` | new — the orchestrator |
| `Source/Common/GridKeys.h` | new — snapshot layout + env names, shared by host and input plugin |
| `Source/Project64-sdl/main.cpp` | branch on `--grid`; add internal tile mode |
| `Source/Project64-sdl/PluginInput.cpp` | read keys from the snapshot when present |
| `Source/Project64-video/Main.cpp` | honor `PJ64_TILE_SIZE` in `ChangeSize()` |
| `Source/Project64-audio/Driver/SdlAudio.cpp` | honor `PJ64_AUDIO_MUTE` via the existing silent path |
| `Makefile` | add `GridHost.cpp` to `FRONTEND_SRC` (`Makefile:291`) |
| `Scripts/grid_selftest.sh` | new — end-to-end key-propagation check |
| `README.md` | document `--grid` |

## Part 2 — Interfaces

### Command line

```
Project64 --grid rom1 rom2 … rom16      # 1..16 paths; anything else is a usage error
Project64 rom1                          # unchanged single-ROM path
Project64 --tile <rom> --tile-rect X,Y,W,H   # internal, used by the orchestrator
```

Tile mode is not for humans; the remaining tile parameters travel in the environment
below. `--grid` and `--tile` are recognised before the single-ROM path so
`Project64 rom1` behaves exactly as it does today.

### Environment contract

| Variable | Set by | Read by | Meaning |
| --- | --- | --- | --- |
| `PJ64_GRID_KEYS_FD` | orchestrator | input plugin | fd of the inherited snapshot; presence selects snapshot input |
| `PJ64_TILE_SIZE` | tile (from `--tile-rect`) | video plugin | `WxH` render and window size for this tile |
| `PJ64_AUDIO_MUTE` | orchestrator | audio plugin | force the existing no-device silent path |
| `PJ64_GRID_SELFTEST` | caller | strip + tiles | write a fixed key pattern / trace the bits read (verification only) |

### Layout

Work area `A` from `SDL_GetDisplayUsableBounds` of the primary display (excludes menu bar
and Dock). Reserve a strip of height `S` (28 px) along the bottom for the control strip.

- `cols = ceil(sqrt(N))`, `rows = ceil(N / cols)` — the near-square grid.
- `cellW = area.w / cols`, `cellH = area.h / rows`.
- Tile size is the largest 4:3 box that fits the cell: `h = min(cellH, cellW·3/4)`,
  `w = 4h/3`. 4:3 is explicit so a non-4:3 cell letterboxes instead of stretching the
  game. 320×240 is a sanity floor: in the unlikely case a cell is smaller, the grid
  overflows the usable area rather than shrinking below it.
- The grid is centred in the area; tile `(r,c)` goes to
  `(originX + c·w, originY + r·h)`.

### Control strip

One plain (non-GL) SDL window, full work-area width, height `S`, at the bottom,
`SDL_WINDOW_ALWAYS_ON_TOP`, focusable, not resizable. Tiles are created
`SDL_WINDOW_NOT_FOCUSABLE`, so clicking a tile never moves focus off the strip. The title
shows the game count and the quit hint, so no font library is needed. Each frame the strip
writes SDL's keyboard state into the snapshot; Esc or closing the window ends the run.

### Key snapshot

`Source/Common/GridKeys.h`:

```c
struct GridKeys
{
    volatile uint32_t Seq;            /* seqlock */
    uint8_t Keys[SDL_SCANCODE_COUNT];
};
```

The orchestrator creates it on a file descriptor (`shm_open` plus `mmap(MAP_SHARED)`) and
`shm_unlink`s the name immediately after mapping. Children inherit the descriptor (passed
as its number in `PJ64_GRID_KEYS_FD`, not `FD_CLOEXEC`) and never need the name, so a crash
of the orchestrator leaves nothing behind. Writer: `Seq++` (odd) → copy → `Seq++`
(even). Reader: read `Seq`, retry while odd, copy, read `Seq` again, retry if it changed.
`PluginInput::GetKeys` reads this when the variable is set and falls back to
`SDL_GetKeyboardState` otherwise, so the scancode→N64 mapping and the gamepad path are
untouched and a gamepad also drives every tile.

## Part 3 — Behavior changes per file

**`ChangeSize()` (video).** If `PJ64_TILE_SIZE` parses as `WxH`, use it verbatim instead of
`GetScreenResWidth/Height(g_settings->ScreenRes())`. Unset means unchanged.

**`SdlAudioDriver` (audio).** If `PJ64_AUDIO_MUTE` is set, set `m_DeviceUnavailable = true`
before the first `OpenStream`, which routes into the existing silence-drain branch. No new
draining code.

**`PluginInput::GetKeys`.** Source the scancode array from the snapshot when
`PJ64_GRID_KEYS_FD` is set; otherwise today's path.

**`main.cpp`.** `--grid` → `GridHostRun(argc, argv)`. `--tile` → the existing path with
window flags and geometry from `--tile-rect`, and a watcher thread that polls `getppid()`
once a second and exits when reparented (macOS has no `PDEATHSIG`, so this is what stops
orphaned tiles after a hard kill of the orchestrator). Tile mode sets `PJ64_TILE_SIZE` from
its own `--tile-rect`, so the window size and the render resolution cannot disagree.

## Part 4 — Lifecycle and error handling

**Startup.** Validate 1–16 paths, each existing and readable; otherwise exit 2 naming the
problem, so a typo cannot silently produce a short grid. If the strip cannot be created,
abort before spawning. A child that fails to spawn is reported and skipped; if none spawn,
exit 1. A child that spawns but cannot load its ROM exits on its own and its slot stays
blank while the rest keep running.

**Runtime.** Reap each child with `waitpid` and log its exit status. No watchdog: a hung
tile holds its last frame and still dies on quit. Tiles are not resizable, so the window
cannot desync from the GL viewport.

**Quit.** Esc, strip close, SIGINT or SIGTERM → SIGTERM every child, one-second grace,
SIGKILL stragglers, unmap the snapshot, destroy the strip, exit 0.

## Verification

1. `make test` stays green — one version line plus four `ok:` lines.
2. Single-ROM regression: `PJ64_FRAME_DUMP` on a known ROM still writes a 640×480 PPM with
   the same non-black share as before the change.
3. Size propagation: run a grid with a forced `PJ64_TILE_SIZE`; every tile's frame dump is
   exactly that size.
4. Key propagation: with `PJ64_GRID_SELFTEST=1`, `Scripts/grid_selftest.sh <rom>` runs one
   ROM in four tiles, the strip writes a fixed key pattern, each tile traces the bits it
   read, and the script greps every tile's log for the pattern. One ROM is enough, so no
   ROM pack is required.
5. Mute: with `PJ64_TRACE=AudioInitShutdown`, every tile logs "draining silently so
   emulation continues" and opens no device.
6. Quit: after Esc no `Project64` processes remain and the shm object is gone.

## Risks

- **N processes cost N× memory and startup.** Sixteen tiles means sixteen copies of the
  core and plugins. Acceptable for a demo on Apple Silicon; the cap is 16.
- **Broadcast latency** is up to about one frame, since the strip writes and tiles read at
  their own frame rates. Fine for a showcase; not suitable for frame-accurate play.
- **Tile size is a computed arbitrary value**, not a tested resolution. The renderer
  already handles many arbitrary sizes; the 320×240 floor keeps it sane.
- **Config is shared across tiles.** They share one `Config/` directory, which is why
  per-tile differences travel in the environment rather than through settings.
- **A hard-killed orchestrator** relies on the tile-side `getppid()` watcher to avoid
  orphans; that is the only safeguard on macOS.
