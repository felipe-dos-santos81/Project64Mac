// Project64 - A Nintendo 64 emulator
// Webcam face gestures through AVFoundation and Vision. Frontend only; the plugin reads
// the eleven gesture bits and the head stick this writes into PointerState. Frames never
// leave memory.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

struct PointerState;

// Asks for camera access, starts capture, and runs face-landmark detection on a private
// queue. Returns false if capture could not start; State->Face already says why. Safe to
// call once; a second call is ignored.
bool FaceTrackerStart(PointerState * State);

// Stops capture. Safe to call without a prior start.
void FaceTrackerStop(void);
