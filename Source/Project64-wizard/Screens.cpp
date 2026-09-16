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

float WizardText(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text)
{
    const float S = (float)Scale;
    SDL_SetRenderScale(Renderer, S, S);
    SDL_RenderDebugText(Renderer, X / S, Y / S, Text);
    SDL_SetRenderScale(Renderer, 1.0f, 1.0f);
    return (float)strlen(Text) * (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * S;
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

void WizardHandleEvent(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft, uint32_t LitGestures)
{
    (void)LitGestures;
    if (Ui->Typing) { HandleTyping(Event, Ui, Draft); return; }
    if (Ui->Screen == WIZARD_BASE) HandleBase(Event, Ui, Draft);
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

void WizardDrawScreen(SDL_Renderer * Renderer, int W, int H, const WizardUi & Ui,
                      const WizardDraft & Draft, uint32_t Gestures, uint32_t Face)
{
    (void)W;
    (void)Draft;
    (void)Gestures;
    (void)Face;
    if (Ui.Screen == WIZARD_BASE) DrawBase(Renderer, Ui);

    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, (float)H - 32.0f, kBody, Ui.Message);
}
