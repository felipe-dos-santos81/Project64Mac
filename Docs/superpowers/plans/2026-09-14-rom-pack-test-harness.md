# ROM Pack Test Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run every N64 ROM in `/Users/felipe.dos.santos/Downloads/N64ROMsPACK` through the frontend, flag the failures, and save a non-black PNG screenshot of every game that works, named after the ROM.

**Architecture:** A small extension to the existing frame-dump path in `CSdlRenderWindow` makes it wait for a non-black frame (opt-in via an environment variable, old behavior otherwise). A Python 3 stdlib harness then launches each ROM in parallel, classifies the outcome from the process status and the written PPM, converts passes to PNG with the native `sips` tool, and aggregates a TSV report plus a plain failure list.

**Tech Stack:** C++ (SDL3/OpenGL frontend), GNU Make, Python 3 standard library, `sips` (macOS).

## Global Constraints

- Platform is macOS on Apple Silicon only; the frontend is interpreter-only and windowed.
- The Python harness uses only the standard library plus `sips`; no third-party packages.
- `make test` must stay green: one version line plus four `ok:` lines.
- Touched source files are LF; preserve that. `Makefile` is UTF-8 with box-drawing characters; preserve encoding and use real tabs for recipe lines.
- Never commit ROM files, screenshots, or the pack's `report.tsv`/`failures.txt`; all outputs live in the ROM pack folder, outside the repo.
- With `PJ64_FRAME_DUMP_MIN_NONBLACK` unset, frame dumping must behave exactly as before (write once at `PJ64_FRAME_DUMP_AT`).
- Non-black rule (shared by emulator and harness): a pixel is non-black when any R/G/B channel is greater than 16.

---

### Task 1: Emulator waits for a non-black frame before dumping

**Files:**
- Modify: `Source/Project64-sdl/SdlRenderWindow.h`
- Modify: `Source/Project64-sdl/SdlRenderWindow.cpp`

**Interfaces:**
- Consumes: existing `PJ64_FRAME_DUMP` and `PJ64_FRAME_DUMP_AT` env vars and the existing `DumpFrame()` call from `SwapWindow()`.
- Produces: two new env vars, `PJ64_FRAME_DUMP_MIN_NONBLACK` (percent, `0`/unset = old behavior) and `PJ64_FRAME_DUMP_MAX` (frame cap, default 3600), and three private methods on `CSdlRenderWindow`: `bool ReadBackBuffer(std::vector<uint8_t>&, int&, int&)`, `double NonBlackShare(const std::vector<uint8_t>&, int, int) const`, `void WriteFrame(const std::vector<uint8_t>&, int, int)`.

- [ ] **Step 1: Read the current files for context**

Run: `file Source/Project64-sdl/SdlRenderWindow.h Source/Project64-sdl/SdlRenderWindow.cpp`
Expected: both `ASCII text` (LF). Read both files fully; `DumpFrame()` currently reads the back buffer and writes one PPM at `m_FrameCount == m_DumpAt`.

- [ ] **Step 2: Extend the header**

In `Source/Project64-sdl/SdlRenderWindow.h`, add `#include <vector>` after `#include <string>`, add the three method declarations, and add the four new members. The class body becomes:

```cpp
class CSdlRenderWindow : public RenderWindow
{
public:
    CSdlRenderWindow(SDL_Window * Window, SDL_GLContext Context, CGLContextObj Cgl);

    void GfxThreadInit();
    void GfxThreadDone();
    void SwapWindow();

private:
    void DumpFrame();
    bool ReadBackBuffer(std::vector<uint8_t> & Pixels, int & Width, int & Height);
    double NonBlackShare(const std::vector<uint8_t> & Pixels, int Width, int Height) const;
    void WriteFrame(const std::vector<uint8_t> & Pixels, int Width, int Height);

    SDL_Window * m_Window;
    SDL_GLContext m_Context;
    // Captured on the main thread while the context was current there. SDL3 documents
    // SDL_GL_MakeCurrent as main-thread-only, and it marshals, so the emulation thread
    // binds the underlying CGL context itself instead.
    CGLContextObj m_Cgl;
    // Frame dumping, driven by PJ64_FRAME_DUMP; see DumpFrame. Both environment
    // variables are read once at construction - they cannot change mid-run, and
    // DumpFrame is on the per-frame present path.
    std::string m_DumpPath;
    uint32_t m_DumpAt;
    uint32_t m_FrameCount;
    bool m_FrameDumped;
    // Non-black wait, driven by PJ64_FRAME_DUMP_MIN_NONBLACK. 0 keeps the old
    // write-once-at-DumpAt behavior; PJ64_FRAME_DUMP_MAX caps the wait.
    double m_MinNonBlack;
    uint32_t m_MaxFrame;
    double m_BestNonBlack;
    std::vector<uint8_t> m_BestPixels;
};
```

- [ ] **Step 3: Read the two new env vars in the constructor**

In `Source/Project64-sdl/SdlRenderWindow.cpp`, add a file-scope constant above the constructor:

```cpp
// How often DumpFrame re-checks the back buffer while waiting for a non-black frame.
// Reading every frame would slow the emulation being waited on.
static const uint32_t DumpCheckInterval = 30;
```

Extend the constructor initializer list with `m_MinNonBlack(0.0), m_MaxFrame(3600), m_BestNonBlack(0.0)` after `m_FrameDumped(false)`, and append to the constructor body:

```cpp
    const char * MinEnv = getenv("PJ64_FRAME_DUMP_MIN_NONBLACK");
    if (MinEnv != nullptr)
    {
        m_MinNonBlack = atof(MinEnv);
    }
    const char * MaxEnv = getenv("PJ64_FRAME_DUMP_MAX");
    if (MaxEnv != nullptr)
    {
        m_MaxFrame = (uint32_t)atoi(MaxEnv);
    }
```

- [ ] **Step 4: Replace `DumpFrame()` and add the helpers**

Replace the whole existing `DumpFrame()` (the function and its comment) with:

```cpp
// Writes one frame to the file named by PJ64_FRAME_DUMP, as a binary PPM, once the frame
// counter reaches PJ64_FRAME_DUMP_AT (default 300, late enough to pass the black frames a
// game shows while it boots). This frontend has no UI, so reading the back buffer is the
// only way to see what was drawn without capturing the whole screen.
//
// With PJ64_FRAME_DUMP_MIN_NONBLACK set, the dump instead waits for the first frame whose
// non-black share clears that percentage, so a boot-time black frame is never the one
// captured. PJ64_FRAME_DUMP_MAX bounds the wait; at the cap the best frame seen is written.
void CSdlRenderWindow::DumpFrame()
{
    if (m_FrameDumped || m_DumpPath.empty())
    {
        return;
    }
    m_FrameCount += 1;
    if (m_FrameCount < m_DumpAt)
    {
        return;
    }

    // No threshold: write the first frame at PJ64_FRAME_DUMP_AT, exactly as before.
    if (m_MinNonBlack <= 0.0)
    {
        std::vector<uint8_t> Pixels;
        int Width = 0, Height = 0;
        if (ReadBackBuffer(Pixels, Width, Height))
        {
            WriteFrame(Pixels, Width, Height);
        }
        return;
    }

    bool AtCap = m_FrameCount >= m_MaxFrame;
    if (!AtCap && (m_FrameCount - m_DumpAt) % DumpCheckInterval != 0)
    {
        return;
    }

    std::vector<uint8_t> Pixels;
    int Width = 0, Height = 0;
    if (!ReadBackBuffer(Pixels, Width, Height))
    {
        return;
    }

    double Share = NonBlackShare(Pixels, Width, Height);
    if (Share >= m_MinNonBlack)
    {
        WriteFrame(Pixels, Width, Height);
        return;
    }
    if (Share > m_BestNonBlack)
    {
        m_BestNonBlack = Share;
        m_BestPixels = Pixels;
    }
    if (AtCap)
    {
        WriteTrace(TraceUserInterface, TraceInfo,
            "Frame cap %u reached with %.1f%% non-black (wanted %.1f%%)",
            (unsigned)m_MaxFrame, Share, m_MinNonBlack);
        WriteFrame(m_BestPixels.empty() ? Pixels : m_BestPixels, Width, Height);
    }
}

bool CSdlRenderWindow::ReadBackBuffer(std::vector<uint8_t> & Pixels, int & Width, int & Height)
{
    GLint Viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, Viewport);
    Width = (int)Viewport[2];
    Height = (int)Viewport[3];
    if (Width <= 0 || Height <= 0)
    {
        return false;
    }
    Pixels.resize((size_t)Width * (size_t)Height * 3);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, Width, Height, GL_RGB, GL_UNSIGNED_BYTE, &Pixels[0]);
    return true;
}

// Share of sampled pixels that are not black, over a sparse grid (every 8th pixel on each
// axis) so the check stays cheap. A pixel is non-black when any channel exceeds 16, which
// ignores low-level dither noise on an otherwise black frame.
double CSdlRenderWindow::NonBlackShare(const std::vector<uint8_t> & Pixels, int Width, int Height) const
{
    const int Stride = 8;
    const uint8_t Threshold = 16;
    size_t Total = 0, NonBlack = 0;
    for (int y = 0; y < Height; y += Stride)
    {
        for (int x = 0; x < Width; x += Stride)
        {
            size_t i = ((size_t)y * (size_t)Width + (size_t)x) * 3;
            Total += 1;
            if (Pixels[i] > Threshold || Pixels[i + 1] > Threshold || Pixels[i + 2] > Threshold)
            {
                NonBlack += 1;
            }
        }
    }
    return Total == 0 ? 0.0 : 100.0 * (double)NonBlack / (double)Total;
}

void CSdlRenderWindow::WriteFrame(const std::vector<uint8_t> & Pixels, int Width, int Height)
{
    const char * Path = m_DumpPath.c_str();
    FILE * File = fopen(Path, "wb");
    if (File == nullptr)
    {
        WriteTrace(TraceUserInterface, TraceError, "Could not open %s for the frame dump", Path);
        m_FrameDumped = true;
        return;
    }
    fprintf(File, "P6\n%d %d\n255\n", Width, Height);
    for (int Row = Height - 1; Row >= 0; Row--) // GL's origin is bottom left, a PPM's is top left
    {
        fwrite(&Pixels[(size_t)Row * (size_t)Width * 3], 1, (size_t)Width * 3, File);
    }
    fclose(File);
    m_FrameDumped = true;
    WriteTrace(TraceUserInterface, TraceInfo, "Wrote frame %u (%dx%d) to %s", (unsigned)m_FrameCount, Width, Height, Path);
}
```

- [ ] **Step 5: Build the frontend**

Run: `make frontend`
Expected: compiles and links `Bin/macOS/Project64` with no warnings from `SdlRenderWindow.cpp`.

- [ ] **Step 6: Smoke test the build**

Run: `make test`
Expected: one `Project64 macOS SDL3 frontend` line followed by four `ok: ...` lines.

- [ ] **Step 7: Regression check — unset threshold behaves as before**

Run:
```sh
rm -f /tmp/pj64_reg.ppm
PJ64_FRAME_DUMP=/tmp/pj64_reg.ppm PJ64_FRAME_DUMP_AT=300 \
  perl -e 'alarm 30; exec @ARGV' -- ./Bin/macOS/Project64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 || true
ls -la /tmp/pj64_reg.ppm
```
Expected: the PPM exists and is 640x480 (about 900 KB). This is the old behavior, unchanged.

- [ ] **Step 8: Verify the new wait produces a non-black frame**

Run:
```sh
rm -f /tmp/pj64_wait.ppm
PJ64_FRAME_DUMP=/tmp/pj64_wait.ppm PJ64_FRAME_DUMP_AT=120 \
PJ64_FRAME_DUMP_MIN_NONBLACK=5 PJ64_FRAME_DUMP_MAX=3600 \
  perl -e 'alarm 60; exec @ARGV' -- ./Bin/macOS/Project64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 || true
python3 - <<'EOF'
d=open('/tmp/pj64_wait.ppm','rb').read().split(b'\n',3)
w,h=map(int,d[1].split()); px=d[3]
nb=sum(1 for i in range(0,w*h*3,3) if px[i]>16 or px[i+1]>16 or px[i+2]>16)
print(f"{w}x{h} non-black {100*nb/(w*h):.1f}%")
EOF
```
Expected: a 640x480 line with non-black well above 5% (Super Mario 64 measures about 90%).

- [ ] **Step 9: Verify the frame cap path**

Run:
```sh
rm -f /tmp/pj64_cap.ppm
PJ64_TRACE=info PJ64_FRAME_DUMP=/tmp/pj64_cap.ppm PJ64_FRAME_DUMP_AT=120 \
PJ64_FRAME_DUMP_MIN_NONBLACK=99 PJ64_FRAME_DUMP_MAX=200 \
  perl -e 'alarm 30; exec @ARGV' -- ./Bin/macOS/Project64 \
  /Users/felipe.dos.santos/Downloads/N64ROMsPACK/super_mario_64_usa.z64 2>&1 || true
```
Expected: a `Frame cap 200 reached with ...` trace line and `/tmp/pj64_cap.ppm` written despite never reaching 99% — proving the cap cannot hang the run.

- [ ] **Step 10: Commit**

```bash
git add Source/Project64-sdl/SdlRenderWindow.h Source/Project64-sdl/SdlRenderWindow.cpp
git commit -m "Wait for a non-black frame when dumping"
```

---

### Task 2: Harness script

**Files:**
- Create: `Scripts/run_rom_pack.py`

**Interfaces:**
- Consumes: `Bin/macOS/Project64` and the env vars from Task 1.
- Produces: `Screenshots/<rom_filename>.png`, `report.tsv` (header `file  status  reason  nonblack_pct  seconds`), `failures.txt`, and `.pj64-run/` scratch (`.ppm` deleted after each ROM, `.result`/`.log` kept).

- [ ] **Step 1: Create the script**

Create `Scripts/run_rom_pack.py` with exactly:

```python
#!/usr/bin/env python3
"""Run every N64 ROM in a pack through the frontend, flag failures, save screenshots.

Design: Docs/superpowers/specs/2026-09-14-rom-pack-test-harness-design.md
"""
import argparse
import os
import signal
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

ROM_EXTENSIONS = {".z64", ".n64", ".v64"}
NONBLACK_CHANNEL = 16
DUMP_AT = 120
DUMP_MAX = 3600


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--roms", default="/Users/felipe.dos.santos/Downloads/N64ROMsPACK",
                        help="directory of ROM files")
    parser.add_argument("--out", default=None, help="output directory (default: --roms)")
    parser.add_argument("--jobs", type=int, default=4, help="games to run at once")
    parser.add_argument("--threshold", type=float, default=5.0,
                        help="percent non-black required for a pass")
    parser.add_argument("--timeout", type=float, default=180.0,
                        help="seconds per game before it is a hang")
    parser.add_argument("--force", action="store_true",
                        help="re-run games that already have a screenshot")
    parser.add_argument("--only", action="append", default=[],
                        help="run only this ROM filename; repeatable")
    args = parser.parse_args()
    if args.out is None:
        args.out = args.roms
    return args


def repo_root():
    return Path(__file__).resolve().parent.parent


def find_binary():
    binary = repo_root() / "Bin" / "macOS" / "Project64"
    if not binary.is_file():
        sys.exit("frontend not built: %s (run `make all`)" % binary)
    return binary


def rom_files(roms_dir):
    return sorted(
        p for p in roms_dir.iterdir()
        if p.is_file() and p.suffix.lower() in ROM_EXTENSIONS
    )


def parse_ppm(path):
    data = path.read_bytes()
    fields = data.split(b"\n", 3)
    if len(fields) != 4 or fields[0].strip() != b"P6":
        raise ValueError("not a P6 PPM: %s" % path)
    width, height = (int(v) for v in fields[1].split())
    return width, height, fields[3]


def nonblack_percent(width, height, pixels):
    total = width * height
    if total == 0:
        return 0.0
    nonblack = 0
    for i in range(0, total * 3, 3):
        if (pixels[i] > NONBLACK_CHANNEL
                or pixels[i + 1] > NONBLACK_CHANNEL
                or pixels[i + 2] > NONBLACK_CHANNEL):
            nonblack += 1
    return 100.0 * nonblack / total


def kill_group(proc):
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass


def run_rom(binary, rom, work_dir, screenshots, threshold, timeout):
    ppm = work_dir / (rom.name + ".ppm")
    if ppm.exists():
        ppm.unlink()

    env = dict(os.environ)
    env.update({
        "PJ64_FRAME_DUMP": str(ppm),
        "PJ64_FRAME_DUMP_AT": str(DUMP_AT),
        "PJ64_FRAME_DUMP_MIN_NONBLACK": str(threshold),
        "PJ64_FRAME_DUMP_MAX": str(DUMP_MAX),
    })

    start = time.monotonic()
    # stderr goes to a file, not a pipe: a full pipe would block the emulator and look
    # like a hang.
    log = work_dir / (rom.name + ".log")
    with log.open("wb") as logfile:
        proc = subprocess.Popen(
            [str(binary), str(rom)],
            stdout=subprocess.DEVNULL,
            stderr=logfile,
            env=env,
            start_new_session=True,
        )

    dumped = False
    timed_out = False
    last_size = -1
    while True:
        if proc.poll() is not None:
            break
        if ppm.exists():
            size = ppm.stat().st_size
            if size > 0 and size == last_size:
                dumped = True
                break
            last_size = size
        if time.monotonic() - start > timeout:
            timed_out = True
            break
        time.sleep(0.2)

    if proc.poll() is None:
        kill_group(proc)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    seconds = time.monotonic() - start
    exit_code = proc.returncode

    pct = None
    if dumped:
        try:
            width, height, pixels = parse_ppm(ppm)
            pct = nonblack_percent(width, height, pixels)
        except (ValueError, IndexError, OSError):
            dumped = False

    if dumped and pct is not None and pct >= threshold:
        png = screenshots / (rom.name + ".png")
        png.parent.mkdir(parents=True, exist_ok=True)
        converted = subprocess.run(
            ["sips", "-s", "format", "png", str(ppm), "--out", str(png)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        status, reason = ("ok", "ok") if converted.returncode == 0 else ("fail", "png_error")
    elif dumped:
        status, reason = "fail", "black"
    elif timed_out:
        status, reason = "fail", "hang"
    elif exit_code is not None and exit_code < 0:
        status, reason = "fail", "crash"
    elif exit_code == 0:
        status, reason = "fail", "no_dump"
    else:
        status, reason = "fail", "load_error"

    if ppm.exists():
        ppm.unlink()

    pct_text = "%.2f" % pct if pct is not None else ""
    return "%s\t%s\t%s\t%s\t%.1f" % (rom.name, status, reason, pct_text, seconds)


def result_file(work_dir, rom):
    return work_dir / (rom.name + ".result")


def process(binary, rom, args, work_dir, screenshots):
    result = result_file(work_dir, rom)
    png = screenshots / (rom.name + ".png")
    if result.exists() and png.exists() and not args.force:
        line = result.read_text().strip()
        fields = line.split("\t")
        if len(fields) >= 2 and fields[1] == "ok":
            return line
    line = run_rom(binary, rom, work_dir, screenshots, args.threshold, args.timeout)
    result.write_text(line + "\n")
    return line


def aggregate(work_dir, out_dir):
    rows = [p.read_text().strip() for p in sorted(work_dir.glob("*.result"))]
    with (out_dir / "report.tsv").open("w") as report:
        report.write("file\tstatus\treason\tnonblack_pct\tseconds\n")
        for row in rows:
            report.write(row + "\n")
    failures = [row.split("\t")[0] for row in rows if row.split("\t")[1] != "ok"]
    (out_dir / "failures.txt").write_text("".join(name + "\n" for name in failures))
    return len(rows), len(failures)


def main():
    args = parse_args()
    binary = find_binary()
    roms_dir = Path(args.roms)
    out_dir = Path(args.out)
    if not roms_dir.is_dir():
        sys.exit("ROM directory not found: %s" % roms_dir)
    work_dir = out_dir / ".pj64-run"
    work_dir.mkdir(parents=True, exist_ok=True)
    screenshots = out_dir / "Screenshots"

    roms = rom_files(roms_dir)
    if args.only:
        wanted = set(args.only)
        roms = [r for r in roms if r.name in wanted]
    if not roms:
        sys.exit("no ROMs found in %s" % roms_dir)

    print("Running %d ROM(s), %d at a time" % (len(roms), args.jobs))
    done = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(process, binary, rom, args, work_dir, screenshots): rom
                   for rom in roms}
        for future in as_completed(futures):
            rom = futures[future]
            done += 1
            try:
                line = future.result()
                print("[%d/%d] %s" % (done, len(roms), line))
            except Exception as exc:  # one bad game must not stop the batch
                result_file(work_dir, rom).write_text(
                    "%s\tfail\tcrash\t\t0.0\n" % rom.name)
                print("[%d/%d] %s ERROR %s" % (done, len(roms), rom.name, exc))

    total, failed = aggregate(work_dir, out_dir)
    print("Wrote %s (%d rows, %d failed)" % (out_dir / "report.tsv", total, failed))
    print("Failures: %s" % (out_dir / "failures.txt"))


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Syntax-check**

Run: `python3 -m py_compile Scripts/run_rom_pack.py`
Expected: exit 0, no output.

- [ ] **Step 3: Run one known-good ROM**

Run: `python3 Scripts/run_rom_pack.py --only super_mario_64_usa.z64`
Expected: a line `[1/1] super_mario_64_usa.z64\tok\tok\t<about 90>\t<seconds>`, then `Wrote .../report.tsv (1 rows, 0 failed)`.

- [ ] **Step 4: Confirm the screenshot and report**

Run:
```sh
ls -la /Users/felipe.dos.santos/Downloads/N64ROMsPACK/Screenshots/super_mario_64_usa.z64.png
file /Users/felipe.dos.santos/Downloads/N64ROMsPACK/Screenshots/super_mario_64_usa.z64.png
cat /Users/felipe.dos.santos/Downloads/N64ROMsPACK/report.tsv
```
Expected: a PNG (`640 x 480`), and a two-line TSV (header plus the `ok` row).

- [ ] **Step 5: Verify a failure is classified, not crashed**

Run:
```sh
mkdir -p /tmp/pj64bad && head -c 4096 /dev/urandom > /tmp/pj64bad/bad.z64
python3 Scripts/run_rom_pack.py --roms /tmp/pj64bad --out /tmp/pj64bad --only bad.z64 --timeout 30
cat /tmp/pj64bad/report.tsv /tmp/pj64bad/failures.txt
```
Expected: the row has `fail` and a reason of `load_error` (or `crash`), `failures.txt` contains `bad.z64`, and no PNG is produced. This proves the failure path works end to end.

- [ ] **Step 6: Verify resume**

Run: `python3 Scripts/run_rom_pack.py --only super_mario_64_usa.z64`
Expected: the ROM is skipped from re-running (returns the stored `ok` row immediately); the report still shows one row.

- [ ] **Step 7: Commit**

```bash
git add Scripts/run_rom_pack.py
git commit -m "Add ROM pack test harness"
```

---

### Task 3: Makefile target and README note

**Files:**
- Modify: `Makefile` (Stage 8 section)
- Modify: `README.md` (Diagnostics section)

**Interfaces:**
- Consumes: `Scripts/run_rom_pack.py` from Task 2.
- Produces: `make rom-test`.

- [ ] **Step 1: Add the Makefile target**

In `Makefile`, directly after the `run:` target (before `test:`), add a recipe. Use a real tab for the command line:

```make
rom-test: ## Run the ROM pack test harness (see Scripts/run_rom_pack.py --help)
	python3 Scripts/run_rom_pack.py
```

- [ ] **Step 2: Verify the target is registered and works**

Run: `make help | grep rom-test`
Expected: the `rom-test` line is listed.

Run: `python3 Scripts/run_rom_pack.py --only super_mario_64_usa.z64`
Expected: the same single-ROM `ok` result as Task 2, Step 3. (`make rom-test` with no arguments runs the full pack, so it is not used for this quick check.)

- [ ] **Step 3: Document the new env vars**

In `README.md`, in the `## Diagnostics` list, after the existing `PJ64_FRAME_DUMP` bullet, add:

```markdown
- `PJ64_FRAME_DUMP_MIN_NONBLACK=<percent>`, with optional `PJ64_FRAME_DUMP_MAX=<frame>`,
  makes the dump wait for the first frame whose non-black share clears the percentage
  instead of writing unconditionally at `PJ64_FRAME_DUMP_AT`.
```

- [ ] **Step 4: Commit**

```bash
git add Makefile README.md
git commit -m "Add rom-test target and document the non-black dump"
```

---

### Task 4: Full pack run and review

**Files:**
- No repository files change. Outputs land in `/Users/felipe.dos.santos/Downloads/N64ROMsPACK/`.

**Interfaces:**
- Consumes: the built frontend from Task 1 and the harness from Task 2/3.
- Produces: `Screenshots/`, `report.tsv`, `failures.txt`.

- [ ] **Step 1: Confirm the tree is built and clean**

Run: `make test`
Expected: version line plus four `ok:` lines. Then `git status --short` shows only the committed changes (no stray files in the repo).

- [ ] **Step 2: Run the full pack**

Run: `python3 Scripts/run_rom_pack.py`
Expected: progress lines for 296 games over roughly 20–30 minutes, ending with `Wrote .../report.tsv (296 rows, N failed)`.

- [ ] **Step 3: Verify the report and screenshots agree**

Run:
```sh
cd /Users/felipe.dos.santos/Downloads/N64ROMsPACK
awk 'NR>1 && $2=="ok"' report.tsv | wc -l
awk 'NR>1 && $2=="fail"' report.tsv | wc -l
ls Screenshots | wc -l
wc -l < failures.txt
comm -3 <(awk 'NR>1 && $2=="fail"{print $1}' report.tsv | sort) <(sort failures.txt)
```
Expected: the `ok` count equals the `Screenshots` file count; the `fail` count equals the `failures.txt` line count; `comm` prints nothing (the two lists match exactly); header plus data is 297 lines in `report.tsv`.

- [ ] **Step 4: Spot-check screenshots**

Run:
```sh
cd /Users/felipe.dos.santos/Downloads/N64ROMsPACK
for f in Screenshots/super_mario_64_usa.z64.png Screenshots/mario_kart_64_u.z64.png Screenshots/banjo_tooie_u.z64.png; do
  file "$f"
done
```
Expected: each is a `640 x 480` PNG. Open a few and confirm they show real game imagery, not black.

- [ ] **Step 5: Record the outcome**

Report to the user: total games, pass count, the failure list grouped by reason (`awk 'NR>1{print $3}' report.tsv | sort | uniq -c`), and where the screenshots and report live. No commit — the outputs are test artifacts, not repository content.
