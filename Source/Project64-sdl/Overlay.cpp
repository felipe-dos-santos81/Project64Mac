// Project64 - A Nintendo 64 emulator
// Fixed-function GL overlay for the pointer layout. See Overlay.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Overlay.h"
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <OpenGL/gl.h>
#include <math.h>
#include <string.h>

static const float kDim = 0.35f;     // resting line and label alpha
static const float kBright = 1.0f;   // held zone and active gesture alpha
static const int kScale = 3;         // font pixel size; a glyph is 15x21 window pixels

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

static void DrawFaceStatus(const PointerState * State, int Height)
{
    const uint32_t Face = State->Face.load(std::memory_order_relaxed);
    if (Face == FACE_OFF) return;
    const float Cx = 14.0f, Cy = (float)Height - 14.0f, R = 6.0f;
    if (Face == FACE_TRACKING)
    {
        DrawCircle(Cx, Cy, R, kBright, true);
    }
    else
    {
        DrawCircle(Cx, Cy, R, kDim, false);
        if (Face == FACE_DENIED || Face == FACE_ERROR)
        {
            glBegin(GL_LINES);
            glVertex2f(Cx - R, Cy - R); glVertex2f(Cx + R, Cy + R);
            glVertex2f(Cx - R, Cy + R); glVertex2f(Cx + R, Cy - R);
            glEnd();
        }
    }
    // Active gestures show as their bound labels to the right of the mark.
    const uint32_t Bits = State->Gestures.load(std::memory_order_relaxed);
    float X = Cx + R + 4.0f + 6.0f * kScale;
    const uint32_t kOrder[3] = { POINTER_GESTURE_EYEBROWS, POINTER_GESTURE_HEAD_LEFT, POINTER_GESTURE_HEAD_RIGHT };
    for (int i = 0; i < 3; i++)
    {
        if ((Bits & kOrder[i]) == 0 || State->GestureLabels[i][0] == '\0') continue;
        DrawText(State->GestureLabels[i], X, Cy, kBright);
        X += 14.0f * kScale;
    }
}

void OverlayDraw(const PointerState * State, int Width, int Height)
{
    if (State == nullptr || Width <= 0 || Height <= 0) return;

    // Save what glPushAttrib does not cover: the bound program and the matrices.
    GLint Program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &Program);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)Width, (double)Height, 0.0, -1.0, 1.0); // y down, like the cursor
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glUseProgram(0);
    glViewport(0, 0, Width, Height);
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
    const float R = PointerStickRadius(Width, Height);
    const float Cx = (float)Width / 2.0f, Cy = (float)Height / 2.0f;

    for (int Zone = 0; Zone < POINTER_ZONE_COUNT; Zone++)
    {
        const float Alpha = Zone == Latched ? kBright : kDim;
        float X0, Y0, X1, Y1;
        PointerZoneRect(Zone, Width, Height, &X0, &Y0, &X1, &Y1);
        if (Zone != POINTER_ZONE_GAME)
        {
            DrawRect(X0, Y0, X1, Y1, Alpha);
            DrawText(State->Labels[Zone], (X0 + X1) / 2.0f, (Y0 + Y1) / 2.0f, Alpha);
        }
        else
        {
            DrawCircle(Cx, Cy, R, Alpha, false);
            DrawCircle(Cx, Cy, R * 0.1f, Alpha, false);
            DrawText(State->Labels[Zone], Cx, Cy - R + 7.0f * kScale, Alpha); // just inside the ring's top
        }
    }
    DrawFaceStatus(State, Height);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glUseProgram((GLuint)Program);
}
