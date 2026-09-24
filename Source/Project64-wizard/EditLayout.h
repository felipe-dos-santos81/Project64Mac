// Project64 - A Nintendo 64 emulator
// The panel editor's screen, with no SDL: where every target is, whether it does anything
// now, what it says, and what a click on it does to the state and the draft. EditScreen.cpp
// only paints this and feeds it clicks. Design: Docs/superpowers/specs/2026-09-24-clickable-wizard-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include "WizardDraft.h"

#include <string>
#include <vector>

#define EDIT_WIDTH 800
#define EDIT_HEIGHT 640
// Chooser choices after the fourteen controls (0 … 13, N64Control order without Stick).
#define EDIT_CHOICE_MENU 14
#define EDIT_CHOICE_HOLD 15
#define EDIT_CHOICE_NOTHING 16
#define EDIT_CHOICE_COUNT 17

enum class EditView { Panel, Chooser, Gestures, Stick };

struct EditState
{
    EditView View = EditView::Panel;
    EditPlace Place;               // what the chooser is for
    bool Dirty = false;            // changed since loaded
    bool ConfirmCancel = false;    // Cancel was clicked once with changes
    std::string Status;            // the status line
};

enum class EditTargetKind { None, Stick, Gestures, Save, Cancel, Zone, Gesture, Choice, Toggle, Back, StickForm };

struct EditTarget
{
    EditTargetKind Kind = EditTargetKind::None;
    int Index = 0;   // Zone: 0 … 13; Gesture: 0 … 10; Choice: 0 … 16; StickForm: 0 pointer, 1 head, 2 head-digital
};

inline bool operator==(EditTarget A, EditTarget B)
{
    return A.Kind == B.Kind && A.Index == B.Index;
}

struct EditRect
{
    float X, Y, W, H;
};

enum class EditCommand { None, Save, Quit };

// True while the gesture list, or a gesture's chooser, is up.
bool EditShowsGestures(const EditState & S);

// The targets the current view offers, in drawing order: the top bar first.
std::vector<EditTarget> EditTargets(const EditState & S);

EditRect EditTargetRect(EditTarget T);

// The target under a point in window coordinates, enabled or not, or None.
EditTarget EditHit(const EditState & S, float X, float Y);

// Whether T does anything now; *Why (when not null) gets the reason when not.
bool EditEnabled(const EditState & S, const WizardDraft & D, EditTarget T, std::string * Why);

// Whether T shows the current choice (the control in the place, the toggle on, the stick form).
bool EditLit(const EditState & S, const WizardDraft & D, EditTarget T);

std::string EditLabel(const EditState & S, const WizardDraft & D, EditTarget T);

// The chooser's header, e.g. "mid2: Z, toggle"; also the status after a change.
std::string EditHeader(const EditState & S, const WizardDraft & D);

// "Not placed: L  D^  Dv", or "Every control has a place".
std::string EditNotPlaced(const WizardDraft & D);

// What a click on T does. A disabled target puts its reason on the status line and does nothing.
EditCommand EditAct(EditState * S, WizardDraft * D, EditTarget T);
