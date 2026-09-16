// Project64 - A Nintendo 64 emulator
// See Screens.h. Text is SDL's debug font: eight pixels square, ASCII only, scaled by the
// renderer. It is a utility font for a utility screen, and it spells a scancode name,
// which the emulator's twenty-one-glyph overlay font cannot.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Screens.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <string.h>

// SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE is 8; a scale of 2 reads comfortably at 800x640.
static const int kBody = 2;
static const int kHead = 3;
static const float kLine = 22.0f;
// The window is 800x640 and not resizable (see main.cpp, which drops SDL_WINDOW_RESIZABLE
// for this reason); screens are laid out for that fixed size, and every WizardTextFit budget
// below is derived from it rather than from the live window size.
static const float kWindowWidth = 800.0f;
static const float kWindowHeight = 640.0f;
// Where WizardDrawScreen (bottom of this file) draws Ui.Message: (float)H - 32.0f, with H
// the live window height. Screens laid out against this fixed-size assumption — as
// kWindowWidth already is — use this rather than repeat the literal.
static const float kMessageY = kWindowHeight - 32.0f;

void WizardText(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text)
{
    const float S = (float)Scale;
    SDL_SetRenderScale(Renderer, S, S);
    SDL_RenderDebugText(Renderer, X / S, Y / S, Text);
    SDL_SetRenderScale(Renderer, 1.0f, 1.0f);
}

// How many glyphs of Scale-scaled debug text fit in MaxWidth pixels. The debug font is fixed
// pitch, so this is the whole of the font geometry any of the three fitting sites below needs
// — the head fit, the tail fit, and DrawReview's split of a reader message across two lines.
static int FitChars(int Scale, float MaxWidth)
{
    const int GlyphWidth = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * Scale;
    return GlyphWidth > 0 ? (int)(MaxWidth / (float)GlyphWidth) : 0;
}

void WizardTextFit(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text, float MaxWidth)
{
    const int MaxChars = FitChars(Scale, MaxWidth);
    if ((int)strlen(Text) <= MaxChars)
    {
        WizardText(Renderer, X, Y, Scale, Text);
        return;
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
    WizardText(Renderer, X, Y, Scale, Buffer);
}

// Like WizardTextFit, but keeps the *tail* of Text — a leading "..." then as many trailing
// characters as fit — instead of the head. Head-fitting is right for prose, where the start
// carries the meaning, but wrong for a path: the start is a prefix the player already knows
// or cannot act on ("/Users/.../"), while the end is the filename, or in an in-progress
// typed path, the caret that has to stay visible for the player to see what they are doing.
static void WizardTextFitTail(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text, float MaxWidth)
{
    const int MaxChars = FitChars(Scale, MaxWidth);
    const int Len = (int)strlen(Text);
    if (Len <= MaxChars)
    {
        WizardText(Renderer, X, Y, Scale, Text);
        return;
    }
    char Buffer[256];
    const int Tail = MaxChars > 3 ? MaxChars - 3 : (MaxChars > 0 ? MaxChars : 0);
    const char * From = Text + (Len - Tail);
    if (MaxChars > 3)
    {
        snprintf(Buffer, sizeof(Buffer), "...%s", From);
    }
    else
    {
        snprintf(Buffer, sizeof(Buffer), "%s", From);
    }
    WizardText(Renderer, X, Y, Scale, Buffer);
}

static void Colour(SDL_Renderer * Renderer, bool Highlight)
{
    if (Highlight) SDL_SetRenderDrawColor(Renderer, 255, 220, 120, 255);
    else SDL_SetRenderDrawColor(Renderer, 220, 220, 220, 255);
}

// The mirror of EnterControlScreen and EnterReview below: every path onto the base screen
// lands here, so the screen's own instruction arrives with it rather than being spelled at
// each arrival — and so a screen the player leaves never leaves its instruction behind.
//
// Row is reset for the same reason EnterControlScreen resets it, and here it is the one
// that bites: mode 5 (or the stick-form picker) can leave Row as high as 10, and this
// screen has 7 rows. Unreset, ChooseBase would take a shipped-layout branch with an
// out-of-range index and a load that silently fails.
static void EnterBaseScreen(WizardUi * Ui)
{
    Ui->Screen = WIZARD_BASE;
    Ui->Mode = WIZARD_MODE_NONE;
    Ui->Row = 0;
    snprintf(Ui->Message, sizeof(Ui->Message), "Up and Down to move, Enter to choose.");
}

void WizardUiInit(WizardUi * Ui)
{
    memset(Ui, 0, sizeof(*Ui));
    snprintf(Ui->Base, sizeof(Ui->Base), "the built-in bindings");
    EnterBaseScreen(Ui);
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
    snprintf(Out, Size, "%s%s", Dir != nullptr ? Dir : "", WizardBaseFile(Row - 1));
}

// Every path onto the control screen lands here, so Row (whatever list the base screen
// left highlighted) and Mode never leak across the transition. Five later tasks add lists
// of their own to the same field, which is why this is the one place that resets it. The
// review screen's Backspace is the one caller that wants a control other than 0 — it lands
// on the last control rather than the first — hence the parameter.
static void EnterControlScreen(WizardUi * Ui, N64Control Control)
{
    Ui->Screen = WIZARD_CONTROL;
    Ui->Control = Control;
    Ui->Mode = WIZARD_MODE_NONE;
    Ui->Row = 0;
    snprintf(Ui->Message, sizeof(Ui->Message), "1-5 to bind, Enter to keep, Delete to inherit.");
}

static void ChooseBase(WizardUi * Ui, WizardDraft * Draft)
{
    if (Ui->Row == 0)
    {
        Draft->LoadDefaults();
        snprintf(Ui->Base, sizeof(Ui->Base), "the built-in bindings");
    }
    else if (Ui->Row == BaseRowCount() - 1)
    {
        Ui->Typing = true;
        Ui->Typed[0] = '\0';
        snprintf(Ui->Message, sizeof(Ui->Message), "Type a path, then Enter. Escape cancels.");
        return;
    }
    else
    {
        char Path[512];
        BasePath(Ui->Row, Path, sizeof(Path));
        if (!Draft->LoadBase(Path))
        {
            snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
            // The screen gets 48 glyphs; stderr gets the whole thing, with the path the
            // message has no room for. One line, and never a non-zero exit: a base that will
            // not load is something the player picks again, not a reason to quit. (This is an
            // fprintf, not an SDL device call, so it does not break --selftest's headless
            // drive of these same handlers.)
            fprintf(stderr, "wizard: base %s: %s\n", Path, Draft->Error());
            return;
        }
        snprintf(Ui->Base, sizeof(Ui->Base), "%s", WizardBaseFile(Ui->Row - 1));
    }
    EnterControlScreen(Ui, N64Control::A);
}

static void TypedBase(WizardUi * Ui, WizardDraft * Draft)
{
    Ui->Typing = false;
    if (!Draft->LoadBase(Ui->Typed))
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
        // Same one line as ChooseBase above, and for the typed path it matters more: a typo
        // is easiest to spot against the path as it was actually read.
        fprintf(stderr, "wizard: base %s: %s\n", Ui->Typed, Draft->Error());
        return;
    }
    snprintf(Ui->Base, sizeof(Ui->Base), "%s", Ui->Typed);
    EnterControlScreen(Ui, N64Control::A);
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
        else { Ui->Typing = false; snprintf(Ui->Message, sizeof(Ui->Message), "Enter again to save."); }
    }
}

static void HandleBase(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_UP:
        if (Ui->Row > 0) Ui->Row--;
        break;
    case SDL_SCANCODE_DOWN:
        if (Ui->Row < BaseRowCount() - 1) Ui->Row++;
        break;
    case SDL_SCANCODE_RETURN:
        // One-shot, unlike Up/Down above: ChooseBase can move Ui->Screen to WIZARD_CONTROL,
        // and once it does, this switch is no longer what routes this key at all — a repeat
        // of the same held Enter, processed as the next event, goes to HandleControl instead
        // and reads there as a fresh Enter, advancing straight past the first control.
        // Filtering the repeat here stops a held Enter from choosing a base more than once.
        if (Event.key.repeat) break;
        ChooseBase(Ui, Draft);
        break;
    case SDL_SCANCODE_ESCAPE:
        // Quit is idempotent, but there is no reason a held Escape should act more than the
        // one press the player made.
        if (Event.key.repeat) break;
        Ui->Quit = true;
        break;
    default:
        break;
    }
}

// The plugin's own gate for "this axis is pushed", so the wizard and the game agree.
static const int kStickThreshold = 16000;

// Defined further down, alongside HandleReview and DrawReview, but Advance (and
// HandleControl's Escape case below) need to call it before that point in the file.
static void EnterReview(WizardUi * Ui, WizardDraft * Draft);

// N64Control is an enum class, so the three places that step through the fifteen controls —
// here, HandleControl's Backspace and HandleReview's — spell the cast rather than every read
// of Ui->Control spelling one.
static N64Control ControlAt(int Index) { return (N64Control)Index; }

static void Advance(WizardUi * Ui, WizardDraft * Draft)
{
    if ((int)Ui->Control + 1 < (int)N64Control::Count)
    {
        Ui->Control = ControlAt((int)Ui->Control + 1);
        Ui->Mode = WIZARD_MODE_NONE;
        Ui->Row = 0;
    }
    else
    {
        EnterReview(Ui, Draft);
    }
}

// Reports what the control is now bound to, and leaves whatever capture mode was armed. The
// second half is not incidental: every capture function relies on the mode going back to NONE
// inside the call that consumed the keydown, which is what puts the repeats of that same held
// key in front of HandleControl's repeat guard instead of back into the capture. Cancelled
// below is the same exit for the other outcome.
static void ShowBindingAndLeaveMode(WizardUi * Ui, const WizardDraft & Draft)
{
    snprintf(Ui->Message, sizeof(Ui->Message), "%s is %s",
             WizardControlName(Ui->Control),
             Draft.Describe(Ui->Control).c_str());
    Ui->Mode = WIZARD_MODE_NONE;
}

// Escape out of an armed capture. Four capture functions below end this way, and all four
// must clear the mode in the same call that consumes the keydown: HandleControl's outer
// repeat guard is what filters the repeats of that same held Escape, and it only ever sees
// them because the mode is already back to NONE by then (see the comment there).
static void Cancelled(WizardUi * Ui)
{
    Ui->Mode = WIZARD_MODE_NONE;
    snprintf(Ui->Message, sizeof(Ui->Message), "Cancelled.");
}

// The four steps of WIZARD_MODE_STICK_KEYS, in the order ChooseStickForm starts them and
// SetStickKeys below takes them (Up, Down, Left, Right) — index by Ui->KeyStep *after* it
// has advanced, to name the direction still wanted.
static const char * const kStickKeyDirections[4] = { "up", "down", "left", "right" };

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
            ShowBindingAndLeaveMode(Ui, *Draft);
        }
        else
        {
            // Without this, "now: <Describe>" does not change until all four keys are in,
            // and the message line still reads "Press the key for up." after up was already
            // taken — indistinguishable from the keypress not registering at all. Fits the
            // 48-glyph budget at x=24 whole: the longest case, "Press the key for right.", is
            // 24 characters.
            snprintf(Ui->Message, sizeof(Ui->Message), "Press the key for %s.",
                     kStickKeyDirections[Ui->KeyStep]);
        }
        return true;
    }
    Draft->SetKey(Ui->Control, Event.key.scancode);
    ShowBindingAndLeaveMode(Ui, *Draft);
    return true;
}

// Modes 2 and 3 do not consume keys, so Escape leaves them.
static bool CapturePad(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type == SDL_EVENT_KEY_DOWN && Event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        Cancelled(Ui);
        return true;
    }
    if (Ui->Mode == WIZARD_MODE_BUTTON && Event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        Draft->SetButton(Ui->Control, (SDL_GamepadButton)Event.gbutton.button);
        ShowBindingAndLeaveMode(Ui, *Draft);
        return true;
    }
    if (Ui->Mode == WIZARD_MODE_AXIS && Event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
    {
        if (Event.gaxis.value > kStickThreshold || Event.gaxis.value < -kStickThreshold)
        {
            Draft->SetAxis(Ui->Control, (SDL_GamepadAxis)Event.gaxis.axis,
                           Event.gaxis.value > 0);
            ShowBindingAndLeaveMode(Ui, *Draft);
        }
        return true;
    }
    return false;
}

static void ChooseStickForm(WizardUi * Ui, WizardDraft * Draft)
{
    switch (Ui->Row)
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
    ShowBindingAndLeaveMode(Ui, *Draft);
}

static bool CaptureStickForm(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    // The design's Part 3: "{stick: left} and {stick: right} (chosen, or captured by moving a
    // gamepad stick)". Pushing a stick is the obvious way to say which stick you mean, and
    // until now the list could only be driven by the arrow keys. A decisive push moves the
    // highlight onto that stick's row and takes it, exactly as if the player had arrowed there
    // and pressed Enter — hence going through Ui->Row and ChooseStickForm rather than calling
    // SetStickWhole directly, so a row and its setter still have one place that pairs them.
    if (Event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
    {
        // Gated on HasGamepad the same way modes 2 and 3 are. main.cpp sets it from the pad it
        // has open; this file reads an event and never asks a device anything, which is what
        // lets --selftest drive these handlers with no SDL at all.
        if (!Ui->HasGamepad) return true;
        const int Axis = (int)Event.gaxis.axis;
        const bool LeftStick = Axis == SDL_GAMEPAD_AXIS_LEFTX || Axis == SDL_GAMEPAD_AXIS_LEFTY;
        const bool RightStick = Axis == SDL_GAMEPAD_AXIS_RIGHTX || Axis == SDL_GAMEPAD_AXIS_RIGHTY;
        // A trigger is an axis too, and neither stick: it must not pick a row.
        if (!LeftStick && !RightStick) return true;
        // kStickThreshold is the plugin's own STICK_THRESHOLD (PluginInput.cpp), reused rather
        // than invented so the wizard and the game agree on what counts as pushed — and so a
        // resting or slightly drifting stick, which sits well inside it, never picks a form the
        // player did not ask for.
        if (Event.gaxis.value <= kStickThreshold && Event.gaxis.value >= -kStickThreshold)
        {
            return true;
        }
        Ui->Row = LeftStick ? 0 : 1;
        ChooseStickForm(Ui, Draft);
        return true;
    }
    if (Event.type != SDL_EVENT_KEY_DOWN) return false;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_UP:
        if (Ui->Row > 0) Ui->Row--;
        return true;
    case SDL_SCANCODE_DOWN:
        if (Ui->Row < WizardStickFormCount() - 1) Ui->Row++;
        return true;
    case SDL_SCANCODE_RETURN:
        // No repeat guard needed: ChooseStickForm always moves Ui->Mode away from STICK
        // before this call returns (to NONE, or to STICK_KEYS for the "type it" row), so the
        // mode dispatch in HandleControl never routes a repeat of this same held Enter back
        // to this case — it falls through to HandleControl's own switch instead, which is
        // where that repeat is actually filtered.
        ChooseStickForm(Ui, Draft);
        return true;
    case SDL_SCANCODE_ESCAPE:
        // Same reasoning as Enter above: Cancelled sets Ui->Mode to NONE directly, so a
        // repeat of this held key is never routed back here either.
        Cancelled(Ui);
        return true;
    default:
        return true;
    }
}

// The panel as the game lays it out: a 640x640 window, the game image on top, the
// thirteen slots below. The wizard draws that rectangle scaled into its own window, so a
// slot is exactly where the player will find it in the game.
static const int kPanelW = 640;
static const int kPanelH = 640;
static const float kPanelScale = 0.5f;
static const float kPanelX = 24.0f;
static const float kPanelY = 300.0f;

static SDL_FRect ZoneScreenRect(int Zone)
{
    float X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
    PointerZoneRect(Zone, kPanelW, kPanelH, &X0, &Y0, &X1, &Y1);
    SDL_FRect R;
    R.x = kPanelX + X0 * kPanelScale;
    R.y = kPanelY + Y0 * kPanelScale;
    R.w = (X1 - X0) * kPanelScale;
    R.h = (Y1 - Y0) * kPanelScale;
    return R;
}

// The zone under a click in the wizard's window, or POINTER_ZONE_NONE.
static int ZoneAtPoint(float X, float Y)
{
    for (int Zone = 0; Zone < POINTER_ZONE_COUNT; Zone++)
    {
        const SDL_FRect R = ZoneScreenRect(Zone);
        if (X >= R.x && X < R.x + R.w && Y >= R.y && Y < R.y + R.h) return Zone;
    }
    return POINTER_ZONE_NONE;
}

static void DrawPanel(SDL_Renderer * Renderer, const WizardDraft & Draft)
{
    // The game image first, so the slots sit on top of it.
    for (int Zone = POINTER_ZONE_COUNT - 1; Zone >= 0; Zone--)
    {
        const SDL_FRect R = ZoneScreenRect(Zone);
        SDL_SetRenderDrawColor(Renderer, Zone == POINTER_ZONE_GAME ? 28 : 44,
                               Zone == POINTER_ZONE_GAME ? 28 : 44,
                               Zone == POINTER_ZONE_GAME ? 34 : 52, 255);
        SDL_RenderFillRect(Renderer, &R);

        // Whatever the draft already puts in this slot, so the choice is made in context.
        // The same two-character label the game's own overlay draws here, not the full
        // control name, which would spill across neighbouring slots. Stick has no overlay
        // label, so it falls back to the first two characters of its full name ("St").
        const N64Control Owner = Draft.ZoneOwner(Zone);
        if (Owner != N64Control::Count)
        {
            const char * Label = InputConfig::ControlLabel(Owner);
            char Short[3];
            if (Label[0] == '\0')
            {
                snprintf(Short, sizeof(Short), "%.2s", WizardControlName(Owner));
                Label = Short;
            }
            Colour(Renderer, false);
            WizardText(Renderer, R.x + 4.0f, R.y + 4.0f, 1, Label);
        }
    }
}

static bool CaptureZone(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type == SDL_EVENT_KEY_DOWN && Event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        Cancelled(Ui);
        return true;
    }
    if (Event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || Event.button.button != SDL_BUTTON_LEFT) return false;
    const int Zone = ZoneAtPoint(Event.button.x, Event.button.y);
    if (Zone == POINTER_ZONE_NONE)
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "That is a gap. Click a slot or the game image.");
        return true;
    }
    Draft->SetZone(Ui->Control, Zone);
    ShowBindingAndLeaveMode(Ui, *Draft);
    return true;
}

static bool CaptureGesture(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft,
                           uint32_t LitGestures)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return false;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_UP:
        if (Ui->Row > 0) Ui->Row--;
        return true;
    case SDL_SCANCODE_DOWN:
        if (Ui->Row < POINTER_GESTURE_COUNT - 1) Ui->Row++;
        return true;
    case SDL_SCANCODE_RETURN:
        // No repeat guard needed: ShowBindingAndLeaveMode() below sets Ui->Mode to NONE
        // before this call returns, so the mode dispatch in HandleControl never routes a
        // repeat of this same held Enter back to this case — it falls through to
        // HandleControl's own switch instead, which is where that repeat is actually filtered.
        Draft->SetGesture(Ui->Control, 1u << Ui->Row);
        ShowBindingAndLeaveMode(Ui, *Draft);
        return true;
    case SDL_SCANCODE_SPACE:
    {
        // Unlike Enter and Escape in this function, Space does not always leave GESTURE mode
        // — see the "else" branch below, which only updates the message and leaves Ui->Mode
        // untouched. So a held Space, unlike a held Enter or Escape, genuinely can be
        // re-dispatched to this case while still repeating; filtered so it re-evaluates which
        // gesture is firing once per press rather than once per repeat. (When exactly one
        // gesture is firing, ShowBindingAndLeaveMode() below exits GESTURE mode before any
        // repeat could arrive, same as Enter — this guard matters only for the
        // ambiguous/nothing-firing branch.)
        if (Event.key.repeat) return true;
        // Only when exactly one gesture is firing: two at once is ambiguous, and taking
        // the lower bit would silently pick for the player.
        int Lit = -1, Count = 0;
        for (int i = 0; i < POINTER_GESTURE_COUNT; i++)
        {
            if ((LitGestures & (1u << i)) != 0) { Lit = i; Count++; }
        }
        if (Count == 1)
        {
            Draft->SetGesture(Ui->Control, 1u << Lit);
            ShowBindingAndLeaveMode(Ui, *Draft);
        }
        else
        {
            // Ui->Message renders through WizardTextFit at x=24 in a 776px budget: 48
            // glyphs at kBody. Both branches must fit whole, not truncate mid-word.
            snprintf(Ui->Message, sizeof(Ui->Message),
                     Count == 0 ? "Nothing is firing. Hold it, or pick a row."
                                : "More than one is firing. Pick a row with Enter.");
        }
        return true;
    }
    case SDL_SCANCODE_ESCAPE:
        // No repeat guard needed, for the same reason as Enter above: Cancelled sets
        // Ui->Mode to NONE directly, so a repeat of this same held key is never routed
        // back here.
        Cancelled(Ui);
        return true;
    default:
        return true;
    }
}

static const char * FaceStatusText(uint32_t Face)
{
    switch (Face)
    {
    case FACE_OFF: return "camera off: the list still works, unlit";
    case FACE_STARTING: return "camera starting";
    case FACE_TRACKING: return "tracking";
    case FACE_NO_FACE: return "no face found";
    case FACE_DENIED: return "camera denied in Settings > Privacy & Security";
    default: return "camera unavailable";
    }
}

static void DrawGestures(SDL_Renderer * Renderer, const WizardUi & Ui, uint32_t Gestures,
                         uint32_t Face)
{
    for (int Row = 0; Row < POINTER_GESTURE_COUNT; Row++)
    {
        const float Y = 140.0f + kLine * (float)Row;
        const bool Firing = (Gestures & (1u << Row)) != 0;
        Colour(Renderer, Row == Ui.Row || Firing);
        WizardText(Renderer, 40.0f, Y, kBody, Row == Ui.Row ? ">" : " ");
        WizardText(Renderer, 64.0f, Y, kBody, PointerGestureTag(Row));
        WizardText(Renderer, 112.0f, Y, kBody, PointerGestureName(Row));
        if (Firing) WizardText(Renderer, 320.0f, Y, kBody, "<- now");
    }
    Colour(Renderer, false);
    // Every FaceStatusText string fits x=40's budget whole (kWindowWidth - 40.0f), but
    // this is variable text like any other status line, so it gets the same Fit
    // truncation as a defence against a future string that doesn't.
    WizardTextFit(Renderer, 40.0f, 140.0f + kLine * (float)POINTER_GESTURE_COUNT + 12.0f, kBody,
                  FaceStatusText(Face), kWindowWidth - 40.0f);
}

// Modes 2 and 3 are the two that need a pad open, and they refuse the same way. Gated on
// Ui->HasGamepad, which main.cpp sets from the pad it actually has open: this file reads
// events and never asks a device anything, which is what lets --selftest drive these
// handlers with no SDL at all. Both prompts fit the message line's 48 glyphs whole.
static void ArmGamepadMode(WizardUi * Ui, WizardMode Mode, const char * Prompt)
{
    if (!Ui->HasGamepad)
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "No gamepad connected.");
        return;
    }
    Ui->Mode = Mode;
    snprintf(Ui->Message, sizeof(Ui->Message), "%s", Prompt);
}

static void HandleControl(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft,
                          uint32_t LitGestures)
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
    case WIZARD_MODE_ZONE:
        if (CaptureZone(Event, Ui, Draft)) return;
        break;
    case WIZARD_MODE_GESTURE:
        if (CaptureGesture(Event, Ui, Draft, LitGestures)) return;
        break;
    default:
        break;
    }
    if (Ui->Mode != WIZARD_MODE_NONE || Event.type != SDL_EVENT_KEY_DOWN) return;
    // Every key this switch handles (1..5, Enter, Backspace, Delete, Escape) is one-shot, so
    // one guard ahead of it is enough — unlike HandleBase above, nothing here needs a held
    // key to repeat. This is also where the real fix for the capture-function repeat hazard
    // lives: a capture function (CaptureGesture, CaptureStickForm, CapturePad, CaptureZone)
    // that finishes or cancels sets Ui->Mode to NONE inside the same call that handles the
    // first, non-repeat keydown, so the mode dispatch above never re-enters that function for
    // the repeat that follows — it falls through to here instead. Two earlier rounds put the
    // repeat filter inside those capture functions, where it could never see the repeat it
    // was meant to catch; filtering it here, ahead of the switch those repeats actually reach,
    // is what stops a held Enter from also calling Advance() or a held Escape from also
    // jumping to the review, right after a capture already consumed the first press.
    if (Event.key.repeat) return;

    const bool IsStick = Ui->Control == N64Control::Stick;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_1:
        if (IsStick) { Ui->Mode = WIZARD_MODE_STICK; Ui->Row = 0;
                       // With a pad open, CaptureStickForm also takes a stick push as the
                       // choice, so the instruction says so — an undiscoverable shortcut is
                       // not the one the design asked for. Both strings fit 48 glyphs whole.
                       snprintf(Ui->Message, sizeof(Ui->Message),
                                Ui->HasGamepad ? "Pick a form and Enter, or push a stick."
                                               : "Pick a form, Enter to take it."); }
        else { Ui->Mode = WIZARD_MODE_KEY;
               snprintf(Ui->Message, sizeof(Ui->Message), "Press any key. It is taken as it comes."); }
        break;
    case SDL_SCANCODE_2:
        if (IsStick) break;
        ArmGamepadMode(Ui, WIZARD_MODE_BUTTON, "Press a gamepad button. Escape cancels.");
        break;
    case SDL_SCANCODE_3:
        if (IsStick) break;
        ArmGamepadMode(Ui, WIZARD_MODE_AXIS, "Push a stick or trigger. Escape cancels.");
        break;
    case SDL_SCANCODE_4:
        if (IsStick) break;
        Ui->Mode = WIZARD_MODE_ZONE;
        snprintf(Ui->Message, sizeof(Ui->Message), "Click a slot or the game image. Escape cancels.");
        break;
    case SDL_SCANCODE_5:
        if (IsStick) break;
        Ui->Mode = WIZARD_MODE_GESTURE;
        Ui->Row = 0;
        Ui->WantCamera = true;
        // The message line is the only instruction visible in mode 5 (its takeover drops
        // the Enter/Delete help line), and it renders through WizardTextFit at 48 glyphs
        // wide, so this has to fit whole rather than truncate mid-word.
        snprintf(Ui->Message, sizeof(Ui->Message),
                 "Arrows and Enter, or Space for what is firing.");
        break;
    case SDL_SCANCODE_RETURN:
        Advance(Ui, Draft);
        break;
    case SDL_SCANCODE_BACKSPACE:
        if ((int)Ui->Control > 0)
        {
            Ui->Control = ControlAt((int)Ui->Control - 1);
            Ui->Mode = WIZARD_MODE_NONE;
            // Whatever list the mode that was open left highlighted must not follow the
            // player onto the control they just stepped back to — the same reset, and the
            // same reason, as EnterControlScreen's.
            Ui->Row = 0;
        }
        else
        {
            // Task 8's ddab7f0 fixed this same defect in the other direction, routing the
            // review screen's Backspace through EnterControlScreen so it picks up the control
            // screen's own message rather than leaving its predecessor's behind (see the
            // comment on HandleReview's Backspace case). This is the half of that edge going
            // the other way: without the base screen's own message here, it would keep
            // reading "1-5 to bind, Enter to keep, Delete to inherit." — an instruction for a
            // screen the player just left.
            EnterBaseScreen(Ui);
        }
        break;
    case SDL_SCANCODE_DELETE:
        Draft->Clear(Ui->Control);
        ShowBindingAndLeaveMode(Ui, *Draft);
        break;
    case SDL_SCANCODE_ESCAPE:
        EnterReview(Ui, Draft);
        break;
    default:
        break;
    }
}

// Arriving at the review runs the same round-trip that saving will, so a mapping the
// reader refuses says so here — in the reader's words — rather than at the last step.
static void EnterReview(WizardUi * Ui, WizardDraft * Draft)
{
    Ui->Screen = WIZARD_REVIEW;
    Ui->Mode = WIZARD_MODE_NONE;
    Ui->Row = 0;
    if (Draft->Validate(Ui->Base))
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "S to save, Backspace to go back.");
    }
    else
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
        // The third of the spec's three failures: a round-trip the reader rejects. One line,
        // the reader's own words, and no change to the exit code. Draft->Error() is that line
        // with Validate's temp path already cut out of it, so it normally begins with the
        // ":<line>:<col>:" that followed the path; the leading colon is skipped so this does
        // not read "mapping: :4:14: ...", and a reader message that never named a file (and
        // so has no leading colon) prints unchanged.
        const char * Err = Draft->Error();
        fprintf(stderr, "wizard: the reader rejected this mapping%s%s\n",
                Err[0] == ':' ? " at " : ": ", Err[0] == ':' ? Err + 1 : Err);
    }
}

// No WizardDraft parameter, unlike its three sibling handlers: nothing this screen's keys do
// touches the draft. S opens the save screen, Backspace goes back to the last control, and
// Escape quits. The validate-on-arrival that the review does run belongs to EnterReview.
static void HandleReview(const SDL_Event & Event, WizardUi * Ui)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return;
    // Every key here is one-shot (there is no list to scroll on this screen), so one guard
    // ahead of the switch is enough — the same shape HandleControl's outer switch uses, and
    // for the same reason: without it, a held key re-fires the action on every OS repeat.
    if (Event.key.repeat) return;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_S:
        Ui->Screen = WIZARD_SAVE;
        Ui->SaveChoice = WIZARD_SAVE_DEFAULT;
        Ui->ConfirmOverwrite = false;
        Ui->ConfirmDefault = false;
        snprintf(Ui->Message, sizeof(Ui->Message), "1, 2 or 3, then Enter.");
        break;
    case SDL_SCANCODE_BACKSPACE:
        // Routed through EnterControlScreen, the same as every other path onto this screen,
        // so the control screen's own message ("1-5 to bind...") replaces this screen's —
        // rather than leaving "S to save, Backspace to go back." on a screen where neither
        // key does anything.
        EnterControlScreen(Ui, ControlAt((int)N64Control::Count - 1));
        break;
    case SDL_SCANCODE_ESCAPE:
        Ui->Quit = true;
        break;
    default:
        break;
    }
}

// No WizardUi parameter, the same as DrawBase takes no WizardDraft: this screen draws the
// draft and a fixed instruction, and has no highlighted row or typed path of its own. The
// message line every screen shares is drawn by WizardDrawScreen, which has the Ui.
static void DrawReview(SDL_Renderer * Renderer, const WizardDraft & Draft)
{
    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, 24.0f, kHead, "Review");
    char Line[320];
    int Shared = 0;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        const N64Control C = (N64Control)i;
        const bool Twice = Draft.SharesInput(C);
        if (Twice) Shared++;
        Colour(Renderer, Draft.Explicit(C));
        snprintf(Line, sizeof(Line), "%-10s %s%s", WizardControlName(C),
                 Draft.Describe(C).c_str(), Twice ? "   (also bound elsewhere)" : "");
        // Fifteen Describe strings land here, the longest 48-59 characters once the control
        // name and "(also bound elsewhere)" are folded in — well past the 48-glyph budget
        // this window gives a line starting at x=24. WizardTextFit exists for exactly this.
        WizardTextFit(Renderer, 24.0f, 64.0f + kLine * (float)i, kBody, Line, kWindowWidth - 24.0f);
    }
    const float SummaryY = 64.0f + kLine * (float)N64Control::Count + 12.0f;
    if (Shared > 0)
    {
        Colour(Renderer, true);
        // Short enough to fit whole at 48 glyphs: the longer form this replaced ("...allowed;
        // press S to save anyway.") ran to 67 characters and lost its own instruction to
        // WizardTextFit's "..." — the player never learned the save was still available.
        snprintf(Line, sizeof(Line), "%d controls share an input. That is allowed.", Shared);
        WizardTextFit(Renderer, 24.0f, SummaryY, kBody, Line, kWindowWidth - 24.0f);
    }

    // The reader's verdict, in the empty space between the summary line above and the
    // message line WizardDrawScreen draws at the bottom of every screen. WizardDraft::Validate
    // already stripped its own temp file's path out of this string, but a reason can still run
    // well past one line's 48 glyphs, so it gets two here rather than the one the message line
    // has room for.
    const char * Err = Draft.Error();
    if (Err[0] != '\0')
    {
        Colour(Renderer, true);
        const int MaxChars = FitChars(kBody, kWindowWidth - 24.0f);
        const int ErrLen = (int)strlen(Err);
        const int FirstLen = ErrLen < MaxChars ? ErrLen : MaxChars;
        char First[64];
        snprintf(First, sizeof(First), "%.*s", FirstLen, Err);
        WizardText(Renderer, 24.0f, SummaryY + kLine, kBody, First);
        if (ErrLen > FirstLen)
        {
            WizardTextFit(Renderer, 24.0f, SummaryY + kLine * 2.0f, kBody, Err + FirstLen,
                          kWindowWidth - 24.0f);
        }
    }

    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, kMessageY - kLine, kBody, "Escape quits.");
}

// Where the three destinations put the file.
static void SavePath(const WizardUi & Ui, char * Out, size_t Size)
{
    if (Ui.SaveChoice == WIZARD_SAVE_DEFAULT)
    {
        const char * Dir = SDL_GetBasePath();
        snprintf(Out, Size, "%sConfig/input.yaml", Dir != nullptr ? Dir : "");
        return;
    }
    if (Ui.SaveChoice == WIZARD_SAVE_ROM)
    {
        if (Ui.Typed[0] == '\0')
        {
            // An empty ROM field has no directory to write beside. Without this, SavePath
            // would find no dot in "", append ".yaml", and return ".yaml" — a path that
            // starts with '.', so the empty-path guard in DoSave never fires, and the file
            // lands in the process's current directory rather than beside any ROM the player
            // chose.
            Out[0] = '\0';
            return;
        }
        // Beside the ROM, named after it: the per-ROM lookup finds it with no input=.
        snprintf(Out, Size, "%s", Ui.Typed);
        char * Dot = strrchr(Out, '.');
        char * Slash = strrchr(Out, '/');
        if (Dot != nullptr && (Slash == nullptr || Dot > Slash)) *Dot = '\0';
        const size_t Len = strlen(Out);
        if (Len == 0 || Out[Len - 1] == '/')
        {
            // A folder, not a ROM: same problem as the empty field above — there is no
            // filename to name the mapping after, so this would otherwise produce a hidden
            // "<folder>/.yaml".
            Out[0] = '\0';
            return;
        }
        snprintf(Out + Len, Size - Len, ".yaml");
        return;
    }
    snprintf(Out, Size, "%s", Ui.Typed);
}

// Under Config/mouse/ or Config/face/, which make deletes and recopies on every build — so
// the question this answers is "will the next build take this file away?", not whether
// anything is being overwritten right now. Matched both with a leading slash (an absolute
// path, or anywhere nested under one) and as a bare prefix (a path typed as
// "Config/mouse/mine.yaml" after cd-ing into Bin/macOS, where the binary lives) — the
// leading-slash form alone misses that second, entirely plausible case.
static bool IsOverwrittenByMake(const char * Path)
{
    if (strstr(Path, "/Config/mouse/") != nullptr || strstr(Path, "/Config/face/") != nullptr) return true;
    return strncmp(Path, "Config/mouse/", strlen("Config/mouse/")) == 0 ||
           strncmp(Path, "Config/face/", strlen("Config/face/")) == 0;
}

// The default mapping, which AGENTS.md's traps require to stay keyboard-active. Matched as
// a suffix, both of an absolute path (SavePath's choice 1 builds SDL_GetBasePath() plus
// "Config/input.yaml") and of a path typed bare from Bin/macOS, the same two shapes
// IsOverwrittenByMake above matches. Suffix, not substring: "Config/input.yaml.bak" and
// "Config/mouse/input.yaml" are other files and must not warn.
static bool IsDefaultInput(const char * Path)
{
    static const char kTail[] = "/Config/input.yaml";
    const size_t TailLen = sizeof(kTail) - 1;
    const size_t Len = strlen(Path);
    if (Len >= TailLen && strcmp(Path + (Len - TailLen), kTail) == 0) return true;
    return strcmp(Path, "Config/input.yaml") == 0;
}

// True when the draft explicitly binds anything the keyboard and gamepad alone cannot
// produce. Binding::Kind (Source/Project64-sdl/InputConfig.h) has nine values; Key, Button,
// Axis, Stick (a gamepad stick, {stick: left} or {stick: right}) and Keys (four keyboard
// keys) are the keyboard-and-gamepad ones. The other four are exactly the kinds this looks
// for: Zone is a mouse-panel slot, Face a camera gesture, Pointer the Stick form
// {stick: pointer}, and HeadStick the Stick forms {stick: head} and {stick: head-digital}
// (code 0 and 1) — every one of them needs a mouse or the camera to be usable at all.
//
// Inherited controls are skipped because Emit never writes them, so they cannot be what
// lands in the file. Every binding of an explicit control is checked, not just the first:
// the wizard's own setters always produce exactly one, but LoadBase copies whatever the
// reader resolved, and a warning that silently looked at half a control would be worse
// than none.
static bool HasNonKeyboardBinding(const WizardDraft & Draft)
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (!Draft.Explicit((N64Control)i)) continue;
        const std::vector<Binding> & B = Draft.Bindings((N64Control)i);
        for (size_t b = 0; b < B.size(); b++)
        {
            if (B[b].kind == Binding::Kind::Zone || B[b].kind == Binding::Kind::Face ||
                B[b].kind == Binding::Kind::Pointer || B[b].kind == Binding::Kind::HeadStick)
            {
                return true;
            }
        }
    }
    return false;
}

static void DoSave(WizardUi * Ui, WizardDraft * Draft)
{
    char Path[512];
    SavePath(*Ui, Path, sizeof(Path));
    if (Path[0] == '\0')
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "Type a path first.");
        return;
    }
    if (IsOverwrittenByMake(Path) && !Ui->ConfirmOverwrite)
    {
        Ui->ConfirmOverwrite = true;
        // Message is drawn through WizardTextFit at 48 glyphs from x=24 (see the bottom of
        // WizardDrawScreen): this has to fit whole, or the "Enter again" instruction that
        // matters most would be the part cut off.
        snprintf(Ui->Message, sizeof(Ui->Message),
                 "make deletes and recopies this. Enter again.");
        return;
    }
    // The second warning, independent of the clobber one above and never a refusal: the
    // reader accepts such a file, so the wizard must not block a player who means it. Its own
    // flag, and its own Enter — a path that somehow tripped both warnings asks twice, and
    // neither confirmation counts as the other. DrawSave spells out why and where these
    // layouts belong while ConfirmDefault is set; this line has 48 glyphs to flag it and say
    // what the next key does.
    if (IsDefaultInput(Path) && HasNonKeyboardBinding(*Draft) && !Ui->ConfirmDefault)
    {
        Ui->ConfirmDefault = true;
        snprintf(Ui->Message, sizeof(Ui->Message),
                 "Mouse/face here breaks the grid. Enter again.");
        return;
    }
    if (!Draft->Save(Path, Ui->Base))
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
        fprintf(stderr, "wizard: %s\n", Draft->Error());
        return;
    }
    // Path is not folded in here: it can run to 116+ characters (SDL_GetBasePath() plus
    // "Config/input.yaml"), which would either overflow this line or, head-truncated, hide
    // the filename that matters most. DrawSave's "to:" line already shows Path, tail-fitted
    // so the filename stays visible; this line only has to say the save happened.
    snprintf(Ui->Message, sizeof(Ui->Message), "Saved. Escape to quit.");
    // Both confirmations are for a save that has not happened yet; once it has, DrawSave must
    // stop drawing the ConfirmDefault why-block (and must not re-arm ConfirmOverwrite) under
    // "Saved. Escape to quit.", or a further Enter would silently re-save the same file.
    Ui->ConfirmOverwrite = false;
    Ui->ConfirmDefault = false;
}

static void HandleSave(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    // No "if (Ui->Typing)" guard here: WizardHandleEvent already routes every event to
    // HandleTyping while Ui->Typing is set, ahead of the per-screen dispatch that reaches
    // this function at all, so a second check here could never see Typing true.
    if (Event.type != SDL_EVENT_KEY_DOWN) return;
    // Same reasoning as HandleReview above: every key here is one-shot, and without this
    // guard a held Enter would call DoSave again on every OS repeat — including immediately
    // after the same held Enter finished typing a path (see HandleTyping's RETURN case),
    // which is exactly the accidental double-save this guard exists to prevent. The clobber
    // confirmation's "second Enter" still works, because it requires a genuine new keydown
    // (repeat == false) after the key is released and pressed again.
    if (Event.key.repeat) return;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_1:
        Ui->SaveChoice = WIZARD_SAVE_DEFAULT;
        Ui->ConfirmOverwrite = false;
        Ui->ConfirmDefault = false;
        break;
    case SDL_SCANCODE_2:
    case SDL_SCANCODE_3:
        Ui->SaveChoice = Event.key.scancode == SDL_SCANCODE_2 ? WIZARD_SAVE_ROM
                                                              : WIZARD_SAVE_TYPED;
        Ui->ConfirmOverwrite = false;
        Ui->ConfirmDefault = false;
        Ui->Typing = true;
        Ui->Typed[0] = '\0';
        snprintf(Ui->Message, sizeof(Ui->Message),
                 Ui->SaveChoice == WIZARD_SAVE_ROM ? "Type the ROM's path, then Enter."
                                                   : "Type where to save, then Enter.");
        break;
    case SDL_SCANCODE_RETURN:
        DoSave(Ui, Draft);
        break;
    case SDL_SCANCODE_BACKSPACE:
        // Re-validating on the way back is cheap and is what makes the review screen honest:
        // without it, the review would show whatever message was left over from before Save
        // was entered (or none), rather than "S to save..." or the reader's current verdict.
        EnterReview(Ui, Draft);
        break;
    case SDL_SCANCODE_ESCAPE:
        Ui->Quit = true;
        break;
    default:
        break;
    }
}

// No WizardDraft parameter, the same as DrawBase: everything on this screen comes out of the
// Ui — the three destinations, the path SavePath builds from the typed one, and the pending
// confirmation. What the draft holds was already shown on the review screen behind it.
static void DrawSave(SDL_Renderer * Renderer, const WizardUi & Ui)
{
    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, 24.0f, kHead, "Save");
    const char * const kRows[3] = {
        "1  the default mapping, Config/input.yaml",
        "2  beside a ROM, so it loads for that game",
        "3  a path I will type",
    };
    for (int Row = 0; Row < 3; Row++)
    {
        Colour(Renderer, Row == (int)Ui.SaveChoice);
        WizardText(Renderer, 24.0f, 72.0f + kLine * (float)Row, kBody, kRows[Row]);
    }
    char Path[512];
    SavePath(Ui, Path, sizeof(Path));
    Colour(Renderer, false);
    char Line[600];
    snprintf(Line, sizeof(Line), "to: %s", Path);
    // Path can be SDL_GetBasePath() plus "Config/input.yaml" — well past the 48-glyph budget
    // a line starting at x=24 gets. Tail-fitted rather than head-fitted: the destination is
    // never legible from a head-truncated prefix (it is always the filename at the end that
    // tells the player where the file goes), so this keeps the end and drops the front.
    WizardTextFitTail(Renderer, 24.0f, 160.0f, kBody, Line, kWindowWidth - 24.0f);
    if (Ui.Typing)
    {
        Colour(Renderer, true);
        snprintf(Line, sizeof(Line), "path: %s_", Ui.Typed);
        // Tail-fitted for the same reason as "to:" above, and for one more: the trailing "_"
        // caret has to stay visible, or the player loses all on-screen feedback for what they
        // are typing once the path runs past about 42 characters.
        WizardTextFitTail(Renderer, 24.0f, 188.0f, kBody, Line, kWindowWidth - 24.0f);
    }
    if (Ui.ConfirmDefault)
    {
        // The why and the where behind DoSave's warning line. The message line at the bottom
        // of the screen has 48 glyphs, which is enough to flag the problem and say what the
        // next key does but not to teach the rule, so the rule lives here, on the screen the
        // player is already looking at, and only while the confirmation is pending. Each line
        // is hand-wrapped to the same 48-glyph budget a line at x=24 gets, and sits in the
        // empty band between the path lines above and the "Escape quits." line below.
        Colour(Renderer, true);
        static const char * const kWhy[4] = {
            "Config/input.yaml must stay keyboard-active:",
            "a mouse or face binding replaces the keyboard",
            "one and breaks the grid's key broadcast. Those",
            "layouts live in Config/mouse/ and Config/face/.",
        };
        for (int Row = 0; Row < 4; Row++)
        {
            WizardTextFit(Renderer, 24.0f, 240.0f + kLine * (float)Row, kBody, kWhy[Row],
                          kWindowWidth - 24.0f);
        }
    }
    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, kMessageY - kLine, kBody, "Escape quits.");
}

void WizardHandleEvent(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft, uint32_t LitGestures)
{
    if (Ui->Typing) { HandleTyping(Event, Ui, Draft); return; }
    if (Ui->Screen == WIZARD_BASE) HandleBase(Event, Ui, Draft);
    else if (Ui->Screen == WIZARD_CONTROL) HandleControl(Event, Ui, Draft, LitGestures);
    else if (Ui->Screen == WIZARD_REVIEW) HandleReview(Event, Ui);
    else if (Ui->Screen == WIZARD_SAVE) HandleSave(Event, Ui, Draft);
}

static void DrawBase(SDL_Renderer * Renderer, const WizardUi & Ui)
{
    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, 24.0f, kHead, "Project64 binding wizard");
    WizardText(Renderer, 24.0f, 64.0f, kBody, "Start from:");
    for (int Row = 0; Row < BaseRowCount(); Row++)
    {
        const float Y = 96.0f + kLine * (float)Row;
        Colour(Renderer, Row == Ui.Row);
        WizardText(Renderer, 40.0f, Y, kBody, Row == Ui.Row ? ">" : " ");
        WizardText(Renderer, 64.0f, Y, kBody, BaseRowLabel(Row));
    }
    if (Ui.Typing)
    {
        Colour(Renderer, true);
        char Line[300];
        snprintf(Line, sizeof(Line), "path: %s_", Ui.Typed);
        // Tail-fitted for the same reason as DrawSave's "path:" line: Ui.Typed is 256 bytes
        // and a real absolute path runs well past this window's width, which would otherwise
        // push the trailing "_" caret — the player's only on-screen feedback for what they
        // are typing — off the right edge.
        WizardTextFitTail(Renderer, 40.0f, 96.0f + kLine * (float)BaseRowCount() + 16.0f, kBody,
                          Line, kWindowWidth - 40.0f);
    }
}

static void DrawControl(SDL_Renderer * Renderer, const WizardUi & Ui, const WizardDraft & Draft,
                        uint32_t Gestures, uint32_t Face)
{
    const N64Control C = Ui.Control;
    char Line[320];

    Colour(Renderer, false);
    snprintf(Line, sizeof(Line), "Control %d of %d", (int)C + 1, (int)N64Control::Count);
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
            Colour(Renderer, Row == Ui.Row);
            WizardText(Renderer, 40.0f, 140.0f + kLine * (float)Row, kBody,
                       Row == Ui.Row ? ">" : " ");
            WizardTextFit(Renderer, 64.0f, 140.0f + kLine * (float)Row, kBody,
                          WizardStickFormLabel(Row), kWindowWidth - 64.0f);
        }
        return;
    }

    // Mode 5 takes the whole lower half the way the stick-form picker does above: its
    // eleven rows start at the same y=140 the "1".."5" menu and the Enter/Delete lines
    // occupy, so drawing both would overlap pixel-for-pixel rather than sit side by side
    // the way DrawPanel (which starts below, at y=300) does.
    if (Ui.Mode == WIZARD_MODE_GESTURE)
    {
        DrawGestures(Renderer, Ui, Gestures, Face);
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
    if (Ui.Mode == WIZARD_MODE_ZONE) DrawPanel(Renderer, Draft);
    // DrawPanel leaves the draw colour wherever its last slot left it, so restore ours
    // before drawing text of our own.
    Colour(Renderer, false);
    // Split across two lines: at kBody scale each glyph advances 16px, and the single-line
    // version from the design ran to 912px in an 800px window. Kept on the grid the "1".."5"
    // lines use (22px apart) and above y=300, where the panel's top edge begins.
    WizardText(Renderer, 24.0f, 250.0f, kBody, "Enter keep   Backspace back");
    WizardText(Renderer, 24.0f, 272.0f, kBody, "Delete inherit   Esc review");
}

void WizardDrawScreen(SDL_Renderer * Renderer, int W, int H, const WizardUi & Ui,
                      const WizardDraft & Draft, uint32_t Gestures, uint32_t Face)
{
    // W is unused on purpose: the window is not resizable (main.cpp), so the live width is
    // always kWindowWidth, and every budget below is taken from that constant instead. H is
    // used only to pin the message line to the bottom.
    (void)W;
    if (Ui.Screen == WIZARD_BASE) DrawBase(Renderer, Ui);
    else if (Ui.Screen == WIZARD_CONTROL) DrawControl(Renderer, Ui, Draft, Gestures, Face);
    else if (Ui.Screen == WIZARD_REVIEW) DrawReview(Renderer, Draft);
    else if (Ui.Screen == WIZARD_SAVE) DrawSave(Renderer, Ui);

    Colour(Renderer, false);
    WizardTextFit(Renderer, 24.0f, (float)H - 32.0f, kBody, Ui.Message, kWindowWidth - 24.0f);
}
