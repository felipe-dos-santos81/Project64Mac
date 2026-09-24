// Project64 - A Nintendo 64 emulator
// N64 control bindings, loaded from Config/input.yaml.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
//
// Two consumers compile this file, each keeping its own InputConfig singleton: the input
// plugin, which reads the file once, in PluginLoaded, and serves GetKeys from the resolved
// table; and the frontend, which loads the same file quietly (see Load) to size its window
// before the plugin exists.
#ifndef INPUT_CONFIG_H
#define INPUT_CONFIG_H

#include <SDL3/SDL.h>
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <cstddef>
#include <vector>

enum class N64Control
{
    A, B, Z, Start, L, R,
    CUp, CDown, CLeft, CRight,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    Stick, Count
};

struct Binding
{
    enum class Kind { Key, Button, Axis, Stick, Keys, Zone, Face, Pointer, HeadStick };

    Kind kind;
    int code;        // Key: SDL_Scancode; Button: SDL_GamepadButton;
                     // Axis: SDL_GamepadAxis; Stick: X axis (Y is code + 1);
                     // Zone: zone index (PointerLayout.h); Face: one PointerGesture bit;
                     // Pointer: unused; HeadStick: 0 for {stick: head}, 1 for {stick: head-digital}
    bool positive;   // Axis: true fires on +, false on -
    SDL_Scancode UpKey, DownKey, LeftKey, RightKey;   // Keys only
    // One-button play (Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md). A
    // member of its own each, with a default, so every existing Binding{...} and Binding{}
    // stays valid; Hold is not the pointer's unused code, which every constructor sets to 0,
    // the pad-up zone.
    bool Toggle = false;                  // Zone only: a press turns it on, the next off
    int Hold = POINTER_ZONE_NONE;         // Pointer only: the stick's hold slot
};

// The hold slot of a Stick binding list ({stick: pointer, hold: <slot>}), or
// POINTER_ZONE_NONE. The reader's table, its slot checks and the wizard's draft all ask this.
inline int StickHoldZone(const std::vector<Binding> & Stick)
{
    return (!Stick.empty() && Stick[0].kind == Binding::Kind::Pointer) ? Stick[0].Hold : POINTER_ZONE_NONE;
}

// The slot or gesture bit of a Menu binding list ({zone:} or {face:} under Menu), or
// POINTER_ZONE_NONE / 0. The reader's table, its slot checks and the wizard's draft ask this.
inline int MenuSlotOf(const std::vector<Binding> & Menu)
{
    return (!Menu.empty() && Menu[0].kind == Binding::Kind::Zone) ? Menu[0].code : POINTER_ZONE_NONE;
}

inline uint32_t MenuGestureOf(const std::vector<Binding> & Menu)
{
    return (!Menu.empty() && Menu[0].kind == Binding::Kind::Face) ? (uint32_t)Menu[0].code : 0u;
}

// Where the added menu goes: the first of mid5, mid4, mid3, mid2, mid1 that Used does not
// mark, else the fallback — pad-down, or pad-up when pad-down is the stick's Hold. The fallback
// comes back whether or not Used marks it: ApplyAutoMenu then takes it from its control, and
// the panel editor refuses. The one statement of the order, for play and for editing.
inline int AutoMenuSlot(const bool Used[POINTER_ZONE_COUNT], int Hold)
{
    static const char * const kOrder[] = { "mid5", "mid4", "mid3", "mid2", "mid1" };
    for (const char * Name : kOrder)
    {
        const int Zone = PointerZoneFromName(Name);
        if (!Used[Zone]) return Zone;
    }
    const int PadDown = PointerZoneFromName("pad-down");
    return Hold == PadDown ? PointerZoneFromName("pad-up") : PadDown;
}

class InputConfig
{
public:
    static InputConfig & Get();

    // Apply a file over the built-in defaults. On any error returns false, logs one
    // line unless Quiet, and leaves the instance exactly as it was. The frontend loads
    // the same file quietly to size its window; the plugin's load reports the error.
    // SeenOut, if given, gets one bool per control (true where the file named it) on
    // success — the same table Load already builds to reject a repeated key, handed back
    // so a caller like the wizard's LoadBase can learn which controls were explicit
    // without parsing the file a second time.
    bool Load(const char * Path, bool Quiet = false, bool * SeenOut = nullptr);

    // Restore the built-in bindings, discarding whatever a file applied. The wizard starts
    // a draft from these; the plugin and the frontend never call it.
    void Reset();

    // One control's built-in binding(s), independent of whatever Load or Reset most
    // recently put in the live table. The wizard's Clear uses this to restore a single
    // control without resetting the other fourteen.
    static std::vector<Binding> DefaultBinding(N64Control Control);

    const std::vector<Binding> & Bindings(N64Control Control) const;

    // Two-character overlay label for a control ("St", "C^", ...); "" for Stick.
    static const char * ControlLabel(N64Control Control);

    // True when any binding is a zone, a face gesture or the pointer stick, which is
    // what turns the overlay on.
    bool UsesPointer() const;

    // True when any binding is a face gesture, which is what starts the camera when
    // PJ64_FACE is unset.
    bool UsesFace() const;

    // True when Stick is {stick: head} or {stick: head-digital}: the tracker's yaw and
    // pitch baselines then hold while the stick is tilted (PointerState::HeadStickWanted).
    bool UsesHeadStick() const;

    // One bit per zone bound with {zone: ..., toggle: true}.
    uint32_t PointerToggleZones() const;

    // The stick's hold slot from {stick: pointer, hold: <slot>}, or POINTER_ZONE_NONE.
    int PointerHoldZone() const;

    // The Menu key of bindings, {zone:} or {face:}, which opens the emulator actions menu
    // and presses no N64 control. Empty when the file names none.
    const std::vector<Binding> & MenuBinding() const;

    // The menu's slot, or POINTER_ZONE_NONE; its gesture bit, or 0.
    int MenuZone() const;
    uint32_t MenuGesture() const;

    // Give a layout with a panel and no Menu key a menu slot, for the launcher
    // (Docs/superpowers/specs/2026-09-24-launcher-design.md): the first of mid5, mid4, mid3,
    // mid2, mid1 no zone binding or stick hold uses, else pad-down, or pad-up when pad-down
    // is the stick's hold slot, taken from whichever control is bound there. Returns the
    // slot, or POINTER_ZONE_NONE when nothing was added. Prints "menu: added on <slot>" or
    // "menu: took <slot> from <control>" unless Quiet.
    // Not part of Load: the wizard loads layouts too and must never write an added menu.
    int ApplyAutoMenu(bool Quiet);

    // True when PJ64_MENU_AUTO is exactly "1": the launcher asks for ApplyAutoMenu. The
    // frontend and the input plugin each ask this after loading their layout.
    static bool AutoMenuWanted();

    // Overlay labels: for each zone and each gesture, the label of the control bound to
    // it, or "" when nothing is.
    void PointerLabels(char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE],
                       char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE]) const;

private:
    InputConfig();

    static void DefaultBindings(std::vector<Binding> * Out);

    std::vector<Binding> m_Bindings[(int)N64Control::Count];
    std::vector<Binding> m_Menu;
};

bool DefaultConfigPath(char * Out, size_t Size);

#endif
