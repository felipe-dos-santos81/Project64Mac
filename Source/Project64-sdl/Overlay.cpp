// Project64 - A Nintendo 64 emulator
// Fixed-function GL overlay for the mouse layout: the panel of slots below the game and
// the direction guide over it. See Overlay.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Overlay.h"
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <Common/Trace.h>
#include <Project64-core/TraceModulesProject64.h>
#include <OpenGL/gl.h>
#include <math.h>
#include <string.h>

static const float kDim = 0.35f;       // resting line and label alpha
static const float kBright = 1.0f;     // held slot, lit arrow and active gesture alpha
static const float kWedge = 0.08f;     // fill of the lit quadrant
static const float kPanelGrey = 0.12f; // the panel's opaque ground
static const int kScale = 3;           // font pixel size; a glyph is 15x21 window pixels

// 5x7 glyphs, one byte per row, bit 4 is the left column. Only what the labels need.
struct Glyph { char C; unsigned char Rows[7]; };
static const Glyph kGlyphs[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 't', { 0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '^', { 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00 } },
    { 'v', { 0x00, 0x00, 0x00, 0x00, 0x11, 0x0A, 0x04 } },
    { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
    { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
};

static const Glyph * FindGlyph(char C)
{
    for (size_t i = 0; i < sizeof(kGlyphs) / sizeof(kGlyphs[0]); i++)
    {
        if (kGlyphs[i].C == C) return &kGlyphs[i];
    }
    return nullptr;
}

// Draws Text with its centre at (Cx, Cy).
static void DrawText(const char * Text, float Cx, float Cy, float Alpha)
{
    const int Len = (int)strlen(Text);
    if (Len == 0) return;
    const float GlyphW = 6.0f * kScale; // 5 columns plus one of spacing
    const float GlyphH = 7.0f * kScale;
    float X = Cx - (Len * GlyphW - kScale) / 2.0f;
    const float Y = Cy - GlyphH / 2.0f;
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(GL_QUADS);
    for (int i = 0; i < Len; i++, X += GlyphW)
    {
        const Glyph * G = FindGlyph(Text[i]);
        if (G == nullptr) continue;
        for (int Row = 0; Row < 7; Row++)
        {
            for (int Col = 0; Col < 5; Col++)
            {
                if ((G->Rows[Row] & (0x10 >> Col)) == 0) continue;
                const float Px = X + Col * kScale, Py = Y + Row * kScale;
                glVertex2f(Px, Py);
                glVertex2f(Px + kScale, Py);
                glVertex2f(Px + kScale, Py + kScale);
                glVertex2f(Px, Py + kScale);
            }
        }
    }
    glEnd();
}

static void DrawCircle(float Cx, float Cy, float R, float Alpha, bool Filled)
{
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(Filled ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
    if (Filled) glVertex2f(Cx, Cy);
    for (int i = 0; i <= 48; i++)
    {
        const float A = (float)i / 48.0f * 6.2831853f;
        glVertex2f(Cx + cosf(A) * R, Cy + sinf(A) * R);
    }
    glEnd();
}

static void DrawRect(float X0, float Y0, float X1, float Y1, float Alpha)
{
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(GL_LINE_LOOP);
    glVertex2f(X0 + 0.5f, Y0 + 0.5f);
    glVertex2f(X1 - 0.5f, Y0 + 0.5f);
    glVertex2f(X1 - 0.5f, Y1 - 0.5f);
    glVertex2f(X0 + 0.5f, Y1 - 0.5f);
    glEnd();
}

static void DrawLine(float X0, float Y0, float X1, float Y1, float Alpha)
{
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(GL_LINES);
    glVertex2f(X0, Y0);
    glVertex2f(X1, Y1);
    glEnd();
}

// The tracker mark with the three gesture labels to its right, bright while held.
static void DrawFaceStatus(const PointerState * State, float Cx, float Cy)
{
    const uint32_t Face = State->Face.load(std::memory_order_relaxed);
    if (Face == FACE_OFF) return;
    const float R = 6.0f;
    if (Face == FACE_TRACKING)
    {
        DrawCircle(Cx, Cy, R, kBright, true);
    }
    else
    {
        DrawCircle(Cx, Cy, R, kDim, false);
        if (Face == FACE_DENIED || Face == FACE_ERROR)
        {
            DrawLine(Cx - R, Cy - R, Cx + R, Cy + R, kDim);
            DrawLine(Cx - R, Cy + R, Cx + R, Cy - R, kDim);
        }
    }
    const uint32_t Bits = State->Gestures.load(std::memory_order_relaxed);
    const uint32_t kOrder[3] = { POINTER_GESTURE_EYEBROWS, POINTER_GESTURE_HEAD_LEFT, POINTER_GESTURE_HEAD_RIGHT };
    for (int i = 0; i < 3; i++)
    {
        if (State->GestureLabels[i][0] == '\0') continue;
        DrawText(State->GestureLabels[i], Cx + 28.0f + 36.0f * i, Cy, (Bits & kOrder[i]) != 0 ? kBright : kDim);
    }
}

// The panel: an opaque ground over the rows below the game, every slot with its label, the
// latched one bright, and the tracker under the middle slots.
static void DrawPanel(const PointerState * State, int W, int H, int GameH, int Latched)
{
    glColor4f(kPanelGrey, kPanelGrey, kPanelGrey, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, (float)GameH);
    glVertex2f((float)W, (float)GameH);
    glVertex2f((float)W, (float)H);
    glVertex2f(0.0f, (float)H);
    glEnd();
    for (int Zone = 0; Zone < POINTER_ZONE_GAME; Zone++)
    {
        const float Alpha = Zone == Latched ? kBright : kDim;
        float X0, Y0, X1, Y1;
        PointerZoneRect(Zone, W, H, &X0, &Y0, &X1, &Y1);
        DrawRect(X0, Y0, X1, Y1, Alpha);
        DrawText(State->Labels[Zone], (X0 + X1) / 2.0f, (Y0 + Y1) / 2.0f, Alpha);
    }
    DrawFaceStatus(State, 178.0f, (float)H - 40.0f);
}

// The guide over the game: the lit quadrant's wedge, the four 45-degree rays, the ring and
// dead zone, the game zone's label, and an arrow per edge with the lit one bright.
static void DrawGuide(const PointerState * State, int W, int H, int GameH, int Latched, int Quadrant)
{
    const float R = PointerStickRadius(W, H);
    const float Cx = (float)W / 2.0f, Cy = (float)GameH / 2.0f;
    const float Gw = (float)W, Gh = (float)GameH;
    const float Lx = Cx - Cy, Rx = Cx + Cy; // where the 45-degree rays meet the top and bottom edges
    if (Quadrant >= 0)
    {
        glColor4f(1.0f, 1.0f, 1.0f, kWedge);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(Cx, Cy);
        switch (Quadrant)
        {
        case 0: glVertex2f(Lx, 0.0f); glVertex2f(Rx, 0.0f); break;
        case 1: glVertex2f(Rx, 0.0f); glVertex2f(Gw, 0.0f); glVertex2f(Gw, Gh); glVertex2f(Rx, Gh); break;
        case 2: glVertex2f(Rx, Gh); glVertex2f(Lx, Gh); break;
        default: glVertex2f(Lx, Gh); glVertex2f(0.0f, Gh); glVertex2f(0.0f, 0.0f); glVertex2f(Lx, 0.0f); break;
        }
        glEnd();
    }
    DrawLine(Cx, Cy, Lx, 0.0f, kDim);
    DrawLine(Cx, Cy, Rx, 0.0f, kDim);
    DrawLine(Cx, Cy, Lx, Gh, kDim);
    DrawLine(Cx, Cy, Rx, Gh, kDim);
    const float GameAlpha = Latched == POINTER_ZONE_GAME ? kBright : kDim;
    DrawCircle(Cx, Cy, R, GameAlpha, false);
    DrawCircle(Cx, Cy, R * 0.1f, GameAlpha, false);
    DrawText(State->Labels[POINTER_ZONE_GAME], Cx, Cy - R + 7.0f * kScale, GameAlpha); // just inside the ring's top
    DrawText("^", Cx, 24.0f, Quadrant == 0 ? kBright : kDim);
    DrawText(">", Gw - 24.0f, Cy, Quadrant == 1 ? kBright : kDim);
    DrawText("v", Cx, Gh - 24.0f, Quadrant == 2 ? kBright : kDim);
    DrawText("<", 24.0f, Cy, Quadrant == 3 ? kBright : kDim);
}

void OverlayDraw(const PointerState * State, const int Viewport[4], bool GuideHidden)
{
    if (State == nullptr || Viewport == nullptr) return;

    // This geometry is only trustworthy when the viewport's origin is the one the frontend
    // arranged: (0, 0) for a keyboard layout with no panel, or (0, POINTER_PANEL_HEIGHT) for
    // a mouse layout's panel (main.cpp's PJ64_VIEWPORT_OFFSET). Any other origin means the
    // renderer was sized by some other route (Source/Project64-video/Main.cpp's
    // GetScreenResWidth/PJ64_TILE_SIZE) that the frontend never consulted, so the panel this
    // draws may not land where the input plugin evaluates clicks. Warn once per process -
    // this runs on the emulation thread inside the draw path, every frame.
    static bool ReportedBadOrigin = false;
    if (!ReportedBadOrigin
        && (Viewport[0] != 0 || (Viewport[1] != 0 && Viewport[1] != POINTER_PANEL_HEIGHT)))
    {
        WriteTrace(TraceUserInterface, TraceWarning,
            "Overlay viewport origin (%d,%d) does not match the frontend's layout; the panel "
            "may be drawn where it cannot be clicked", Viewport[0], Viewport[1]);
        ReportedBadOrigin = true;
    }

    // The renderer's viewport is the game image; the window is its union with the rows
    // below it, which the frontend reserved for the panel (Design: Docs/superpowers/specs/
    // 2026-09-15-mouse-panel-design.md). With no offset there is no panel to draw.
    const int GameH = Viewport[3];
    const int W = Viewport[0] + Viewport[2];
    const int H = Viewport[1] + GameH;
    if (W <= 0 || GameH <= 0) return;

    // Save what glPushAttrib does not cover: the bound program and the matrices.
    GLint Program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)W, (double)H, 0.0, -1.0, 1.0); // y down, like the cursor
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glUseProgram(0);
    glViewport(0, 0, W, H);
    glDisable(GL_DEPTH_TEST);
    // The video plugin leaves GL_TEXTURE_2D enabled on texture units 0-2 (see
    // OGLcombiner.cpp's init_combiner/gfxStippleMode) and never disables them; glDisable
    // is per-unit, so disable every unit here or the overlay gets multitextured through a
    // stale game texture and can vanish. The glPushAttrib(GL_ALL_ATTRIB_BITS) above
    // restores both the per-unit enables and the active-unit selector, so this cannot leak.
    for (int Unit = 3; Unit >= 0; Unit--)
    {
        glActiveTexture(GL_TEXTURE0 + Unit);
        glDisable(GL_TEXTURE_2D);
    }                                   // leaves unit 0 active, matching prior behavior
    glDisable(GL_FOG);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);

    const int Latched = State->LatchedZone.load(std::memory_order_relaxed);
    const int Quadrant = State->Quadrant.load(std::memory_order_relaxed);
    if (H > GameH)
    {
        DrawPanel(State, W, H, GameH, Latched);
    }
    if (!GuideHidden)
    {
        DrawGuide(State, W, H, GameH, Latched, Quadrant);
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glUseProgram((GLuint)Program);
}
