// Project64 - A Nintendo 64 emulator
// The emulator actions menu's host: the frontend's main-thread side. It reads menu clicks
// from the published pointer sample (the input plugin cannot while the game is paused),
// pauses and resumes the core, and shows every change by letting the game run until the
// overlay has drawn one frame, since the main thread must never draw.
// Design: Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <Common/PointerMenu.h>
#include <Common/PointerState.h>
#include <SDL3/SDL.h>
#include <stdint.h>

class MenuHost
{
public:
    MenuHost(PointerState * State, SDL_Window * Window, int MenuZone, uint32_t MenuGesture);

    // One pass of the main loop, on the main thread. False when the player chose Quit.
    bool Poll();

private:
    enum class Phase { Closed, Drawing, Pausing, Paused, Stepping };

    void Enter(Phase Next);
    bool Act(PointerMenuAction Action);
    void Close();
    void ResumeUntilDrawn(Phase Next);
    void HealLatePause();

    PointerState * m_State;
    SDL_Window * m_Window;
    int m_MenuZone;
    uint32_t m_MenuGesture;
    PointerMenu m_Menu;
    Phase m_Phase;
    Uint64 m_Since;       // when the current phase began, in SDL ticks
    uint32_t m_Frames;    // OverlayFrames when the current wait began
    bool m_Trace;         // PJ64_MENU_SELFTEST
};
