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
        "PJ64_FACE": "0",
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
        try:
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
        except KeyboardInterrupt:
            pool.shutdown(wait=False, cancel_futures=True)
            print("Interrupted; %d game(s) finished, writing a partial report" % done)

    total, failed = aggregate(work_dir, out_dir)
    print("Wrote %s (%d rows, %d failed)" % (out_dir / "report.tsv", total, failed))
    print("Failures: %s" % (out_dir / "failures.txt"))


if __name__ == "__main__":
    main()
