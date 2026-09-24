// Project64 - A Nintendo 64 emulator
// Tests for the panel editor's layout and click logic (EditLayout.h). No window, no renderer.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "EditLayout.h"
#include <Project64-sdl/UnitTest.h>

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <string>
#include <vector>

static int Slot(const char * Name) { return PointerZoneFromName(Name); }

static EditTarget Target(EditTargetKind Kind, int Index = 0)
{
    EditTarget T;
    T.Kind = Kind;
    T.Index = Index;
    return T;
}

static EditTarget HitCentre(const EditState & S, EditTarget T)
{
    const EditRect R = EditTargetRect(T);
    return EditHit(S, R.X + R.W / 2, R.Y + R.H / 2);
}

// A small layout: the pointer stick with its hold on mid5, A on the picture, Z a toggle on
// mid2, the menu on pad-down.
static WizardDraft Small()
{
    WizardDraft D;
    std::string N;
    EditPlace Picture;
    Picture.Index = POINTER_ZONE_GAME;
    EditPlace Mid2;
    Mid2.Index = Slot("mid2");
    CHECK(D.SetStickForm(EditStick::Pointer, &N));
    CHECK(D.PlaceControl(Picture, N64Control::A, &N));
    CHECK(D.PlaceControl(Mid2, N64Control::Z, &N));
    CHECK(D.SetToggle(Slot("mid2"), true, &N));
    CHECK(D.PlaceHold(Slot("mid5"), &N));
    CHECK(D.PlaceMenu(Slot("pad-down"), &N));
    return D;
}

static void Geometry()
{
    // Every view: every target inside the window, no two overlapping.
    EditState S;
    const EditView Views[] = { EditView::Panel, EditView::Chooser, EditView::Gestures, EditView::Stick };
    for (EditView V : Views)
    {
        for (int GestureChooser = 0; GestureChooser < 2; GestureChooser++)
        {
            S.View = V;
            S.Place.Gesture = GestureChooser == 1;
            S.Place.Index = GestureChooser == 1 ? 0 : Slot("mid2");
            const std::vector<EditTarget> All = EditTargets(S);
            for (size_t i = 0; i < All.size(); i++)
            {
                const EditRect A = EditTargetRect(All[i]);
                CHECK(A.X >= 0 && A.Y >= 0 && A.X + A.W <= EDIT_WIDTH && A.Y + A.H <= EDIT_HEIGHT && A.W > 0 && A.H > 0);
                for (size_t j = i + 1; j < All.size(); j++)
                {
                    const EditRect B = EditTargetRect(All[j]);
                    CHECK(A.X + A.W <= B.X || B.X + B.W <= A.X || A.Y + A.H <= B.Y || B.Y + B.H <= A.Y);
                }
            }
            for (const EditTarget & T : All) CHECK(HitCentre(S, T) == T);
        }
    }

    // The panel is the game's own: mid1 is 56x48, its top 8 points below the panel's top.
    const EditRect Mid1 = EditTargetRect(Target(EditTargetKind::Zone, Slot("mid1")));
    CHECK(Mid1.W == 56 && Mid1.H == 48 && Mid1.X == 80 + 164 && Mid1.Y == 424 + 8);
    const EditRect Picture = EditTargetRect(Target(EditTargetKind::Zone, POINTER_ZONE_GAME));
    CHECK(Picture.X == 80 && Picture.Y == 96 && Picture.W == 640 && Picture.H == 320);
    S.View = EditView::Panel;
    CHECK(EditHit(S, 80 + 164 + 56 + 4, 424 + 20).Kind == EditTargetKind::None);   // between mid1 and mid2
    CHECK(EditHit(S, 10, 300).Kind == EditTargetKind::None);                       // left of the picture

    // What each view offers.
    S.View = EditView::Chooser;
    S.Place.Gesture = false;
    S.Place.Index = Slot("mid2");
    CHECK(EditTargets(S).size() == 4 + EDIT_CHOICE_COUNT + 2);          // top bar, choices, Toggle, Back
    S.Place.Gesture = true;
    S.Place.Index = PointerGestureIndex(POINTER_GESTURE_SMILE);
    CHECK(EditTargets(S).size() == 4 + EDIT_CHOICE_COUNT - 2 + 1);      // no Menu, Hold or Toggle
    S.View = EditView::Gestures;
    CHECK(EditTargets(S).size() == 4 + POINTER_GESTURE_COUNT);
}

static void Rules()
{
    WizardDraft D = Small();
    EditState S;
    std::string Why;

    // The picture: no menu, no hold; a toggle once a control is there.
    S.View = EditView::Chooser;
    S.Place.Index = POINTER_ZONE_GAME;
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_MENU), &Why) && Why == "the picture cannot hold the menu");
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_HOLD), &Why) && Why == "the hold cannot go on the picture");
    CHECK(EditEnabled(S, D, Target(EditTargetKind::Toggle), &Why));
    CHECK(EditLit(S, D, Target(EditTargetKind::Choice, (int)N64Control::A)));
    CHECK(EditHeader(S, D) == "the picture: A");

    // A slot: lit choices, the header, the labels.
    S.Place.Index = Slot("mid2");
    CHECK(EditHeader(S, D) == "mid2: Z, toggle");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Toggle)) == "Toggle: on");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Choice, (int)N64Control::Start)) == "St");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_NOTHING)) == "Nothing");
    S.Place.Index = Slot("mid1");
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Toggle), &Why) && Why == "a toggle needs a control in the slot");
    CHECK(EditLit(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_NOTHING)));
    CHECK(EditHeader(S, D) == "mid1: nothing");
    S.Place.Index = Slot("mid5");
    CHECK(EditHeader(S, D) == "mid5: the hold");
    S.View = EditView::Panel;
    CHECK(EditLabel(S, D, Target(EditTargetKind::Zone, Slot("mid5"))) == "Ho");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Zone, Slot("pad-down"))) == "==");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Zone, Slot("mid2"))) == "Z");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Stick)) == "Stick: pointer");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Gestures)) == "Gestures");
    CHECK(EditNotPlaced(D) == "Not placed: B  St  L  R  C^  Cv  C<  C>  D^  Dv  D<  D>");

    // A head stick: no hold, and the head directions dimmed in the gesture list.
    std::string N;
    CHECK(D.SetStickForm(EditStick::HeadDigital, &N));
    S.View = EditView::Chooser;
    S.Place.Index = Slot("mid1");
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Choice, EDIT_CHOICE_HOLD), &Why) && Why == "the hold needs the stick to be the pointer");
    S.View = EditView::Gestures;
    CHECK(!EditEnabled(S, D, Target(EditTargetKind::Gesture, PointerGestureIndex(POINTER_GESTURE_HEAD_UP)), &Why) && Why == "the head moves the stick");
    CHECK(EditEnabled(S, D, Target(EditTargetKind::Gesture, PointerGestureIndex(POINTER_GESTURE_SMILE)), &Why));
    CHECK(EditLabel(S, D, Target(EditTargetKind::Stick)) == "Stick: head-digital");
    CHECK(EditLabel(S, D, Target(EditTargetKind::Gestures)) == "Back to panel");
}

static void Acts()
{
    WizardDraft D = Small();
    EditState S;

    // A slot opens its chooser; a control closes it, marks the draft changed and says what happened.
    CHECK(EditAct(&S, &D, Target(EditTargetKind::Zone, Slot("mid1"))) == EditCommand::None);
    CHECK(S.View == EditView::Chooser && !S.Place.Gesture && S.Place.Index == Slot("mid1"));
    CHECK(EditAct(&S, &D, Target(EditTargetKind::Choice, (int)N64Control::Start)) == EditCommand::None);
    CHECK(S.View == EditView::Panel && S.Dirty && S.Status == "mid1: Start");

    // Toggle stays in the chooser; Back leaves it unchanged.
    EditAct(&S, &D, Target(EditTargetKind::Zone, Slot("mid1")));
    EditAct(&S, &D, Target(EditTargetKind::Toggle));
    CHECK(S.View == EditView::Chooser && D.Toggled(Slot("mid1")) && S.Status == "mid1: Start, toggle");
    EditAct(&S, &D, Target(EditTargetKind::Back));
    CHECK(S.View == EditView::Panel);

    // A refused choice keeps the chooser, shows the reason and changes nothing.
    WizardDraft Full;
    CHECK(Full.LoadBase(TestWriteTemp(
        "bindings:\n  Stick: {stick: pointer, hold: mid5}\n"
        "  A: {zone: mid1}\n  B: {zone: mid2}\n  Z: {zone: mid3}\n  Start: {zone: mid4}\n"
        "  L: {zone: pad-up}\n  R: {zone: pad-left}\n  CUp: {zone: pad-right}\n"
        "  CDown: {zone: c-up}\n  CLeft: {zone: c-down}\n  CRight: {zone: c-left}\n  DPadUp: {zone: c-right}\n"
        "  Menu: {zone: pad-down}\n")));
    EditState F;
    const std::string Before = Full.Emit("x");
    EditAct(&F, &Full, Target(EditTargetKind::Zone, Slot("pad-down")));
    CHECK(EditAct(&F, &Full, Target(EditTargetKind::Choice, EDIT_CHOICE_NOTHING)) == EditCommand::None);
    CHECK(F.View == EditView::Chooser && !F.Dirty && F.Status == "The panel is full: free a slot for the menu first");
    CHECK(Full.Emit("x") == Before);
    // A dimmed choice says why and does nothing.
    EditAct(&F, &Full, Target(EditTargetKind::Back));
    EditAct(&F, &Full, Target(EditTargetKind::Zone, POINTER_ZONE_GAME));
    CHECK(EditAct(&F, &Full, Target(EditTargetKind::Choice, EDIT_CHOICE_MENU)) == EditCommand::None);
    CHECK(F.Status == "the picture cannot hold the menu" && Full.Emit("x") == Before);

    // Gestures: the button flips between the list and the panel; a row's chooser returns to the list.
    EditState G;
    EditAct(&G, &D, Target(EditTargetKind::Gestures));
    CHECK(G.View == EditView::Gestures && EditShowsGestures(G));
    EditAct(&G, &D, Target(EditTargetKind::Gesture, PointerGestureIndex(POINTER_GESTURE_SMILE)));
    CHECK(G.View == EditView::Chooser && G.Place.Gesture && EditShowsGestures(G));
    EditAct(&G, &D, Target(EditTargetKind::Choice, (int)N64Control::B));
    CHECK(G.View == EditView::Gestures && G.Status == "smile: B");
    EditAct(&G, &D, Target(EditTargetKind::Gestures));
    CHECK(G.View == EditView::Panel && !EditShowsGestures(G));

    // The stick: its chooser, then back to the panel.
    EditAct(&G, &D, Target(EditTargetKind::Stick));
    CHECK(G.View == EditView::Stick);
    EditAct(&G, &D, Target(EditTargetKind::StickForm, 1));
    CHECK(G.View == EditView::Panel && D.StickForm() == EditStick::Head && D.HoldZone() == POINTER_ZONE_NONE);
    CHECK(G.Status == "The stick is head; the hold is gone");

    // Save and Cancel.
    EditState C;
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Save)) == EditCommand::Save);
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Cancel)) == EditCommand::Quit);          // nothing changed
    C.Dirty = true;
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Cancel)) == EditCommand::None);
    CHECK(C.ConfirmCancel && C.Status == "Cancel again to discard your changes");
    EditAct(&C, &D, Target(EditTargetKind::Zone, Slot("mid3")));                          // any other click forgets it
    CHECK(!C.ConfirmCancel);
    EditAct(&C, &D, Target(EditTargetKind::Back));
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Cancel)) == EditCommand::None);
    CHECK(EditAct(&C, &D, Target(EditTargetKind::Cancel)) == EditCommand::Quit);
}

void RunWizardEditTests()
{
    Geometry();
    Rules();
    Acts();
}
