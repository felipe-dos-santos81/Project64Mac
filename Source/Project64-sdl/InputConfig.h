// Project64 - A Nintendo 64 emulator
// N64 control bindings, loaded from Config/input.yaml.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
//
// The input plugin is the only consumer. The file is read once, in PluginLoaded;
// GetKeys only reads the resolved table.
#ifndef INPUT_CONFIG_H
#define INPUT_CONFIG_H

#include <SDL3/SDL.h>
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
    enum class Kind { Key, Button, Axis, Stick, Keys };

    Kind kind;
    int code;        // Key: SDL_Scancode; Button: SDL_GamepadButton;
                     // Axis: SDL_GamepadAxis; Stick: X axis (Y is code + 1)
    bool positive;   // Axis: true fires on +, false on -
    SDL_Scancode UpKey, DownKey, LeftKey, RightKey;   // Keys only
};

class InputConfig
{
public:
    static InputConfig & Get();

    // Apply a file over the built-in defaults. On any error returns false, logs one
    // line, and leaves the instance exactly as it was.
    bool Load(const char * Path);

    const std::vector<Binding> & Bindings(N64Control Control) const;

private:
    InputConfig();

    static void DefaultBindings(std::vector<Binding> * Out);

    std::vector<Binding> m_Bindings[(int)N64Control::Count];
};

#endif
