// Project64 - A Nintendo 64 emulator
// See Screens.h. The debug font is ASCII only and fixed pitch, which is all the pack's
// file names need; it draws with the wizard's helpers (Source/Project64-sdl/DebugText.h).
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Screens.h"

#include <Project64-sdl/DebugText.h>

#include <stdio.h>
#include <string.h>
#include <string>

static const int kScale = 2;
static const float kGlyph = (float)(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * kScale);   // 16
static const int kRowChars = 41;      // a row's title with nothing beside it: 656 of the row's 676 points
static const int kTitleChars = 33;    // a row's title leaving room for "generic"
static const int kLineChars = 48;     // a full-width line at kScale

static void Fill(SDL_Renderer * R, LauncherRect Rect, Uint8 Red, Uint8 Green, Uint8 Blue)
{
    SDL_SetRenderDrawColor(R, Red, Green, Blue, 255);
    const SDL_FRect F = { Rect.X, Rect.Y, Rect.W, Rect.H };
    SDL_RenderFillRect(R, &F);
}

static void Ink(SDL_Renderer * R, bool Bright)
{
    if (Bright) SDL_SetRenderDrawColor(R, 230, 230, 230, 255);
    else SDL_SetRenderDrawColor(R, 90, 90, 96, 255);
}

// A labelled button: lit under the pointer, dimmed when it does nothing.
static void Button(SDL_Renderer * R, LauncherRect Rect, const char * Label, bool Enabled, bool Hot)
{
    if (!Enabled) Fill(R, Rect, 28, 28, 32);
    else if (Hot) Fill(R, Rect, 70, 90, 140);
    else Fill(R, Rect, 40, 44, 56);
    Ink(R, Enabled);
    const float W = (float)strlen(Label) * kGlyph;
    DebugText(R, Rect.X + (Rect.W - W) / 2, Rect.Y + (Rect.H - kGlyph) / 2, kScale, Label);
}

static void Row(SDL_Renderer * R, LauncherRect Rect, const LauncherGame & G, bool Hot)
{
    if (Hot) Fill(R, Rect, 48, 56, 80);
    else Fill(R, Rect, 24, 26, 32);
    const float Y = Rect.Y + (Rect.H - kGlyph) / 2;
    Ink(R, true);
    DebugText(R, Rect.X + 8, Y, kScale, DebugTextFitHead(G.Title.c_str(), G.Generic ? kTitleChars : kRowChars).c_str());
    if (G.Generic)
    {
        Ink(R, false);
        DebugText(R, Rect.X + Rect.W - 8 - 7 * kGlyph, Y, kScale, "generic");
    }
}

void LauncherDraw(SDL_Renderer * R, const LauncherState & S, LauncherTarget Hover, const LauncherLabels & L)
{
    SDL_SetRenderDrawColor(R, 16, 16, 20, 255);
    SDL_RenderClear(R);
    Ink(R, true);
    DebugText(R, 16, 14, 3, "Project64");

    if (!S.EmulatorFound)
    {
        const LauncherTarget Quit = { LauncherTargetKind::Quit, 0 };
        Ink(R, true);
        DebugText(R, 16, 300, kScale, "Project64 not found beside the app");
        Button(R, LauncherTargetRect(Quit), "Quit", true, Hover == Quit);
        return;
    }

    for (const LauncherTarget & T : LauncherTargets())
    {
        const bool On = LauncherEnabled(S, T);
        const bool Hot = On && Hover == T;
        const LauncherRect Rect = LauncherTargetRect(T);
        switch (T.Kind)
        {
        case LauncherTargetKind::Face: Button(R, Rect, L.FaceOn ? "Face: on" : "Face: off", On, Hot); break;
        case LauncherTargetKind::Folder: Button(R, Rect, "Folder", On, Hot); break;
        case LauncherTargetKind::Quit: Button(R, Rect, "Quit", On, Hot); break;
        case LauncherTargetKind::Recent: Button(R, Rect, "Recent", On, Hot); break;
        case LauncherTargetKind::Letter:
        {
            const char Letter[2] = { (char)('A' + T.Index), '\0' };
            Button(R, Rect, Letter, On, Hot);
            break;
        }
        case LauncherTargetKind::Row:
        {
            const LauncherGame * G = LauncherRowGame(S, T.Index);
            if (G != nullptr) Row(R, Rect, *G, Hot);
            break;
        }
        case LauncherTargetKind::Edit:
            if (LauncherRowGame(S, T.Index) != nullptr) Button(R, Rect, "Edit", On, Hot);
            break;
        case LauncherTargetKind::Prev: Button(R, Rect, "<", On, Hot); break;
        case LauncherTargetKind::Next: Button(R, Rect, ">", On, Hot); break;
        case LauncherTargetKind::Choose:
            if (On) Button(R, Rect, "Choose a folder", On, Hot);
            break;
        case LauncherTargetKind::None: break;
        }
    }

    if (LauncherEmpty(S))
    {
        static const char * const kNoGames = "No games in ";
        const std::string Message = L.Folder[0] == '\0'
            ? std::string("No folder chosen")
            : std::string(kNoGames) + DebugTextFitTail(L.Folder, kLineChars - (int)strlen(kNoGames));
        Ink(R, true);
        DebugText(R, 16, 280, kScale, Message.c_str());
    }

    char Page[32];
    if (S.View == LAUNCHER_VIEW_RECENT) snprintf(Page, sizeof(Page), "recent games");
    else snprintf(Page, sizeof(Page), "page %d of %d", S.View + 1, LauncherPageCount((int)S.Games.size()));
    Ink(R, true);
    DebugText(R, (LAUNCHER_WIDTH - (float)strlen(Page) * kGlyph) / 2, 572, kScale, Page);

    // The status line: at twice the font's size when it fits, else at its own size, so the
    // exit code at the end of a long title's error is never cut off.
    const bool Fits = (int)strlen(L.Status) <= kLineChars;
    DebugText(R, 16, 616, Fits ? kScale : 1, DebugTextFitHead(L.Status, 2 * kLineChars).c_str());
}
