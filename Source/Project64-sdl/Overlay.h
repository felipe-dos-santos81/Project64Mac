// Project64 - A Nintendo 64 emulator
// The pointer overlay: ring, cells, labels, face status. Drawn by the render window on
// the emulation thread right before each present, with the GL 2.1 compatibility context
// current. Leaves every piece of GL state as it found it.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

struct PointerState;

void OverlayDraw(const PointerState * State, int Width, int Height);
