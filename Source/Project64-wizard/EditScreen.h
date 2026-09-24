// Project64 - A Nintendo 64 emulator
// The panel editor's window side: paints EditLayout's targets and turns a press-and-release
// into EditAct. No keyboard handling: a one-button player drives it with the pointer alone.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include "EditLayout.h"

#include <SDL3/SDL.h>

void EditDraw(SDL_Renderer * Renderer, const EditState & S, const WizardDraft & D, const char * Title,
              EditTarget Hover, uint32_t Gestures, uint32_t Face);

// One event. Motion updates *Hover; a left press records *Pressed; a left release on the
// target it was pressed on acts. Closing the window is a click on Cancel.
EditCommand EditHandleEvent(const SDL_Event & E, EditState * S, WizardDraft * D, EditTarget * Hover, EditTarget * Pressed);
