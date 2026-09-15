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

// True when (X, Y) evaluates to Zone in the 640x640 window.
static bool ZoneAt(float X, float Y, int Zone)
{
    return PointerLayoutEvaluate(X, Y, 640, 640, true).Zone == Zone;
}

int main()
{
    // The 640x640 window: game image 640x480, centre (320,240), R = 160, panel from y=480.
    const int W = 640, H = 640;
    CHECK(POINTER_ZONE_COUNT == 14);
    CHECK(POINTER_PANEL_HEIGHT == 160);
    CHECK(PointerGameHeight(H) == 480);
    CHECK(PointerStickRadius(W, H) == 160.0f);

    // Zone names round-trip and cover the whole table.
    CHECK(strcmp(PointerZoneName(0), "pad-up") == 0);
    CHECK(strcmp(PointerZoneName(1), "pad-down") == 0);
    CHECK(strcmp(PointerZoneName(2), "pad-left") == 0);
    CHECK(strcmp(PointerZoneName(3), "pad-right") == 0);
    CHECK(strcmp(PointerZoneName(4), "c-up") == 0);
    CHECK(strcmp(PointerZoneName(7), "c-right") == 0);
    CHECK(strcmp(PointerZoneName(8), "mid1") == 0);
    CHECK(strcmp(PointerZoneName(12), "mid5") == 0);
    CHECK(strcmp(PointerZoneName(POINTER_ZONE_GAME), "game") == 0);
    for (int i = 0; i < POINTER_ZONE_COUNT; i++)
    {
        CHECK(PointerZoneFromName(PointerZoneName(i)) == i);
    }
    CHECK(PointerZoneFromName("top4") == POINTER_ZONE_NONE);
    CHECK(PointerZoneFromName("centre") == POINTER_ZONE_NONE);
    CHECK(PointerZoneFromName("") == POINTER_ZONE_NONE);

    // Every slot at its centre.
    CHECK(ZoneAt(80, 512, 0));     // pad-up
    CHECK(ZoneAt(80, 608, 1));     // pad-down
    CHECK(ZoneAt(32, 560, 2));     // pad-left
    CHECK(ZoneAt(128, 560, 3));    // pad-right
    CHECK(ZoneAt(560, 512, 4));    // c-up
    CHECK(ZoneAt(560, 608, 5));    // c-down
    CHECK(ZoneAt(512, 560, 6));    // c-left
    CHECK(ZoneAt(608, 560, 7));    // c-right
    CHECK(ZoneAt(192, 512, 8));    // mid1
    CHECK(ZoneAt(256, 512, 9));    // mid2
    CHECK(ZoneAt(320, 512, 10));   // mid3
    CHECK(ZoneAt(384, 512, 11));   // mid4
    CHECK(ZoneAt(448, 512, 12));   // mid5
    // pad-left's right edge: x=55 is the slot, x=56 is the cross's empty centre.
    CHECK(ZoneAt(55, 560, 2));
    CHECK(ZoneAt(56, 560, POINTER_ZONE_NONE));
    // Gaps: between mid1 and mid2, the panel's top-left corner, the right cross's centre.
    CHECK(ZoneAt(224, 512, POINTER_ZONE_NONE));
    CHECK(ZoneAt(2, 482, POINTER_ZONE_NONE));
    CHECK(ZoneAt(560, 560, POINTER_ZONE_NONE));
    // The whole game image is one zone; the first panel row has no slot.
    CHECK(ZoneAt(0, 0, POINTER_ZONE_GAME));
    CHECK(ZoneAt(639, 479, POINTER_ZONE_GAME));
    CHECK(ZoneAt(320, 240, POINTER_ZONE_GAME));
    CHECK(ZoneAt(320, 480, POINTER_ZONE_NONE));

    // Stick: centre is neutral; the dead zone is 0.1 R = 16 px.
    PointerEval E = PointerLayoutEvaluate(320, 240, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(320 + 15, 240, W, H, true);
    CHECK(E.StickX == 0 && E.StickY == 0);
    // At the ring the tilt is full.
    E = PointerLayoutEvaluate(320 + 160, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_GAME && E.StickX == 80 && E.StickY == 0);
    // Half way is half tilt.
    E = PointerLayoutEvaluate(320 + 80, 240, W, H, true);
    CHECK(E.StickX == 40 && E.StickY == 0);
    // Beyond the ring, still in the image, clamps to full tilt.
    E = PointerLayoutEvaluate(320 + 300, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_GAME && E.StickX == 80 && E.StickY == 0);
    // Screen up is stick up (positive N64 Y).
    E = PointerLayoutEvaluate(320, 240 - 159, W, H, true);
    CHECK(E.StickX == 0 && E.StickY > 78);
    E = PointerLayoutEvaluate(320, 240 + 159, W, H, true);
    CHECK(E.StickY < -78);
    // A diagonal beyond the ring is clamped to unit length, not per axis.
    E = PointerLayoutEvaluate(320 + 160, 240 - 159, W, H, true);
    CHECK(E.StickX > 50 && E.StickX < 62 && E.StickY > 50 && E.StickY < 62);
    // In the panel the stick is neutral, slot or not.
    E = PointerLayoutEvaluate(192, 512, W, H, true);
    CHECK(E.Zone == 8 && E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(320, 600, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0 && E.StickY == 0);
    // Outside the window or unfocused: no zone, neutral stick.
    E = PointerLayoutEvaluate(320, 240, W, H, false);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(-5, 240, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE);
    E = PointerLayoutEvaluate(320, 640, W, H, true);
    CHECK(E.Zone == POINTER_ZONE_NONE);
    // A window too short for the panel, or degenerate, is safe.
    E = PointerLayoutEvaluate(320, 100, 640, 160, true);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0);
    E = PointerLayoutEvaluate(0, 0, 0, 0, true);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0);

    // Slot rectangles: exact positions, inside the panel, no overlaps.
    float X0, Y0, X1, Y1;
    PointerZoneRect(0, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 56 && Y0 == 488 && X1 == 104 && Y1 == 536);
    PointerZoneRect(7, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 584 && Y0 == 536 && X1 == 632 && Y1 == 584);
    PointerZoneRect(12, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 420 && Y0 == 488 && X1 == 476 && Y1 == 536);
    PointerZoneRect(POINTER_ZONE_GAME, W, H, &X0, &Y0, &X1, &Y1);
    CHECK(X0 == 0 && Y0 == 0 && X1 == 640 && Y1 == 480);
    for (int A = 0; A < POINTER_ZONE_GAME; A++)
    {
        float Ax0, Ay0, Ax1, Ay1;
        PointerZoneRect(A, W, H, &Ax0, &Ay0, &Ax1, &Ay1);
        CHECK(Ax0 >= 0 && Ay0 >= 480 && Ax1 <= 640 && Ay1 <= 640);
        for (int B = A + 1; B < POINTER_ZONE_GAME; B++)
        {
            float Bx0, By0, Bx1, By1;
            PointerZoneRect(B, W, H, &Bx0, &By0, &Bx1, &By1);
            const bool Apart = Ax1 <= Bx0 || Bx1 <= Ax0 || Ay1 <= By0 || By1 <= Ay0;
            CHECK(Apart);
        }
    }

    // Quadrants: vertical wins a tie, so 45 degrees is up and 46 is right.
    CHECK(PointerQuadrant(0, 80) == 0);
    CHECK(PointerQuadrant(80, 0) == 1);
    CHECK(PointerQuadrant(0, -80) == 2);
    CHECK(PointerQuadrant(-80, 0) == 3);
    CHECK(PointerQuadrant(0, 0) == -1);
    CHECK(PointerQuadrant(56, 58) == 0);   // 44 degrees from vertical
    CHECK(PointerQuadrant(58, 56) == 1);   // 46 degrees from vertical
    CHECK(PointerQuadrant(56, 56) == 0);
    CHECK(PointerQuadrant(-56, -58) == 2);

    // Flick gate. A 300 px jump inside the image keeps the previous tilt; a 5 px move follows.
    PointerGate G = { false, 0, 0, 0, 0 };
    E = PointerLayoutEvaluate(320, 140, W, H, true);
    PointerGateStick(&G, &E, 320, 140, POINTER_FLICK_PX);
    CHECK(E.StickX == 0 && E.StickY == 50);           // first poll is never gated
    E = PointerLayoutEvaluate(320, 440, W, H, true);
    PointerGateStick(&G, &E, 320, 440, POINTER_FLICK_PX);
    CHECK(E.StickX == 0 && E.StickY == 50);           // jumped 300 px: held
    E = PointerLayoutEvaluate(320, 445, W, H, true);
    PointerGateStick(&G, &E, 320, 445, POINTER_FLICK_PX);
    CHECK(E.StickY == -80);                           // moved 5 px: follows the cursor
    // A jump that lands in the panel reads neutral, gate or not.
    E = PointerLayoutEvaluate(320, 600, W, H, true);
    PointerGateStick(&G, &E, 320, 600, POINTER_FLICK_PX);
    CHECK(E.Zone == POINTER_ZONE_NONE && E.StickX == 0 && E.StickY == 0);
    // Coming back fast from the panel holds neutral until the cursor slows.
    E = PointerLayoutEvaluate(320, 400, W, H, true);
    PointerGateStick(&G, &E, 320, 400, POINTER_FLICK_PX);
    CHECK(E.Zone == POINTER_ZONE_GAME && E.StickX == 0 && E.StickY == 0);
    E = PointerLayoutEvaluate(320, 398, W, H, true);
    PointerGateStick(&G, &E, 320, 398, POINTER_FLICK_PX);
    CHECK(E.StickY == -79);
    // Threshold 0 disables the gate.
    PointerGate Off = { false, 0, 0, 0, 0 };
    E = PointerLayoutEvaluate(320, 140, W, H, true);
    PointerGateStick(&Off, &E, 320, 140, 0.0f);
    E = PointerLayoutEvaluate(320, 440, W, H, true);
    PointerGateStick(&Off, &E, 320, 440, 0.0f);
    CHECK(E.StickY == -80);

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
    PointerSample In = { 12.5f, 34.0f, 640, 640, true, true };
    PointerPublish(&State, In);
    PointerSample Out;
    PointerSnapshot(&State, &Out);
    CHECK(Out.X == 12.5f && Out.Y == 34.0f && Out.W == 640 && Out.H == 640 && Out.Inside && Out.Button);
    CHECK(State.Seq == 2);

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: pointer layout\n");
    return 0;
}
