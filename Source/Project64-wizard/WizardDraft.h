// Project64 - A Nintendo 64 emulator
// The binding wizard's draft mapping: fifteen controls, each inherited or explicitly
// bound, and the YAML they become. No window, no renderer and no event loop, so the part
// that can be silently wrong is the part the tests cover.
// Design: Docs/superpowers/specs/2026-09-15-binding-wizard-design.md
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

#include <Project64-sdl/InputConfig.h>

#include <string>
#include <vector>

// The reader's own spelling of a control: "A", "CUp", "DPadLeft", "Stick". "" out of range.
const char * WizardControlName(N64Control Control);

// The shipped layouts offered as starting points, in the order the base screen lists them.
int WizardBaseCount();
const char * WizardBaseLabel(int Index);   // "Mouse: Super Mario 64"; "" out of range
const char * WizardBaseFile(int Index);    // "Config/mouse/super_mario_64_usa.yaml"; "" out of range

// The six forms the Stick chooser offers, in the order it lists them.
int WizardStickFormCount();
const char * WizardStickFormLabel(int Index);

// A place the panel editor edits: a zone (0 … POINTER_ZONE_COUNT - 1, the picture included)
// or a face gesture (its index, 0 … POINTER_GESTURE_COUNT - 1).
struct EditPlace
{
    bool Gesture = false;
    int Index = POINTER_ZONE_NONE;
};

// The stick forms the panel editor offers; Other is any form it does not (a gamepad stick,
// four keys), kept as it is until one of the three is chosen.
enum class EditStick { Pointer, Head, HeadDigital, Other };

class WizardDraft
{
public:
    // Starts from the built-in bindings, with no control explicit.
    WizardDraft();

    void LoadDefaults();

    // Replaces the draft with Path's bindings; the controls Path names become explicit.
    // False leaves the draft untouched and puts the reason in Error().
    bool LoadBase(const char * Path);

    // One input replaces whatever the control had, and marks it explicit.
    void SetKey(N64Control Control, SDL_Scancode Code);
    void SetButton(N64Control Control, SDL_GamepadButton Button);
    void SetAxis(N64Control Control, SDL_GamepadAxis Axis, bool Positive);
    void SetZone(N64Control Control, int Zone);
    void SetGesture(N64Control Control, uint32_t Bit);

    // Stick only. Whole: {stick: left} or {stick: right}. Head: {stick: head} or
    // {stick: head-digital}. Keys: four scancodes as a digital stick.
    void SetStickWhole(bool Right);
    void SetStickPointer();
    void SetStickHead(bool Digital);
    void SetStickKeys(SDL_Scancode Up, SDL_Scancode Down, SDL_Scancode Left, SDL_Scancode Right);

    // Back to inherited: the control leaves the file and keeps its built-in binding,
    // which may be a pair the one-input-per-control grammar cannot write out.
    void Clear(N64Control Control);

    bool Explicit(N64Control Control) const;
    const std::vector<Binding> & Bindings(N64Control Control) const;

    // True when another explicit control is bound to the same input. The reader allows it —
    // the N64 can have two buttons on one key — so this is something to warn about, never to
    // refuse. An inherited control is neither asked about nor compared against: only what
    // Emit writes can collide in the file.
    //
    // Only Bindings[0] is compared, so Stick's four-key form — whose UpKey/DownKey/LeftKey/
    // RightKey live inside one Kind::Keys binding rather than in code/positive — is never
    // checked against any other control's binding. That form's collisions go unwarned.
    bool SharesInput(N64Control Control) const;

    // The first control whose binding is this panel slot, or N64Control::Count when nothing
    // sits there. Bindings[0] only, the same first-binding rule SharesInput uses; unlike
    // SharesInput, an inherited control counts, because the panel shows what the player will
    // actually find in the slot rather than only what this draft will write.
    N64Control ZoneOwner(int Zone) const;

    // The stick's hold slot ({stick: pointer, hold: <slot>}), or POINTER_ZONE_NONE. It
    // belongs to no control, so ZoneOwner never names it.
    int HoldZone() const;

    // The draft's menu slot ({zone:} under Menu), or POINTER_ZONE_NONE. Like the hold slot
    // it belongs to no control. The wizard keeps Menu from a base and writes it back;
    // choosing it is the clickable wizard's job.
    int MenuZone() const;

    // ---- The panel editor (Docs/superpowers/specs/2026-09-24-clickable-wizard-design.md) ----
    // A slot holds one thing: a control, the menu, the hold or nothing; a gesture holds one
    // control or nothing. Each Place/Set call works on a copy and commits only on success: a
    // refusal leaves the draft as it was, with the reason in *Note. On success *Note lists
    // what moved as a side effect, joined by "; ", or is empty.

    // Every control bound to Place. A loaded layout may share a slot.
    std::vector<N64Control> Occupants(EditPlace Place) const;
    // True when the controls in Zone are a toggle slot.
    bool Toggled(int Zone) const;
    // The menu's gesture index, or -1 when the menu is not a gesture.
    int MenuGesture() const;
    EditStick StickForm() const;
    // Every control except Stick with neither a zone nor a gesture, in N64Control order.
    std::vector<N64Control> NotPlaced() const;

    // Whether a choice is allowed where it would go; *Why gets the reason when not.
    bool CanPlaceMenu(int Zone, std::string * Why) const;
    bool CanPlaceHold(int Zone, std::string * Why) const;
    bool CanToggle(int Zone, std::string * Why) const;
    bool CanUseGesture(int Gesture, std::string * Why) const;

    bool PlaceControl(EditPlace Place, N64Control Control, std::string * Note);
    bool PlaceMenu(int Zone, std::string * Note);
    bool PlaceHold(int Zone, std::string * Note);
    bool PlaceNothing(EditPlace Place, std::string * Note);
    bool SetToggle(int Zone, bool On, std::string * Note);
    bool SetStickForm(EditStick Form, std::string * Note);

    // For a draft with no menu: puts it where PJ64_MENU_AUTO would at play time (AutoMenuSlot),
    // taking that slot from its control when the panel is full. False when a menu exists.
    bool EnsureMenu(std::string * Note);

    // What the control is bound to, in English: "key X", "zone mid1",
    // "gesture mouth-open (Mo)", "keys Up/Down/Left/Right". An inherited control reads
    // "inherited: " followed by every built-in input, joined by " or ".
    std::string Describe(N64Control Control) const;

    // The YAML this draft emits, always ending in a newline. BaseName goes in the header.
    std::string Emit(const char * BaseName) const;

    // Round-trips Emit through the real reader, keeping no file. False when the reader
    // rejects it, and Error() then holds the reader's own line.
    bool Validate(const char * BaseName);

    // Validates, then writes to Path. False on a rejected draft or a write error.
    bool Save(const char * Path, const char * BaseName);

    // "" when the last Validate or Save succeeded.
    const char * Error() const { return m_Error.c_str(); }

private:
    void Replace(N64Control Control, const Binding & Value);
    void ClearControl(N64Control Control, std::string * Note);
    void RemoveHold(std::string * Note);
    // Moves the menu to the first free slot by AutoMenuSlot, counting Avoid as taken. False,
    // with the full-panel reason in *Note, when no slot is free.
    bool MoveMenu(int Avoid, std::string * Note);

    std::vector<Binding> m_Bindings[(int)N64Control::Count];
    bool m_Explicit[(int)N64Control::Count];
    std::string m_Error;
    std::string m_LastEmit;   // set by Validate; Save reuses it rather than calling Emit twice
    std::vector<Binding> m_Menu;   // the base's Menu key, empty for none
};
