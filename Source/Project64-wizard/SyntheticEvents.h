// Project64 - A Nintendo 64 emulator
// Synthetic SDL events, shaped the way the wizard's handlers read them. Two callers walk
// the real screens with these and no window: --selftest (main.cpp), which proves the file
// the screens write, and --screenshots (Screenshots.cpp), which pictures the screens for
// Docs/UserGuide.md. Neither needs SDL_Init.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

#include <SDL3/SDL.h>

#include <string.h>

// A keydown as the handlers see it. The wizard's screens only ever read `type` and
// `key.scancode`, so a synthetic event needs nothing else.
static inline SDL_Event KeyEvent(SDL_Scancode Code)
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
static inline SDL_Event KeyEventRepeat(SDL_Scancode Code)
{
    SDL_Event E = KeyEvent(Code);
    E.key.repeat = true;
    return E;
}

// A gamepad axis at a given value, as CapturePad and CaptureStickForm (Screens.cpp) read it:
// only `type`, `gaxis.axis` and `gaxis.value`.
static inline SDL_Event AxisEvent(SDL_GamepadAxis Axis, Sint16 Value)
{
    SDL_Event E;
    memset(&E, 0, sizeof(E));
    E.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    E.gaxis.axis = (Uint8)Axis;
    E.gaxis.value = Value;
    return E;
}

static inline SDL_Event ClickEvent(float X, float Y)
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

// HandleTyping (Screens.cpp) fills Ui.Typed from SDL_EVENT_TEXT_INPUT events, reading only
// Event.text.text, and appends whatever string that event carries. SDL itself may deliver a
// typed path one character (or one IME composition) at a time, but HandleTyping just appends
// each event's string in turn, so one event carrying the whole path is equivalent to many
// carrying one character each. Text is not copied into the event: the caller must keep it
// alive for the call.
static inline SDL_Event TextEvent(const char * Text)
{
    SDL_Event E;
    memset(&E, 0, sizeof(E));
    E.type = SDL_EVENT_TEXT_INPUT;
    E.text.text = Text;
    return E;
}
