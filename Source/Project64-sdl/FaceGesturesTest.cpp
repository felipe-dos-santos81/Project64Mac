// Project64 - A Nintendo 64 emulator
// Tests for GestureClassifier with synthetic samples. No camera, no SDL.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "FaceGestures.h"

#include <stdio.h>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

static const double kFrame = 1.0 / 30.0;

// Feed N frames of one sample, returning the last result.
static uint32_t Feed(GestureClassifier & C, double & T, int Frames, bool Face, float Brow, float Yaw)
{
    uint32_t Last = 0;
    for (int i = 0; i < Frames; i++)
    {
        T += kFrame;
        Last = C.Update(GestureSample{ Face, Brow, Yaw, T });
    }
    return Last;
}

int main()
{
    GestureThresholds Th;
    CHECK(Th.Brow == 0.035f && Th.Yaw == 0.25f && Th.ReleaseFraction == 0.6f);
    CHECK(Th.BaselineSeconds == 5.0 && Th.DebounceFrames == 2 && Th.NoFaceSeconds == 0.5);

    {
        // Debounce: one raised frame is nothing; the second sets the bit.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);                       // settle: 2 s at rest
        CHECK(C.BrowBaseline() > 0.099f && C.BrowBaseline() < 0.101f);
        CHECK(Feed(C, T, 1, true, 0.15f, 0.0f) == 0);
        CHECK(Feed(C, T, 1, true, 0.15f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        // Hysteresis: dropping to just above the release level keeps it set...
        CHECK(Feed(C, T, 5, true, 0.10f + 0.035f * 0.6f + 0.002f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        // ...and going below it clears after two frames.
        CHECK(Feed(C, T, 1, true, 0.10f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        CHECK(Feed(C, T, 1, true, 0.10f, 0.0f) == 0);
    }
    {
        // The baseline is frozen while the gesture is held: ten seconds raised, still set.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(Feed(C, T, 300, true, 0.15f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        CHECK(C.BrowBaseline() < 0.105f);
    }
    {
        // The baseline follows a slow drift with no gesture firing.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        bool Fired = false;
        for (int i = 0; i < 600; i++)                             // 20 s, 0.10 -> 0.12
        {
            if (Feed(C, T, 1, true, 0.10f + 0.02f * (float)i / 600.0f, 0.0f) != 0) Fired = true;
        }
        CHECK(!Fired);
        CHECK(C.BrowBaseline() > 0.112f);                         // lags the ramp by rate * tau = 0.005
    }
    {
        // Yaw: negative is head-left, positive is head-right; both need debounce.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(Feed(C, T, 2, true, 0.10f, -0.30f) == POINTER_GESTURE_HEAD_LEFT);
        CHECK(Feed(C, T, 2, true, 0.10f, 0.0f) == 0);
        CHECK(Feed(C, T, 2, true, 0.10f, 0.30f) == POINTER_GESTURE_HEAD_RIGHT);
        // Yaw baseline is frozen while a head bit is held.
        Feed(C, T, 300, true, 0.10f, 0.30f);
        CHECK(C.YawBaseline() < 0.02f);
        // Brows and a head turn at once are both reported.
        CHECK(Feed(C, T, 2, true, 0.15f, 0.30f) == (POINTER_GESTURE_EYEBROWS | POINTER_GESTURE_HEAD_RIGHT));
    }
    {
        // No face for longer than the timeout clears everything; a brief dropout does not.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(Feed(C, T, 2, true, 0.15f, 0.0f) == POINTER_GESTURE_EYEBROWS);
        CHECK(Feed(C, T, 5, false, 0.0f, 0.0f) == POINTER_GESTURE_EYEBROWS);   // 0.17 s without a face
        CHECK(Feed(C, T, 15, false, 0.0f, 0.0f) == 0);                          // 0.67 s: cleared
        // The baseline survives the dropout, so the next raise fires at once.
        CHECK(C.BrowBaseline() > 0.099f && C.BrowBaseline() < 0.101f);
        CHECK(Feed(C, T, 2, true, 0.15f, 0.0f) == POINTER_GESTURE_EYEBROWS);
    }
    {
        // A custom threshold set is honoured.
        GestureThresholds Loose;
        Loose.Brow = 0.01f;
        Loose.DebounceFrames = 1;
        GestureClassifier C(Loose);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(Feed(C, T, 1, true, 0.115f, 0.0f) == POINTER_GESTURE_EYEBROWS);
    }

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: face gestures\n");
    return 0;
}
