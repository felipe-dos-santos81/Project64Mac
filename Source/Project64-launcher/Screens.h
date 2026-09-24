// Project64 - A Nintendo 64 emulator
// Draws the launcher's one screen from LauncherModel's state and rectangles, in SDL's 8x8
// debug font at twice its size. Drawing only: what a click does is LauncherAct's.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include "LauncherModel.h"
#include <SDL3/SDL.h>

struct LauncherLabels
{
    bool FaceOn;
    const char * Folder;   // "" when none is chosen
    const char * Status;   // "" for none
};

void LauncherDraw(SDL_Renderer * Renderer, const LauncherState & State, LauncherTarget Hover,
                  const LauncherLabels & Labels);
