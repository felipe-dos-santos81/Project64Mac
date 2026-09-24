// Project64 - A Nintendo 64 emulator
// See WizardDraft.h. Emission is hand-written rather than yaml-cpp's emitter, so the
// result reads like the shipped layouts; validation is a round-trip through the reader,
// so a grammar rule has exactly one statement of itself in this codebase.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "WizardDraft.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Defined below Emit; forward-declared here so LoadBase can use it. SeenOut, when given,
// gets InputConfig::Load's own Seen[] table: one bool per control, true where Path named it.
static bool LoadCapturingStderr(const char * Path, std::string * Message, bool * SeenOut = nullptr);

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
    "head pose, digital (stick: head-digital)",
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

namespace
{
// What one binding says about itself, read off it once. Two renderings work from this and
// nothing else — ValueText below writes the file's voice, DescribeBinding (further down,
// beside Describe, which is its only caller) the screen's — so a Binding's fields are
// unpacked in one place rather than in two nine-case switches that drift apart.
//
// They stay two functions on purpose. YAML punctuation has no business on the review screen,
// and "gesture mouth-open (Mo)" has none in a file the reader has to accept; the shared part
// is which fields a kind uses, not how they are spelled.
struct BindingFacts
{
    const char * YamlKey;   // the mapping key the file writes: "key", "axis", "stick", "keys"
    const char * Noun;      // what the screen calls it: the same word, except face/gesture
    const char * Name;      // the input's own name: "X", "leftx", "mid1", "mouth-open", "left"
    const char * Tag;       // Kind::Face only: the gesture's two-character overlay tag
    char Sign;              // Kind::Axis only: '+' or '-'
    const char * Keys[4];   // Kind::Keys only: up, down, left, right
    bool Toggle;            // Kind::Zone only: {zone: ..., toggle: true}
    const char * Hold;      // Kind::Pointer only: the hold slot's name, or nullptr
};

// Every name stored here outlives the call: SDL's and PointerLayout's name functions all
// return static strings, and the rest are literals. A kind neither renderer knows leaves the
// result zeroed, which reads as "{}" in the file and "nothing" on screen — the two fallbacks
// the switches below used to spell separately.
BindingFacts FactsOf(const Binding & B)
{
    BindingFacts F = {};
    switch (B.kind)
    {
    case Binding::Kind::Key:
        F.YamlKey = "key";
        F.Noun = "key";
        F.Name = SDL_GetScancodeName((SDL_Scancode)B.code);
        break;
    case Binding::Kind::Button:
        F.YamlKey = "button";
        F.Noun = "button";
        F.Name = SDL_GetGamepadStringForButton((SDL_GamepadButton)B.code);
        break;
    case Binding::Kind::Axis:
        F.YamlKey = "axis";
        F.Noun = "axis";
        F.Name = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)B.code);
        F.Sign = B.positive ? '+' : '-';
        break;
    case Binding::Kind::Zone:
        F.YamlKey = "zone";
        F.Noun = "zone";
        F.Name = PointerZoneName(B.code);
        F.Toggle = B.Toggle;
        break;
    case Binding::Kind::Face:
    {
        // The one kind whose two voices differ in the word itself: the file's key is "face",
        // because that is the grammar the reader accepts, but a player reads "gesture".
        const int Index = PointerGestureIndex((uint32_t)B.code);
        F.YamlKey = "face";
        F.Noun = "gesture";
        F.Name = PointerGestureName(Index);
        F.Tag = PointerGestureTag(Index);
        break;
    }
    case Binding::Kind::Stick:
        F.YamlKey = "stick";
        F.Noun = "stick";
        F.Name = B.code == (int)SDL_GAMEPAD_AXIS_LEFTX ? "left" : "right";
        break;
    case Binding::Kind::Pointer:
        F.YamlKey = "stick";
        F.Noun = "stick";
        F.Name = "pointer";
        F.Hold = B.Hold != POINTER_ZONE_NONE ? PointerZoneName(B.Hold) : nullptr;
        break;
    case Binding::Kind::HeadStick:
        F.YamlKey = "stick";
        F.Noun = "stick";
        F.Name = B.code == 0 ? "head" : "head-digital";
        break;
    case Binding::Kind::Keys:
        F.YamlKey = "keys";
        F.Noun = "keys";
        F.Keys[0] = SDL_GetScancodeName(B.UpKey);
        F.Keys[1] = SDL_GetScancodeName(B.DownKey);
        F.Keys[2] = SDL_GetScancodeName(B.LeftKey);
        F.Keys[3] = SDL_GetScancodeName(B.RightKey);
        break;
    }
    return F;
}
}

// The flow mapping for one binding, e.g. "{key: X}". The file's voice.
static std::string ValueText(const Binding & B)
{
    const BindingFacts F = FactsOf(B);
    if (F.YamlKey == nullptr) return "{}";
    char Buf[160];
    if (F.Keys[0] != nullptr)
    {
        snprintf(Buf, sizeof(Buf), "{%s: {up: %s, down: %s, left: %s, right: %s}}", F.YamlKey,
                 Scalar(F.Keys[0]).c_str(), Scalar(F.Keys[1]).c_str(),
                 Scalar(F.Keys[2]).c_str(), Scalar(F.Keys[3]).c_str());
    }
    else if (F.Sign != '\0')
    {
        snprintf(Buf, sizeof(Buf), "{%s: %s, sign: %c}", F.YamlKey, Scalar(F.Name).c_str(), F.Sign);
    }
    else
    {
        // Every name goes through Scalar, though only a scancode name ("Left Shift", "Keypad
        // Enter") has ever needed the quoting: a zone, gesture or stick name is already a
        // plain scalar, and Scalar hands those back unchanged.
        std::string Extra;
        if (F.Toggle) Extra += ", toggle: true";
        if (F.Hold != nullptr) { Extra += ", hold: "; Extra += F.Hold; }
        snprintf(Buf, sizeof(Buf), "{%s: %s%s}", F.YamlKey, Scalar(F.Name).c_str(), Extra.c_str());
    }
    return Buf;
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

    // Which controls Path named, straight from InputConfig::Load's own Seen[] table — it
    // already builds this to reject a repeated key, so LoadBase does not need a second parse
    // of its own to learn the same thing. Only a named control may be written back out: a
    // built-in binding can be a pair, which one input per control cannot express.
    bool Named[(int)N64Control::Count] = { false };
    if (!LoadCapturingStderr(Path, &m_Error, Named)) return false;

    InputConfig & C = InputConfig::Get();
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        m_Bindings[i] = C.Bindings((N64Control)i);
        m_Explicit[i] = Named[i];
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
    // A slot is a toggle for every control on it or for none (the reader's rule), so a
    // control joining a toggle slot, or re-capturing its own, is a toggle too.
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (m_Bindings[i].empty()) continue;
        const Binding & Other = m_Bindings[i][0];
        if (Other.kind == Binding::Kind::Zone && Other.code == Zone && Other.Toggle) B.Toggle = true;
    }
    // The hold slot is the stick's; a control taking it takes it off the stick.
    std::vector<Binding> & Stick = m_Bindings[(int)N64Control::Stick];
    if (!Stick.empty() && Stick[0].kind == Binding::Kind::Pointer && Stick[0].Hold == Zone)
    {
        Stick[0].Hold = POINTER_ZONE_NONE;
    }
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
    m_Bindings[(int)Control] = InputConfig::DefaultBinding(Control);
    m_Explicit[(int)Control] = false;
}

bool WizardDraft::Explicit(N64Control Control) const
{
    return m_Explicit[(int)Control];
}

const std::vector<Binding> & WizardDraft::Bindings(N64Control Control) const
{
    return m_Bindings[(int)Control];
}

bool WizardDraft::SharesInput(N64Control Control) const
{
    const int Index = (int)Control;
    if (!m_Explicit[Index] || m_Bindings[Index].empty()) return false;
    const Binding & Mine = m_Bindings[Index][0];
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (i == Index || !m_Explicit[i] || m_Bindings[i].empty()) continue;
        const Binding & Other = m_Bindings[i][0];
        // positive only distinguishes anything for Axis (+ vs -); the setters above zero-init
        // it to false for every other kind while InputConfig's own Make* helpers hardcode it
        // true, so comparing it unconditionally would compare wizard-made bindings against
        // wizard-made bindings only, missing a wizard binding that lands on the same
        // key/button/zone/face slot as one the base layout already set explicitly.
        if (Other.kind == Mine.kind && Other.code == Mine.code &&
            (Other.kind != Binding::Kind::Axis || Other.positive == Mine.positive))
        {
            return true;
        }
    }
    return false;
}

N64Control WizardDraft::ZoneOwner(int Zone) const
{
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (m_Bindings[i].empty()) continue;
        const Binding & B = m_Bindings[i][0];
        if (B.kind == Binding::Kind::Zone && B.code == Zone) return (N64Control)i;
    }
    return N64Control::Count;
}

int WizardDraft::HoldZone() const
{
    const std::vector<Binding> & Stick = m_Bindings[(int)N64Control::Stick];
    return (!Stick.empty() && Stick[0].kind == Binding::Kind::Pointer) ? Stick[0].Hold : POINTER_ZONE_NONE;
}

// One binding in English. ValueText is the file's voice; this is the screen's. Both read the
// same BindingFacts and render it differently, which is the whole point of the split: nothing
// here is quoted, bracketed or comma-separated, and the gesture kind reads by its English
// noun and its overlay tag rather than by the file's "face" key.
static std::string DescribeBinding(const Binding & B)
{
    const BindingFacts F = FactsOf(B);
    if (F.Noun == nullptr) return "nothing";
    char Buf[160];
    if (F.Keys[0] != nullptr)
    {
        snprintf(Buf, sizeof(Buf), "%s %s/%s/%s/%s", F.Noun, F.Keys[0], F.Keys[1], F.Keys[2],
                 F.Keys[3]);
    }
    else if (F.Sign != '\0')
    {
        snprintf(Buf, sizeof(Buf), "%s %s %c", F.Noun, F.Name, F.Sign);
    }
    else if (F.Tag != nullptr)
    {
        snprintf(Buf, sizeof(Buf), "%s %s (%s)", F.Noun, F.Name, F.Tag);
    }
    else
    {
        std::string Extra;
        if (F.Toggle) Extra += ", toggle";
        if (F.Hold != nullptr) { Extra += ", hold "; Extra += F.Hold; }
        snprintf(Buf, sizeof(Buf), "%s %s%s", F.Noun, F.Name, Extra.c_str());
    }
    return Buf;
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

// A scratch file under /tmp, its name expanded into Path. Returns the open fd, or -1 with
// the reason in *Message. Both users below want the path as well as the fd — one redirects
// stderr onto it, the other writes the draft to it and hands the name to the reader — and
// both remove it themselves once done.
static int OpenScratchFile(const char * Template, char * Path, size_t Size, std::string * Message)
{
    snprintf(Path, Size, "%s", Template);
    const int Fd = mkstemp(Path);
    if (Fd < 0) *Message = "could not create a temporary file";
    return Fd;
}

// Loads Path through the reader with stderr captured, so a rejection comes back in the
// reader's own words. Redirects the fd underneath stderr with dup2, not freopen: freopen
// would reassociate the stderr FILE object with a regular file and leave it fully buffered
// even after the fd is restored, reordering every later fprintf(stderr, ...).
static bool LoadCapturingStderr(const char * Path, std::string * Message, bool * SeenOut)
{
    char ScratchPath[64];
    const int Fd = OpenScratchFile("/tmp/pj64-wizard-err-XXXXXX", ScratchPath,
                                   sizeof(ScratchPath), Message);
    if (Fd < 0) return false;

    fflush(stderr);
    const int SavedStderr = dup(fileno(stderr));
    dup2(Fd, fileno(stderr));
    close(Fd);

    const bool Ok = InputConfig::Get().Load(Path, false, SeenOut);

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
    const int Fd = OpenScratchFile("/tmp/pj64-wizard-XXXXXX", Path, sizeof(Path), &m_Error);
    if (Fd < 0) return false;
    m_LastEmit = Emit(BaseName);
    const bool Wrote = write(Fd, m_LastEmit.data(), m_LastEmit.size()) == (ssize_t)m_LastEmit.size();
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
    // Validate above just emitted this same text (for the round-trip check) and left it in
    // m_LastEmit, so the draft is not re-rendered to a second string for the real write.
    const bool Ok = fwrite(m_LastEmit.data(), 1, m_LastEmit.size(), F) == m_LastEmit.size();
    fclose(F);
    if (!Ok)
    {
        m_Error = "could not write ";
        m_Error += Path;
    }
    return Ok;
}
