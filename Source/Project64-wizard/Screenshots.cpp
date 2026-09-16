// Project64 - A Nintendo 64 emulator
// The screenshot tour behind --screenshots <dir>: the pictures in Docs/UserGuide.md.
//
// Each stop is drawn by the same WizardDrawScreen the window shows, into an 800x640 surface
// behind SDL's software renderer, and saved with SDL_SavePNG. Nothing here opens a window,
// calls SDL_Init or starts the tracker: the gesture list at stop 07 is told through the
// draw call's own arguments that a face is tracked and mouth-open is firing.
//
// The software rasteriser is deterministic, so Scripts/wizard_screenshots_check.sh can
// compare a fresh render with the committed Docs/img/wizard byte for byte. That only holds
// while nothing machine-specific reaches the screen: the tour picks base row 0 only (the
// shipped layouts resolve through SDL_GetBasePath) and never save choice 1 (which prints
// SDL_GetBasePath too). Typed paths live under a neutral /Users/you/.
//
// The tour never writes a YAML: stop 10 ends when typing ends, stop 11 is never confirmed.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Screenshots.h"
#include "Screens.h"
#include "SyntheticEvents.h"
#include "WizardDraft.h"

#include <Common/PointerState.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

namespace
{

// The size Screens.cpp lays every screen out against (its kWindowWidth/kWindowHeight).
const int kWidth = 800;
const int kHeight = 640;

struct Tour
{
    SDL_Surface * Surface;
    SDL_Renderer * Renderer;
    const char * Dir;
    WizardUi Ui;
    WizardDraft Draft;
    int Written;
    bool Failed;
};

// One picture: clear to the window loop's background (main.cpp uses 16,16,20), draw the
// screen the way the loop does, flush the batch, save.
void Capture(Tour & T, const char * Name, uint32_t Gestures, uint32_t Face)
{
    if (T.Failed) return;
    SDL_SetRenderDrawColor(T.Renderer, 16, 16, 20, 255);
    SDL_RenderClear(T.Renderer);
    WizardDrawScreen(T.Renderer, kWidth, kHeight, T.Ui, T.Draft, Gestures, Face);
    SDL_RenderPresent(T.Renderer);
    char Path[1024];
    snprintf(Path, sizeof(Path), "%s/%s", T.Dir, Name);
    if (!SDL_SavePNG(T.Surface, Path))
    {
        fprintf(stderr, "wizard --screenshots: %s: %s\n", Path, SDL_GetError());
        T.Failed = true;
        return;
    }
    T.Written++;
}

void Send(Tour & T, const SDL_Event & E, uint32_t LitGestures = 0)
{
    WizardHandleEvent(E, &T.Ui, &T.Draft, LitGestures);
}

// A stop whose screen is not what the guide describes is a failure, not a picture.
bool Expect(Tour & T, bool Ok, const char * What)
{
    if (Ok) return true;
    fprintf(stderr, "wizard --screenshots: %s\n", What);
    T.Failed = true;
    return false;
}

void Walk(Tour & T)
{
    WizardUiInit(&T.Ui);
    // The pictures show the keyboard-only case; stop 05 depends on no pad being open.
    T.Ui.HasGamepad = false;

    // 01: the base screen, as the wizard opens.
    Capture(T, "01-base.png", 0, FACE_OFF);

    // 02: Enter picks the built-in bindings and lands on control 1 of 15, A, whose "now:"
    // line shows the inherited key-and-button pair.
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));
    if (!Expect(T, T.Ui.Screen == WIZARD_CONTROL && T.Ui.Control == N64Control::A,
                "Enter on the base screen did not land on A")) return;
    Capture(T, "02-control.png", 0, FACE_OFF);

    // 03: 1 arms key capture.
    Send(T, KeyEvent(SDL_SCANCODE_1));
    Capture(T, "03-key-armed.png", 0, FACE_OFF);

    // 04: X is taken as it comes.
    Send(T, KeyEvent(SDL_SCANCODE_X));
    if (!Expect(T, T.Draft.Describe(N64Control::A) == "key X", "X did not bind A")) return;
    Capture(T, "04-key-bound.png", 0, FACE_OFF);

    // 05: 2 with no gamepad open is refused in the message line and arms nothing.
    Send(T, KeyEvent(SDL_SCANCODE_2));
    if (!Expect(T, T.Ui.Mode == WIZARD_MODE_NONE, "2 with no gamepad armed a mode")) return;
    Capture(T, "05-no-gamepad.png", 0, FACE_OFF);

    // Enter keeps A and moves to B; Enter keeps B and moves to Z.
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));
    if (!Expect(T, T.Ui.Control == N64Control::Z, "two Enters did not reach Z")) return;

    // 06: on Z, 4 shows the panel; a click on mid1 takes the slot and leaves the mode, so
    // 4 once more shows the panel with Z's label sitting in mid1. Escape leaves it again.
    // mid1's centre in the wizard's half-scale panel is (120, 556): see Selftest in main.cpp.
    Send(T, KeyEvent(SDL_SCANCODE_4));
    Send(T, ClickEvent(120.0f, 556.0f));
    if (!Expect(T, T.Draft.Describe(N64Control::Z) == "zone mid1", "the click did not take mid1")) return;
    Send(T, KeyEvent(SDL_SCANCODE_4));
    Capture(T, "06-zone.png", 0, FACE_OFF);
    Send(T, KeyEvent(SDL_SCANCODE_ESCAPE));
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));  // Z -> Start
    if (!Expect(T, T.Ui.Control == N64Control::Start, "Enter after the panel did not reach Start")) return;

    // 07: on Start, 5 opens the gesture list. The tracker never runs here; the draw call is
    // told a face is tracked and mouth-open is firing, so that row lights with "<- now".
    Send(T, KeyEvent(SDL_SCANCODE_5), POINTER_GESTURE_MOUTH_OPEN);
    Capture(T, "07-gestures.png", POINTER_GESTURE_MOUTH_OPEN, FACE_TRACKING);
    Send(T, KeyEvent(SDL_SCANCODE_SPACE), POINTER_GESTURE_MOUTH_OPEN);  // binds what fires
    if (!Expect(T, T.Draft.Describe(N64Control::Start) == "gesture mouth-open (Mo)",
                "Space did not bind mouth-open")) return;
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));  // Start -> L

    // Enter through L .. DPadRight, keeping each. Bounded the way Selftest's loop is: a
    // handler that stops clearing its mode would otherwise spin here forever.
    int Guard = 0;
    while (T.Ui.Screen == WIZARD_CONTROL && T.Ui.Control != N64Control::Stick)
    {
        if (!Expect(T, Guard < (int)N64Control::Count, "stuck before reaching Stick")) return;
        Guard++;
        Send(T, KeyEvent(SDL_SCANCODE_RETURN));
    }

    // 08: on Stick, 1 opens the six forms; four Downs highlight head-digital.
    Send(T, KeyEvent(SDL_SCANCODE_1));
    for (int i = 0; i < 4; i++) Send(T, KeyEvent(SDL_SCANCODE_DOWN));
    if (!Expect(T, T.Ui.Mode == WIZARD_MODE_STICK && T.Ui.Row == 4,
                "the stick chooser is not on head-digital")) return;
    Capture(T, "08-stick-forms.png", 0, FACE_OFF);
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));  // chooses head-digital
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));  // Stick was last: the review

    // 09: the review, inherited controls dimmed, the three explicit ones lit.
    if (!Expect(T, T.Ui.Screen == WIZARD_REVIEW, "the last Enter did not reach the review")) return;
    Capture(T, "09-review.png", 0, FACE_OFF);

    // 10: S, then 2 and a ROM path. The mapping's name beside it is derived and shown on the
    // "to:" line; the first Enter only ends typing, so nothing is written.
    Send(T, KeyEvent(SDL_SCANCODE_S));
    Send(T, KeyEvent(SDL_SCANCODE_2));
    static const char kRom[] = "/Users/you/Roms/super_mario_64.z64";
    Send(T, TextEvent(kRom));
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));
    if (!Expect(T, T.Ui.Screen == WIZARD_SAVE && !T.Ui.Typing, "the ROM path did not end typing")) return;
    Capture(T, "10-save.png", 0, FACE_OFF);

    // 11: 3 and a path ending in Config/input.yaml, Enter to end typing, Enter to save. The
    // draft binds a zone, a gesture and a head stick, so DoSave warns and waits for one more
    // Enter, which never comes. The directory does not exist either way.
    Send(T, KeyEvent(SDL_SCANCODE_3));
    static const char kDefault[] = "/Users/you/Project64Mac/Bin/macOS/Config/input.yaml";
    Send(T, TextEvent(kDefault));
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));
    Send(T, KeyEvent(SDL_SCANCODE_RETURN));
    if (!Expect(T, T.Ui.ConfirmDefault, "Config/input.yaml did not warn")) return;
    Capture(T, "11-save-warning.png", 0, FACE_OFF);
}

}  // namespace

int WizardScreenshots(const char * Dir)
{
    Tour T;
    memset(&T.Ui, 0, sizeof(T.Ui));
    T.Dir = Dir;
    T.Written = 0;
    T.Failed = false;
    T.Surface = SDL_CreateSurface(kWidth, kHeight, SDL_PIXELFORMAT_RGBA32);
    if (T.Surface == nullptr)
    {
        fprintf(stderr, "wizard --screenshots: SDL_CreateSurface: %s\n", SDL_GetError());
        return 1;
    }
    T.Renderer = SDL_CreateSoftwareRenderer(T.Surface);
    if (T.Renderer == nullptr)
    {
        fprintf(stderr, "wizard --screenshots: SDL_CreateSoftwareRenderer: %s\n", SDL_GetError());
        SDL_DestroySurface(T.Surface);
        return 1;
    }

    Walk(T);

    SDL_DestroyRenderer(T.Renderer);
    SDL_DestroySurface(T.Surface);
    if (T.Failed) return 1;
    if (T.Written != 11)
    {
        fprintf(stderr, "wizard --screenshots: wrote %d files, not 11\n", T.Written);
        return 1;
    }
    printf("wizard --screenshots wrote 11 files to %s\n", Dir);
    return 0;
}
