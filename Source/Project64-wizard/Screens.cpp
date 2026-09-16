// Project64 - A Nintendo 64 emulator
// See Screens.h. Text is SDL's debug font: eight pixels square, ASCII only, scaled by the
// renderer. It is a utility font for a utility screen, and it spells a scancode name,
// which the emulator's twenty-glyph overlay font cannot.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Screens.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <string.h>

// SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE is 8; a scale of 2 reads comfortably at 720x640.
static const int kBody = 2;
static const int kHead = 3;
static const float kLine = 22.0f;
// The window is 800x640 (see main.cpp); screens are laid out for this fixed size rather
// than the live, resizable window size.
static const float kWindowWidth = 800.0f;

float WizardText(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text)
{
    const float S = (float)Scale;
    SDL_SetRenderScale(Renderer, S, S);
    SDL_RenderDebugText(Renderer, X / S, Y / S, Text);
    SDL_SetRenderScale(Renderer, 1.0f, 1.0f);
    return (float)strlen(Text) * (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * S;
}

float WizardTextFit(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text, float MaxWidth)
{
    const int GlyphWidth = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * Scale;
    const int MaxChars = GlyphWidth > 0 ? (int)(MaxWidth / (float)GlyphWidth) : 0;
    if ((int)strlen(Text) <= MaxChars)
    {
        return WizardText(Renderer, X, Y, Scale, Text);
    }
    char Buffer[256];
    if (MaxChars > 3)
    {
        snprintf(Buffer, sizeof(Buffer), "%.*s...", MaxChars - 3, Text);
    }
    else
    {
        snprintf(Buffer, sizeof(Buffer), "%.*s", MaxChars > 0 ? MaxChars : 0, Text);
    }
    return WizardText(Renderer, X, Y, Scale, Buffer);
}

static void Colour(SDL_Renderer * Renderer, bool Highlight)
{
    if (Highlight) SDL_SetRenderDrawColor(Renderer, 255, 220, 120, 255);
    else SDL_SetRenderDrawColor(Renderer, 220, 220, 220, 255);
}

void WizardUiInit(WizardUi * Ui)
{
    memset(Ui, 0, sizeof(*Ui));
    Ui->Screen = WIZARD_BASE;
    Ui->Mode = WIZARD_MODE_NONE;
    snprintf(Ui->Base, sizeof(Ui->Base), "the built-in bindings");
    snprintf(Ui->Message, sizeof(Ui->Message), "Up and Down to move, Enter to choose.");
}

// Rows of the base screen: the built-in bindings, the shipped layouts, then a typed path.
static int BaseRowCount() { return WizardBaseCount() + 2; }

static const char * BaseRowLabel(int Row)
{
    if (Row == 0) return "The built-in bindings (keyboard and gamepad)";
    if (Row == BaseRowCount() - 1) return "A file I will type the path of";
    return WizardBaseLabel(Row - 1);
}

// Absolute path of a shipped layout: they sit beside the executable, as Config/ does.
static void BasePath(int Row, char * Out, size_t Size)
{
    const char * Dir = SDL_GetBasePath();
    snprintf(Out, Size, "%s%s", Dir != NULL ? Dir : "", WizardBaseFile(Row - 1));
}

// Every path onto the control screen lands here, so List (whatever list the base screen
// left highlighted) and Mode never leak across the transition. Five later tasks add lists
// of their own to the same field, which is why this is the one place that resets it.
static void EnterControlScreen(WizardUi * Ui)
{
    Ui->Screen = WIZARD_CONTROL;
    Ui->Control = 0;
    Ui->Mode = WIZARD_MODE_NONE;
    Ui->List = 0;
    snprintf(Ui->Message, sizeof(Ui->Message), "1-5 to bind, Enter to keep, Delete to inherit.");
}

static void ChooseBase(WizardUi * Ui, WizardDraft * Draft)
{
    if (Ui->List == 0)
    {
        Draft->LoadDefaults();
        snprintf(Ui->Base, sizeof(Ui->Base), "the built-in bindings");
    }
    else if (Ui->List == BaseRowCount() - 1)
    {
        Ui->Typing = true;
        Ui->Typed[0] = '\0';
        snprintf(Ui->Message, sizeof(Ui->Message), "Type a path, then Enter. Escape cancels.");
        return;
    }
    else
    {
        char Path[512];
        BasePath(Ui->List, Path, sizeof(Path));
        if (!Draft->LoadBase(Path))
        {
            snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
            return;
        }
        snprintf(Ui->Base, sizeof(Ui->Base), "%s", WizardBaseFile(Ui->List - 1));
    }
    EnterControlScreen(Ui);
}

static void TypedBase(WizardUi * Ui, WizardDraft * Draft)
{
    Ui->Typing = false;
    if (!Draft->LoadBase(Ui->Typed))
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
        return;
    }
    snprintf(Ui->Base, sizeof(Ui->Base), "%s", Ui->Typed);
    EnterControlScreen(Ui);
}

static void HandleTyping(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type == SDL_EVENT_TEXT_INPUT)
    {
        const size_t Len = strlen(Ui->Typed);
        snprintf(Ui->Typed + Len, sizeof(Ui->Typed) - Len, "%s", Event.text.text);
        return;
    }
    if (Event.type != SDL_EVENT_KEY_DOWN) return;
    if (Event.key.scancode == SDL_SCANCODE_BACKSPACE)
    {
        const size_t Len = strlen(Ui->Typed);
        if (Len > 0) Ui->Typed[Len - 1] = '\0';
    }
    else if (Event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        Ui->Typing = false;
        snprintf(Ui->Message, sizeof(Ui->Message), "Cancelled.");
    }
    else if (Event.key.scancode == SDL_SCANCODE_RETURN)
    {
        if (Ui->Screen == WIZARD_BASE) TypedBase(Ui, Draft);
    }
}

static void HandleBase(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_UP:
        if (Ui->List > 0) Ui->List--;
        break;
    case SDL_SCANCODE_DOWN:
        if (Ui->List < BaseRowCount() - 1) Ui->List++;
        break;
    case SDL_SCANCODE_RETURN:
        ChooseBase(Ui, Draft);
        break;
    case SDL_SCANCODE_ESCAPE:
        Ui->Quit = true;
        break;
    default:
        break;
    }
}

// The plugin's own gate for "this axis is pushed", so the wizard and the game agree.
static const int kStickThreshold = 16000;

static N64Control CurrentControl(const WizardUi & Ui)
{
    return (N64Control)Ui.Control;
}

static void Advance(WizardUi * Ui)
{
    if (Ui->Control + 1 < (int)N64Control::Count)
    {
        Ui->Control++;
        Ui->Mode = WIZARD_MODE_NONE;
        Ui->List = 0;
    }
    else
    {
        Ui->Screen = WIZARD_REVIEW;
        Ui->Mode = WIZARD_MODE_NONE;
        Ui->List = 0;
        snprintf(Ui->Message, sizeof(Ui->Message), "S to save, Backspace to go back.");
    }
}

static void Bound(WizardUi * Ui, const WizardDraft & Draft)
{
    snprintf(Ui->Message, sizeof(Ui->Message), "%s is %s",
             WizardControlName(CurrentControl(*Ui)),
             Draft.Describe(CurrentControl(*Ui)).c_str());
    Ui->Mode = WIZARD_MODE_NONE;
}

// Mode 1: the next keydown is the binding, whatever it is. There is no cancel, because
// every key is a legal answer — Escape and the arrows included.
static bool CaptureKey(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return false;
    // SDL3 resends SDL_EVENT_KEY_DOWN for OS key repeat. Every keydown here is literal, so
    // an unfiltered repeat would keep re-binding (or, in WIZARD_MODE_STICK_KEYS, silently
    // finish the whole four-key sequence from one held key). Consumed but ignored.
    if (Event.key.repeat) return true;
    if (Ui->Mode == WIZARD_MODE_STICK_KEYS)
    {
        Ui->Keys[Ui->KeyStep] = Event.key.scancode;
        if (++Ui->KeyStep == 4)
        {
            Draft->SetStickKeys(Ui->Keys[0], Ui->Keys[1], Ui->Keys[2], Ui->Keys[3]);
            Ui->KeyStep = 0;
            Bound(Ui, *Draft);
        }
        return true;
    }
    Draft->SetKey(CurrentControl(*Ui), Event.key.scancode);
    Bound(Ui, *Draft);
    return true;
}

// Modes 2 and 3 do not consume keys, so Escape leaves them.
static bool CapturePad(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type == SDL_EVENT_KEY_DOWN && Event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        Ui->Mode = WIZARD_MODE_NONE;
        snprintf(Ui->Message, sizeof(Ui->Message), "Cancelled.");
        return true;
    }
    if (Ui->Mode == WIZARD_MODE_BUTTON && Event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        Draft->SetButton(CurrentControl(*Ui), (SDL_GamepadButton)Event.gbutton.button);
        Bound(Ui, *Draft);
        return true;
    }
    if (Ui->Mode == WIZARD_MODE_AXIS && Event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
    {
        if (Event.gaxis.value > kStickThreshold || Event.gaxis.value < -kStickThreshold)
        {
            Draft->SetAxis(CurrentControl(*Ui), (SDL_GamepadAxis)Event.gaxis.axis,
                           Event.gaxis.value > 0);
            Bound(Ui, *Draft);
        }
        return true;
    }
    return false;
}

static void ChooseStickForm(WizardUi * Ui, WizardDraft * Draft)
{
    switch (Ui->List)
    {
    case 0: Draft->SetStickWhole(false); break;
    case 1: Draft->SetStickWhole(true); break;
    case 2: Draft->SetStickPointer(); break;
    case 3: Draft->SetStickHead(false); break;
    case 4: Draft->SetStickHead(true); break;
    default:
        Ui->Mode = WIZARD_MODE_STICK_KEYS;
        Ui->KeyStep = 0;
        snprintf(Ui->Message, sizeof(Ui->Message), "Press the key for up.");
        return;
    }
    Bound(Ui, *Draft);
}

static bool CaptureStickForm(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return false;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_UP:
        if (Ui->List > 0) Ui->List--;
        return true;
    case SDL_SCANCODE_DOWN:
        if (Ui->List < WizardStickFormCount() - 1) Ui->List++;
        return true;
    case SDL_SCANCODE_RETURN:
        ChooseStickForm(Ui, Draft);
        return true;
    case SDL_SCANCODE_ESCAPE:
        Ui->Mode = WIZARD_MODE_NONE;
        snprintf(Ui->Message, sizeof(Ui->Message), "Cancelled.");
        return true;
    default:
        return true;
    }
}

static void HandleControl(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    switch (Ui->Mode)
    {
    case WIZARD_MODE_KEY:
    case WIZARD_MODE_STICK_KEYS:
        if (CaptureKey(Event, Ui, Draft)) return;
        break;
    case WIZARD_MODE_BUTTON:
    case WIZARD_MODE_AXIS:
        if (CapturePad(Event, Ui, Draft)) return;
        break;
    case WIZARD_MODE_STICK:
        if (CaptureStickForm(Event, Ui, Draft)) return;
        break;
    default:
        break;
    }
    if (Ui->Mode != WIZARD_MODE_NONE || Event.type != SDL_EVENT_KEY_DOWN) return;

    const bool IsStick = CurrentControl(*Ui) == N64Control::Stick;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_1:
        if (IsStick) { Ui->Mode = WIZARD_MODE_STICK; Ui->List = 0;
                       snprintf(Ui->Message, sizeof(Ui->Message), "Pick a form, Enter to take it."); }
        else { Ui->Mode = WIZARD_MODE_KEY;
               snprintf(Ui->Message, sizeof(Ui->Message), "Press any key. It is taken as it comes."); }
        break;
    case SDL_SCANCODE_2:
        if (IsStick) break;
        if (!Ui->HasGamepad)
        {
            snprintf(Ui->Message, sizeof(Ui->Message), "No gamepad connected.");
            break;
        }
        Ui->Mode = WIZARD_MODE_BUTTON;
        snprintf(Ui->Message, sizeof(Ui->Message), "Press a gamepad button. Escape cancels.");
        break;
    case SDL_SCANCODE_3:
        if (IsStick) break;
        if (!Ui->HasGamepad)
        {
            snprintf(Ui->Message, sizeof(Ui->Message), "No gamepad connected.");
            break;
        }
        Ui->Mode = WIZARD_MODE_AXIS;
        snprintf(Ui->Message, sizeof(Ui->Message), "Push a stick or trigger. Escape cancels.");
        break;
    case SDL_SCANCODE_RETURN:
        Advance(Ui);
        break;
    case SDL_SCANCODE_BACKSPACE:
        if (Ui->Control > 0) Ui->Control--;
        else Ui->Screen = WIZARD_BASE;
        Ui->Mode = WIZARD_MODE_NONE;
        break;
    case SDL_SCANCODE_DELETE:
        Draft->Clear(CurrentControl(*Ui));
        Bound(Ui, *Draft);
        break;
    case SDL_SCANCODE_ESCAPE:
        Ui->Screen = WIZARD_REVIEW;
        Ui->List = 0;
        snprintf(Ui->Message, sizeof(Ui->Message), "S to save, Backspace to go back.");
        break;
    default:
        break;
    }
}

void WizardHandleEvent(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft, uint32_t LitGestures)
{
    (void)LitGestures;
    if (Ui->Typing) { HandleTyping(Event, Ui, Draft); return; }
    if (Ui->Screen == WIZARD_BASE) HandleBase(Event, Ui, Draft);
    else if (Ui->Screen == WIZARD_CONTROL) HandleControl(Event, Ui, Draft);
}

static void DrawBase(SDL_Renderer * Renderer, const WizardUi & Ui)
{
    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, 24.0f, kHead, "Project64 binding wizard");
    WizardText(Renderer, 24.0f, 64.0f, kBody, "Start from:");
    for (int Row = 0; Row < BaseRowCount(); Row++)
    {
        const float Y = 96.0f + kLine * (float)Row;
        Colour(Renderer, Row == Ui.List);
        WizardText(Renderer, 40.0f, Y, kBody, Row == Ui.List ? ">" : " ");
        WizardText(Renderer, 64.0f, Y, kBody, BaseRowLabel(Row));
    }
    if (Ui.Typing)
    {
        Colour(Renderer, true);
        char Line[300];
        snprintf(Line, sizeof(Line), "path: %s_", Ui.Typed);
        WizardText(Renderer, 40.0f, 96.0f + kLine * (float)BaseRowCount() + 16.0f, kBody, Line);
    }
}

static void DrawControl(SDL_Renderer * Renderer, const WizardUi & Ui, const WizardDraft & Draft)
{
    const N64Control C = (N64Control)Ui.Control;
    char Line[320];

    Colour(Renderer, false);
    snprintf(Line, sizeof(Line), "Control %d of %d", Ui.Control + 1, (int)N64Control::Count);
    WizardText(Renderer, 24.0f, 24.0f, kBody, Line);

    Colour(Renderer, true);
    WizardText(Renderer, 24.0f, 56.0f, kHead, WizardControlName(C));

    Colour(Renderer, false);
    snprintf(Line, sizeof(Line), "now: %s", Draft.Describe(C).c_str());
    WizardTextFit(Renderer, 24.0f, 96.0f, kBody, Line, kWindowWidth - 24.0f);

    if (C == N64Control::Stick && Ui.Mode == WIZARD_MODE_STICK)
    {
        for (int Row = 0; Row < WizardStickFormCount(); Row++)
        {
            Colour(Renderer, Row == Ui.List);
            WizardText(Renderer, 40.0f, 140.0f + kLine * (float)Row, kBody,
                       Row == Ui.List ? ">" : " ");
            WizardTextFit(Renderer, 64.0f, 140.0f + kLine * (float)Row, kBody,
                          WizardStickFormLabel(Row), kWindowWidth - 64.0f);
        }
        return;
    }

    Colour(Renderer, false);
    if (C == N64Control::Stick)
    {
        WizardText(Renderer, 24.0f, 140.0f, kBody, "1  choose how the stick is driven");
    }
    else
    {
        WizardText(Renderer, 24.0f, 140.0f, kBody, "1  a keyboard key");
        WizardText(Renderer, 24.0f, 162.0f, kBody, "2  a gamepad button");
        WizardText(Renderer, 24.0f, 184.0f, kBody, "3  a gamepad axis");
        WizardText(Renderer, 24.0f, 206.0f, kBody, "4  a panel slot");
        WizardText(Renderer, 24.0f, 228.0f, kBody, "5  a face gesture");
    }
    // Split across two lines: at kBody scale each glyph advances 16px, and the single-line
    // version from the design ran to 912px in an 800px window.
    WizardText(Renderer, 24.0f, 268.0f, kBody, "Enter keep   Backspace back");
    WizardText(Renderer, 24.0f, 290.0f, kBody, "Delete inherit   Esc review");
}

void WizardDrawScreen(SDL_Renderer * Renderer, int W, int H, const WizardUi & Ui,
                      const WizardDraft & Draft, uint32_t Gestures, uint32_t Face)
{
    (void)W;
    (void)Gestures;
    (void)Face;
    if (Ui.Screen == WIZARD_BASE) DrawBase(Renderer, Ui);
    else if (Ui.Screen == WIZARD_CONTROL) DrawControl(Renderer, Ui, Draft);

    Colour(Renderer, false);
    WizardTextFit(Renderer, 24.0f, (float)H - 32.0f, kBody, Ui.Message, kWindowWidth - 24.0f);
}
