// Project64 - A Nintendo 64 emulator
// Tests for GestureClassifier with synthetic samples. No camera, no SDL.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "FaceGestures.h"

#include <stdio.h>
#include <string.h>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

static const double kFrame = 1.0 / 30.0;

// A sample with every measure at zero.
static GestureSample Sample(bool Face, double T)
{
    GestureSample S;
    S.FaceFound = Face;
    for (int i = 0; i < FACE_MEASURE_COUNT; i++) S.M[i] = 0.0f;
    S.Time = T;
    return S;
}

// Feed N frames of one brow/yaw pair, every other measure at zero, returning the last result.
static uint32_t Feed(GestureClassifier & C, double & T, int Frames, bool Face, float Brow, float Yaw)
{
    uint32_t Last = 0;
    for (int i = 0; i < Frames; i++)
    {
        T += kFrame;
        GestureSample S = Sample(Face, T);
        S.M[FACE_BROW] = Brow;
        S.M[FACE_YAW] = Yaw;
        Last = C.Update(S);
    }
    return Last;
}

// Feed N frames of one full sample (its Time is overwritten per frame), returning the last result.
static uint32_t FeedSample(GestureClassifier & C, double & T, int Frames, GestureSample S, bool HeadStick = false)
{
    uint32_t Last = 0;
    for (int i = 0; i < Frames; i++)
    {
        T += kFrame;
        S.Time = T;
        Last = C.Update(S, HeadStick);
    }
    return Last;
}

int main()
{
    GestureThresholds Th;
    CHECK(Th.Brow == 0.035f && Th.Yaw == 0.25f && Th.ReleaseFraction == 0.6f);
    CHECK(Th.Pitch == 0.20f && Th.Roll == 0.25f && Th.Mouth == 0.06f && Th.Smile == 0.05f && Th.Eye == 0.12f);
    CHECK(Th.StickYaw == 0.26f && Th.StickPitch == 0.17f);
    CHECK(Th.BaselineSeconds == 5.0 && Th.DebounceFrames == 2 && Th.NoFaceSeconds == 0.5);
    CHECK(POINTER_GESTURE_COUNT == 11);
    CHECK(PointerGestureFromName("wink-right") == POINTER_GESTURE_WINK_RIGHT);
    CHECK(PointerGestureFromName("mouth-open") == (1u << 7));
    CHECK(PointerGestureFromName("wink") == 0);
    CHECK(PointerGestureIndex(POINTER_GESTURE_TILT_LEFT) == 5);
    CHECK(strcmp(PointerGestureName(3), "head-up") == 0);
    CHECK(strcmp(PointerGestureName(11), "") == 0);
    CHECK(strcmp(PointerGestureTag(0), "Br") == 0 && strcmp(PointerGestureTag(10), "W>") == 0);
    CHECK((POINTER_GESTURE_HEAD_DIRECTIONS & POINTER_GESTURE_HEAD_UP) != 0);
    CHECK((POINTER_GESTURE_HEAD_DIRECTIONS & POINTER_GESTURE_TILT_LEFT) == 0);
    {
        // Baseline(FaceMeasure) is the general accessor; the two named ones still work.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(C.Baseline(FACE_BROW) == C.BrowBaseline());
        CHECK(C.Baseline(FACE_YAW) == C.YawBaseline());
        CHECK(C.StickX() == 0 && C.StickY() == 0);
    }

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

    // A face at rest for the new measures: mouth nearly shut, a resting lip width, both
    // eyes open at the same aperture.
    auto Rest = [](double T) {
        GestureSample S = Sample(true, T);
        S.M[FACE_BROW] = 0.10f;
        S.M[FACE_MOUTH] = 0.02f;
        S.M[FACE_SMILE] = 0.50f;
        S.M[FACE_EYE_LEFT] = 0.30f;
        S.M[FACE_EYE_RIGHT] = 0.30f;
        return S;
    };
    {
        // Mouth and smile each set after two frames and clear after two at rest.
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0));
        GestureSample S = Rest(0);
        S.M[FACE_MOUTH] = 0.09f;                                  // +0.07 > 0.06
        CHECK(FeedSample(C, T, 1, S) == 0);
        CHECK(FeedSample(C, T, 1, S) == POINTER_GESTURE_MOUTH_OPEN);
        S.M[FACE_SMILE] = 0.56f;                                  // +0.06 > 0.05
        CHECK(FeedSample(C, T, 2, S) == (POINTER_GESTURE_MOUTH_OPEN | POINTER_GESTURE_SMILE));
        CHECK(FeedSample(C, T, 2, Rest(0)) == 0);
        // The mouth baseline was frozen during the hold.
        CHECK(C.Baseline(FACE_MOUTH) < 0.025f);
    }
    {
        // Pitch: nose up sets head-up, nose down sets head-down. Roll: left ear down is
        // tilt-left, right ear down is tilt-right.
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0));
        GestureSample S = Rest(0);
        S.M[FACE_PITCH] = 0.25f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_HEAD_UP);
        S.M[FACE_PITCH] = -0.25f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_HEAD_DOWN);
        S.M[FACE_PITCH] = 0.0f;
        S.M[FACE_ROLL] = -0.30f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_TILT_LEFT);
        S.M[FACE_ROLL] = 0.30f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_TILT_RIGHT);
        // Roll and yaw are independent: a tilt with a turn reports both.
        S.M[FACE_YAW] = -0.30f;
        CHECK(FeedSample(C, T, 2, S) == (POINTER_GESTURE_TILT_RIGHT | POINTER_GESTURE_HEAD_LEFT));
    }
    {
        // Winks: one eye closing while the other stays open. A blink is neither.
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0));
        GestureSample S = Rest(0);
        S.M[FACE_EYE_LEFT] = 0.15f;                               // -0.15 beyond 0.12
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_WINK_LEFT);
        CHECK(FeedSample(C, T, 2, Rest(0)) == 0);
        S = Rest(0);
        S.M[FACE_EYE_RIGHT] = 0.15f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_WINK_RIGHT);
        CHECK(FeedSample(C, T, 2, Rest(0)) == 0);
        S = Rest(0);
        S.M[FACE_EYE_LEFT] = 0.15f;
        S.M[FACE_EYE_RIGHT] = 0.15f;                              // blink
        CHECK(FeedSample(C, T, 5, S) == 0);
        CHECK(FeedSample(C, T, 2, Rest(0)) == 0);
        // A wink that turns into a blink releases: the other eye closing past its release
        // level (0.12 * 0.6 = 0.072 below baseline) ends the wink within two frames.
        S = Rest(0);
        S.M[FACE_EYE_LEFT] = 0.15f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_WINK_LEFT);
        S.M[FACE_EYE_RIGHT] = 0.20f;                              // -0.10: past release
        CHECK(FeedSample(C, T, 2, S) == 0);
        // Each eye baseline froze on its own closed condition throughout, even while blink
        // suppression zeroed its wink channel: an unfrozen run drifts to about 0.294 here,
        // so 0.298 discriminates.
        CHECK(C.Baseline(FACE_EYE_LEFT) > 0.298f && C.Baseline(FACE_EYE_RIGHT) > 0.298f);
    }
    {
        // A custom mouth threshold is honoured.
        GestureThresholds Loose;
        Loose.Mouth = 0.01f;
        Loose.DebounceFrames = 1;
        GestureClassifier C(Loose);
        double T = 0;
        FeedSample(C, T, 60, Rest(0));
        GestureSample S = Rest(0);
        S.M[FACE_MOUTH] = 0.035f;
        CHECK(FeedSample(C, T, 1, S) == POINTER_GESTURE_MOUTH_OPEN);
    }

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: face gestures\n");
    return 0;
}
