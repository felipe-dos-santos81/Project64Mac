// Project64 - A Nintendo 64 emulator
// Tests for the emulator actions menu's pure rules (PointerMenu.h). No window, no core.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include <Common/PointerMenu.h>
#include "UnitTest.h"

// A zone by name, so the cases read like the spec's table.
static int Z(const char * Name)
{
    return PointerZoneFromName(Name);
}

// One click: the button goes down over Zone on one poll and up on the next. Returns what
// the press did.
static PointerMenuAction Click(PointerMenu * M, int Zone, int MenuZone, bool FaceOn = true)
{
    const PointerMenuAction A = PointerMenuStep(M, true, Zone, false, MenuZone, FaceOn);
    PointerMenuStep(M, false, Zone, false, MenuZone, FaceOn);
    return A;
}

void RunPointerMenuTests()
{
    const int Menu = Z("pad-down");

    // The table: one item per slot, the menu's own slot resumes, Fc only with the camera.
    CHECK(PointerMenuItemAt(Z("mid3"), Menu, true) == MENU_ITEM_RESUME);
    CHECK(PointerMenuItemAt(Menu, Menu, true) == MENU_ITEM_RESUME);
    CHECK(PointerMenuItemAt(Z("mid1"), Menu, true) == MENU_ITEM_FULLSCREEN);
    CHECK(PointerMenuItemAt(Z("mid2"), Menu, true) == MENU_ITEM_RECENTRE);
    CHECK(PointerMenuItemAt(Z("mid2"), Menu, false) == MENU_ITEM_NONE);
    CHECK(PointerMenuItemAt(Z("mid4"), Menu, true) == MENU_ITEM_SAVE);
    CHECK(PointerMenuItemAt(Z("mid5"), Menu, true) == MENU_ITEM_LOAD);
    CHECK(PointerMenuItemAt(Z("pad-left"), Menu, true) == MENU_ITEM_RESET);
    CHECK(PointerMenuItemAt(Z("c-right"), Menu, true) == MENU_ITEM_QUIT);
    CHECK(PointerMenuItemAt(Z("pad-up"), Menu, true) == MENU_ITEM_NONE);
    CHECK(PointerMenuItemAt(POINTER_ZONE_GAME, Menu, true) == MENU_ITEM_NONE);
    CHECK(PointerMenuItemAt(POINTER_ZONE_NONE, Menu, true) == MENU_ITEM_NONE);
    CHECK(PointerMenuItemAt(POINTER_ZONE_NONE, POINTER_ZONE_NONE, true) == MENU_ITEM_NONE);
    // On an item's slot the item wins; on mid2 without the camera the menu slot resumes.
    CHECK(PointerMenuItemAt(Z("mid4"), Z("mid4"), true) == MENU_ITEM_SAVE);
    CHECK(PointerMenuItemAt(Z("mid2"), Z("mid2"), false) == MENU_ITEM_RESUME);

    // Labels: two characters for every item, none for a blank slot; four items are guarded.
    for (int I = MENU_ITEM_RESUME; I <= MENU_ITEM_QUIT; I++)
    {
        CHECK(strlen(PointerMenuLabel((PointerMenuItem)I)) == 2);
    }
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_NONE), "") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_RESUME), "Go") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_FULLSCREEN), "Fs") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_RECENTRE), "Fc") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_SAVE), "Sv") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_LOAD), "Ld") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_RESET), "Rs") == 0);
    CHECK(strcmp(PointerMenuLabel(MENU_ITEM_QUIT), "Qt") == 0);
    CHECK(PointerMenuGuarded(MENU_ITEM_SAVE) && PointerMenuGuarded(MENU_ITEM_LOAD));
    CHECK(PointerMenuGuarded(MENU_ITEM_RESET) && PointerMenuGuarded(MENU_ITEM_QUIT));
    CHECK(!PointerMenuGuarded(MENU_ITEM_RESUME) && !PointerMenuGuarded(MENU_ITEM_FULLSCREEN));
    CHECK(!PointerMenuGuarded(MENU_ITEM_RECENTRE) && !PointerMenuGuarded(MENU_ITEM_NONE));

    // The camera "runs" while starting, tracking or looking for a face.
    CHECK(PointerMenuFaceOn(FACE_STARTING) && PointerMenuFaceOn(FACE_TRACKING) && PointerMenuFaceOn(FACE_NO_FACE));
    CHECK(!PointerMenuFaceOn(FACE_OFF) && !PointerMenuFaceOn(FACE_DENIED) && !PointerMenuFaceOn(FACE_ERROR));

    // Opening: a press on the menu slot, not elsewhere; holding it acts once; the menu's
    // own slot then resumes.
    {
        PointerMenu M;
        CHECK(Click(&M, Z("mid3"), Menu) == MENU_ACTION_NONE && !M.Open);
        CHECK(PointerMenuStep(&M, true, Menu, false, Menu, true) == MENU_ACTION_OPEN && M.Open);
        CHECK(PointerMenuStep(&M, true, Menu, false, Menu, true) == MENU_ACTION_NONE);
        PointerMenuStep(&M, false, Menu, false, Menu, true);
        CHECK(Click(&M, Menu, Menu) == MENU_ACTION_RESUME && !M.Open);
    }

    // The gesture's rising edge opens it and, again, resumes; holding the gesture does
    // neither twice. A layout with a gesture and no slot passes POINTER_ZONE_NONE.
    {
        PointerMenu M;
        CHECK(PointerMenuStep(&M, false, POINTER_ZONE_NONE, true, POINTER_ZONE_NONE, true) == MENU_ACTION_OPEN);
        CHECK(PointerMenuStep(&M, false, POINTER_ZONE_NONE, true, POINTER_ZONE_NONE, true) == MENU_ACTION_NONE);
        CHECK(M.Open);
        PointerMenuStep(&M, false, POINTER_ZONE_NONE, false, POINTER_ZONE_NONE, true);
        CHECK(PointerMenuStep(&M, false, POINTER_ZONE_NONE, true, POINTER_ZONE_NONE, true) == MENU_ACTION_RESUME);
        CHECK(!M.Open);
    }

    // A press already down when the gesture opens the menu counts only after a release.
    {
        PointerMenu M;
        PointerMenuStep(&M, true, Z("mid3"), false, Menu, true);
        CHECK(PointerMenuStep(&M, true, Z("mid3"), true, Menu, true) == MENU_ACTION_OPEN);
        CHECK(PointerMenuStep(&M, true, Z("mid3"), false, Menu, true) == MENU_ACTION_NONE && M.Open);
        PointerMenuStep(&M, false, Z("mid3"), false, Menu, true);
        CHECK(Click(&M, Z("mid3"), Menu) == MENU_ACTION_RESUME);
    }

    // Unguarded items act at once; Full screen and Recentre keep the menu open.
    {
        PointerMenu M;
        Click(&M, Menu, Menu);
        CHECK(Click(&M, Z("mid1"), Menu) == MENU_ACTION_FULLSCREEN && M.Open);
        CHECK(Click(&M, Z("mid2"), Menu) == MENU_ACTION_RECENTRE && M.Open);
        CHECK(Click(&M, Z("mid2"), Menu, false) == MENU_ACTION_NONE && M.Open);
        CHECK(Click(&M, Z("mid3"), Menu) == MENU_ACTION_RESUME && !M.Open);
    }

    // Each guarded item arms on the first click and acts on the second, closing the menu.
    const struct
    {
        const char * Slot;
        PointerMenuAction Action;
    } Guarded[] = {
        { "mid4", MENU_ACTION_SAVE },
        { "mid5", MENU_ACTION_LOAD },
        { "pad-left", MENU_ACTION_RESET },
        { "c-right", MENU_ACTION_QUIT },
    };
    for (const auto & G : Guarded)
    {
        PointerMenu M;
        Click(&M, Menu, Menu);
        CHECK(Click(&M, Z(G.Slot), Menu) == MENU_ACTION_REPAINT);
        CHECK(M.Armed == Z(G.Slot) && M.Open);
        CHECK(Click(&M, Z(G.Slot), Menu) == G.Action);
        CHECK(!M.Open && M.Armed == POINTER_ZONE_NONE);
    }

    // Disarming: a blank slot or the game image only disarms (and repaints only when
    // something was armed); another guarded item arms itself instead; an unguarded item
    // disarms and acts.
    {
        PointerMenu M;
        Click(&M, Menu, Menu);
        Click(&M, Z("mid4"), Menu);
        CHECK(Click(&M, Z("pad-up"), Menu) == MENU_ACTION_REPAINT);
        CHECK(M.Armed == POINTER_ZONE_NONE && M.Open);
        CHECK(Click(&M, Z("pad-up"), Menu) == MENU_ACTION_NONE);
        Click(&M, Z("mid4"), Menu);
        CHECK(Click(&M, POINTER_ZONE_GAME, Menu) == MENU_ACTION_REPAINT && M.Armed == POINTER_ZONE_NONE);
        Click(&M, Z("mid4"), Menu);
        CHECK(Click(&M, Z("c-right"), Menu) == MENU_ACTION_REPAINT && M.Armed == Z("c-right"));
        CHECK(Click(&M, Z("mid1"), Menu) == MENU_ACTION_FULLSCREEN && M.Armed == POINTER_ZONE_NONE);
        CHECK(Click(&M, Z("mid4"), Menu) == MENU_ACTION_REPAINT);
        CHECK(M.Open);
    }
}
