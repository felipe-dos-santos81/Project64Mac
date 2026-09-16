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
#include <stdlib.h>
#include <string.h>

// SDL3 delivers SDL_EVENT_GAMEPAD_BUTTON_DOWN and SDL_EVENT_GAMEPAD_AXIS_MOTION only for a
// gamepad that has been opened, so mode 2 and 3 capture would otherwise see nothing.
// Screens.cpp makes no SDL device calls of its own; it only reads WizardUi::HasGamepad,
// which this file sets from g_Gamepad each frame.
//
// Follows Source/Project64-sdl/PluginInput.cpp:134-159 (OpenFirstGamepad/CloseGamepad) and
// its per-call use at :342: OpenFirstGamepad is idempotent and safe to call every frame, so
// a still-attached second pad is picked up automatically the frame after the first
// disconnects, with no need to special-case SDL_EVENT_GAMEPAD_ADDED/REMOVED here.
static SDL_Gamepad * g_Gamepad = nullptr;

static void OpenFirstGamepad(void)
{
    if (g_Gamepad != nullptr) return;
    int Count = 0;
    SDL_JoystickID * Ids = SDL_GetGamepads(&Count);
    if (Ids != nullptr)
    {
        if (Count > 0) g_Gamepad = SDL_OpenGamepad(Ids[0]);
        SDL_free(Ids);
    }
}

static void CloseGamepad(void)
{
    if (g_Gamepad != nullptr)
    {
        SDL_CloseGamepad(g_Gamepad);
        g_Gamepad = nullptr;
    }
}

// File-scope rather than a local in main(): FaceTrackerStart can return before the OS
// permission prompt is answered, and its completion block later calls StartSession on
// whatever queue answers it. If the player quits while the prompt is still up and answers
// it during SDL teardown, that late write needs somewhere valid to land; a stack frame
// that has already returned is not that place, but a static, which outlives main(), is.
static PointerState g_State;

// A keydown as the handlers see it. The wizard's screens only ever read `type` and
// `key.scancode`, so a synthetic event needs nothing else.
static SDL_Event KeyEvent(SDL_Scancode Code)
{
    SDL_Event E;
    memset(&E, 0, sizeof(E));
    E.type = SDL_EVENT_KEY_DOWN;
    E.key.scancode = Code;
    return E;
}

// Same as KeyEvent, but with the OS-repeat flag SDL sets on a resent keydown for a key
// still held. HandleControl's repeat guard (Screens.cpp) and its five siblings are the one
// thing standing between a held key and it firing its action twice — four fix rounds across
// Tasks 5-7 went into getting that right — and KeyEvent alone can never exercise the
// direction that matters, since every event it builds always has repeat == false.
static SDL_Event KeyEventRepeat(SDL_Scancode Code)
{
    SDL_Event E = KeyEvent(Code);
    E.key.repeat = true;
    return E;
}

static SDL_Event ClickEvent(float X, float Y)
{
    SDL_Event E;
    memset(&E, 0, sizeof(E));
    E.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    E.button.x = X;
    E.button.y = Y;
    // CaptureZone (Screens.cpp) requires Event.button.button == SDL_BUTTON_LEFT before it
    // will look at the click at all; a zeroed button field reads as no button and the
    // click is silently ignored.
    E.button.button = SDL_BUTTON_LEFT;
    return E;
}

// Walks the real screens with a canned sequence and writes the result to Path. No window,
// no renderer, no camera: this is the end-to-end proof that runs anywhere.
static int Selftest(const char * Path)
{
    WizardDraft Draft;
    WizardUi Ui;
    WizardUiInit(&Ui);
    // No real gamepad is opened in this path (no SDL_Init, no device polling), but the B
    // step below exercises the gamepad-button capture mode, which HandleControl (Screens.cpp)
    // gates on Ui.HasGamepad. main.cpp normally sets this from whether a pad is actually
    // open; here there is no device loop to do that, so it is set directly.
    Ui.HasGamepad = true;

    // Base screen: the built-in bindings, the first row.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // A held Enter resends as a repeat. HandleControl must swallow it here, on the control
    // screen this first Enter just landed on, rather than calling Advance() a second time —
    // if it doesn't, every step below binds the wrong control and the emitted block comes
    // out misaligned (a row missing, or a row bound that shouldn't be).
    WizardHandleEvent(KeyEventRepeat(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // A: a keyboard key.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_1), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_X), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // B: a gamepad button.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_2), &Ui, &Draft, 0);
    SDL_Event Pad;
    memset(&Pad, 0, sizeof(Pad));
    Pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    Pad.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    WizardHandleEvent(Pad, &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // Z: the panel slot mid1, clicked at its centre. PointerZoneRect puts mid1 at
    // x 164..220, y 488..536 in a 640x640 window; the wizard draws that at half scale
    // from (24, 300), so its centre is (24 + 192*0.5, 300 + 512*0.5) = (120, 556).
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_4), &Ui, &Draft, 0);
    WizardHandleEvent(ClickEvent(120.0f, 556.0f), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // Start: the gesture that is firing, with mouth-open held.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_5), &Ui, &Draft, POINTER_GESTURE_MOUTH_OPEN);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_SPACE), &Ui, &Draft, POINTER_GESTURE_MOUTH_OPEN);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // L, with no gamepad: HandleControl's mode-2/3 gate (Screens.cpp) must refuse to arm
    // axis capture, the one HandleControl branch the B step above cannot reach, since B
    // needed HasGamepad true to test the button side of the same gate. L stays inherited
    // (absent from the emitted block) either way, so what this actually proves is that
    // pressing 3 here did not silently arm WIZARD_MODE_AXIS and consume the events after it.
    Ui.HasGamepad = false;
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_3), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // Everything up to Stick keeps what it has. Capped at N64Control::Count iterations: a
    // regression that leaves Ui.Mode non-NONE on a control screen (a capture function
    // consuming Enter without clearing the mode back to NONE — the exact bug class commits
    // fbfd419 and f2e6139 chased) would make every iteration here a no-op, hanging this
    // binary, and the make target that runs it, forever instead of failing.
    int Guard = 0;
    while (Ui.Screen == WIZARD_CONTROL && (N64Control)Ui.Control != N64Control::Stick)
    {
        if (Guard++ >= (int)N64Control::Count)
        {
            fprintf(stderr,
                    "wizard-selftest: stuck after %d Enters on screen %d, control %d, mode %d\n",
                    Guard, (int)Ui.Screen, Ui.Control, (int)Ui.Mode);
            return 1;
        }
        WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);
    }

    // Stick: the fifth form, a digital head stick.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_1), &Ui, &Draft, 0);
    for (int i = 0; i < 4; i++) WizardHandleEvent(KeyEvent(SDL_SCANCODE_DOWN), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    if (!Draft.Save(Path, Ui.Base))
    {
        fprintf(stderr, "wizard-selftest: %s\n", Draft.Error());
        return 1;
    }
    printf("wizard-selftest wrote %s\n", Path);
    return 0;
}

int main(int argc, char ** argv)
{
    if (argc >= 2 && strcmp(argv[1], "--version") == 0)
    {
        printf("Project64 binding wizard\n");
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "--selftest") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "usage: %s --selftest <path>\n", argv[0]);
            return 1;
        }
        return Selftest(argv[2]);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

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

    memset(&g_State, 0, sizeof(g_State));

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
        // Reopens on the frame after a disconnect (or picks up a pad connected mid-session),
        // exactly as PluginInput.cpp does before every GetKeys call.
        OpenFirstGamepad();
        if (g_Gamepad != nullptr && !SDL_GamepadConnected(g_Gamepad))
        {
            CloseGamepad();
        }
        Ui.HasGamepad = (g_Gamepad != nullptr);

        SDL_Event Event;
        while (SDL_PollEvent(&Event))
        {
            if (Event.type == SDL_EVENT_QUIT) { Ui.Quit = true; break; }
            WizardHandleEvent(Event, &Ui, &Draft,
                              g_State.Gestures.load(std::memory_order_relaxed));
        }

        // The camera starts the first time a gesture list asks for one, and never before:
        // opening the wizard is not consent to be filmed. PJ64_FACE=0 keeps it shut.
        if (Ui.WantCamera && !CameraStarted)
        {
            const char * Off = getenv("PJ64_FACE");
            if (Off != NULL && strcmp(Off, "0") == 0)
            {
                g_State.Face.store(FACE_OFF, std::memory_order_relaxed);
                Ui.WantCamera = false;
            }
            else
            {
                CameraStarted = FaceTrackerStart(&g_State);
                Ui.WantCamera = false;
            }
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
                         g_State.Gestures.load(std::memory_order_relaxed),
                         g_State.Face.load(std::memory_order_relaxed));
        SDL_RenderPresent(Renderer);
        SDL_Delay(16);
    }

    if (WasTyping) SDL_StopTextInput(Window);
    if (CameraStarted) FaceTrackerStop();
    CloseGamepad();
    SDL_DestroyRenderer(Renderer);
    SDL_DestroyWindow(Window);
    SDL_Quit();
    return 0;
}
