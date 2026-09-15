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
