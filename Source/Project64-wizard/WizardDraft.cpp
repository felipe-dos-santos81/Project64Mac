// Project64 - A Nintendo 64 emulator
// See WizardDraft.h. Emission is hand-written rather than yaml-cpp's emitter, so the
// result reads like the shipped layouts; validation is a round-trip through the reader,
// so a grammar rule has exactly one statement of itself in this codebase.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "WizardDraft.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <yaml-cpp/yaml.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Defined below Emit; forward-declared here so LoadBase can use it.
static bool LoadCapturingStderr(const char * Path, std::string * Message);

const char * WizardControlName(N64Control Control)
{
    static const char * const kNames[(int)N64Control::Count] = {
        "A", "B", "Z", "Start", "L", "R",
        "CUp", "CDown", "CLeft", "CRight",
        "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
        "Stick",
    };
    const int i = (int)Control;
    return (i >= 0 && i < (int)N64Control::Count) ? kNames[i] : "";
}

namespace
{
struct BaseEntry { const char * Label; const char * File; };

const BaseEntry kBases[] = {
    { "Mouse: Super Mario 64", "Config/mouse/super_mario_64_usa.yaml" },
    { "Mouse: GoldenEye 007", "Config/mouse/goldeneye_007_u.yaml" },
    { "Mouse: Mario Kart 64", "Config/mouse/mario_kart_64_u.yaml" },
    { "Face: Super Mario 64", "Config/face/super_mario_64_usa.yaml" },
    { "Face: Mario Kart 64", "Config/face/mario_kart_64_u.yaml" },
};
const int kBaseCount = (int)(sizeof(kBases) / sizeof(kBases[0]));
}

int WizardBaseCount() { return kBaseCount; }

const char * WizardBaseLabel(int Index)
{
    return (Index >= 0 && Index < kBaseCount) ? kBases[Index].Label : "";
}

const char * WizardBaseFile(int Index)
{
    return (Index >= 0 && Index < kBaseCount) ? kBases[Index].File : "";
}

namespace
{
const char * const kStickForms[] = {
    "gamepad left stick",
    "gamepad right stick",
    "the mouse (stick: pointer)",
    "head pose, analog (stick: head)",
    "head pose, four directions (stick: head-digital)",
    "four keyboard keys",
};
const int kStickFormCount = (int)(sizeof(kStickForms) / sizeof(kStickForms[0]));
}

int WizardStickFormCount() { return kStickFormCount; }

const char * WizardStickFormLabel(int Index)
{
    return (Index >= 0 && Index < kStickFormCount) ? kStickForms[Index] : "";
}

// A YAML scalar that needs no quoting is plain; anything else is double quoted. SDL spells
// scancodes like "Left Shift" and "Keypad Enter".
static std::string Scalar(const char * Name)
{
    if (Name == nullptr || Name[0] == '\0') return "\"\"";
    bool Plain = true;
    for (const char * P = Name; *P != '\0'; P++)
    {
        const bool Ok = (*P >= 'A' && *P <= 'Z') || (*P >= 'a' && *P <= 'z') ||
                        (*P >= '0' && *P <= '9') || *P == '_' || *P == '-';
        if (!Ok) { Plain = false; break; }
    }
    if (Plain) return Name;
    std::string Out = "\"";
    for (const char * P = Name; *P != '\0'; P++)
    {
        if (*P == '"' || *P == '\\') Out += '\\';
        Out += *P;
    }
    Out += '"';
    return Out;
}

static std::string KeyName(SDL_Scancode Code)
{
    return Scalar(SDL_GetScancodeName(Code));
}

// The flow mapping for one binding, e.g. "{key: X}".
static std::string ValueText(const Binding & B)
{
    char Buf[160];
    switch (B.kind)
    {
    case Binding::Kind::Key:
        snprintf(Buf, sizeof(Buf), "{key: %s}", KeyName((SDL_Scancode)B.code).c_str());
        return Buf;
    case Binding::Kind::Button:
        snprintf(Buf, sizeof(Buf), "{button: %s}",
                 Scalar(SDL_GetGamepadStringForButton((SDL_GamepadButton)B.code)).c_str());
        return Buf;
    case Binding::Kind::Axis:
        snprintf(Buf, sizeof(Buf), "{axis: %s, sign: %c}",
                 Scalar(SDL_GetGamepadStringForAxis((SDL_GamepadAxis)B.code)).c_str(),
                 B.positive ? '+' : '-');
        return Buf;
    case Binding::Kind::Zone:
        snprintf(Buf, sizeof(Buf), "{zone: %s}", PointerZoneName(B.code));
        return Buf;
    case Binding::Kind::Face:
        snprintf(Buf, sizeof(Buf), "{face: %s}",
                 PointerGestureName(PointerGestureIndex((uint32_t)B.code)));
        return Buf;
    case Binding::Kind::Stick:
        return B.code == (int)SDL_GAMEPAD_AXIS_LEFTX ? "{stick: left}" : "{stick: right}";
    case Binding::Kind::Pointer:
        return "{stick: pointer}";
    case Binding::Kind::HeadStick:
        return B.code == 0 ? "{stick: head}" : "{stick: head-digital}";
    case Binding::Kind::Keys:
        snprintf(Buf, sizeof(Buf), "{keys: {up: %s, down: %s, left: %s, right: %s}}",
                 KeyName(B.UpKey).c_str(), KeyName(B.DownKey).c_str(),
                 KeyName(B.LeftKey).c_str(), KeyName(B.RightKey).c_str());
        return Buf;
    default:
        return "{}";
    }
}

WizardDraft::WizardDraft()
{
    LoadDefaults();
}

void WizardDraft::LoadDefaults()
{
    InputConfig & C = InputConfig::Get();
    C.Reset();
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        m_Bindings[i] = C.Bindings((N64Control)i);
        m_Explicit[i] = false;
    }
    m_Error.clear();
}

bool WizardDraft::LoadBase(const char * Path)
{
    m_Error.clear();

    // The key set first. InputConfig merges a file over the built-in bindings and cannot
    // say which controls the file named, and only those may be written back out: a
    // built-in binding can be a pair, which one input per control cannot express.
    std::vector<std::string> Named;
    try
    {
        YAML::Node Root = YAML::LoadFile(Path);
        const YAML::Node Bindings = Root["bindings"];
        if (Bindings && Bindings.IsMap())
        {
            for (YAML::const_iterator It = Bindings.begin(); It != Bindings.end(); ++It)
            {
                Named.push_back(It->first.as<std::string>());
            }
        }
    }
    catch (const std::exception & E)
    {
        m_Error = E.what();
        return false;
    }

    if (!LoadCapturingStderr(Path, &m_Error)) return false;

    InputConfig & C = InputConfig::Get();
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        m_Bindings[i] = C.Bindings((N64Control)i);
        m_Explicit[i] = false;
        for (size_t n = 0; n < Named.size(); n++)
        {
            if (Named[n] == WizardControlName((N64Control)i)) { m_Explicit[i] = true; break; }
        }
    }
    return true;
}

void WizardDraft::Replace(N64Control Control, const Binding & Value)
{
    const int i = (int)Control;
    m_Bindings[i].clear();
    m_Bindings[i].push_back(Value);
    m_Explicit[i] = true;
}

void WizardDraft::SetKey(N64Control Control, SDL_Scancode Code)
{
    Binding B = {};
    B.kind = Binding::Kind::Key;
    B.code = (int)Code;
    Replace(Control, B);
}

void WizardDraft::SetButton(N64Control Control, SDL_GamepadButton Button)
{
    Binding B = {};
    B.kind = Binding::Kind::Button;
    B.code = (int)Button;
    Replace(Control, B);
}

void WizardDraft::SetAxis(N64Control Control, SDL_GamepadAxis Axis, bool Positive)
{
    Binding B = {};
    B.kind = Binding::Kind::Axis;
    B.code = (int)Axis;
    B.positive = Positive;
    Replace(Control, B);
}

void WizardDraft::SetZone(N64Control Control, int Zone)
{
    Binding B = {};
    B.kind = Binding::Kind::Zone;
    B.code = Zone;
    Replace(Control, B);
}

void WizardDraft::SetGesture(N64Control Control, uint32_t Bit)
{
    Binding B = {};
    B.kind = Binding::Kind::Face;
    B.code = (int)Bit;
    Replace(Control, B);
}

void WizardDraft::SetStickWhole(bool Right)
{
    Binding B = {};
    B.kind = Binding::Kind::Stick;
    B.code = (int)(Right ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX);
    Replace(N64Control::Stick, B);
}

void WizardDraft::SetStickPointer()
{
    Binding B = {};
    B.kind = Binding::Kind::Pointer;
    Replace(N64Control::Stick, B);
}

void WizardDraft::SetStickHead(bool Digital)
{
    Binding B = {};
    B.kind = Binding::Kind::HeadStick;
    B.code = Digital ? 1 : 0;
    Replace(N64Control::Stick, B);
}

void WizardDraft::SetStickKeys(SDL_Scancode Up, SDL_Scancode Down, SDL_Scancode Left, SDL_Scancode Right)
{
    Binding B = {};
    B.kind = Binding::Kind::Keys;
    B.UpKey = Up;
    B.DownKey = Down;
    B.LeftKey = Left;
    B.RightKey = Right;
    Replace(N64Control::Stick, B);
}

void WizardDraft::Clear(N64Control Control)
{
    const int i = (int)Control;
    InputConfig & C = InputConfig::Get();
    C.Reset();
    m_Bindings[i] = C.Bindings(Control);
    m_Explicit[i] = false;
}

bool WizardDraft::Explicit(N64Control Control) const
{
    return m_Explicit[(int)Control];
}

const std::vector<Binding> & WizardDraft::Bindings(N64Control Control) const
{
    return m_Bindings[(int)Control];
}

// One binding in English. ValueText is the file's voice; this is the screen's.
static std::string DescribeBinding(const Binding & B)
{
    char Buf[160];
    switch (B.kind)
    {
    case Binding::Kind::Key:
        snprintf(Buf, sizeof(Buf), "key %s", SDL_GetScancodeName((SDL_Scancode)B.code));
        return Buf;
    case Binding::Kind::Button:
        snprintf(Buf, sizeof(Buf), "button %s",
                 SDL_GetGamepadStringForButton((SDL_GamepadButton)B.code));
        return Buf;
    case Binding::Kind::Axis:
        snprintf(Buf, sizeof(Buf), "axis %s %c",
                 SDL_GetGamepadStringForAxis((SDL_GamepadAxis)B.code), B.positive ? '+' : '-');
        return Buf;
    case Binding::Kind::Zone:
        snprintf(Buf, sizeof(Buf), "zone %s", PointerZoneName(B.code));
        return Buf;
    case Binding::Kind::Face:
    {
        const int Index = PointerGestureIndex((uint32_t)B.code);
        snprintf(Buf, sizeof(Buf), "gesture %s (%s)", PointerGestureName(Index),
                 PointerGestureTag(Index));
        return Buf;
    }
    case Binding::Kind::Stick:
        return B.code == (int)SDL_GAMEPAD_AXIS_LEFTX ? "stick left" : "stick right";
    case Binding::Kind::Pointer:
        return "stick pointer";
    case Binding::Kind::HeadStick:
        return B.code == 0 ? "stick head" : "stick head-digital";
    case Binding::Kind::Keys:
        snprintf(Buf, sizeof(Buf), "keys %s/%s/%s/%s", SDL_GetScancodeName(B.UpKey),
                 SDL_GetScancodeName(B.DownKey), SDL_GetScancodeName(B.LeftKey),
                 SDL_GetScancodeName(B.RightKey));
        return Buf;
    }
    return "nothing";
}

std::string WizardDraft::Describe(N64Control Control) const
{
    const int i = (int)Control;
    if (m_Bindings[i].empty()) return m_Explicit[i] ? "nothing" : "inherited: nothing";
    std::string Text = DescribeBinding(m_Bindings[i][0]);
    for (size_t b = 1; b < m_Bindings[i].size(); b++)
    {
        Text += " or ";
        Text += DescribeBinding(m_Bindings[i][b]);
    }
    return m_Explicit[i] ? Text : ("inherited: " + Text);
}

std::string WizardDraft::Emit(const char * BaseName) const
{
    std::string Out;
    Out += "# Written by Project64-wizard from ";
    Out += (BaseName != nullptr && BaseName[0] != '\0') ? BaseName : "the built-in bindings";
    Out += ".\n#\n";
    Out += "# A control named here gets exactly one input, replacing its built-in binding.\n";
    Out += "# An omitted control keeps its built-in binding.\n\n";

    int Count = 0;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (m_Explicit[i] && !m_Bindings[i].empty()) Count++;
    }
    if (Count == 0)
    {
        Out += "bindings: {}\n";
        return Out;
    }
    Out += "bindings:\n";
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (!m_Explicit[i] || m_Bindings[i].empty()) continue;
        std::string Label = WizardControlName((N64Control)i);
        Label += ":";
        char Line[256];
        snprintf(Line, sizeof(Line), "  %-10s %s\n", Label.c_str(),
                 ValueText(m_Bindings[i][0]).c_str());
        Out += Line;
    }
    return Out;
}

// Loads Path through the reader with stderr captured, so a rejection comes back in the
// reader's own words. Redirects the fd underneath stderr with dup2, not freopen: freopen
// would reassociate the stderr FILE object with a regular file and leave it fully buffered
// even after the fd is restored, reordering every later fprintf(stderr, ...).
static bool LoadCapturingStderr(const char * Path, std::string * Message)
{
    char ScratchPath[64];
    snprintf(ScratchPath, sizeof(ScratchPath), "/tmp/pj64-wizard-err-XXXXXX");
    int Fd = mkstemp(ScratchPath);
    if (Fd < 0)
    {
        *Message = "could not create a temporary file";
        return false;
    }

    fflush(stderr);
    const int SavedStderr = dup(fileno(stderr));
    dup2(Fd, fileno(stderr));
    close(Fd);

    const bool Ok = InputConfig::Get().Load(Path, false);

    fflush(stderr);
    dup2(SavedStderr, fileno(stderr));
    close(SavedStderr);

    Message->clear();
    if (!Ok)
    {
        FILE * Scratch = fopen(ScratchPath, "r");
        if (Scratch != nullptr)
        {
            char Line[512];
            if (fgets(Line, sizeof(Line), Scratch) != nullptr)
            {
                size_t Len = strlen(Line);
                while (Len > 0 && (Line[Len - 1] == '\n' || Line[Len - 1] == '\r')) Line[--Len] = '\0';
                *Message = Line;
            }
            fclose(Scratch);
        }
        if (Message->empty()) *Message = "the input reader rejected this mapping";
    }
    remove(ScratchPath);
    return Ok;
}

bool WizardDraft::Validate(const char * BaseName)
{
    m_Error.clear();
    char Path[64];
    snprintf(Path, sizeof(Path), "/tmp/pj64-wizard-XXXXXX");
    const int Fd = mkstemp(Path);
    if (Fd < 0)
    {
        m_Error = "could not create a temporary file";
        return false;
    }
    const std::string Text = Emit(BaseName);
    const bool Wrote = write(Fd, Text.data(), Text.size()) == (ssize_t)Text.size();
    close(Fd);
    bool Ok = false;
    if (Wrote)
    {
        Ok = LoadCapturingStderr(Path, &m_Error);
        if (!Ok)
        {
            // The reader's message is "input: <path>:<line>:<col>: reason", and Path here is
            // the exact temp file just written above — known literally, so cut through it
            // rather than guess the prefix's shape by scanning for colons. Leaves ":<line>:
            // <col>: reason" if found. If the message doesn't contain Path (a reader message
            // that never named the file, say), it is shown unchanged.
            const size_t Pos = m_Error.find(Path);
            if (Pos != std::string::npos)
            {
                m_Error.erase(0, Pos + strlen(Path));
            }
        }
    }
    else
    {
        m_Error = "could not write a temporary file";
    }
    remove(Path);
    return Ok;
}

bool WizardDraft::Save(const char * Path, const char * BaseName)
{
    if (!Validate(BaseName)) return false;
    FILE * F = fopen(Path, "w");
    if (F == nullptr)
    {
        m_Error = "could not write ";
        m_Error += Path;
        return false;
    }
    const std::string Text = Emit(BaseName);
    const bool Ok = fwrite(Text.data(), 1, Text.size(), F) == Text.size();
    fclose(F);
    if (!Ok)
    {
        m_Error = "could not write ";
        m_Error += Path;
    }
    return Ok;
}
