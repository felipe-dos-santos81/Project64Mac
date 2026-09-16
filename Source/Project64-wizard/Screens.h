// Project64 - A Nintendo 64 emulator
// The wizard's screens: one draws, one handles an event. Everything the wizard knows
// about bindings lives in WizardDraft; this file only turns events into calls on it.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

#include "WizardDraft.h"

#include <SDL3/SDL.h>

enum WizardScreen
{
    WIZARD_BASE = 0,     // pick a starting point
    WIZARD_CONTROL,      // one of the fifteen controls
    WIZARD_REVIEW,       // everything, then where to save
    WIZARD_SAVE,         // the three destinations
};

// Which capture is armed on a control screen. NONE means the screen's own keys are live.
enum WizardMode
{
    WIZARD_MODE_NONE = 0,
    WIZARD_MODE_KEY,
    WIZARD_MODE_BUTTON,
    WIZARD_MODE_AXIS,
    WIZARD_MODE_ZONE,
    WIZARD_MODE_GESTURE,
    WIZARD_MODE_STICK,       // the six-form chooser, Stick only
    WIZARD_MODE_STICK_KEYS,  // capturing up, down, left, right in turn
};

struct WizardUi
{
    WizardScreen Screen;
    int Control;             // 0..14 while Screen is WIZARD_CONTROL
    WizardMode Mode;
    int List;                // highlighted row of whatever list is on screen
    int KeyStep;             // 0..3 while Mode is WIZARD_MODE_STICK_KEYS
    SDL_Scancode Keys[4];    // collected so far in that mode
    char Base[256];          // the base's name, for the emitted header
    char Typed[256];         // the path being typed, when Typing
    bool Typing;
    int SaveChoice;          // 0 default file, 1 beside a ROM, 2 a typed path
    bool ConfirmClobber;     // a second Enter is needed for Config/mouse or Config/face
    // A second, independent Enter, needed when a draft holding a mouse-panel, face-gesture,
    // pointer or head binding is aimed at Config/input.yaml. Separate from ConfirmClobber on
    // purpose: the two warnings are about different things, they can both apply to one path,
    // and one Enter must never be allowed to satisfy the other.
    bool ConfirmDefault;
    char Message[256];       // the line under the screen
    bool WantCamera;         // set the first time gesture mode is entered
    bool HasGamepad;         // set by main.cpp each frame: a gamepad is currently open
    bool Quit;
};

void WizardUiInit(WizardUi * Ui);

// One event. LitGestures is PointerState::Gestures, or 0 with no camera.
void WizardHandleEvent(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft, uint32_t LitGestures);

// Draws the current screen. Gestures and Face are PointerState::Gestures and ::Face.
void WizardDrawScreen(SDL_Renderer * Renderer, int W, int H, const WizardUi & Ui,
                      const WizardDraft & Draft, uint32_t Gestures, uint32_t Face);

// One line of text at window pixels X,Y in the current draw colour. Returns the width the
// line occupied, so a caller can put something after it.
float WizardText(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text);

// Like WizardText, but truncates Text (ending in "...") so it never draws past MaxWidth
// pixels from X. MaxWidth is pixels, not characters, so a caller doesn't have to redo the
// glyph-width arithmetic for its own window position.
float WizardTextFit(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text, float MaxWidth);
