// Project64 - A Nintendo 64 emulator
// The mouse geometry, shared by the input plugin (evaluation) and the overlay (drawing) so
// the two can never disagree about where a slot is. The game image is the stick and the
// zone "game"; the panel below it holds thirteen slots. Pure functions, no SDL.
// Design: Docs/superpowers/specs/2026-09-15-mouse-panel-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum
{
    POINTER_ZONE_NONE = -1,
    POINTER_ZONE_GAME = 13,
};

#define POINTER_PANEL_HEIGHT 160   // rows below the game image, in launch-size pixels
#define POINTER_FLICK_PX 24.0f     // cursor travel per poll above which the stick holds
#define POINTER_SETTLE_PX 8.0f     // a cursor within this distance of its anchor is resting
#define POINTER_SETTLE_POLLS 9     // polls it must rest for to settle (about 150 ms at 60 a second)

// Zone order: the left cross, the right cross, the five middle slots, then the game image.
inline const char * PointerZoneName(int Zone)
{
    static const char * const kNames[14] = {
        "pad-up", "pad-down", "pad-left", "pad-right",
        "c-up", "c-down", "c-left", "c-right",
        "mid1", "mid2", "mid3", "mid4", "mid5",
        "game",
    };
    return (Zone >= 0 && Zone < 14) ? kNames[Zone] : "";
}

inline int PointerZoneFromName(const char * Name)
{
    for (int i = 0; i < 14; i++)
    {
        if (strcmp(Name, PointerZoneName(i)) == 0) return i;
    }
    return POINTER_ZONE_NONE;
}

// Height of the game image: everything above the panel.
inline int PointerGameHeight(int H)
{
    return H - POINTER_PANEL_HEIGHT;
}

// Full-tilt radius: a third of the game image's height (160 px at 640x480).
inline float PointerStickRadius(int W, int H)
{
    (void)W;
    return (float)PointerGameHeight(H) / 3.0f;
}

// Pixel rectangle of a zone, top-left origin. Slots are fixed-size cells hung from the
// panel's top edge: the left cross from the left edge, the right cross from the right edge,
// the middle slots from the left edge (symmetric at W = 640). The centre cell of each cross
// is empty.
inline void PointerZoneRect(int Zone, int W, int H, float * X0, float * Y0, float * X1, float * Y1)
{
    const float Top = (float)PointerGameHeight(H);
    float X = 0, Y = 0, Cw = 48, Ch = 48;
    switch (Zone)
    {
    case 0: X = 56; Y = Top + 8; break;                 // pad-up
    case 1: X = 56; Y = Top + 104; break;               // pad-down
    case 2: X = 8; Y = Top + 56; break;                 // pad-left
    case 3: X = 104; Y = Top + 56; break;               // pad-right
    case 4: X = (float)W - 104; Y = Top + 8; break;     // c-up
    case 5: X = (float)W - 104; Y = Top + 104; break;   // c-down
    case 6: X = (float)W - 152; Y = Top + 56; break;    // c-left
    case 7: X = (float)W - 56; Y = Top + 56; break;     // c-right
    case 8: case 9: case 10: case 11: case 12:          // mid1..mid5
        X = 164 + 64 * (float)(Zone - 8); Y = Top + 8; Cw = 56; break;
    case POINTER_ZONE_GAME:
        X = 0; Y = 0; Cw = (float)W; Ch = Top; break;
    default:
        *X0 = *Y0 = *X1 = *Y1 = 0; return;
    }
    *X0 = X; *Y0 = Y; *X1 = X + Cw; *Y1 = Y + Ch;
}

struct PointerEval
{
    int Zone;
    int8_t StickX, StickY;
};

// X,Y in launch-size pixels from the top left (see PointerFitToBase below for how a
// resized or full-screen window's cursor maps into them). Inside false means "not over
// the picture", which reads as no zone and a neutral stick. Anywhere in the game image is
// the zone "game" with the stick from the offset to its centre; the panel gives its slot
// and a neutral stick; gaps in the panel give no zone.
inline PointerEval PointerLayoutEvaluate(float X, float Y, int W, int H, bool Inside)
{
    PointerEval E = { POINTER_ZONE_NONE, 0, 0 };
    const int Gh = PointerGameHeight(H);
    if (!Inside || W <= 0 || Gh <= 0 || X < 0 || Y < 0 || X >= (float)W || Y >= (float)H)
    {
        return E;
    }
    if (Y >= (float)Gh)
    {
        for (int Zone = 0; Zone < POINTER_ZONE_GAME; Zone++)
        {
            float X0, Y0, X1, Y1;
            PointerZoneRect(Zone, W, H, &X0, &Y0, &X1, &Y1);
            if (X >= X0 && X < X1 && Y >= Y0 && Y < Y1)
            {
                E.Zone = Zone;
                return E;
            }
        }
        return E;
    }
    E.Zone = POINTER_ZONE_GAME;
    const float R = PointerStickRadius(W, H);
    float Nx = (X - (float)W / 2.0f) / R;
    float Ny = (Y - (float)Gh / 2.0f) / R;
    const float Len = sqrtf(Nx * Nx + Ny * Ny);
    if (Len < 0.1f)
    {
        return E;
    }
    if (Len > 1.0f)
    {
        Nx /= Len;
        Ny /= Len;
    }
    E.StickX = (int8_t)lroundf(Nx * 80.0f);
    E.StickY = (int8_t)lroundf(-Ny * 80.0f); // screen y grows downward; N64 y grows upward
    return E;
}

// Which direction a tilt points: 0 up, 1 right, 2 down, 3 left, -1 neutral. Vertical wins
// a tie, so the 45-degree guide lines and this rule agree.
inline int PointerQuadrant(int8_t StickX, int8_t StickY)
{
    if (StickX == 0 && StickY == 0) return -1;
    const int Ax = StickX < 0 ? -StickX : StickX;
    const int Ay = StickY < 0 ? -StickY : StickY;
    if (Ay >= Ax) return StickY > 0 ? 0 : 2;
    return StickX > 0 ? 1 : 3;
}

// Snaps a stick pair to one of the four full-tilt axis directions, or to centre, by the
// same quadrant rule PointerQuadrant already owns: 0 (up) -> (0,+80), 1 (right) -> (+80,0),
// 2 (down) -> (0,-80), 3 (left) -> (-80,0), -1 (centred) -> (0,0). Used for a digital head
// stick, so a tilted head reads as one of four N64 directions rather than an analog value.
inline void PointerSnapToQuadrant(int8_t * X, int8_t * Y)
{
    const int Q = PointerQuadrant(*X, *Y);
    const int8_t Max = 80;
    *X = (int8_t)(Q == 1 ? Max : Q == 3 ? -Max : 0);
    *Y = (int8_t)(Q == 0 ? Max : Q == 2 ? -Max : 0);
}

// Flick gate state: the previous poll's cursor and the stick it produced.
struct PointerGate
{
    bool HavePrev;
    float PrevX, PrevY;
    int8_t PrevStickX, PrevStickY;
};

// Applies the flick rule to E, already evaluated at X,Y, and records this poll in G:
// outside the game image the stick is neutral; inside, a cursor that moved more than
// Threshold pixels since the previous poll keeps that poll's stick, so a flick to the panel
// never reads as a tilt on the way. Threshold <= 0 disables the gate.
inline void PointerGateStick(PointerGate * G, PointerEval * E, float X, float Y, float Threshold)
{
    if (E->Zone != POINTER_ZONE_GAME)
    {
        E->StickX = 0;
        E->StickY = 0;
    }
    else if (G->HavePrev && Threshold > 0.0f)
    {
        const float Dx = X - G->PrevX, Dy = Y - G->PrevY;
        if (sqrtf(Dx * Dx + Dy * Dy) > Threshold)
        {
            E->StickX = G->PrevStickX;
            E->StickY = G->PrevStickY;
        }
    }
    G->HavePrev = true;
    G->PrevX = X;
    G->PrevY = Y;
    G->PrevStickX = E->StickX;
    G->PrevStickY = E->StickY;
}

// Where the cursor last rested in the game image, for the stick hold. A slow drag to the
// panel never rests, so it never records the backward tilt the image's bottom edge reads
// as. Zero-initialise for "nothing yet".
// Design: Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md
struct PointerSettle
{
    bool HaveAnchor;
    float AnchorX, AnchorY;
    int Count;                 // consecutive polls within the radius, capped at Polls + 1
    bool HaveSettled;
    int8_t SettledX, SettledY; // the gated stick at the last settle
    bool JustSettled;          // true only on the poll the count reached Polls
};

// Feeds one poll, after PointerGateStick, so E carries the gated stick. Outside the game
// image the anchor and the count reset but the recorded tilt stays. Radius <= 0 or
// Polls <= 0 never settles.
inline void PointerSettleStep(PointerSettle * S, const PointerEval & E, float X, float Y, float Radius, int Polls)
{
    S->JustSettled = false;
    if (E.Zone != POINTER_ZONE_GAME || Radius <= 0.0f || Polls <= 0)
    {
        S->HaveAnchor = false;
        S->Count = 0;
        return;
    }
    const float Dx = X - S->AnchorX, Dy = Y - S->AnchorY;
    if (!S->HaveAnchor || sqrtf(Dx * Dx + Dy * Dy) > Radius)
    {
        S->HaveAnchor = true;
        S->AnchorX = X;
        S->AnchorY = Y;
        S->Count = 1;
    }
    else if (S->Count <= Polls)
    {
        S->Count++;
    }
    if (S->Count == Polls)
    {
        S->HaveSettled = true;
        S->SettledX = E.StickX;
        S->SettledY = E.StickY;
        S->JustSettled = true;
    }
}

// The one button's state across polls: the momentary latch, the toggle slots that are on,
// and the stick hold. Start from PointerClicksInit().
struct PointerClicks
{
    bool PrevButton;
    int Latched;               // the zone pressed and still held, or POINTER_ZONE_NONE
    uint32_t Toggled;          // one bit per zone: the toggle slots that are on
    bool Holding;
    int8_t HeldX, HeldY;       // the stick while Holding
};

inline PointerClicks PointerClicksInit()
{
    PointerClicks C = { false, POINTER_ZONE_NONE, 0u, false, 0, 0 };
    return C;
}

// One poll of the button over Zone. On the press edge the hold slot turns the hold on or
// off (on copies the last settled tilt, or neutral before any settle), a slot in ToggleMask
// flips its bit and latches nothing, and anything else is latched until release. A settle
// in the game image ends the hold. With ToggleMask 0 and HoldZone POINTER_ZONE_NONE this is
// exactly the latch GetKeys always had.
inline void PointerClickStep(PointerClicks * C, bool Button, int Zone, uint32_t ToggleMask, int HoldZone, const PointerSettle & Settle)
{
    if (Button && !C->PrevButton && Zone != POINTER_ZONE_NONE)
    {
        if (Zone == HoldZone)
        {
            C->Holding = !C->Holding;
            C->HeldX = C->Holding && Settle.HaveSettled ? Settle.SettledX : 0;
            C->HeldY = C->Holding && Settle.HaveSettled ? Settle.SettledY : 0;
        }
        else if ((ToggleMask & (1u << Zone)) != 0)
        {
            C->Toggled ^= 1u << Zone;
        }
        else
        {
            C->Latched = Zone;
        }
    }
    if (!Button)
    {
        C->Latched = POINTER_ZONE_NONE;
    }
    if (C->Holding && Settle.JustSettled)
    {
        C->Holding = false;
    }
    C->PrevButton = Button;
}

// PJ64_POINTER_SETTLE: "0" never settles; "<px>,<polls>" with both positive sets the two.
// Anything else returns false and leaves both outputs as they were.
inline bool PointerParseSettle(const char * Text, float * Radius, int * Polls)
{
    if (strcmp(Text, "0") == 0)
    {
        *Radius = 0.0f;
        return true;
    }
    float R = 0.0f;
    int P = 0;
    char Tail = '\0';
    if (sscanf(Text, "%f,%d%c", &R, &P, &Tail) != 2 || R <= 0.0f || P <= 0)
    {
        return false;
    }
    *Radius = R;
    *Polls = P;
    return true;
}

// The single-game window can be resized or taken full screen, but the GL surface stays at
// the launch size and the window server scales it to fit, centred, keeping its shape: bars
// appear on the two long sides when the window's shape differs, as in full screen. Everything
// above works in launch-size pixels, so the frontend maps the cursor back through this.
//
// Maps (X, Y) in a WinW x WinH window onto the BaseW x BaseH picture. Writes the position
// in base coordinates and returns true when the cursor is over the picture, edges included.
// Over a bar it returns false and writes the position clamped to the picture's edge, so a
// cursor leaving through a bar does not jump. A size that is not positive returns false and
// writes (X, Y) unchanged.
inline bool PointerFitToBase(float WinW, float WinH, float BaseW, float BaseH,
                             float X, float Y, float * OutX, float * OutY)
{
    if (WinW <= 0.0f || WinH <= 0.0f || BaseW <= 0.0f || BaseH <= 0.0f)
    {
        *OutX = X;
        *OutY = Y;
        return false;
    }
    const float ScaleX = WinW / BaseW, ScaleY = WinH / BaseH;
    const float Scale = ScaleX < ScaleY ? ScaleX : ScaleY;
    const float OriginX = (WinW - BaseW * Scale) * 0.5f;
    const float OriginY = (WinH - BaseH * Scale) * 0.5f;
    const float Bx = (X - OriginX) / Scale;
    const float By = (Y - OriginY) / Scale;
    const bool Over = Bx >= 0.0f && Bx <= BaseW && By >= 0.0f && By <= BaseH;
    *OutX = Bx < 0.0f ? 0.0f : (Bx > BaseW ? BaseW : Bx);
    *OutY = By < 0.0f ? 0.0f : (By > BaseH ? BaseH : By);
    return Over;
}
