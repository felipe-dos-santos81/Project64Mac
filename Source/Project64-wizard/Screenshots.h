// Project64 - A Nintendo 64 emulator
// Renders the wizard's screens offscreen for Docs/UserGuide.md. See Screenshots.cpp.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

// Walks a fixed tour through the real screens and writes one PNG per stop into Dir, which
// must exist. No window, no SDL_Init, no camera. Returns 0 when all fourteen files were
// written, 1 after one line on stderr otherwise.
int WizardScreenshots(const char * Dir);
