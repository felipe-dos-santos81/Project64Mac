// Project64 - A Nintendo 64 emulator
// N64 control bindings, loaded from Config/input.yaml.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "InputConfig.h"

#include <yaml-cpp/yaml.h>

InputConfig::InputConfig()
{
    DefaultBindings(m_Bindings);
}

InputConfig & InputConfig::Get()
{
    static InputConfig Instance;
    return Instance;
}

const std::vector<Binding> & InputConfig::Bindings(N64Control Control) const
{
    return m_Bindings[(int)Control];
}

bool InputConfig::Load(const char * Path)
{
    YAML::Node Root = YAML::LoadFile(Path);
    return Root.IsDefined();
}

// N64 stick range is -80..80; the SDL axis range is -32768..32767.
static const int16_t STICK_DEAD_ZONE = 4000;
static const int16_t STICK_THRESHOLD = 16000;
static const int N64_AXIS_MAX = 80;

static Binding MakeKey(SDL_Scancode Sc)
{
    return Binding{ Binding::Kind::Key, (int)Sc, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeButton(SDL_GamepadButton Button)
{
    return Binding{ Binding::Kind::Button, (int)Button, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeAxis(SDL_GamepadAxis Axis, bool Positive)
{
    return Binding{ Binding::Kind::Axis, (int)Axis, Positive, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeStick(SDL_GamepadAxis XAxis)
{
    return Binding{ Binding::Kind::Stick, (int)XAxis, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeStickKeys(SDL_Scancode Up, SDL_Scancode Down, SDL_Scancode Left, SDL_Scancode Right)
{
    return Binding{ Binding::Kind::Keys, 0, true, Up, Down, Left, Right };
}

void InputConfig::DefaultBindings(std::vector<Binding> * Out)
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        Out[i].clear();
    }
    auto add = [Out](N64Control Control, const Binding & B) { Out[(int)Control].push_back(B); };

    add(N64Control::A, MakeKey(SDL_SCANCODE_X));
    add(N64Control::A, MakeButton(SDL_GAMEPAD_BUTTON_SOUTH));
    add(N64Control::B, MakeKey(SDL_SCANCODE_C));
    add(N64Control::B, MakeButton(SDL_GAMEPAD_BUTTON_WEST));
    add(N64Control::Z, MakeKey(SDL_SCANCODE_Z));
    add(N64Control::Z, MakeAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true));
    add(N64Control::Start, MakeKey(SDL_SCANCODE_RETURN));
    add(N64Control::Start, MakeButton(SDL_GAMEPAD_BUTTON_START));
    add(N64Control::L, MakeKey(SDL_SCANCODE_Q));
    add(N64Control::L, MakeButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    add(N64Control::R, MakeKey(SDL_SCANCODE_E));
    add(N64Control::R, MakeButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    add(N64Control::CUp, MakeKey(SDL_SCANCODE_W));
    add(N64Control::CUp, MakeAxis(SDL_GAMEPAD_AXIS_RIGHTY, false));
    add(N64Control::CDown, MakeKey(SDL_SCANCODE_S));
    add(N64Control::CDown, MakeAxis(SDL_GAMEPAD_AXIS_RIGHTY, true));
    add(N64Control::CLeft, MakeKey(SDL_SCANCODE_A));
    add(N64Control::CLeft, MakeAxis(SDL_GAMEPAD_AXIS_RIGHTX, false));
    add(N64Control::CRight, MakeKey(SDL_SCANCODE_D));
    add(N64Control::CRight, MakeAxis(SDL_GAMEPAD_AXIS_RIGHTX, true));
    add(N64Control::DPadUp, MakeKey(SDL_SCANCODE_I));
    add(N64Control::DPadUp, MakeButton(SDL_GAMEPAD_BUTTON_DPAD_UP));
    add(N64Control::DPadDown, MakeKey(SDL_SCANCODE_K));
    add(N64Control::DPadDown, MakeButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    add(N64Control::DPadLeft, MakeKey(SDL_SCANCODE_J));
    add(N64Control::DPadLeft, MakeButton(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    add(N64Control::DPadRight, MakeKey(SDL_SCANCODE_L));
    add(N64Control::DPadRight, MakeButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    add(N64Control::Stick, MakeStickKeys(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT));
    add(N64Control::Stick, MakeStick(SDL_GAMEPAD_AXIS_LEFTX));
}
