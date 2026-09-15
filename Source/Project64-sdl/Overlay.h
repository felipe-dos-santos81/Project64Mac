// Project64 - A Nintendo 64 emulator
// The mouse overlay: the panel of slots below the game and the direction guide over it.
// Drawn by the render window on the emulation thread right before each present, with the
// GL 2.1 compatibility context current. Leaves every piece of GL state as it found it.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

struct PointerState;

// Viewport is the renderer's GL viewport (x, y, w, h) in drawable pixels: the game image
// is that rectangle and the window is (x + w) by (y + h), so the y rows below the game are
// the panel. GuideHidden skips the guide over the game (PJ64_OVERLAY=0); the panel is the
// controls and always draws.
void OverlayDraw(const PointerState * State, const int Viewport[4], bool GuideHidden);
