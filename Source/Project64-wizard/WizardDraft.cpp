// Project64 - A Nintendo 64 emulator
// See WizardDraft.h. Emission is hand-written rather than yaml-cpp's emitter, so the
// result reads like the shipped layouts; validation is a round-trip through the reader,
// so a grammar rule has exactly one statement of itself in this codebase.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "WizardDraft.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include <Project64-sdl/GameConfig.h>

#include <errno.h>
#include <limits.h>
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
    m_Menu.clear();
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
    m_Menu = C.MenuBinding();
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
    // A slot is a toggle for every control on it or for none (the reader's rule), so its
    // first owner speaks for all of them: a control joining a toggle slot, or re-capturing
    // its own, is a toggle too.
    const N64Control Owner = ZoneOwner(Zone);
    B.Toggle = Owner != N64Control::Count && m_Bindings[(int)Owner][0].Toggle;
    // The hold slot is the stick's; a control taking it takes it off the stick.
    if (Zone != POINTER_ZONE_NONE && Zone == HoldZone())
    {
        m_Bindings[(int)N64Control::Stick][0].Hold = POINTER_ZONE_NONE;
    }
    // So is the menu's slot; a control taking it takes it off the menu.
    if (Zone != POINTER_ZONE_NONE && Zone == MenuZone()) m_Menu.clear();
    Replace(Control, B);
}

void WizardDraft::SetGesture(N64Control Control, uint32_t Bit)
{
    Binding B = {};
    B.kind = Binding::Kind::Face;
    B.code = (int)Bit;
    // A control taking the menu's gesture takes it off the menu.
    if (MenuGestureOf(m_Menu) == Bit) m_Menu.clear();
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
    return StickHoldZone(m_Bindings[(int)N64Control::Stick]);
}

int WizardDraft::MenuZone() const
{
    return MenuSlotOf(m_Menu);
}

namespace
{
const char * const kFullPanel = "The panel is full: free a slot for the menu first";

bool IsHeadDirection(int Gesture)
{
    return Gesture >= 0 && Gesture < POINTER_GESTURE_COUNT && (POINTER_GESTURE_HEAD_DIRECTIONS & (1u << Gesture)) != 0;
}

void AddNote(std::string * Note, const std::string & Text)
{
    if (!Note->empty()) *Note += "; ";
    *Note += Text;
}

Binding ZoneBinding(int Zone, bool Toggle)
{
    Binding B = {};
    B.kind = Binding::Kind::Zone;
    B.code = Zone;
    B.Toggle = Toggle;
    return B;
}

bool BindsPlace(const Binding & B, EditPlace Place)
{
    if (Place.Gesture) return B.kind == Binding::Kind::Face && (uint32_t)B.code == (1u << Place.Index);
    return B.kind == Binding::Kind::Zone && B.code == Place.Index;
}
}

std::vector<N64Control> WizardDraft::Occupants(EditPlace Place) const
{
    std::vector<N64Control> Out;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (i == (int)N64Control::Stick) continue;
        for (const Binding & B : m_Bindings[i])
        {
            if (BindsPlace(B, Place))
            {
                Out.push_back((N64Control)i);
                break;
            }
        }
    }
    return Out;
}

bool WizardDraft::Toggled(int Zone) const
{
    EditPlace Place;
    Place.Index = Zone;
    for (N64Control C : Occupants(Place))
    {
        for (const Binding & B : m_Bindings[(int)C])
        {
            if (BindsPlace(B, Place) && B.Toggle) return true;
        }
    }
    return false;
}

int WizardDraft::MenuGesture() const
{
    const uint32_t Bit = MenuGestureOf(m_Menu);
    return Bit != 0 ? PointerGestureIndex(Bit) : -1;
}

EditStick WizardDraft::StickForm() const
{
    const std::vector<Binding> & S = m_Bindings[(int)N64Control::Stick];
    if (S.size() != 1) return EditStick::Other;
    if (S[0].kind == Binding::Kind::Pointer) return EditStick::Pointer;
    if (S[0].kind == Binding::Kind::HeadStick) return S[0].code == 0 ? EditStick::Head : EditStick::HeadDigital;
    return EditStick::Other;
}

std::vector<N64Control> WizardDraft::NotPlaced() const
{
    std::vector<N64Control> Out;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (i == (int)N64Control::Stick) continue;
        bool Placed = false;
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone || B.kind == Binding::Kind::Face) Placed = true;
        }
        if (!Placed) Out.push_back((N64Control)i);
    }
    return Out;
}

bool WizardDraft::CanPlaceMenu(int Zone, std::string * Why) const
{
    if (Zone == POINTER_ZONE_GAME) { *Why = "the picture cannot hold the menu"; return false; }
    return true;
}

bool WizardDraft::CanPlaceHold(int Zone, std::string * Why) const
{
    if (Zone == POINTER_ZONE_GAME) { *Why = "the hold cannot go on the picture"; return false; }
    if (StickForm() != EditStick::Pointer) { *Why = "the hold needs the stick to be the pointer"; return false; }
    return true;
}

bool WizardDraft::CanToggle(int Zone, std::string * Why) const
{
    EditPlace Place;
    Place.Index = Zone;
    if (Occupants(Place).empty()) { *Why = "a toggle needs a control in the slot"; return false; }
    return true;
}

bool WizardDraft::CanUseGesture(int Gesture, std::string * Why) const
{
    const EditStick Form = StickForm();
    if ((Form == EditStick::Head || Form == EditStick::HeadDigital) && IsHeadDirection(Gesture))
    {
        *Why = "the head moves the stick";
        return false;
    }
    return true;
}

void WizardDraft::ClearControl(N64Control Control, std::string * Note)
{
    Clear(Control);
    AddNote(Note, std::string(WizardControlName(Control)) + " is not placed now");
}

void WizardDraft::RemoveHold(std::string * Note)
{
    m_Bindings[(int)N64Control::Stick][0].Hold = POINTER_ZONE_NONE;
    AddNote(Note, "the hold is gone");
}

bool WizardDraft::MoveMenu(int Avoid, std::string * Note)
{
    bool Used[POINTER_ZONE_COUNT] = { false };
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone) Used[B.code] = true;
        }
    }
    const int Hold = HoldZone();
    if (Hold != POINTER_ZONE_NONE) Used[Hold] = true;
    if (Avoid != POINTER_ZONE_NONE) Used[Avoid] = true;
    int Slot = AutoMenuSlot(Used, Hold);
    if (Used[Slot])
    {
        // AutoMenuSlot's own answer is taken: this is editor-only, so unlike play time the
        // menu does not have to take it from its control. Try every panel slot in zone order
        // instead, the picture (POINTER_ZONE_GAME) never among them, and refuse only once
        // none of the thirteen is free.
        Slot = POINTER_ZONE_NONE;
        for (int Zone = 0; Zone < POINTER_ZONE_GAME; Zone++)
        {
            if (!Used[Zone]) { Slot = Zone; break; }
        }
        if (Slot == POINTER_ZONE_NONE)
        {
            *Note = kFullPanel;
            return false;
        }
    }
    m_Menu.assign(1, ZoneBinding(Slot, false));
    AddNote(Note, std::string("the menu moved to ") + PointerZoneName(Slot));
    return true;
}

bool WizardDraft::PlaceControl(EditPlace Place, N64Control Control, std::string * Note)
{
    Note->clear();
    WizardDraft Next = *this;
    if (Place.Gesture)
    {
        if (!CanUseGesture(Place.Index, Note)) return false;
        for (N64Control C : Occupants(Place))
        {
            if (C != Control) Next.ClearControl(C, Note);
        }
        Binding B = {};
        B.kind = Binding::Kind::Face;
        B.code = (int)(1u << Place.Index);
        Next.Replace(Control, B);
        if (Next.MenuGesture() == Place.Index && !Next.MoveMenu(POINTER_ZONE_NONE, Note)) return false;
    }
    else
    {
        const int Zone = Place.Index;
        bool KeepToggle = false;
        for (N64Control C : Occupants(Place))
        {
            if (C == Control) KeepToggle = Toggled(Zone);
            else Next.ClearControl(C, Note);
        }
        if (Next.HoldZone() == Zone) Next.RemoveHold(Note);
        Next.Replace(Control, ZoneBinding(Zone, KeepToggle));
        if (Next.MenuZone() == Zone && !Next.MoveMenu(Zone, Note)) return false;
    }
    *this = Next;
    return true;
}

bool WizardDraft::PlaceMenu(int Zone, std::string * Note)
{
    Note->clear();
    if (!CanPlaceMenu(Zone, Note)) return false;
    WizardDraft Next = *this;
    EditPlace Place;
    Place.Index = Zone;
    for (N64Control C : Occupants(Place)) Next.ClearControl(C, Note);
    if (Next.HoldZone() == Zone) Next.RemoveHold(Note);
    Next.m_Menu.assign(1, ZoneBinding(Zone, false));
    *this = Next;
    return true;
}

bool WizardDraft::PlaceHold(int Zone, std::string * Note)
{
    Note->clear();
    if (!CanPlaceHold(Zone, Note)) return false;
    WizardDraft Next = *this;
    EditPlace Place;
    Place.Index = Zone;
    for (N64Control C : Occupants(Place)) Next.ClearControl(C, Note);
    Next.m_Bindings[(int)N64Control::Stick][0].Hold = Zone;
    if (Next.MenuZone() == Zone && !Next.MoveMenu(Zone, Note)) return false;
    *this = Next;
    return true;
}

bool WizardDraft::PlaceNothing(EditPlace Place, std::string * Note)
{
    Note->clear();
    WizardDraft Next = *this;
    for (N64Control C : Occupants(Place)) Next.ClearControl(C, Note);
    if (Place.Gesture)
    {
        if (Next.MenuGesture() == Place.Index && !Next.MoveMenu(POINTER_ZONE_NONE, Note)) return false;
    }
    else
    {
        if (Next.HoldZone() == Place.Index) Next.RemoveHold(Note);
        if (Next.MenuZone() == Place.Index && !Next.MoveMenu(Place.Index, Note)) return false;
    }
    *this = Next;
    return true;
}

bool WizardDraft::SetToggle(int Zone, bool On, std::string * Note)
{
    Note->clear();
    if (!CanToggle(Zone, Note)) return false;
    EditPlace Place;
    Place.Index = Zone;
    // Every control on the slot, so a shared slot stays all-or-nothing, as the reader requires.
    for (N64Control C : Occupants(Place))
    {
        for (Binding & B : m_Bindings[(int)C])
        {
            if (BindsPlace(B, Place)) B.Toggle = On;
        }
    }
    return true;
}

bool WizardDraft::SetStickForm(EditStick Form, std::string * Note)
{
    Note->clear();
    if (Form == EditStick::Other)
    {
        *Note = "that stick form is set in the step-by-step wizard";
        return false;
    }
    if (Form == StickForm()) return true;
    WizardDraft Next = *this;
    if (Form == EditStick::Pointer)
    {
        Next.SetStickPointer();
    }
    else
    {
        if (Next.HoldZone() != POINTER_ZONE_NONE) Next.RemoveHold(Note);
        for (int G = 0; G < POINTER_GESTURE_COUNT; G++)
        {
            if (!IsHeadDirection(G)) continue;
            EditPlace Place;
            Place.Gesture = true;
            Place.Index = G;
            for (N64Control C : Next.Occupants(Place)) Next.ClearControl(C, Note);
            if (Next.MenuGesture() == G && !Next.MoveMenu(POINTER_ZONE_NONE, Note)) return false;
        }
        Next.SetStickHead(Form == EditStick::HeadDigital);
    }
    *this = Next;
    return true;
}

bool WizardDraft::EnsureMenu(std::string * Note)
{
    Note->clear();
    if (!m_Menu.empty()) return false;
    bool Used[POINTER_ZONE_COUNT] = { false };
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : m_Bindings[i])
        {
            if (B.kind == Binding::Kind::Zone) Used[B.code] = true;
        }
    }
    const int Hold = HoldZone();
    if (Hold != POINTER_ZONE_NONE) Used[Hold] = true;
    const int Slot = AutoMenuSlot(Used, Hold);
    EditPlace Place;
    Place.Index = Slot;
    const char * Taken = nullptr;
    for (N64Control C : Occupants(Place))
    {
        Clear(C);
        if (Taken == nullptr) Taken = WizardControlName(C);
    }
    m_Menu.assign(1, ZoneBinding(Slot, false));
    *Note = Taken != nullptr ? std::string("the menu took ") + PointerZoneName(Slot) + " from " + Taken
                             : std::string("the menu was added on ") + PointerZoneName(Slot);
    return true;
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
    if (Count == 0 && m_Menu.empty())
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
    if (!m_Menu.empty())
    {
        char Line[256];
        snprintf(Line, sizeof(Line), "  %-10s %s\n", "Menu:", ValueText(m_Menu[0]).c_str());
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

std::string WizardDraft::LoadForRom(const char * RomPath, const char * ExeDir, std::string * Note)
{
    Note->clear();
    char Own[PATH_MAX];
    if (GameConfigPath(RomPath, ExeDir, Own, sizeof(Own)))
    {
        if (LoadBase(Own)) return Own;
        // The reader's own line is "input: <Own>[:<line>:<col>]: reason; using built-in
        // defaults" (InputConfig::Load's own fprintf). Cut through Own exactly as Validate
        // cuts through its temp path, leaving ": reason" or ":<line>:<col>: reason", and drop
        // the trailing clause: this note is about the panel editor, which has no built-in
        // defaults of its own to fall back to mid-sentence.
        std::string Reason = m_Error;
        const size_t Pos = Reason.find(Own);
        if (Pos != std::string::npos) Reason.erase(0, Pos + strlen(Own));
        const char * const kSuffix = "; using built-in defaults";
        const size_t SuffixLen = strlen(kSuffix);
        if (Reason.size() >= SuffixLen &&
            Reason.compare(Reason.size() - SuffixLen, SuffixLen, kSuffix) == 0)
        {
            Reason.erase(Reason.size() - SuffixLen);
        }
        *Note = "Your layout could not be read (" + Reason + "); starting from the generic layout";
    }
    const std::string Generic = std::string(ExeDir) + "/Config/mouse/default.yaml";
    if (LoadBase(Generic.c_str())) return Generic;
    return "";
}

// A byte-for-byte copy; false when either side cannot be opened or the write falls short.
static bool CopyFile(const std::string & From, const std::string & To)
{
    FILE * In = fopen(From.c_str(), "rb");
    if (In == nullptr) return false;
    FILE * Out = fopen(To.c_str(), "wb");
    if (Out == nullptr)
    {
        fclose(In);
        return false;
    }
    bool Ok = true;
    char Buf[4096];
    size_t N;
    while (Ok && (N = fread(Buf, 1, sizeof(Buf), In)) > 0) Ok = fwrite(Buf, 1, N, Out) == N;
    fclose(In);
    Ok = fclose(Out) == 0 && Ok;
    if (!Ok) remove(To.c_str());
    return Ok;
}

bool WizardDraft::SaveBesideRom(const char * RomPath, const char * BaseName, std::string * Saved, bool * MadeOrig)
{
    *MadeOrig = false;
    char Path[PATH_MAX];
    if (!GameConfigBesideRom(RomPath, Path, sizeof(Path)))
    {
        m_Error = "the ROM has no name to save a layout under";
        return false;
    }
    *Saved = Path;
    if (!Validate(BaseName)) return false;

    const std::string Temp = std::string(Path) + ".tmp";
    FILE * F = fopen(Temp.c_str(), "w");
    if (F == nullptr)
    {
        m_Error = strerror(errno);
        return false;
    }
    const bool Wrote = fwrite(m_LastEmit.data(), 1, m_LastEmit.size(), F) == m_LastEmit.size();
    // A power loss between the write and the rename must not leave an empty (or short)
    // layout behind: flush libc's buffer, then ask the kernel to flush its own before the
    // file is closed and renamed into place.
    const bool Flushed = fflush(F) == 0 && fsync(fileno(F)) == 0;
    if (fclose(F) != 0 || !Wrote || !Flushed)
    {
        remove(Temp.c_str());
        m_Error = "could not write the file";
        return false;
    }

    const std::string Orig = std::string(Path) + ".orig";
    if (access(Path, F_OK) == 0 && access(Orig.c_str(), F_OK) != 0)
    {
        if (!CopyFile(Path, Orig))
        {
            remove(Temp.c_str());
            m_Error = "could not keep the original";
            return false;
        }
        *MadeOrig = true;
    }
    if (rename(Temp.c_str(), Path) != 0)
    {
        m_Error = strerror(errno);
        remove(Temp.c_str());
        return false;
    }
    return true;
}
