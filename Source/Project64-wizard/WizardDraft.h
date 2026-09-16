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

class WizardDraft
{
public:
    // Starts from the built-in bindings, with no control explicit.
    WizardDraft();

    void LoadDefaults();

    // One input replaces whatever the control had, and marks it explicit.
    void SetKey(N64Control Control, SDL_Scancode Code);
    void SetButton(N64Control Control, SDL_GamepadButton Button);
    void SetAxis(N64Control Control, SDL_GamepadAxis Axis, bool Positive);
    void SetZone(N64Control Control, int Zone);
    void SetGesture(N64Control Control, uint32_t Bit);

    // Back to inherited: the control leaves the file and keeps its built-in binding,
    // which may be a pair the one-input-per-control grammar cannot write out.
    void Clear(N64Control Control);

    bool Explicit(N64Control Control) const;
    const std::vector<Binding> & Bindings(N64Control Control) const;

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
