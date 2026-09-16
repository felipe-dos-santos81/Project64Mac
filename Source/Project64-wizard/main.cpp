// Project64 - A Nintendo 64 emulator
// The binding wizard: a window, an event loop and nothing else. It never loads a ROM and
// never starts the camera until a gesture list asks for one.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Screens.h"
#include "WizardDraft.h"

#include <Common/PointerState.h>
#include <Project64-sdl/FaceTracker.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

// SDL3 delivers SDL_EVENT_GAMEPAD_BUTTON_DOWN and SDL_EVENT_GAMEPAD_AXIS_MOTION only for a
// gamepad that has been opened, so mode 2 and 3 capture would otherwise see nothing. Follows
// the pattern at Source/Project64-sdl/PluginInput.cpp:134-159.
static SDL_Gamepad * OpenFirstGamepad(void)
{
    SDL_Gamepad * Pad = nullptr;
    int Count = 0;
    SDL_JoystickID * Ids = SDL_GetGamepads(&Count);
    if (Ids != nullptr)
    {
        if (Count > 0) Pad = SDL_OpenGamepad(Ids[0]);
        SDL_free(Ids);
    }
    return Pad;
}

int main(int argc, char ** argv)
{
    if (argc >= 2 && strcmp(argv[1], "--version") == 0)
    {
        printf("Project64 binding wizard\n");
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Gamepad * Gamepad = OpenFirstGamepad();

    SDL_Window * Window = SDL_CreateWindow("Project64 binding wizard", 800, 640,
                                           SDL_WINDOW_RESIZABLE);
    if (Window == NULL)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer * Renderer = SDL_CreateRenderer(Window, NULL);
    if (Renderer == NULL)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(Window);
        SDL_Quit();
        return 1;
    }

    PointerState State;
    memset(&State, 0, sizeof(State));

    WizardDraft Draft;
    WizardUi Ui;
    WizardUiInit(&Ui);

    bool CameraStarted = false;
    // WizardHandleEvent stays window-free (a later task drives it with synthetic events and
    // no window at all), so text input is started and stopped here, from the transitions of
    // Ui.Typing, rather than inside the handler.
    bool WasTyping = false;
    while (!Ui.Quit)
    {
        SDL_Event Event;
        while (SDL_PollEvent(&Event))
        {
            if (Event.type == SDL_EVENT_QUIT) { Ui.Quit = true; break; }
            if (Event.type == SDL_EVENT_GAMEPAD_ADDED && Gamepad == nullptr)
            {
                Gamepad = SDL_OpenGamepad(Event.gdevice.which);
            }
            else if (Event.type == SDL_EVENT_GAMEPAD_REMOVED && Gamepad != nullptr &&
                     Event.gdevice.which == SDL_GetGamepadID(Gamepad))
            {
                SDL_CloseGamepad(Gamepad);
                Gamepad = nullptr;
            }
            WizardHandleEvent(Event, &Ui, &Draft,
                              State.Gestures.load(std::memory_order_relaxed));
        }

        if (Ui.Typing != WasTyping)
        {
            if (Ui.Typing) SDL_StartTextInput(Window);
            else SDL_StopTextInput(Window);
            WasTyping = Ui.Typing;
        }

        int W = 0, H = 0;
        SDL_GetWindowSize(Window, &W, &H);
        SDL_SetRenderDrawColor(Renderer, 16, 16, 20, 255);
        SDL_RenderClear(Renderer);
        WizardDrawScreen(Renderer, W, H, Ui, Draft,
                         State.Gestures.load(std::memory_order_relaxed),
                         State.Face.load(std::memory_order_relaxed));
        SDL_RenderPresent(Renderer);
        SDL_Delay(16);
    }

    if (WasTyping) SDL_StopTextInput(Window);
    if (CameraStarted) FaceTrackerStop();
    if (Gamepad != nullptr) SDL_CloseGamepad(Gamepad);
    SDL_DestroyRenderer(Renderer);
    SDL_DestroyWindow(Window);
    SDL_Quit();
    return 0;
}
