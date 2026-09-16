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

protected:
    void Replace(N64Control Control, const Binding & Value);

    std::vector<Binding> m_Bindings[(int)N64Control::Count];
    bool m_Explicit[(int)N64Control::Count];
    std::string m_Error;
};
