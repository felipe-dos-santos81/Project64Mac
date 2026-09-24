// Project64 - A Nintendo 64 emulator
// N64 control bindings, loaded from Config/input.yaml.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "InputConfig.h"

#include <yaml-cpp/yaml.h>

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <string>

InputConfig::InputConfig()
{
    DefaultBindings(m_Bindings);
}

void InputConfig::Reset()
{
    DefaultBindings(m_Bindings);
}

std::vector<Binding> InputConfig::DefaultBinding(N64Control Control)
{
    std::vector<Binding> Defaults[(int)N64Control::Count];
    DefaultBindings(Defaults);
    return Defaults[(int)Control];
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

const char * InputConfig::ControlLabel(N64Control Control)
{
    static const char * const kLabels[(int)N64Control::Count] = {
        "A", "B", "Z", "St", "L", "R",
        "C^", "Cv", "C<", "C>",
        "D^", "Dv", "D<", "D>",
        "",
    };
    return kLabels[(int)Control];
}

bool InputConfig::UsesPointer() const
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone || B.kind == Binding::Kind::Face || B.kind == Binding::Kind::Pointer || B.kind == Binding::Kind::HeadStick)
            {
                return true;
            }
        }
    }
    return false;
}

bool InputConfig::UsesFace() const
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Face || B.kind == Binding::Kind::HeadStick)
            {
                return true;
            }
        }
    }
    return false;
}

bool InputConfig::UsesHeadStick() const
{
    for (const Binding & B : m_Bindings[(int)N64Control::Stick])
    {
        if (B.kind == Binding::Kind::HeadStick)
        {
            return true;
        }
    }
    return false;
}

uint32_t InputConfig::PointerToggleZones() const
{
    uint32_t Mask = 0;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone && B.Toggle) Mask |= 1u << B.code;
        }
    }
    return Mask;
}

int InputConfig::PointerHoldZone() const
{
    const std::vector<Binding> & Stick = m_Bindings[(int)N64Control::Stick];
    return (!Stick.empty() && Stick[0].kind == Binding::Kind::Pointer) ? Stick[0].Hold : POINTER_ZONE_NONE;
}

void InputConfig::PointerLabels(char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE],
                                char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE]) const
{
    memset(Labels, 0, POINTER_ZONE_COUNT * POINTER_LABEL_SIZE);
    memset(GestureLabels, 0, POINTER_GESTURE_COUNT * POINTER_LABEL_SIZE);
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone)
            {
                snprintf(Labels[B.code], POINTER_LABEL_SIZE, "%s", ControlLabel((N64Control)i));
            }
            else if (B.kind == Binding::Kind::Face)
            {
                snprintf(GestureLabels[PointerGestureIndex((uint32_t)B.code)], POINTER_LABEL_SIZE, "%s", ControlLabel((N64Control)i));
            }
        }
    }
    // The hold slot belongs to no control; the overlay still has to say it is taken.
    const int Hold = PointerHoldZone();
    if (Hold != POINTER_ZONE_NONE)
    {
        snprintf(Labels[Hold], POINTER_LABEL_SIZE, "%s", "Ho");
    }
}

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

static Binding MakeZone(int Zone)
{
    return Binding{ Binding::Kind::Zone, Zone, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeFace(uint32_t Gesture)
{
    return Binding{ Binding::Kind::Face, (int)Gesture, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakePointer()
{
    return Binding{ Binding::Kind::Pointer, 0, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}

static Binding MakeHeadStick(bool Digital)
{
    return Binding{ Binding::Kind::HeadStick, Digital ? 1 : 0, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
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

static const char * const kControlNames[] = {
    "A", "B", "Z", "Start", "L", "R",
    "CUp", "CDown", "CLeft", "CRight",
    "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
    "Stick"
};

static int ControlFromName(const std::string & Name)
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (Name == kControlNames[i]) return i;
    }
    return -1;
}

// Set for the duration of a quiet Load: errors are still errors, but nothing is printed.
static bool g_Quiet = false;

struct QuietScope
{
    explicit QuietScope(bool Quiet) { g_Quiet = Quiet; }
    ~QuietScope() { g_Quiet = false; }
};

static void ConfigError(const char * Path, const YAML::Node & Node, const std::string & Message)
{
    if (g_Quiet) return;
    const YAML::Mark Mark = Node.Mark();
    if (Mark.is_null())
    {
        fprintf(stderr, "input: %s: %s; using built-in defaults\n", Path, Message.c_str());
    }
    else
    {
        fprintf(stderr, "input: %s:%d:%d: %s; using built-in defaults\n",
            Path, (int)Mark.line + 1, (int)Mark.column + 1, Message.c_str());
    }
}

static bool IsFormKey(const std::string & Key)
{
    return Key == "key" || Key == "button" || Key == "axis" || Key == "stick" || Key == "keys"
        || Key == "zone" || Key == "face";
}

static bool ParseBinding(const char * Path, const YAML::Node & Value, N64Control Control, Binding & Out)
{
    static const std::string FormError = "value must be one of {key:}, {button:}, {axis:}, {stick:}, {keys:}, {zone:}, {face:}";
    if (!Value.IsMap())
    {
        ConfigError(Path, Value, FormError);
        return false;
    }

    std::string Form;
    for (const auto & Entry : Value)
    {
        const std::string Key = Entry.first.as<std::string>();
        if (IsFormKey(Key))
        {
            if (!Form.empty()) { ConfigError(Path, Value, FormError); return false; }
            Form = Key;
        }
    }
    if (Form.empty())
    {
        ConfigError(Path, Value, FormError);
        return false;
    }
    // Three forms take a second key: {axis:, sign:}, {zone:, toggle:}, {stick:, hold:}. The
    // two one-button keys name their own form when they turn up anywhere else.
    for (const auto & Entry : Value)
    {
        const std::string Key = Entry.first.as<std::string>();
        if (Key == Form) continue;
        if ((Form == "axis" && Key == "sign") || (Form == "zone" && Key == "toggle") || (Form == "stick" && Key == "hold")) continue;
        if (Key == "toggle") { ConfigError(Path, Entry.first, "toggle only applies to {zone:}"); return false; }
        if (Key == "hold") { ConfigError(Path, Entry.first, "hold only applies to {stick: pointer}"); return false; }
        ConfigError(Path, Value, FormError);
        return false;
    }

    const bool ForStick = (Control == N64Control::Stick);
    if ((Form == "key" || Form == "button" || Form == "axis" || Form == "zone" || Form == "face") && ForStick)
    {
        ConfigError(Path, Value[Form], Form + " cannot drive Stick; use {keys:}, {stick: left/right}, {stick: pointer} or {stick: head/head-digital}");
        return false;
    }

    if (Form == "key")
    {
        const std::string Name = Value[Form].as<std::string>();
        const SDL_Scancode Sc = SDL_GetScancodeFromName(Name.c_str());
        if (Sc == SDL_SCANCODE_UNKNOWN) { ConfigError(Path, Value[Form], "unknown key \"" + Name + "\""); return false; }
        Out = MakeKey(Sc);
        return true;
    }
    if (Form == "button")
    {
        const std::string Name = Value[Form].as<std::string>();
        const SDL_GamepadButton B = SDL_GetGamepadButtonFromString(Name.c_str());
        if (B == SDL_GAMEPAD_BUTTON_INVALID) { ConfigError(Path, Value[Form], "unknown gamepad button \"" + Name + "\""); return false; }
        Out = MakeButton(B);
        return true;
    }
    if (Form == "axis")
    {
        const std::string Name = Value["axis"].as<std::string>();
        bool Positive = true;
        if (Value["sign"])
        {
            const std::string S = Value["sign"].as<std::string>();
            if (S == "+") Positive = true;
            else if (S == "-") Positive = false;
            else { ConfigError(Path, Value["sign"], "sign must be + or -"); return false; }
        }
        const SDL_GamepadAxis A = SDL_GetGamepadAxisFromString(Name.c_str());
        if (A == SDL_GAMEPAD_AXIS_INVALID) { ConfigError(Path, Value["axis"], "unknown gamepad axis \"" + Name + "\""); return false; }
        Out = MakeAxis(A, Positive);
        return true;
    }
    if (Form == "zone")
    {
        const std::string Name = Value[Form].as<std::string>();
        const int Zone = PointerZoneFromName(Name.c_str());
        if (Zone == POINTER_ZONE_NONE) { ConfigError(Path, Value[Form], "unknown zone \"" + Name + "\""); return false; }
        Out = MakeZone(Zone);
        if (Value["toggle"])
        {
            bool Toggle = false;
            if (!YAML::convert<bool>::decode(Value["toggle"], Toggle))
            {
                ConfigError(Path, Value["toggle"], "toggle must be true or false");
                return false;
            }
            Out.Toggle = Toggle;
        }
        return true;
    }
    if (Form == "face")
    {
        const std::string Name = Value[Form].as<std::string>();
        const uint32_t Gesture = PointerGestureFromName(Name.c_str());
        if (Gesture == 0) { ConfigError(Path, Value[Form], "unknown face gesture \"" + Name + "\""); return false; }
        Out = MakeFace(Gesture);
        return true;
    }
    if (Form == "stick")
    {
        if (!ForStick) { ConfigError(Path, Value[Form], "stick is only valid on Stick"); return false; }
        const std::string Name = Value[Form].as<std::string>();
        const bool HasHold = (bool)Value["hold"];
        if (HasHold && Name != "pointer") { ConfigError(Path, Value["hold"], "hold only applies to {stick: pointer}"); return false; }
        if (Name == "left") { Out = MakeStick(SDL_GAMEPAD_AXIS_LEFTX); return true; }
        if (Name == "right") { Out = MakeStick(SDL_GAMEPAD_AXIS_RIGHTX); return true; }
        if (Name == "pointer")
        {
            Out = MakePointer();
            if (HasHold)
            {
                const std::string Slot = Value["hold"].as<std::string>();
                const int Zone = PointerZoneFromName(Slot.c_str());
                if (Zone == POINTER_ZONE_NONE || Zone == POINTER_ZONE_GAME)
                {
                    ConfigError(Path, Value["hold"], "hold must name a panel slot");
                    return false;
                }
                Out.Hold = Zone;
            }
            return true;
        }
        if (Name == "head") { Out = MakeHeadStick(false); return true; }
        if (Name == "head-digital") { Out = MakeHeadStick(true); return true; }
        ConfigError(Path, Value[Form], "stick must be left, right, pointer, head or head-digital");
        return false;
    }
    // Form detection guarantees the remaining form is "keys".
    else
    {
        if (!ForStick) { ConfigError(Path, Value[Form], "keys is only valid on Stick"); return false; }
        const YAML::Node Arg = Value[Form];
        if (!Arg.IsMap() || !Arg["up"] || !Arg["down"] || !Arg["left"] || !Arg["right"])
        {
            ConfigError(Path, Arg, "keys needs up, down, left, right");
            return false;
        }
        static const char * kDirections[] = { "up", "down", "left", "right" };
        SDL_Scancode Sc[4];
        for (int i = 0; i < 4; i++)
        {
            const std::string Name = Arg[kDirections[i]].as<std::string>();
            Sc[i] = SDL_GetScancodeFromName(Name.c_str());
            if (Sc[i] == SDL_SCANCODE_UNKNOWN) { ConfigError(Path, Arg[kDirections[i]], "unknown key \"" + Name + "\" for " + kDirections[i]); return false; }
        }
        Out = MakeStickKeys(Sc[0], Sc[1], Sc[2], Sc[3]);
        return true;
    }
}

// The one-button slot rules (Docs/superpowers/specs/2026-09-24-one-button-mouse-design.md):
// every zone binding on one slot agrees on toggle, and the stick's hold slot is no control's
// zone. Run over the resolved table after every control is read, so the order the file
// names them in does not matter; controls are compared in enum order, which fixes which one
// an error names. Nodes[i] is the file's value for control i (null for an unnamed one,
// which is never a zone).
static bool CheckSlots(const char * Path, const std::vector<Binding> * Next, const YAML::Node * Nodes)
{
    int Hold = POINTER_ZONE_NONE;
    const std::vector<Binding> & Stick = Next[(int)N64Control::Stick];
    if (!Stick.empty() && Stick[0].kind == Binding::Kind::Pointer) Hold = Stick[0].Hold;

    int Owner[POINTER_ZONE_COUNT];
    bool OwnerToggle[POINTER_ZONE_COUNT];
    for (int z = 0; z < POINTER_ZONE_COUNT; z++)
    {
        Owner[z] = -1;
        OwnerToggle[z] = false;
    }
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : Next[i])
        {
            if (B.kind != Binding::Kind::Zone) continue;
            const std::string Slot = PointerZoneName(B.code);
            if (B.code == Hold)
            {
                ConfigError(Path, Nodes[i], Slot + " is the stick's hold slot and cannot also be bound");
                return false;
            }
            if (Owner[B.code] < 0)
            {
                Owner[B.code] = i;
                OwnerToggle[B.code] = B.Toggle;
                continue;
            }
            if (OwnerToggle[B.code] != B.Toggle)
            {
                const int T = B.Toggle ? i : Owner[B.code];
                const int M = B.Toggle ? Owner[B.code] : i;
                ConfigError(Path, Nodes[i], Slot + " is a toggle for " + kControlNames[T] + " but not for " + kControlNames[M]);
                return false;
            }
        }
    }
    return true;
}

bool InputConfig::Load(const char * Path, bool Quiet, bool * SeenOut)
{
    QuietScope Scope(Quiet);
    YAML::Node Root;
    try
    {
        Root = YAML::LoadFile(Path);
    }
    catch (const YAML::Exception & e)
    {
        if (!g_Quiet) fprintf(stderr, "input: %s: %s; using built-in defaults\n", Path, e.what());
        return false;
    }

    std::vector<Binding> Next[(int)N64Control::Count];
    DefaultBindings(Next);
    bool Seen[(int)N64Control::Count] = { false };
    YAML::Node ControlNodes[(int)N64Control::Count];

    // The head-direction rule (spec Part 1): remembered during the loop, checked after it,
    // so it holds whichever order the file names Stick and the gesture in.
    bool StickIsHead = false;
    YAML::Node HeadGestureNode;
    std::string HeadGestureName;

    try
    {
        const YAML::Node Bindings = Root["bindings"];
        if (Bindings)
        {
            if (!Bindings.IsMap()) { ConfigError(Path, Bindings, "'bindings' must be a map"); return false; }
            for (const auto & Entry : Bindings)
            {
                const std::string ControlName = Entry.first.as<std::string>();
                const int Index = ControlFromName(ControlName);
                if (Index < 0) { ConfigError(Path, Entry.first, "unknown control \"" + ControlName + "\""); return false; }
                if (Seen[Index]) { ConfigError(Path, Entry.first, "control named twice"); return false; }
                Seen[Index] = true;
                ControlNodes[Index] = Entry.second;
                Binding B;
                if (!ParseBinding(Path, Entry.second, (N64Control)Index, B)) return false;
                Next[Index].clear();
                Next[Index].push_back(B);
                if (B.kind == Binding::Kind::HeadStick) StickIsHead = true;
                if (B.kind == Binding::Kind::Face && ((uint32_t)B.code & POINTER_GESTURE_HEAD_DIRECTIONS) != 0 && HeadGestureName.empty())
                {
                    HeadGestureNode = Entry.second["face"];
                    HeadGestureName = PointerGestureName(PointerGestureIndex((uint32_t)B.code));
                }
            }
        }
        if (!CheckSlots(Path, Next, ControlNodes)) return false;
        if (StickIsHead && !HeadGestureName.empty())
        {
            ConfigError(Path, HeadGestureNode, HeadGestureName + " cannot be bound while Stick is head");
            return false;
        }
    }
    catch (const YAML::Exception &)
    {
        ConfigError(Path, Root, "malformed value");
        return false;
    }

    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        m_Bindings[i] = Next[i];
    }
    if (SeenOut != nullptr)
    {
        for (int i = 0; i < (int)N64Control::Count; i++) SeenOut[i] = Seen[i];
    }
    return true;
}

// The plugin lives at <bin>/Plugin/Input/<name>.dylib. Stripping the file name and then
// Input/ and Plugin/ (three strips) reaches <bin>, where Config/input.yaml sits.
// Resolving from the dylib keeps the result independent of the working directory, which
// "make run" and "make grid" both rely on.
bool DefaultConfigPath(char * Out, size_t Size)
{
    Dl_info Info;
    if (dladdr((void *)&DefaultConfigPath, &Info) == 0 || Info.dli_fname == nullptr)
    {
        return false;
    }
    std::string Dir = Info.dli_fname;
    for (int i = 0; i < 3; i++)
    {
        const size_t Slash = Dir.find_last_of('/');
        if (Slash == std::string::npos) return false;
        Dir.erase(Slash);
    }
    const std::string Path = Dir + "/Config/input.yaml";
    snprintf(Out, Size, "%s", Path.c_str());
    return true;
}
