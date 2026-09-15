// Project64 - A Nintendo 64 emulator
// Tests for PointerLayout. No window and no SDL init; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <string.h>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

int main()
{
    // A 640x480 window: cells are 160x120, centre (320,240), R = 120.
    const int W = 640, H = 480;
    CHECK(PointerStickRadius(W, H) == 120.0f);

    // Zone names round-trip and cover the whole table.
    CHECK(strcmp(PointerZoneName(0), "top1") == 0);
    CHECK(strcmp(PointerZoneName(3), "top4") == 0);
    CHECK(strcmp(PointerZoneName(4), "left2") == 0);
    CHECK(strcmp(PointerZoneName(5), "right2") == 0);
    CHECK(strcmp(PointerZoneName(6), "left3") == 0);
    CHECK(strcmp(PointerZoneName(7), "right3") == 0);
    CHECK(strcmp(PointerZoneName(8), "bottom1") == 0);
    CHECK(strcmp(PointerZoneName(11), "bottom4") == 0);
    CHECK(strcmp(PointerZoneName(POINTER_ZONE_CENTRE), "centre") == 0);
    for (int i = 0; i < POINTER_ZONE_COUNT; i++)
    {
        CHECK(PointerZoneFromName(PointerZoneName(i)) == i);
    }
    CHECK(PointerZoneFromName("middle") == POINTER_ZONE_NONE);
    CHECK(PointerZoneFromName("") == POINTER_ZONE_NONE);

    // Cell lookup at corners and edges.
    CHECK(PointerLayoutEvaluate(0, 0, W, H, true).Zone == 0);          // top1
    CHECK(PointerLayoutEvaluate(639, 0, W, H, true).Zone == 3);        // top4
    CHECK(PointerLayoutEvaluate(100, 200, W, H, true).Zone == 4);      // left2
    CHECK(PointerLayoutEvaluate(600, 200, W, H, true).Zone == 5);      // right2
    CHECK(PointerLayoutEvaluate(100, 300, W, H, true).Zone == 6);      // left3
    CHECK(PointerLayoutEvaluate(600, 300, W, H, true).Zone == 7);      // right3
    CHECK(PointerLayoutEvaluate(0, 479, W, H, true).Zone == 8);        // bottom1
    CHECK(PointerLayoutEvaluate(639, 479, W, H, true).Zone == 11);     // bottom4
    CHECK(PointerLayoutEvaluate(320, 240, W, H, true).Zone == POINTER_ZONE_CENTRE);
    CHECK(PointerLayoutEvaluate(160, 120, W, H, true).Zone == POINTER_ZONE_CENTRE);  // inner block's top-left pixel
    CHECK(PointerLayoutEvaluate(159, 120, W, H, true).Zone == 4);      // one pixel left of it is left2

    // Stick: centre is neutral; the dead zone is 0.1 R = 12 px.
    PointerEval E = PointerLayoutEvaluate(320, 240, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(320 + 11, 240, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    // At the ring edge the tilt is full.
    E = PointerLayoutEvaluate(320 + 120, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_CENTRE && E.StickX == 80 && E.StickY == 0);
    // Half way is half tilt.
    E = PointerLayoutEvaluate(320 + 60, 240, W, H, true);
    CHECK(E.StickX == 40 && E.StickY == 0);
    // Beyond the ring but still in the inner block clamps to full tilt.
    E = PointerLayoutEvaluate(320 + 150, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_CENTRE && E.StickX == 80 && E.StickY == 0);
    // Screen up is stick up (positive N64 Y).
    E = PointerLayoutEvaluate(320, 240 - 119, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_CENTRE && E.StickX == 0 && E.StickY > 78);
    E = PointerLayoutEvaluate(320, 240 + 119, W, H, true);
    CHECK(E.StickY < -78);
    // A diagonal beyond the ring is clamped to unit length, not per axis.
    E = PointerLayoutEvaluate(320 + 120, 240 - 119, W, H, true);
    CHECK(E.StickX > 50 && E.StickX < 62 && E.StickY > 50 && E.StickY < 62);

    // In an outer cell the stick is neutral.
    E = PointerLayoutEvaluate(600, 300, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    // Outside the window or unfocused: no zone, neutral stick.
    E = PointerLayoutEvaluate(320, 240, W, H, false);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(-5, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE);
    E = PointerLayoutEvaluate(320, 480, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE);
    // A degenerate window is safe.
    E = PointerLayoutEvaluate(0, 0, 0, 0, true);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0);

    // Cell rectangles tile the window.
    float X0, Y0, X1, Y1;
    PointerZoneRect(0, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 0 && Y0 == 0 && X1 == 160 && Y1 == 120);
    PointerZoneRect(11, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 480 && Y0 == 360 && X1 == 640 && Y1 == 480);
    PointerZoneRect(POINTER_ZONE_CENTRE, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 160 && Y0 == 120 && X1 == 480 && Y1 == 360);

    // Gesture names.
    CHECK(PointerGestureFromName("eyebrows") == POINTER_GESTURE_EYEBROWS);
    CHECK(PointerGestureFromName("head-left") == POINTER_GESTURE_HEAD_LEFT);
    CHECK(PointerGestureFromName("head-right") == POINTER_GESTURE_HEAD_RIGHT);
    CHECK(PointerGestureFromName("wink") == 0);
    CHECK(PointerGestureIndex(POINTER_GESTURE_EYEBROWS) == 0);
    CHECK(PointerGestureIndex(POINTER_GESTURE_HEAD_RIGHT) == 2);

    // The seqlock round-trips a sample.
    PointerState State;
    memset(&State, 0, sizeof(State));
    PointerSample In = { 12.5f, 34.0f, 640, 480, true, true };
    PointerPublish(&State, In);
    PointerSample Out;
    PointerSnapshot(&State, &Out);
    CHECK(Out.X == 12.5f && Out.Y == 34.0f && Out.W == 640 && Out.H == 480 && Out.Inside && Out.Button);
    CHECK(State.Seq == 2);

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: pointer layout\n");
    return 0;
}
