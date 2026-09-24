// Project64 - A Nintendo 64 emulator
// See EditScreen.h. Everything shown comes from EditLayout; this file only paints it.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "EditScreen.h"
#include "Screens.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <Project64-sdl/DebugText.h>

#include <string>

namespace
{
const int kScale = 2;
const float kGlyph = (float)(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * kScale);   // 16
const int kLineChars = 47;   // a line from x 24 at kScale

void Fill(SDL_Renderer * R, EditRect Rect, Uint8 Red, Uint8 Green, Uint8 Blue)
{
    SDL_SetRenderDrawColor(R, Red, Green, Blue, 255);
    const SDL_FRect F = { Rect.X, Rect.Y, Rect.W, Rect.H };
    SDL_RenderFillRect(R, &F);
}

// The fill of whatever target is under the pointer.
void FillHot(SDL_Renderer * R, EditRect Rect)
{
    Fill(R, Rect, 70, 90, 140);
}

// Bright for text that acts, dim for text that does not, gold for the current choice.
void Ink(SDL_Renderer * R, bool Bright, bool Lit)
{
    if (Lit) SDL_SetRenderDrawColor(R, 255, 220, 120, 255);
    else if (Bright) SDL_SetRenderDrawColor(R, 220, 220, 220, 255);
    else SDL_SetRenderDrawColor(R, 90, 90, 96, 255);
}

void Centred(SDL_Renderer * R, EditRect Rect, const std::string & Text)
{
    const float W = (float)Text.size() * kGlyph;
    DebugText(R, Rect.X + (Rect.W - W) / 2, Rect.Y + (Rect.H - kGlyph) / 2, kScale, Text.c_str());
}

void Button(SDL_Renderer * R, EditRect Rect, const std::string & Label, bool On, bool Lit, bool Hot)
{
    if (!On) Fill(R, Rect, 28, 28, 32);
    else if (Hot) FillHot(R, Rect);
    else if (Lit) Fill(R, Rect, 72, 62, 30);
    else Fill(R, Rect, 40, 44, 56);
    Ink(R, On, On && Lit);
    Centred(R, Rect, Label);
}

void Panel(SDL_Renderer * R, const EditState & S, const WizardDraft & D, EditTarget Hover)
{
    for (int Z = 0; Z < POINTER_ZONE_COUNT; Z++)
    {
        const EditTarget T = { EditTargetKind::Zone, Z };
        const EditRect Rect = EditTargetRect(T);
        const bool Hot = S.View == EditView::Panel && Hover == T;
        if (Hot) FillHot(R, Rect);
        else if (Z == POINTER_ZONE_GAME) Fill(R, Rect, 28, 28, 34);
        else Fill(R, Rect, 44, 44, 52);
        Ink(R, true, false);
        Centred(R, Rect, EditLabel(S, D, T));
        if (D.Toggled(Z))
        {
            // A toggle slot's mark: a gold bar along its foot.
            Fill(R, EditRect{ Rect.X, Rect.Y + Rect.H - 4, Rect.W, 4 }, 255, 220, 120);
        }
    }
}
}

void EditDraw(SDL_Renderer * R, const EditState & S, const WizardDraft & D, const char * Title,
              EditTarget Hover, uint32_t Gestures, uint32_t Face)
{
    SDL_SetRenderDrawColor(R, 16, 16, 20, 255);
    SDL_RenderClear(R);
    Ink(R, true, false);
    DebugText(R, 24, 12, kScale, DebugTextFitHead(Title, kLineChars).c_str());

    // The picture and the panel stay behind a chooser, so a change is made in context; only
    // the gesture list replaces them.
    if (S.View != EditView::Gestures) Panel(R, S, D, Hover);
    if (S.View == EditView::Chooser || S.View == EditView::Stick)
    {
        const EditRect Picture = EditTargetRect(EditTarget{ EditTargetKind::Zone, POINTER_ZONE_GAME });
        Fill(R, Picture, 24, 26, 32);
        Ink(R, true, false);
        DebugText(R, Picture.X + 8, Picture.Y + 8, kScale, DebugTextFitHead(EditHeader(S, D).c_str(), 39).c_str());
    }

    for (const EditTarget & T : EditTargets(S))
    {
        if (T.Kind == EditTargetKind::Zone) continue;   // drawn with the panel
        const bool On = EditEnabled(S, D, T, nullptr);
        const bool Hot = On && Hover == T;
        const EditRect Rect = EditTargetRect(T);
        if (T.Kind == EditTargetKind::Gesture)
        {
            const bool Firing = (Gestures & (1u << T.Index)) != 0;
            if (Hot) FillHot(R, Rect);
            else Fill(R, Rect, 24, 26, 32);
            Ink(R, On, On && Firing);
            DebugText(R, Rect.X + 8, Rect.Y + (Rect.H - kGlyph) / 2, kScale, EditLabel(S, D, T).c_str());
            continue;
        }
        Button(R, Rect, EditLabel(S, D, T), On, EditLit(S, D, T), Hot);
    }
    if (S.View == EditView::Gestures)
    {
        Ink(R, true, false);
        DebugText(R, 80, 564, kScale, DebugTextFitHead(WizardFaceStatus(Face), 40).c_str());
    }

    Ink(R, true, false);
    DebugText(R, 24, 592, kScale, DebugTextFitHead(EditNotPlaced(D).c_str(), kLineChars).c_str());
    // At twice the font's size when it fits, else at its own, so the end is never cut.
    const bool Fits = (int)S.Status.size() <= kLineChars;
    DebugText(R, 24, 616, Fits ? kScale : 1, DebugTextFitHead(S.Status.c_str(), 2 * kLineChars).c_str());
}

EditCommand EditHandleEvent(const SDL_Event & E, EditState * S, WizardDraft * D, EditTarget * Hover, EditTarget * Pressed)
{
    switch (E.type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return EditAct(S, D, EditTarget{ EditTargetKind::Cancel, 0 });
    case SDL_EVENT_MOUSE_MOTION:
        *Hover = EditHit(*S, E.motion.x, E.motion.y);
        return EditCommand::None;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (E.button.button == SDL_BUTTON_LEFT) *Pressed = EditHit(*S, E.button.x, E.button.y);
        return EditCommand::None;
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        if (E.button.button != SDL_BUTTON_LEFT) return EditCommand::None;
        const EditTarget Released = EditHit(*S, E.button.x, E.button.y);
        const EditTarget Was = *Pressed;
        *Pressed = EditTarget();
        if (Released.Kind == EditTargetKind::None || !(Released == Was)) return EditCommand::None;
        return EditAct(S, D, Released);
    }
    default:
        return EditCommand::None;
    }
}
