// Project64 - A Nintendo 64 emulator
// The 4x4 pointer grid, shared by the input plugin (evaluation) and the overlay (drawing)
// so the two can never disagree about where a cell is. Pure functions, no SDL.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

enum
{
    POINTER_ZONE_NONE = -1,
    POINTER_ZONE_CENTRE = 12,
};

// Zone order: top row, then the two side pairs, then the bottom row, then centre.
// Indices 0-11 are the outer cells; 12 is the inner 2x2 block.
inline const char * PointerZoneName(int Zone)
{
    static const char * const kNames[13] = {
        "top1", "top2", "top3", "top4",
        "left2", "right2", "left3", "right3",
        "bottom1", "bottom2", "bottom3", "bottom4",
        "centre",
    };
    return (Zone >= 0 && Zone < 13) ? kNames[Zone] : "";
}

inline int PointerZoneFromName(const char * Name)
{
    for (int i = 0; i < 13; i++)
    {
        if (strcmp(Name, PointerZoneName(i)) == 0) return i;
    }
    return POINTER_ZONE_NONE;
}

inline float PointerStickRadius(int W, int H)
{
    return (float)(W < H ? W : H) / 4.0f;
}

// Cell (Col,Row) of the 4x4 grid -> zone index.
inline int PointerZoneFromCell(int Col, int Row)
{
    if (Row == 0) return Col;                         // top1..top4
    if (Row == 3) return 8 + Col;                     // bottom1..bottom4
    if (Col == 0) return Row == 1 ? 4 : 6;            // left2, left3
    if (Col == 3) return Row == 1 ? 5 : 7;            // right2, right3
    return POINTER_ZONE_CENTRE;
}

// Pixel rectangle of a zone, top-left origin, for the overlay.
inline void PointerZoneRect(int Zone, int W, int H, float * X0, float * Y0, float * X1, float * Y1)
{
    const float CellW = (float)W / 4.0f, CellH = (float)H / 4.0f;
    int Col = 0, Row = 0, Cols = 1, Rows = 1;
    if (Zone == POINTER_ZONE_CENTRE) { Col = 1; Row = 1; Cols = 2; Rows = 2; }
    else if (Zone >= 0 && Zone < 4) { Col = Zone; Row = 0; }
    else if (Zone >= 8 && Zone < 12) { Col = Zone - 8; Row = 3; }
    else if (Zone == 4) { Col = 0; Row = 1; }
    else if (Zone == 5) { Col = 3; Row = 1; }
    else if (Zone == 6) { Col = 0; Row = 2; }
    else if (Zone == 7) { Col = 3; Row = 2; }
    *X0 = Col * CellW;
    *Y0 = Row * CellH;
    *X1 = (Col + Cols) * CellW;
    *Y1 = (Row + Rows) * CellH;
}

struct PointerEval
{
    int Zone;
    int8_t StickX, StickY;
};

// X,Y in window pixels from the top left. Inside false means "not over this window",
// which reads as no zone and a neutral stick.
inline PointerEval PointerLayoutEvaluate(float X, float Y, int W, int H, bool Inside)
{
    PointerEval E = { POINTER_ZONE_NONE, 0, 0 };
    if (!Inside || W <= 0 || H <= 0 || X < 0 || Y < 0 || X >= (float)W || Y >= (float)H)
    {
        return E;
    }
    const int Col = (int)(X / ((float)W / 4.0f));
    const int Row = (int)(Y / ((float)H / 4.0f));
    E.Zone = PointerZoneFromCell(Col > 3 ? 3 : Col, Row > 3 ? 3 : Row);
    if (E.Zone != POINTER_ZONE_CENTRE)
    {
        return E;
    }
    const float R = PointerStickRadius(W, H);
    float Nx = (X - (float)W / 2.0f) / R;
    float Ny = (Y - (float)H / 2.0f) / R;
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
