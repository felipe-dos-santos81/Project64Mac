// Project64 - A Nintendo 64 emulator
// N64 control bindings, loaded from Config/input.yaml.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
//
// The input plugin is the only consumer. The file is read once, in PluginLoaded;
// GetKeys only reads the resolved table.
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
    enum class Kind { Key, Button, Axis, Stick, Keys, Zone, Face, Pointer };

    Kind kind;
    int code;        // Key: SDL_Scancode; Button: SDL_GamepadButton;
                     // Axis: SDL_GamepadAxis; Stick: X axis (Y is code + 1);
                     // Zone: zone index (PointerLayout.h); Face: one PointerGesture bit;
                     // Pointer: unused
    bool positive;   // Axis: true fires on +, false on -
    SDL_Scancode UpKey, DownKey, LeftKey, RightKey;   // Keys only
};

class InputConfig
{
public:
    static InputConfig & Get();

    // Apply a file over the built-in defaults. On any error returns false, logs one
    // line unless Quiet, and leaves the instance exactly as it was. The frontend loads
    // the same file quietly to size its window; the plugin's load reports the error.
    bool Load(const char * Path, bool Quiet = false);

    const std::vector<Binding> & Bindings(N64Control Control) const;

    // Two-character overlay label for a control ("St", "C^", ...); "" for Stick.
    static const char * ControlLabel(N64Control Control);

    // True when any binding is a zone, a face gesture or the pointer stick, which is
    // what turns the overlay on.
    bool UsesPointer() const;

    // True when any binding is a face gesture, which is what starts the camera when
    // PJ64_FACE is unset.
    bool UsesFace() const;

    // Overlay labels: for each zone and each gesture, the label of the control bound to
    // it, or "" when nothing is.
    void PointerLabels(char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE],
                       char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE]) const;

private:
    InputConfig();

    static void DefaultBindings(std::vector<Binding> * Out);

    std::vector<Binding> m_Bindings[(int)N64Control::Count];
};

bool DefaultConfigPath(char * Out, size_t Size);

#endif
