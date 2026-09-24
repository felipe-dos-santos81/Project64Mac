// Project64 - A Nintendo 64 emulator
// SDL's 8x8 debug font, scaled, and the two ways to fit a line of it into a width: keep the
// head (prose, where the start carries the meaning) or keep the tail (a path, where the end
// is the file or folder that matters). The wizard and the launcher both draw with these;
// the debug font is fixed pitch and ASCII only, which is all either needs.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <SDL3/SDL.h>

#include <string.h>
#include <string>

// Text at Scale times the font's size, the renderer's scale put back afterwards.
inline void DebugText(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text)
{
    const float S = (float)Scale;
    SDL_SetRenderScale(Renderer, S, S);
    SDL_RenderDebugText(Renderer, X / S, Y / S, Text);
    SDL_SetRenderScale(Renderer, 1.0f, 1.0f);
}

// How many glyphs at Scale fit in MaxWidth pixels.
inline int DebugTextChars(int Scale, float MaxWidth)
{
    const int GlyphWidth = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * Scale;
    return GlyphWidth > 0 ? (int)(MaxWidth / (float)GlyphWidth) : 0;
}

// Text cut to MaxChars, ending in "..." when it was longer (and there is room for it).
inline std::string DebugTextFitHead(const char * Text, int MaxChars)
{
    const int Len = (int)strlen(Text);
    if (Len <= MaxChars) return Text;
    if (MaxChars <= 3) return std::string(Text, MaxChars > 0 ? MaxChars : 0);
    return std::string(Text, MaxChars - 3) + "...";
}

// Text cut to MaxChars from the front, starting with "..." when it was longer (and there is
// room for it).
inline std::string DebugTextFitTail(const char * Text, int MaxChars)
{
    const int Len = (int)strlen(Text);
    if (Len <= MaxChars) return Text;
    if (MaxChars <= 3) return std::string(Text + Len - (MaxChars > 0 ? MaxChars : 0));
    return "..." + std::string(Text + Len - (MaxChars - 3));
}
