# ROM pack test harness — design

Date: 2026-09-14
Status: approved (design), implementation not started

## Goal

Run every game in `/Users/felipe.dos.santos/Downloads/N64ROMsPACK` through the macOS
frontend, flag the ones that fail, and save a non-black screenshot of every game that
works, named after the ROM.

## Context

- The pack holds 296 ROMs: 253 `.z64`, 32 `.n64`, 11 `.v64` (plus non-ROM files such as
  `README.md` and metadata that the harness ignores).
- The frontend (`Source/Project64-sdl/main.cpp`) takes one ROM path, opens a 640×480
  window, and runs until the window is closed or emulation stops on its own. It never
  exits by itself for a normal game, so the harness must kill each process.
- Frame dumping already exists: `PJ64_FRAME_DUMP` names a PPM path and
  `PJ64_FRAME_DUMP_AT` (default 300) is the frame at which one frame is written, once,
  unconditionally (`Source/Project64-sdl/SdlRenderWindow.cpp`). It cannot guarantee the
  captured frame is non-black, and it never retries.
- Measured on this machine, reaching frame 300 takes roughly 6–20 seconds per game,
  depending on the title.

## Non-goals

- No ROM browser or UI. The frontend keeps taking one ROM path.
- No accuracy testing beyond "does a non-black frame render": no input simulation, no
  audio verification, no per-frame comparison.
- No changes to the build stages beyond adding one convenience Makefile target.
- Non-ROM files in the pack are not tested.

## Failure definition

A game is a failure if any of the following holds:

| reason | condition |
| --- | --- |
| `load_error` | process exits non-zero with no dump and stderr contains `Failed to load ROM` |
| `crash` | process is terminated by a signal |
| `hang` | no dump within the per-game timeout; the process group is killed |
| `black` | a dump is written but its non-black share is below the threshold |
| `no_dump` | process exits 0 without writing a dump |
| `png_error` | the dump is non-black but `sips` fails to convert it |

Everything else is `ok`.

## Part 1 — Emulator change

File: `Source/Project64-sdl/SdlRenderWindow.{h,cpp}`. This is the layer that owns frame
dumping, so the "wait until non-black" rule lives here and the harness stays a thin loop.

Two optional environment variables, read once in the constructor alongside the existing
two:

- `PJ64_FRAME_DUMP_MIN_NONBLACK` — percentage of sampled pixels that must be non-black
  before a frame is written. Unset or `0` preserves today's behavior exactly (write
  unconditionally at `PJ64_FRAME_DUMP_AT`).
- `PJ64_FRAME_DUMP_MAX` — frame number cap while waiting. Default `3600`.

Behavior when `PJ64_FRAME_DUMP_MIN_NONBLACK > 0`:

1. Count frames as today; ignore frames before `PJ64_FRAME_DUMP_AT`.
2. Every 30th frame from `PJ64_FRAME_DUMP_AT` onward, read the back buffer and sample a
   sparse grid: every 8th pixel in each axis; a sample counts as non-black if any of its
   R/G/B channels is greater than 16. Compute the non-black share over the samples.
3. When the share meets the threshold, write the full frame (existing PPM path) and stop.
4. If `PJ64_FRAME_DUMP_MAX` is reached first, write the best frame seen so far and log
   that the threshold was never met.

Rationale for sparse sampling plus a 30-frame interval: reading the full 640×480×3 back
buffer on every frame would slow the emulation being waited on. The harness independently
recomputes the true full-buffer percentage from the written PPM, so the emulator's sample
only decides *when* to write, never what gets reported.

With `PJ64_FRAME_DUMP_MIN_NONBLACK` unset the function is unchanged, so `make test` and
existing diagnostic usage are unaffected.

## Part 2 — Harness

File: `Scripts/run_rom_pack.py`, Python 3 standard library only (no third-party
packages). Python is used over shell for parallel process control, per-process timeouts,
signal detection, and PPM analysis. PNG conversion uses `sips`, which ships with macOS.

### CLI

```
--roms      DIR   default /Users/felipe.dos.santos/Downloads/N64ROMsPACK
--out       DIR   default: same as --roms
--jobs      N     default 4
--threshold PCT   default 5   (percent non-black; passed to the emulator and used for classification)
--timeout   SEC   default 180 (per game, wall clock)
--force           re-run ROMs that already have a successful screenshot
--only      ROM   run only the named ROM(s); repeatable
```

The binary path is resolved relative to the script location: `<repo>/Bin/macOS/Project64`.

### Per-ROM flow

1. Build a unique temp PPM path under `<out>/.pj64-run/` and launch
   `Bin/macOS/Project64 <rom>` in its own process group (`start_new_session=True`) with:
   - `PJ64_FRAME_DUMP=<temp ppm>`
   - `PJ64_FRAME_DUMP_AT=120`
   - `PJ64_FRAME_DUMP_MIN_NONBLACK=<threshold>`
   - `PJ64_FRAME_DUMP_MAX=3600`
2. Wait for a stable PPM (file present and its size unchanged across two consecutive
   polls), or the process exiting, or `--timeout`; on timeout kill the whole process
   group.
3. Classify per the table above, using the exit status (signal vs. code) and the
   full-buffer non-black percentage computed from the PPM. A pixel counts as non-black
   when any of its R/G/B channels is greater than 16, matching the emulator's sample
   rule.
4. On `ok`, convert `<rom_filename>` → `<out>/Screenshots/<rom_filename>.png` with
   `sips -s format png`. The temp PPM is deleted afterward in every case.
5. Write `<out>/.pj64-run/<rom_filename>.result` (one file per ROM, no shared-file
   locking). The parent aggregates all result files into `report.tsv` and
   `failures.txt`. Result files persist between runs so resume can reuse them.

### Resume

By default a ROM with an existing successful PNG is skipped and its previous result is
reused; `--force` re-runs it. Interrupting the batch loses only the games in flight.
`--only` re-runs specific ROMs (for example the contents of `failures.txt`) without a
full sweep.

## Part 3 — Outputs

All under the ROM pack folder; nothing is written into the repo except the script and the
Makefile target.

- `Screenshots/<rom_filename>.png` — e.g. `super_mario_64_usa.z64.png`. The full filename
  including extension keeps `.z64`/`.n64`/`.v64` names distinct.
- `report.tsv` — header `file  status  reason  nonblack_pct  seconds`, one row per ROM,
  sorted by filename. `status` is `ok` or `fail`; `reason` is `ok` for passes and the
  failure code otherwise; `nonblack_pct` is the true full-buffer percentage.
- `failures.txt` — plain list of failed ROM filenames, one per line.
- `.pj64-run/` — scratch PPMs (deleted after each ROM) and per-ROM `.result` files
  (kept so a re-run can resume).

## Part 4 — Makefile

Add one convenience target:

```
rom-test: ## Run the ROM pack test harness (see Scripts/run_rom_pack.py --help)
	python3 Scripts/run_rom_pack.py
```

## Verification

1. `make test` after the emulator edit — one version line plus four `ok:` lines.
2. Regression: with `PJ64_FRAME_DUMP_MIN_NONBLACK` unset, the dump still writes at
   `PJ64_FRAME_DUMP_AT` as before.
3. Harness smoke on `super_mario_64_usa.z64`: produces
   `Screenshots/super_mario_64_usa.z64.png`, non-black around 90%, one `ok` row.
4. Full run: `report.tsv` has 296 rows; every `ok` row has a matching PNG; `failures.txt`
   matches the `fail` rows exactly; spot-check several PNGs are valid 640×480 images and
   genuinely non-black.

## Risks

- **Threshold is a judgement call.** A legitimately dark frame (small HUD on black) could
  be misread as `black`. The threshold is a flag and every row records the real
  percentage, so borderline results are visible and re-runnable with `--force --only`.
- **Parallel GL/audio contention** could slow a game enough to hit the timeout and be
  falsely flagged `hang`. `--jobs 1` re-runs suspects cleanly.
- **Some games genuinely hang or never render.** Those are real `hang`/`black` results,
  which is the point of the exercise.
- **Runtime** is roughly 20–30 minutes for the full pack at 4 workers; the frame cap and
  timeout bound the worst case per game.
