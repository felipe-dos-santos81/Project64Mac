<p align="center">
  <img src="./Docs/img/screen.png" alt="logo" width="1024" />
</p>

# Project64 for Apple Silicon

A fork of [Project64](https://github.com/project64/project64), simplified down to a
single platform: it builds and runs only on macOS / arm64, using SDL3 for the window,
input and audio, and OpenGL for rendering. Everything that existed only for Windows,
Linux or Android has been removed, and this tree does not track upstream.

Built for educational purposes only — it exists to study how the emulator works on
Apple Silicon, not as a maintained release.

## Build

Prerequisites: Xcode command line tools plus Homebrew SDL3 and yaml-cpp
(`brew install sdl3 pkg-config yaml-cpp`).

```sh
make                    # core, four plugins, frontend, and the ROM database beside the binary
make test               # smoke test: version string and plugin exports
make input-config-test  # parser tests for the YAML input mapping
```

`make help` lists every target with its build stage.

## Game files

No game data ships with this repository and the build produces none — you supply your
own ROM files.

There is no ROM browser: the frontend takes ROM paths as arguments — one by default, or
several in grid mode. The convention is a `Roms/` folder at the repository root,
git-ignored so game files are never committed.

```sh
mkdir -p Roms
cp ~/Downloads/super_mario_64.z64 Roms/
make run rom=Roms/super_mario_64.z64
```

Any path works — `./Bin/macOS/Project64 /path/to/game.z64`. Use `.z64`, `.n64`, `.v64`
or a `.zip` containing one of them; 7-Zip archives are not supported.

## Grid mode

`make grid roms="Roms/a.z64 Roms/b.z64"` — or `./Bin/macOS/Project64 --grid a.z64 b.z64 …`
— opens 1–16 games side by side in a near-square grid and sends every keystroke to all of
them. A small always-on-top strip along the bottom owns the keyboard (the tiles never take
focus); Esc or closing the strip quits everything. Tiles render at the largest 4:3 size
that fits their cell and are muted. Each game is a separate process running the normal
single-ROM path, so one game cannot take down the others.

`make grid-selftest rom=Roms/a.z64` proves the broadcast by running one ROM in four tiles
and checking each read the strip's key state.

## Input mapping

Bindings are read once at startup from `Bin/macOS/Config/input.yaml`
(`PJ64_INPUT_YAML=/path` overrides it); delete the file for the built-in mapping. Each
control takes exactly one input — `{key: X}`, `{button: a}`, `{axis: rightx, sign: -}`,
and for `Stick` also `{stick: left}` or `{keys: {up: Up, …}}` — so naming a control replaces
its built-in binding. The shipped file maps the **keyboard**; its commented block is the
full gamepad alternative. A file with a mistake is ignored whole, with one line on stderr.

## What works

Super Mario 64 renders, plays audio and runs at full speed, with keyboard input through
SDL3; a gamepad works after swapping in the commented block in `Config/input.yaml`. The
window is fixed at 640x480 and the mouse cursor is never captured.

Two limits worth knowing: the interpreter is the only CPU core, because Apple Silicon
refuses the writable-and-executable memory the dynamic recompiler needs; and there is
no settings UI, so configuration means editing `Bin/macOS/Config/Project64.cfg`.

## Diagnostics

With no UI, three environment variables are the way to see inside a running build:

- `PJ64_TRACE='Glide64=debug,AudioDriver=verbose'` raises trace levels for the named
  modules — or a bare level for all of them — and echoes to stderr.
- `PJ64_FRAME_DUMP=/tmp/frame.ppm`, with optional `PJ64_FRAME_DUMP_AT=400`, writes one
  frame from the back buffer as a binary PPM.
- `PJ64_FRAME_DUMP_MIN_NONBLACK=<percent>`, with optional `PJ64_FRAME_DUMP_MAX=<frame>`,
  makes the dump wait for the first frame whose non-black share clears the percentage
  instead of writing unconditionally at `PJ64_FRAME_DUMP_AT`.

## License

GPL-2.0, see [license.md](license.md). Working on this code with a coding agent?
[AGENTS.md](AGENTS.md) has the architecture notes and the traps.
