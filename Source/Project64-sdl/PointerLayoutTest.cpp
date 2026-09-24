// Project64 - A Nintendo 64 emulator
// Tests for PointerLayout. No window and no SDL init; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include "UnitTest.h"

#include <stdio.h>
#include <string.h>

// True when (X, Y) evaluates to Zone in the 640x640 window.
static bool ZoneAt(float X, float Y, int Zone)
{
    return PointerLayoutEvaluate(X, Y, 640, 640, true).Zone == Zone;
}

// One poll at (X, Y) in the 640x640 window through the gate and the settle, in the order
// GetKeys runs them. Returns the evaluated zone.
static int SettlePoll(PointerGate * G, PointerSettle * S, float X, float Y,
                      const PointerSettleRule & Rule = PointerSettleRule())
{
    PointerEval E = PointerLayoutEvaluate(X, Y, 640, 640, true);
    PointerGateStick(G, &E, X, Y, POINTER_FLICK_PX);
    PointerSettleStep(S, E, X, Y, Rule);
    return E.Zone;
}

void RunPointerLayoutTests()
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

    // Head-digital snap: reads the pair, calls PointerQuadrant the same way the plugin
    // does, and writes back the snapped pair. All four quadrants and the centred case.
    int8_t Sx = 10, Sy = 50; PointerSnapToQuadrant(&Sx, &Sy);
    CHECK(Sx == 0 && Sy == 80);       // quadrant 0 (up)
    Sx = 50; Sy = 10; PointerSnapToQuadrant(&Sx, &Sy);
    CHECK(Sx == 80 && Sy == 0);       // quadrant 1 (right)
    Sx = 10; Sy = -50; PointerSnapToQuadrant(&Sx, &Sy);
    CHECK(Sx == 0 && Sy == -80);      // quadrant 2 (down)
    Sx = -50; Sy = 10; PointerSnapToQuadrant(&Sx, &Sy);
    CHECK(Sx == -80 && Sy == 0);      // quadrant 3 (left)
    Sx = 0; Sy = 0; PointerSnapToQuadrant(&Sx, &Sy);
    CHECK(Sx == 0 && Sy == 0);        // centred
    // The diagonal tie-break must agree with PointerQuadrant's own convention: derive the
    // expected axis from PointerQuadrant itself rather than restating which way it snaps.
    int8_t Dx = 56, Dy = 58;
    const int DiagQ = PointerQuadrant(Dx, Dy);
    PointerSnapToQuadrant(&Dx, &Dy);
    CHECK(Dx == (int8_t)(DiagQ == 1 ? 80 : DiagQ == 3 ? -80 : 0));
    CHECK(Dy == (int8_t)(DiagQ == 0 ? 80 : DiagQ == 2 ? -80 : 0));

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

    // A window resized or taken full screen shows the launch-size picture scaled to fit,
    // centred, keeping its shape; PointerFitToBase maps the cursor back into that picture.
    {
        float Bx = -1.0f, By = -1.0f;

        // Same size: the identity, inside.
        CHECK(PointerFitToBase(640, 640, 640, 640, 123.0f, 456.0f, &Bx, &By));
        CHECK(Bx == 123.0f && By == 456.0f);

        // Twice the size.
        CHECK(PointerFitToBase(1280, 1280, 640, 640, 640.0f, 1000.0f, &Bx, &By));
        CHECK(Bx == 320.0f && By == 500.0f);

        // Half the size, the minimum.
        CHECK(PointerFitToBase(320, 320, 640, 640, 160.0f, 250.0f, &Bx, &By));
        CHECK(Bx == 320.0f && By == 500.0f);

        // Full screen 1920x1080 over 640x640: scale 1.6875, a 1080-wide picture, bars of 420
        // on each side. The picture's corners are inside, edges included.
        CHECK(PointerFitToBase(1920, 1080, 640, 640, 420.0f, 0.0f, &Bx, &By));
        CHECK(Bx == 0.0f && By == 0.0f);
        CHECK(PointerFitToBase(1920, 1080, 640, 640, 1500.0f, 1080.0f, &Bx, &By));
        CHECK(Bx == 640.0f && By == 640.0f);

        // On the left bar: outside, and clamped to the picture's left edge.
        CHECK(!PointerFitToBase(1920, 1080, 640, 640, 100.0f, 540.0f, &Bx, &By));
        CHECK(Bx == 0.0f && By == 320.0f);

        // On the right bar: outside, clamped to the right edge.
        CHECK(!PointerFitToBase(1920, 1080, 640, 640, 1800.0f, 540.0f, &Bx, &By));
        CHECK(Bx == 640.0f && By == 320.0f);

        // A 640x480 game-only picture in a 1920x1080 screen: scale 2.25, bars of 240.
        CHECK(PointerFitToBase(1920, 1080, 640, 480, 960.0f, 540.0f, &Bx, &By));
        CHECK(Bx == 320.0f && By == 240.0f);

        // Sizes that are not positive map nothing.
        CHECK(!PointerFitToBase(0, 640, 640, 640, 10.0f, 10.0f, &Bx, &By));
        CHECK(!PointerFitToBase(640, -1, 640, 640, 10.0f, 10.0f, &Bx, &By));
        CHECK(!PointerFitToBase(640, 640, 0, 640, 10.0f, 10.0f, &Bx, &By));
        CHECK(!PointerFitToBase(640, 640, 640, -5, 10.0f, 10.0f, &Bx, &By));
    }

    // Settling: nine polls within 8 px in the game image record the gated stick, once.
    // (400,240) is half tilt right: X 40, Y 0.
    {
        PointerGate G = { false, 0, 0, 0, 0 };
        PointerSettle S = {};
        CHECK(!S.HaveSettled);                        // nothing before any rest
        for (int i = 0; i < 8; i++)
        {
            SettlePoll(&G, &S, i % 2 == 0 ? 400.0f : 405.0f, 240);   // jitter inside the radius
            CHECK(!S.JustSettled);
        }
        CHECK(!S.HaveSettled);
        SettlePoll(&G, &S, 400, 240);
        CHECK(S.JustSettled && S.HaveSettled);
        CHECK(S.SettledX == 40 && S.SettledY == 0);
        SettlePoll(&G, &S, 400, 240);
        CHECK(!S.JustSettled && S.HaveSettled);       // resting on does not record again

        // Leaving the image resets the count and keeps the tilt; resting in the panel never
        // settles, however long.
        for (int i = 0; i < 20; i++)
        {
            CHECK(SettlePoll(&G, &S, 256, 512) == 9); // mid2
            CHECK(!S.JustSettled);
        }
        CHECK(S.HaveSettled && S.SettledX == 40 && S.SettledY == 0);

        // Back in the image at the top edge: the flick from the panel reads neutral on its
        // first poll (the gate), then the rest records full tilt up.
        for (int i = 0; i < 8; i++)
        {
            SettlePoll(&G, &S, 320, 80);
            CHECK(!S.JustSettled);
        }
        SettlePoll(&G, &S, 320, 80);
        CHECK(S.JustSettled && S.SettledX == 0 && S.SettledY == 80);
    }

    // A slow drag from the centre to the panel, 2 px a poll, never rests, so it never records
    // the backward tilt the bottom edge reads as.
    {
        PointerGate G = { false, 0, 0, 0, 0 };
        PointerSettle S = {};
        for (float Y = 240; Y < 480; Y += 2)
        {
            SettlePoll(&G, &S, 320, Y);
        }
        CHECK(!S.HaveSettled);
    }

    // A flick from a rest to the panel keeps the tilt the rest recorded.
    {
        PointerGate G = { false, 0, 0, 0, 0 };
        PointerSettle S = {};
        for (int i = 0; i < 9; i++)
        {
            SettlePoll(&G, &S, 400, 240);
        }
        CHECK(S.JustSettled && S.SettledX == 40);
        SettlePoll(&G, &S, 330, 470);                 // one 239 px jump inside the image
        CHECK(SettlePoll(&G, &S, 320, 512) == 10);    // mid3
        CHECK(S.HaveSettled && S.SettledX == 40 && S.SettledY == 0);
    }

    // A radius of 0 never settles.
    {
        PointerGate G = { false, 0, 0, 0, 0 };
        PointerSettle S = {};
        PointerSettleRule Never;
        Never.Radius = 0.0f;
        for (int i = 0; i < 20; i++)
        {
            SettlePoll(&G, &S, 400, 240, Never);
        }
        CHECK(!S.HaveSettled);
    }

    // The click step with no toggles and no hold is the old latch: the zone under the press
    // stays held wherever the cursor goes until release, and a press on a gap does nothing.
    {
        const PointerSettle None = {};
        PointerClicks C;
        CHECK(C.Latched == POINTER_ZONE_NONE && C.Toggled == 0u && !C.Holding && !C.PrevButton);
        PointerClickStep(&C, true, 8, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == 8);
        PointerClickStep(&C, true, POINTER_ZONE_GAME, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == 8);
        PointerClickStep(&C, false, POINTER_ZONE_GAME, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == POINTER_ZONE_NONE);
        PointerClickStep(&C, true, POINTER_ZONE_NONE, 0u, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == POINTER_ZONE_NONE && C.Toggled == 0u && !C.Holding);
    }

    // A toggle slot flips on the press edge, ignores the release, and leaves the button free.
    {
        const PointerSettle None = {};
        const uint32_t Mid2 = 1u << 9;
        PointerClicks C;
        PointerClickStep(&C, true, 9, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Toggled == Mid2 && C.Latched == POINTER_ZONE_NONE);
        PointerClickStep(&C, true, 9, Mid2, POINTER_ZONE_NONE, None);   // still down: no second flip
        CHECK(C.Toggled == Mid2);
        PointerClickStep(&C, false, 9, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Toggled == Mid2);                                        // the release does nothing
        PointerClickStep(&C, true, POINTER_ZONE_GAME, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == POINTER_ZONE_GAME && C.Toggled == Mid2);      // Z and A together
        PointerClickStep(&C, false, POINTER_ZONE_GAME, Mid2, POINTER_ZONE_NONE, None);
        PointerClickStep(&C, true, 9, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Toggled == 0u);                                          // the next press lets go
    }

    // Dragging onto a toggle slot with the button already down does not flip it.
    {
        const PointerSettle None = {};
        const uint32_t Mid2 = 1u << 9;
        PointerClicks C;
        PointerClickStep(&C, true, 8, Mid2, POINTER_ZONE_NONE, None);
        PointerClickStep(&C, true, 9, Mid2, POINTER_ZONE_NONE, None);
        CHECK(C.Latched == 8 && C.Toggled == 0u);
    }

    // The hold slot (mid5, zone 12): a press copies the settled tilt, or neutral before any
    // settle; a second press lets go; other presses keep it; a settle in the image ends it.
    {
        PointerSettle S = {};
        PointerClicks C;
        PointerClickStep(&C, true, 12, 0u, 12, S);
        CHECK(C.Holding && C.HeldX == 0 && C.HeldY == 0 && C.Latched == POINTER_ZONE_NONE);
        PointerClickStep(&C, false, 12, 0u, 12, S);
        PointerClickStep(&C, true, 12, 0u, 12, S);
        CHECK(!C.Holding);
        S.HaveSettled = true;
        S.SettledX = 40;
        S.SettledY = -20;
        PointerClickStep(&C, false, 12, 0u, 12, S);
        PointerClickStep(&C, true, 12, 0u, 12, S);
        CHECK(C.Holding && C.HeldX == 40 && C.HeldY == -20);
        PointerClickStep(&C, false, 10, 0u, 12, S);
        PointerClickStep(&C, true, 10, 0u, 12, S);                       // B on mid3 while holding
        CHECK(C.Holding && C.Latched == 10);
        PointerClickStep(&C, false, POINTER_ZONE_GAME, 0u, 12, S);
        PointerClickStep(&C, true, POINTER_ZONE_GAME, 0u, 12, S);        // A before the cursor rests
        CHECK(C.Holding && C.Latched == POINTER_ZONE_GAME);
        S.JustSettled = true;
        PointerClickStep(&C, true, POINTER_ZONE_GAME, 0u, 12, S);
        CHECK(!C.Holding);
    }

    // PJ64_POINTER_SETTLE: "0" or "<px>,<polls>", both positive; anything else changes nothing.
    {
        PointerSettleRule Rule;
        CHECK(PointerParseSettle("12,20", &Rule) && Rule.Radius == 12.0f && Rule.Polls == 20);
        CHECK(PointerParseSettle("0", &Rule) && Rule.Radius == 0.0f && Rule.Polls == 20);
        Rule.Radius = 5.0f;
        Rule.Polls = 7;
        CHECK(!PointerParseSettle("", &Rule));
        CHECK(!PointerParseSettle("abc", &Rule));
        CHECK(!PointerParseSettle("8", &Rule));
        CHECK(!PointerParseSettle("8,0", &Rule));
        CHECK(!PointerParseSettle("-1,9", &Rule));
        CHECK(!PointerParseSettle("8,9x", &Rule));
        CHECK(!PointerParseSettle("nan,9", &Rule));
        CHECK(!PointerParseSettle("inf,9", &Rule));
        CHECK(Rule.Radius == 5.0f && Rule.Polls == 7);
    }

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

}
