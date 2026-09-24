// Project64 - A Nintendo 64 emulator
// The emulator actions menu's rules, shared by the frontend's MenuHost, which acts on them,
// and the overlay, which draws the item labels. Pure functions: no SDL, no core.
// Design: Docs/superpowers/specs/2026-09-24-emulator-actions-menu-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <stdint.h>

enum PointerMenuItem
{
    MENU_ITEM_NONE,
    MENU_ITEM_RESUME,
    MENU_ITEM_FULLSCREEN,
    MENU_ITEM_RECENTRE,
    MENU_ITEM_SAVE,
    MENU_ITEM_LOAD,
    MENU_ITEM_RESET,
    MENU_ITEM_QUIT,
};

// The item on Zone while the menu is open. Zones are PointerZoneName's indices. An item's
// own slot wins over the menu slot's Resume; Recentre is there only while the camera runs.
inline PointerMenuItem PointerMenuItemAt(int Zone, int MenuZone, bool FaceOn)
{
    switch (Zone)
    {
    case 8: return MENU_ITEM_FULLSCREEN;                        // mid1
    case 9: if (FaceOn) return MENU_ITEM_RECENTRE; break;       // mid2
    case 10: return MENU_ITEM_RESUME;                           // mid3
    case 11: return MENU_ITEM_SAVE;                             // mid4
    case 12: return MENU_ITEM_LOAD;                             // mid5
    case 2: return MENU_ITEM_RESET;                             // pad-left
    case 7: return MENU_ITEM_QUIT;                              // c-right
    default: break;
    }
    return (Zone != POINTER_ZONE_NONE && Zone == MenuZone) ? MENU_ITEM_RESUME : MENU_ITEM_NONE;
}

// Two characters the overlay's font can draw, or "" for a blank slot.
inline const char * PointerMenuLabel(PointerMenuItem Item)
{
    switch (Item)
    {
    case MENU_ITEM_RESUME: return "Go";
    case MENU_ITEM_FULLSCREEN: return "Fs";
    case MENU_ITEM_RECENTRE: return "Fc";
    case MENU_ITEM_SAVE: return "Sv";
    case MENU_ITEM_LOAD: return "Ld";
    case MENU_ITEM_RESET: return "Rs";
    case MENU_ITEM_QUIT: return "Qt";
    default: return "";
    }
}

// The items a first click only arms: each loses progress if hit by mistake.
inline bool PointerMenuGuarded(PointerMenuItem Item)
{
    return Item == MENU_ITEM_SAVE || Item == MENU_ITEM_LOAD || Item == MENU_ITEM_RESET || Item == MENU_ITEM_QUIT;
}

// Whether the face tracker is running, from PointerState::Face.
inline bool PointerMenuFaceOn(uint32_t FaceStatus)
{
    return FaceStatus == FACE_STARTING || FaceStatus == FACE_TRACKING || FaceStatus == FACE_NO_FACE;
}

// The menu's state across polls. Default-constructed: closed, nothing armed, nothing held.
struct PointerMenu
{
    bool Open = false;
    int Armed = POINTER_ZONE_NONE;   // the guarded item's slot waiting for its second click
    bool PrevButton = false;
    bool PrevGesture = false;
};

enum PointerMenuAction
{
    MENU_ACTION_NONE,        // nothing changed
    MENU_ACTION_OPEN,        // the menu opened
    MENU_ACTION_REPAINT,     // armed or disarmed: the picture changed, the menu stays open
    MENU_ACTION_RESUME,      // the rest close the menu except Full screen and Recentre
    MENU_ACTION_FULLSCREEN,
    MENU_ACTION_RECENTRE,
    MENU_ACTION_SAVE,
    MENU_ACTION_LOAD,
    MENU_ACTION_RESET,
    MENU_ACTION_QUIT,
};

// One poll. Button and Zone are the left button and the zone under the cursor; Gesture is
// whether the menu's gesture is held (false when the layout has none). Closed, a press on
// the menu slot or the gesture's rising edge opens it. Open, the gesture's rising edge
// resumes, and a press does what its slot's item does: a guarded item needs a second press
// on the same slot; any other press disarms first.
inline PointerMenuAction PointerMenuStep(PointerMenu * M, bool Button, int Zone, bool Gesture, int MenuZone, bool FaceOn)
{
    const bool Press = Button && !M->PrevButton;
    const bool Rise = Gesture && !M->PrevGesture;
    M->PrevButton = Button;
    M->PrevGesture = Gesture;
    if (!M->Open)
    {
        if (Rise || (Press && Zone != POINTER_ZONE_NONE && Zone == MenuZone))
        {
            M->Open = true;
            M->Armed = POINTER_ZONE_NONE;
            return MENU_ACTION_OPEN;
        }
        return MENU_ACTION_NONE;
    }
    PointerMenuItem Item = MENU_ITEM_NONE;
    if (Rise)
    {
        Item = MENU_ITEM_RESUME;
    }
    else if (Press)
    {
        Item = PointerMenuItemAt(Zone, MenuZone, FaceOn);
        if (PointerMenuGuarded(Item) && M->Armed != Zone)
        {
            M->Armed = Zone;
            return MENU_ACTION_REPAINT;
        }
    }
    else
    {
        return MENU_ACTION_NONE;
    }
    const bool WasArmed = M->Armed != POINTER_ZONE_NONE;
    M->Armed = POINTER_ZONE_NONE;
    switch (Item)
    {
    case MENU_ITEM_FULLSCREEN: return MENU_ACTION_FULLSCREEN;
    case MENU_ITEM_RECENTRE: return MENU_ACTION_RECENTRE;
    case MENU_ITEM_NONE: return WasArmed ? MENU_ACTION_REPAINT : MENU_ACTION_NONE;
    default: break;
    }
    M->Open = false;
    switch (Item)
    {
    case MENU_ITEM_SAVE: return MENU_ACTION_SAVE;
    case MENU_ITEM_LOAD: return MENU_ACTION_LOAD;
    case MENU_ITEM_RESET: return MENU_ACTION_RESET;
    case MENU_ITEM_QUIT: return MENU_ACTION_QUIT;
    default: return MENU_ACTION_RESUME;
    }
}
