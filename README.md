# Project64 for Apple Silicon

<p align="center">
  <img src="./Docs/img/screen.png" alt="logo" width="1024" />
</p>

A fork of [Project64](https://github.com/project64/project64), simplified down to a
single platform: it builds and runs only on macOS / arm64, using SDL3 for the window,
input and audio, and OpenGL for rendering. Everything that existed only for Windows,
Linux or Android has been removed, and this tree does not track upstream.

Built for educational purposes only — it exists to study how the emulator works on
Apple Silicon, not as a maintained release.

## Build

Prerequisites: Xcode command line tools plus Homebrew SDL3 (3.4 or newer), pkg-config and
yaml-cpp (`brew install sdl3 pkg-config yaml-cpp`).

```sh
make -j8 all              # core, four plugins, frontend, wizard, and the data beside the binary
make test                 # smoke test: version string and plugin exports
make run rom=Roms/game.z64
```

`make help` lists every target. No game data ships with this repository; you supply your
own ROM files.

## Using it

**[The user guide](Docs/UserGuide.md)** covers everything from here: adding ROMs, the
built-in controls, running several games in a grid, custom bindings, the binding wizard
(with pictures), playing with a mouse panel, playing with your face, per-game layouts,
and what to do when something goes wrong.

## What works

Super Mario 64 renders, plays audio and runs at full speed. Keyboard, gamepad, a
one-button mouse with optional face gestures, and the face alone all work as controllers.

Two limits worth knowing: the interpreter is the only CPU core, because Apple Silicon
refuses the writable-and-executable memory the dynamic recompiler needs; and there is no
settings UI, so configuration means editing `Bin/macOS/Config/Project64.cfg`.

## License

GPL-2.0, see [license.md](license.md). Working on this code with a coding agent?
[AGENTS.md](AGENTS.md) has the architecture notes and the traps.
