// Project64 - A Nintendo 64 emulator
// See EditLayout.h. The panel's rectangles come from PointerZoneRect, the game's own geometry,
// so a slot here is the slot the player clicks in play.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "EditLayout.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>

namespace
{
const EditRect kPicture = { 80, 96, 640, 320 };
const float kPanelLeft = 80.0f;
const float kPanelTop = 424.0f;
const int kChoiceColumns = 5;
const float kChoiceLeft = 84.0f, kChoiceTop = 136.0f, kChoiceW = 120.0f, kChoiceH = 48.0f, kChoiceGap = 8.0f;
const float kGestureTop = 96.0f, kGesturePitch = 42.0f;
const EditStick kForms[3] = { EditStick::Pointer, EditStick::Head, EditStick::HeadDigital };

bool Contains(EditRect R, float X, float Y)
{
    return X >= R.X && X < R.X + R.W && Y >= R.Y && Y < R.Y + R.H;
}

std::string PlaceName(EditPlace P)
{
    if (P.Gesture) return PointerGestureName(P.Index);
    return P.Index == POINTER_ZONE_GAME ? "the picture" : PointerZoneName(P.Index);
}

const char * StickName(EditStick Form)
{
    switch (Form)
    {
    case EditStick::Pointer: return "pointer";
    case EditStick::Head: return "head";
    case EditStick::HeadDigital: return "head-digital";
    case EditStick::Other: break;
    }
    return "other";
}

bool MenuAt(const WizardDraft & D, EditPlace P)
{
    return P.Gesture ? D.MenuGesture() == P.Index : D.MenuZone() == P.Index;
}

bool HoldAt(const WizardDraft & D, EditPlace P)
{
    return !P.Gesture && D.HoldZone() == P.Index;
}
}

bool EditShowsGestures(const EditState & S)
{
    return S.View == EditView::Gestures || (S.View == EditView::Chooser && S.Place.Gesture);
}

std::vector<EditTarget> EditTargets(const EditState & S)
{
    std::vector<EditTarget> Out;
    const EditTargetKind Bar[] = { EditTargetKind::Stick, EditTargetKind::Gestures, EditTargetKind::Save, EditTargetKind::Cancel };
    for (EditTargetKind K : Bar) Out.push_back(EditTarget{ K, 0 });
    switch (S.View)
    {
    case EditView::Panel:
        for (int Z = 0; Z < POINTER_ZONE_COUNT; Z++) Out.push_back(EditTarget{ EditTargetKind::Zone, Z });
        break;
    case EditView::Chooser:
        for (int C = 0; C < EDIT_CHOICE_COUNT; C++)
        {
            if (S.Place.Gesture && (C == EDIT_CHOICE_MENU || C == EDIT_CHOICE_HOLD)) continue;
            Out.push_back(EditTarget{ EditTargetKind::Choice, C });
        }
        if (!S.Place.Gesture) Out.push_back(EditTarget{ EditTargetKind::Toggle, 0 });
        Out.push_back(EditTarget{ EditTargetKind::Back, 0 });
        break;
    case EditView::Gestures:
        for (int G = 0; G < POINTER_GESTURE_COUNT; G++) Out.push_back(EditTarget{ EditTargetKind::Gesture, G });
        break;
    case EditView::Stick:
        for (int F = 0; F < 3; F++) Out.push_back(EditTarget{ EditTargetKind::StickForm, F });
        Out.push_back(EditTarget{ EditTargetKind::Back, 0 });
        break;
    }
    return Out;
}

EditRect EditTargetRect(EditTarget T)
{
    switch (T.Kind)
    {
    case EditTargetKind::Stick: return EditRect{ 24, 40, 320, 40 };
    case EditTargetKind::Gestures: return EditRect{ 352, 40, 216, 40 };
    case EditTargetKind::Save: return EditRect{ 576, 40, 96, 40 };
    case EditTargetKind::Cancel: return EditRect{ 680, 40, 104, 40 };
    case EditTargetKind::Zone:
    {
        if (T.Index == POINTER_ZONE_GAME) return kPicture;
        float X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
        PointerZoneRect(T.Index, 640, 640, &X0, &Y0, &X1, &Y1);
        const float Top = (float)PointerGameHeight(640);   // where the game's own panel starts
        return EditRect{ kPanelLeft + X0, kPanelTop + (Y0 - Top), X1 - X0, Y1 - Y0 };
    }
    case EditTargetKind::Gesture: return EditRect{ 80, kGestureTop + T.Index * kGesturePitch, 640, 40 };
    case EditTargetKind::Choice:
    {
        const int Column = T.Index % kChoiceColumns;
        const int Row = T.Index / kChoiceColumns;
        return EditRect{ kChoiceLeft + Column * (kChoiceW + kChoiceGap), kChoiceTop + Row * (kChoiceH + kChoiceGap), kChoiceW, kChoiceH };
    }
    case EditTargetKind::Toggle: return EditRect{ 88, 360, 200, 48 };
    case EditTargetKind::Back: return EditRect{ 512, 360, 200, 48 };
    case EditTargetKind::StickForm: return EditRect{ 88 + T.Index * 208.0f, 136, 200, 48 };
    case EditTargetKind::None: break;
    }
    return EditRect{ 0, 0, 0, 0 };
}

EditTarget EditHit(const EditState & S, float X, float Y)
{
    for (const EditTarget & T : EditTargets(S))
    {
        if (Contains(EditTargetRect(T), X, Y)) return T;
    }
    return EditTarget();
}

bool EditEnabled(const EditState & S, const WizardDraft & D, EditTarget T, std::string * Why)
{
    std::string Scratch;
    if (Why == nullptr) Why = &Scratch;
    Why->clear();
    switch (T.Kind)
    {
    case EditTargetKind::Choice:
        if (T.Index == EDIT_CHOICE_MENU) return D.CanPlaceMenu(S.Place.Index, Why);
        if (T.Index == EDIT_CHOICE_HOLD) return D.CanPlaceHold(S.Place.Index, Why);
        return true;
    case EditTargetKind::Toggle: return D.CanToggle(S.Place.Index, Why);
    case EditTargetKind::Gesture: return D.CanUseGesture(T.Index, Why);
    case EditTargetKind::None: return false;
    default: return true;
    }
}

bool EditLit(const EditState & S, const WizardDraft & D, EditTarget T)
{
    switch (T.Kind)
    {
    case EditTargetKind::Choice:
    {
        const std::vector<N64Control> Here = D.Occupants(S.Place);
        if (T.Index < EDIT_CHOICE_MENU)
        {
            for (N64Control C : Here) if ((int)C == T.Index) return true;
            return false;
        }
        if (T.Index == EDIT_CHOICE_MENU) return MenuAt(D, S.Place);
        if (T.Index == EDIT_CHOICE_HOLD) return HoldAt(D, S.Place);
        return Here.empty() && !MenuAt(D, S.Place) && !HoldAt(D, S.Place);
    }
    case EditTargetKind::Toggle: return D.Toggled(S.Place.Index);
    case EditTargetKind::StickForm: return D.StickForm() == kForms[T.Index];
    default: return false;
    }
}

std::string EditLabel(const EditState & S, const WizardDraft & D, EditTarget T)
{
    switch (T.Kind)
    {
    case EditTargetKind::Stick: return std::string("Stick: ") + StickName(D.StickForm());
    case EditTargetKind::Gestures: return EditShowsGestures(S) ? "Back to panel" : "Gestures";
    case EditTargetKind::Save: return "Save";
    case EditTargetKind::Cancel: return "Cancel";
    case EditTargetKind::Back: return "Back";
    case EditTargetKind::Toggle: return D.Toggled(S.Place.Index) ? "Toggle: on" : "Toggle: off";
    case EditTargetKind::StickForm: return StickName(kForms[T.Index]);
    case EditTargetKind::Choice:
        if (T.Index < EDIT_CHOICE_MENU) return InputConfig::ControlLabel((N64Control)T.Index);
        if (T.Index == EDIT_CHOICE_MENU) return "Menu";
        if (T.Index == EDIT_CHOICE_HOLD) return "Hold";
        return "Nothing";
    case EditTargetKind::Zone:
    {
        EditPlace P(T.Index);
        if (HoldAt(D, P)) return "Ho";
        if (MenuAt(D, P)) return "==";
        const std::vector<N64Control> Here = D.Occupants(P);
        if (Here.empty()) return "";
        return std::string(InputConfig::ControlLabel(Here[0])) + (Here.size() > 1 ? "+" : "");
    }
    case EditTargetKind::Gesture:
    {
        EditPlace P(T.Index, true);
        std::string Holder;
        if (MenuAt(D, P)) Holder = "==";
        for (N64Control C : D.Occupants(P))
        {
            if (!Holder.empty()) Holder += ", ";
            Holder += WizardControlName(C);
        }
        char Line[96];
        snprintf(Line, sizeof(Line), "%-4s%-12s%s", PointerGestureTag(T.Index), PointerGestureName(T.Index), Holder.c_str());
        return Line;
    }
    case EditTargetKind::None: break;
    }
    return "";
}

std::string EditHeader(const EditState & S, const WizardDraft & D)
{
    if (S.View == EditView::Stick) return "What moves the stick:";
    std::string What;
    if (MenuAt(D, S.Place)) What = "the menu";
    else if (HoldAt(D, S.Place)) What = "the hold";
    else
    {
        for (N64Control C : D.Occupants(S.Place))
        {
            if (!What.empty()) What += " and ";
            What += WizardControlName(C);
        }
        if (What.empty()) What = "nothing";
        else if (!S.Place.Gesture && D.Toggled(S.Place.Index)) What += ", toggle";
    }
    return PlaceName(S.Place) + ": " + What;
}

std::string EditNotPlaced(const WizardDraft & D)
{
    const std::vector<N64Control> Left = D.NotPlaced();
    if (Left.empty()) return "Every control has a place";
    std::string Line = "Not placed:";
    bool First = true;
    for (N64Control C : Left)
    {
        Line += First ? " " : "  ";
        Line += InputConfig::ControlLabel(C);
        First = false;
    }
    return Line;
}

// The draft call behind a Choice, Toggle or StickForm click.
static bool Edit(WizardDraft * D, const EditState & S, EditTarget T, std::string * Note)
{
    if (T.Kind == EditTargetKind::Toggle) return D->SetToggle(S.Place.Index, !D->Toggled(S.Place.Index), Note);
    if (T.Kind == EditTargetKind::StickForm) return D->SetStickForm(kForms[T.Index], Note);
    if (T.Index < EDIT_CHOICE_MENU) return D->PlaceControl(S.Place, (N64Control)T.Index, Note);
    if (T.Index == EDIT_CHOICE_MENU) return D->PlaceMenu(S.Place.Index, Note);
    if (T.Index == EDIT_CHOICE_HOLD) return D->PlaceHold(S.Place.Index, Note);
    return D->PlaceNothing(S.Place, Note);
}

EditCommand EditAct(EditState * S, WizardDraft * D, EditTarget T)
{
    if (T.Kind != EditTargetKind::Cancel) S->ConfirmCancel = false;
    std::string Why;
    if (!EditEnabled(*S, *D, T, &Why))
    {
        S->Status = Why;
        return EditCommand::None;
    }
    std::string Note;
    switch (T.Kind)
    {
    case EditTargetKind::Stick:
        S->View = EditView::Stick;
        S->Status = "Choose what moves the stick.";
        return EditCommand::None;
    case EditTargetKind::Gestures:
        S->View = EditShowsGestures(*S) ? EditView::Panel : EditView::Gestures;
        S->Status.clear();
        return EditCommand::None;
    case EditTargetKind::Save:
        return EditCommand::Save;
    case EditTargetKind::Cancel:
        if (!S->Dirty || S->ConfirmCancel) return EditCommand::Quit;
        S->ConfirmCancel = true;
        S->Status = "Cancel again to discard your changes";
        return EditCommand::None;
    case EditTargetKind::Zone:
    case EditTargetKind::Gesture:
        S->View = EditView::Chooser;
        S->Place.Gesture = T.Kind == EditTargetKind::Gesture;
        S->Place.Index = T.Index;
        S->Status.clear();
        return EditCommand::None;
    case EditTargetKind::Back:
        S->View = (S->View == EditView::Chooser && S->Place.Gesture) ? EditView::Gestures : EditView::Panel;
        S->Status.clear();
        return EditCommand::None;
    case EditTargetKind::Choice:
    case EditTargetKind::Toggle:
    case EditTargetKind::StickForm:
    {
        // The draft decides; the layout is "changed" only when the file it would write changed,
        // so a click that puts back what was there does not make Cancel ask twice.
        const std::string Before = D->Emit("");
        if (!Edit(D, *S, T, &Note))
        {
            S->Status = Note;
            return EditCommand::None;
        }
        if (D->Emit("") != Before) S->Dirty = true;
        const std::string What = T.Kind == EditTargetKind::StickForm
            ? std::string("The stick is ") + StickName(D->StickForm()) : EditHeader(*S, *D);
        S->Status = What + (Note.empty() ? "" : "; " + Note);
        if (T.Kind == EditTargetKind::Choice) S->View = S->Place.Gesture ? EditView::Gestures : EditView::Panel;
        else if (T.Kind == EditTargetKind::StickForm) S->View = EditView::Panel;
        return EditCommand::None;
    }
    case EditTargetKind::None:
        break;
    }
    return EditCommand::None;
}
