// Project64 - A Nintendo 64 emulator
// See MenuHost.h.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "MenuHost.h"
#include <Common/PointerLayout.h>
// The core's headers are not self-contained: Plugin.h brings the __interface shim and the
// standard containers they rely on, as it does for main.cpp through SdlRenderWindow.h.
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/SettingsID.h>
#include <stdio.h>
#include <stdlib.h>

static const Uint64 kFrameWaitMs = 500;    // longest wait for the overlay to draw
static const Uint64 kPauseWaitMs = 1000;   // longest wait for the core to confirm a pause

MenuHost::MenuHost(PointerState * State, SDL_Window * Window, int MenuZone, uint32_t MenuGesture) :
    m_State(State),
    m_Window(Window),
    m_MenuZone(MenuZone),
    m_MenuGesture(MenuGesture),
    m_Phase(Phase::Closed),
    m_Since(0),
    m_Frames(0),
    m_Trace(getenv("PJ64_MENU_SELFTEST") != nullptr)
{
}

void MenuHost::Enter(Phase Next)
{
    static const char * const kNames[] = { "closed", "drawing", "pausing", "paused", "stepping" };
    m_Phase = Next;
    m_Since = SDL_GetTicks();
    if (m_Trace)
    {
        fprintf(stderr, "menu: %s\n", kNames[(int)Next]);
    }
}

// Lets the game run until the overlay has drawn once more (or the wait times out), after
// which Poll pauses it again.
void MenuHost::ResumeUntilDrawn(Phase Next)
{
    m_Frames = m_State->OverlayFrames.load(std::memory_order_acquire);
    g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_FromMenu);
    Enter(Next);
}

void MenuHost::Close()
{
    m_State->MenuArmed.store(POINTER_ZONE_NONE, std::memory_order_release);
    m_State->MenuOpen.store(0u, std::memory_order_release);
    g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_FromMenu);
    Enter(Phase::Closed);
}

bool MenuHost::Act(PointerMenuAction Action)
{
    switch (Action)
    {
    case MENU_ACTION_NONE:
        return true;
    case MENU_ACTION_OPEN:
        m_State->MenuArmed.store(POINTER_ZONE_NONE, std::memory_order_release);
        m_Frames = m_State->OverlayFrames.load(std::memory_order_acquire);
        m_State->MenuOpen.store(1u, std::memory_order_release);
        Enter(Phase::Drawing);
        return true;
    case MENU_ACTION_RESUME:
        Close();
        return true;
    case MENU_ACTION_SAVE:
    case MENU_ACTION_LOAD:
        g_Settings->SaveDword(Game_CurrentSaveState, 0);
        g_BaseSystem->ExternalEvent(Action == MENU_ACTION_SAVE ? SysEvent_SaveMachineState : SysEvent_LoadMachineState);
        Close();
        return true;
    case MENU_ACTION_RESET:
        // A soft reset never reaches the plugin's RomClosed, so the clicks are cleared here.
        m_State->ClearClicks.fetch_add(1u, std::memory_order_release);
        g_BaseSystem->ExternalEvent(SysEvent_ResetCPU_Soft);
        Close();
        return true;
    case MENU_ACTION_QUIT:
        return false; // CloseSystem resumes a paused CPU before stopping it
    case MENU_ACTION_FULLSCREEN:
        SDL_SetWindowFullscreen(m_Window, (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN) == 0);
        break;
    case MENU_ACTION_RECENTRE:
        m_State->RecentreFace.store(1u, std::memory_order_release);
        break;
    case MENU_ACTION_REPAINT:
        break;
    }
    // The menu stays open and its picture changed: show it by stepping one frame.
    m_State->MenuArmed.store(m_Menu.Armed, std::memory_order_release);
    ResumeUntilDrawn(Phase::Stepping);
    return true;
}

bool MenuHost::Poll()
{
    if (g_BaseSystem == nullptr)
    {
        return true;
    }
    PointerSample S;
    PointerSnapshot(m_State, &S);
    const int Zone = PointerLayoutEvaluate(S.X, S.Y, S.W, S.H, S.Inside).Zone;
    const bool Gesture = m_MenuGesture != 0 && (m_State->Gestures.load(std::memory_order_relaxed) & m_MenuGesture) != 0;
    const bool FaceOn = PointerMenuFaceOn(m_State->Face.load(std::memory_order_relaxed));
    const Uint64 Now = SDL_GetTicks();

    switch (m_Phase)
    {
    case Phase::Closed:
    case Phase::Paused:
        return Act(PointerMenuStep(&m_Menu, S.Button, Zone, Gesture, m_MenuZone, FaceOn));
    case Phase::Drawing:
    case Phase::Stepping:
        if (m_State->OverlayFrames.load(std::memory_order_acquire) != m_Frames || Now - m_Since >= kFrameWaitMs)
        {
            g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_FromMenu);
            Enter(Phase::Pausing);
        }
        break;
    case Phase::Pausing:
        if (g_Settings->LoadBool(GameRunning_CPU_Paused))
        {
            Enter(Phase::Paused);
        }
        else if (Now - m_Since >= kPauseWaitMs)
        {
            fprintf(stderr, "menu: the game did not pause\n");
            Enter(Phase::Paused);
        }
        break;
    }
    // Presses while waiting are ignored, but the button and the gesture are still tracked,
    // so one that began now needs a release before it counts.
    m_Menu.PrevButton = S.Button;
    m_Menu.PrevGesture = Gesture;
    return true;
}
