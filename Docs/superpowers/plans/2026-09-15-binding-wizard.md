# Binding Wizard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A second binary, `Bin/macOS/Project64-wizard`, walks the player through the fifteen N64 controls and writes an input-mapping YAML file the emulator accepts, reaching every binding form by watching what the player does.

**Architecture:** A pure `WizardDraft` holds fifteen controls, each inherited or explicitly bound, and turns them into YAML; it has no window, no renderer and no event loop, so it is unit-tested headlessly. A thin SDL3 shell (`main.cpp` + `Screens.cpp`) draws one screen at a time with `SDL_Renderer` and `SDL_RenderDebugText` and feeds events into the draft. Saving is gated by a round-trip through `InputConfig`, the emulator's own parser.

**Tech Stack:** C++14, SDL3 (renderer, debug font, text input), yaml-cpp, Objective-C++ via the existing `FaceTracker`, GNU Make, POSIX sh.

**Spec:** `Docs/superpowers/specs/2026-09-15-binding-wizard-design.md`

## Global Constraints

- **Never capture the screen.** Pixels are only ever read from inside the program (`PJ64_FRAME_DUMP`); never a screenshot utility.
- **Automated runs never start the camera.** Every scripted or test run sets `PJ64_FACE=0`; the wizard's `--selftest` never starts the tracker whatever `PJ64_FACE` says.
- **Privacy:** frames are never stored, shown or written; nothing about them is logged. The only face information on screen is which gestures are firing.
- **Windowed runs in scripts are bounded:** `perl -e 'alarm N; exec @ARGV' -- <cmd> || true`.
- **Line endings:** run `file <path>` before editing any tracked file; every file this plan touches is LF and must stay LF. New files are LF.
- **Language level:** `-std=c++14`; no new frameworks, no new dependencies. SDL3, yaml-cpp and the Apple frameworks are already linked by the frontend.
- **No OpenGL in the wizard.** Drawing is `SDL_Renderer`; text is `SDL_RenderDebugText` scaled with `SDL_SetRenderScale`. The overlay's twenty-glyph tag font is not reused.
- **The reader is the authority.** The wizard never re-implements a grammar rule; it writes a temp file and asks `InputConfig::Load` whether the result is acceptable, and shows the reader's own error line.
- **Naming a control replaces its built-in binding; omitting it keeps it.** A built-in default may be a *pair* of inputs, which the one-input-per-control grammar cannot express, so inherited-versus-explicit is a real state, not a formatting choice.
- **Left and right are the player's own** for every gesture and for the stick.
- **Build check per task:** `make all` must stay warning-free apart from the pre-existing macOS `-Wdeprecated-declarations` notices from the GL 2.1 overlay.
- **Commit messages** end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.

---

## File structure

| File | Responsibility | Change |
|---|---|---|
| `Source/Project64-wizard/WizardDraft.h` | draft interface, control-name helper | new |
| `Source/Project64-wizard/WizardDraft.cpp` | draft state, base loading, emission, validation | new |
| `Source/Project64-wizard/WizardDraftTest.cpp` | headless tests for all of the above | new |
| `Source/Project64-wizard/Screens.h` | screen/mode enums, UI state, three entry points | new |
| `Source/Project64-wizard/Screens.cpp` | draw one screen; turn one event into one draft change | new |
| `Source/Project64-wizard/main.cpp` | SDL init, window, event loop, tracker lifetime, `--selftest` | new |
| `Source/Project64-sdl/InputConfig.h`, `.cpp` | `Reset()`, so the draft can ask for the built-in bindings | modify |
| `Source/Project64-sdl/FaceTracker.h` | stale "three bits" comment | modify |
| `Makefile` | `WIZARD_SRC`, step 7c, `wizard-draft-test`, `wizard-selftest`, `all` | modify |
| `Scripts/wizard_selftest.sh` | end-to-end proof, no window and no camera | new |
| `README.md`, `AGENTS.md` | the wizard section, architecture note, one trap | modify |

---

### Task 1: The draft, the five simple forms, and the round-trip gate

**Files:**
- Create: `Source/Project64-wizard/WizardDraft.h`, `Source/Project64-wizard/WizardDraft.cpp`
- Create: `Source/Project64-wizard/WizardDraftTest.cpp`
- Modify: `Source/Project64-sdl/InputConfig.h`, `Source/Project64-sdl/InputConfig.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `N64Control`, `Binding`, `InputConfig::Get()`, `InputConfig::Load(const char *, bool)`, `InputConfig::Bindings(N64Control)` from `Source/Project64-sdl/InputConfig.h`; `PointerZoneName`, `PointerZoneFromName` from `Source/Common/PointerLayout.h`; `PointerGestureName`, `PointerGestureTag`, `PointerGestureIndex` from `Source/Common/PointerState.h`.
- Produces (later tasks rely on these exact names):
  - `const char * WizardControlName(N64Control)`.
  - `class WizardDraft` with `LoadDefaults()`, `SetKey`, `SetButton`, `SetAxis`, `SetZone`, `SetGesture`, `Clear`, `Explicit`, `Bindings`, `Emit`, `Validate`, `Save`, `Error`.
  - `void InputConfig::Reset()`.
  - `make wizard-draft-test`.

- [ ] **Step 1: Check line endings**

Run: `file Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp Makefile`
Expected: none reports CRLF.

- [ ] **Step 2: Write the failing test**

Create `Source/Project64-wizard/WizardDraftTest.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// Tests for WizardDraft. No window and no renderer; safe to run anywhere.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "WizardDraft.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <string.h>

static int Failures = 0;

#define CHECK(Cond) \
    do { if (!(Cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond); Failures++; } } while (0)

// True when Text contains Needle. The emitted file is small, so substring checks read
// better here than parsing it back a second time.
static bool Has(const std::string & Text, const char * Needle)
{
    return Text.find(Needle) != std::string::npos;
}

int main()
{
    // Control names are the reader's own spelling.
    CHECK(strcmp(WizardControlName(N64Control::A), "A") == 0);
    CHECK(strcmp(WizardControlName(N64Control::CUp), "CUp") == 0);
    CHECK(strcmp(WizardControlName(N64Control::DPadLeft), "DPadLeft") == 0);
    CHECK(strcmp(WizardControlName(N64Control::Stick), "Stick") == 0);

    // A fresh draft inherits everything, so it writes an empty map, and the reader
    // accepts it.
    {
        WizardDraft D;
        CHECK(!D.Explicit(N64Control::A));
        CHECK(Has(D.Emit("the built-in bindings"), "bindings: {}"));
        CHECK(D.Validate("the built-in bindings"));
        CHECK(strcmp(D.Error(), "") == 0);
    }

    // Each simple form emits its own syntax and survives the reader.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_X);
        D.SetButton(N64Control::B, SDL_GAMEPAD_BUTTON_SOUTH);
        D.SetAxis(N64Control::Z, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true);
        D.SetAxis(N64Control::R, SDL_GAMEPAD_AXIS_RIGHTY, false);
        D.SetZone(N64Control::Start, 8);
        D.SetGesture(N64Control::L, POINTER_GESTURE_MOUTH_OPEN);
        const std::string Text = D.Emit("the built-in bindings");
        CHECK(Has(Text, "A:         {key: X}"));
        CHECK(Has(Text, "B:         {button: a}"));
        CHECK(Has(Text, "Z:         {axis: lefttrigger, sign: +}"));
        CHECK(Has(Text, "R:         {axis: righty, sign: -}"));
        CHECK(Has(Text, "Start:     {zone: mid1}"));
        CHECK(Has(Text, "L:         {face: mouth-open}"));
        CHECK(D.Validate("the built-in bindings"));
        CHECK(D.Explicit(N64Control::A));
        CHECK(D.Bindings(N64Control::A).size() == 1);
        CHECK(D.Bindings(N64Control::A)[0].code == SDL_SCANCODE_X);
    }

    // A name with a space is quoted, and the reader still takes it.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_LSHIFT);
        CHECK(Has(D.Emit("x"), "{key: \"Left Shift\"}"));
        CHECK(D.Validate("x"));
    }

    // Clearing returns a control to inherited: it leaves the file and keeps whatever the
    // built-in bindings gave it, which for B is a pair the grammar could not write out.
    {
        WizardDraft D;
        const size_t BDefaults = D.Bindings(N64Control::B).size();
        CHECK(BDefaults == 2);
        D.SetKey(N64Control::B, SDL_SCANCODE_Q);
        CHECK(Has(D.Emit("x"), "B:"));
        D.Clear(N64Control::B);
        CHECK(!D.Explicit(N64Control::B));
        CHECK(D.Bindings(N64Control::B).size() == BDefaults);
        CHECK(!Has(D.Emit("x"), "\n  B:"));
    }

    // The header names the base, so a file says where it came from.
    {
        WizardDraft D;
        CHECK(Has(D.Emit("Config/mouse/goldeneye_007_u.yaml"),
                  "# Written by Project64-wizard from Config/mouse/goldeneye_007_u.yaml."));
    }

    // A rule the reader owns is reported in the reader's own words, not paraphrased.
    {
        WizardDraft D;
        D.SetGesture(N64Control::Z, POINTER_GESTURE_HEAD_UP);
        D.SetKey(N64Control::A, SDL_SCANCODE_X);
        CHECK(D.Validate("x"));            // head-up alone is fine
        CHECK(strcmp(D.Error(), "") == 0);
    }

    if (Failures != 0) { fprintf(stderr, "%d failure(s)\n", Failures); return 1; }
    printf("ok: wizard draft\n");
    return 0;
}
```

- [ ] **Step 3: Add the Makefile target and run the test to watch it fail**

In `Makefile`, after the `INPUT_SRC` block (the line beginning `FRONTEND_SRC =`), add:

```make
# The wizard's own sources. main.cpp and Screens.cpp join this list in Task 4; until then
# the draft is the only part that exists, and only its test builds.
WIZARD_SRC = Project64-wizard/WizardDraft.cpp
```

After `FRONTEND_MM_OBJS = $(call objs,$(FRONTEND_MM_SRC))` add:

```make
WIZARD_OBJS = $(call objs,$(WIZARD_SRC))
```

Add `$(WIZARD_OBJS)` to the end of `ALL_OBJS`'s last line.

In the per-component flags block, extend the two `CPPFLAGS` lines and the `WARN` line so the wizard's objects and its test compile like the frontend's:

```make
$(AUDIO_OBJS) $(INPUT_OBJS) $(FRONTEND_OBJS) $(FRONTEND_MM_OBJS) $(WIZARD_OBJS) $(BUILD)/Project64-sdl/InputConfigTest.o $(BUILD)/Project64-wizard/WizardDraftTest.o: CPPFLAGS += $(SDL_CFLAGS)
$(INPUT_OBJS) $(WIZARD_OBJS) $(BUILD)/Project64-sdl/InputConfigTest.o $(BUILD)/Project64-wizard/WizardDraftTest.o: CPPFLAGS += $(YAML_CFLAGS)
$(FRONTEND_OBJS) $(FRONTEND_MM_OBJS) $(WIZARD_OBJS): WARN = -Wall
```

Beside the other test targets (after `game-config-test`), add:

```make
wizard-draft-test: $(BUILD)/Project64-wizard/WizardDraftTest.o $(BUILD)/Project64-wizard/WizardDraft.o $(BUILD)/Project64-sdl/InputConfig.o ## Run the WizardDraft tests
	$(CXX) $(LDFLAGS) -o $(BUILD)/wizard-draft-test $^ $(SDL_LIBS) $(YAML_LIBS)
	@$(BUILD)/wizard-draft-test
```

Add `wizard-draft-test` to the `.PHONY` line.

Run: `make wizard-draft-test`
Expected: FAIL — `WizardDraft.h` does not exist.

- [ ] **Step 4: Give InputConfig a way to restore its defaults**

In `Source/Project64-sdl/InputConfig.h`, after the `Load` declaration add:

```cpp
    // Restore the built-in bindings, discarding whatever a file applied. The wizard starts
    // a draft from these; the plugin and the frontend never call it.
    void Reset();
```

In `Source/Project64-sdl/InputConfig.cpp`, beside the constructor add:

```cpp
void InputConfig::Reset()
{
    DefaultBindings(m_Bindings);
}
```

This is the constructor's body (`InputConfig.cpp:13-16`) verbatim: `DefaultBindings` fills the whole array in one call.

- [ ] **Step 5: Write the header**

Create `Source/Project64-wizard/WizardDraft.h`:

```cpp
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
```

`Replace` and the members are `protected` rather than `private` because Task 3 adds the `Stick` setters and `Describe` to this same class; keeping them reachable avoids a second edit pass over the access specifiers.

- [ ] **Step 6: Write the body**

Create `Source/Project64-wizard/WizardDraft.cpp`:

```cpp
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

// A YAML scalar that needs no quoting is plain; anything else is double quoted. SDL spells
// scancodes like "Left Shift" and "Keypad Enter".
static std::string Scalar(const char * Name)
{
    if (Name == NULL || Name[0] == '\0') return "\"\"";
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

std::string WizardDraft::Emit(const char * BaseName) const
{
    std::string Out;
    Out += "# Written by Project64-wizard from ";
    Out += (BaseName != NULL && BaseName[0] != '\0') ? BaseName : "the built-in bindings";
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
        if (Scratch != NULL)
        {
            char Line[512];
            if (fgets(Line, sizeof(Line), Scratch) != NULL)
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
    if (F == NULL)
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
```

- [ ] **Step 7: Run the test**

Run: `make wizard-draft-test`
Expected: `ok: wizard draft`

- [ ] **Step 8: Run the rest of the suite**

Run: `make input-config-test pointer-layout-test face-gesture-test game-config-test && make all`
Expected: every suite prints its `ok:` line; `make all` is warning-free apart from the overlay's deprecation notices. `InputConfig::Reset` is new API on a file the plugin also compiles, so `input-config-test` passing matters here.

- [ ] **Step 9: Commit**

```bash
git add Source/Project64-wizard/WizardDraft.h Source/Project64-wizard/WizardDraft.cpp \
        Source/Project64-wizard/WizardDraftTest.cpp Source/Project64-sdl/InputConfig.h \
        Source/Project64-sdl/InputConfig.cpp Makefile
git commit -m "$(cat <<'EOF'
Hold a wizard draft and gate it on the reader

Fifteen controls, each inherited or explicitly bound, emitted as the YAML the
emulator already reads. Saving is a round-trip through InputConfig, so a
grammar rule keeps exactly one statement of itself and a rejection comes back
in the reader's own words.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Loading a base, and which controls it named

**Files:**
- Modify: `Source/Project64-wizard/WizardDraft.h`, `Source/Project64-wizard/WizardDraft.cpp`
- Test: `Source/Project64-wizard/WizardDraftTest.cpp`

**Interfaces:**
- Consumes: Task 1's `WizardDraft`, `WizardControlName`.
- Produces: `bool WizardDraft::LoadBase(const char * Path)`; `int WizardBaseCount()`, `const char * WizardBaseLabel(int)`, `const char * WizardBaseFile(int)`.

A base gives the draft its starting values, and only the controls the base file *named* start explicit — which `InputConfig` cannot report, because it merges a file over the defaults. So the base is read twice: once by the reader for values, once by yaml-cpp for the key set.

- [ ] **Step 1: Write the failing tests**

Add to `Source/Project64-wizard/WizardDraftTest.cpp`, before the `Failures != 0` check:

```cpp
    // The five shipped layouts are offered as bases, by label and by path.
    CHECK(WizardBaseCount() == 5);
    CHECK(strcmp(WizardBaseFile(0), "Config/mouse/super_mario_64_usa.yaml") == 0);
    CHECK(strcmp(WizardBaseFile(4), "Config/face/mario_kart_64_u.yaml") == 0);
    CHECK(strcmp(WizardBaseLabel(0), "Mouse: Super Mario 64") == 0);
    CHECK(strcmp(WizardBaseFile(-1), "") == 0);
    CHECK(strcmp(WizardBaseFile(5), "") == 0);

    // A base marks exactly the controls it named, and re-emits to something the reader
    // still accepts. The SM64 face layout names eight controls and inherits the rest.
    {
        WizardDraft D;
        CHECK(D.LoadBase("Config/face/super_mario_64_usa.yaml"));
        CHECK(D.Explicit(N64Control::Stick));
        CHECK(D.Explicit(N64Control::A));
        CHECK(D.Explicit(N64Control::CRight));
        CHECK(!D.Explicit(N64Control::DPadUp));      // not named: inherited
        CHECK(D.Bindings(N64Control::A)[0].kind == Binding::Kind::Face);
        CHECK(D.Bindings(N64Control::A)[0].code == (int)POINTER_GESTURE_MOUTH_OPEN);
        const std::string Text = D.Emit("Config/face/super_mario_64_usa.yaml");
        CHECK(Has(Text, "Stick:     {stick: head}"));
        CHECK(Has(Text, "A:         {face: mouth-open}"));
        CHECK(!Has(Text, "\n  DPadUp:"));
        CHECK(D.Validate("Config/face/super_mario_64_usa.yaml"));
    }

    // Every shipped layout survives the same round trip.
    for (int i = 0; i < WizardBaseCount(); i++)
    {
        WizardDraft D;
        CHECK(D.LoadBase(WizardBaseFile(i)));
        CHECK(D.Validate(WizardBaseFile(i)));
        if (!D.Validate(WizardBaseFile(i))) fprintf(stderr, "  base %s: %s\n", WizardBaseFile(i), D.Error());
    }

    // A mouse layout's zones come back as zones.
    {
        WizardDraft D;
        CHECK(D.LoadBase("Config/mouse/super_mario_64_usa.yaml"));
        CHECK(D.Explicit(N64Control::Stick));
        CHECK(D.Bindings(N64Control::Stick)[0].kind == Binding::Kind::Pointer);
        CHECK(Has(D.Emit("x"), "{stick: pointer}"));
    }

    // A base that does not exist, or that the reader rejects, leaves the draft alone and
    // reports why.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_X);
        CHECK(!D.LoadBase("/tmp/pj64-wizard-no-such-file.yaml"));
        CHECK(strcmp(D.Error(), "") != 0);
        CHECK(D.Explicit(N64Control::A));                       // untouched
        CHECK(D.Bindings(N64Control::A)[0].code == SDL_SCANCODE_X);
    }

    // The head-direction rule belongs to the reader, and the wizard surfaces its words.
    {
        WizardDraft D;
        CHECK(D.LoadBase("Config/face/super_mario_64_usa.yaml"));
        D.SetGesture(N64Control::DPadUp, POINTER_GESTURE_HEAD_UP);
        CHECK(!D.Validate("x"));
        CHECK(strstr(D.Error(), "head-up cannot be bound while Stick is head") != NULL);
    }
```

The `Emit` of a `Pointer` and a `HeadStick` binding arrives in Task 3; until then `ValueText` returns `{}` for them, so the two shipped-mouse and shipped-face assertions above fail. That is the intended RED.

- [ ] **Step 2: Run the test to see it fail**

Run: `make wizard-draft-test`
Expected: FAIL — `WizardBaseCount` is not declared.

- [ ] **Step 3: Declare the base list and the loader**

In `Source/Project64-wizard/WizardDraft.h`, after `WizardControlName`, add:

```cpp
// The shipped layouts offered as starting points, in the order the base screen lists them.
int WizardBaseCount();
const char * WizardBaseLabel(int Index);   // "Mouse: Super Mario 64"; "" out of range
const char * WizardBaseFile(int Index);    // "Config/mouse/super_mario_64_usa.yaml"; "" out of range
```

and inside the class, after `LoadDefaults()`:

```cpp
    // Replaces the draft with Path's bindings; the controls Path names become explicit.
    // False leaves the draft untouched and puts the reason in Error().
    bool LoadBase(const char * Path);
```

- [ ] **Step 4: Implement them**

In `Source/Project64-wizard/WizardDraft.cpp`, add `#include <yaml-cpp/yaml.h>` to the includes, and after `WizardControlName` add:

```cpp
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
```

and after `LoadDefaults`:

```cpp
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
```

`LoadCapturingStderr` is defined below `Emit` in Task 1's file; move its definition above `LoadBase`, or add a forward declaration beside the includes:

```cpp
static bool LoadCapturingStderr(const char * Path, std::string * Message);
```

- [ ] **Step 5: Run the test**

Run: `make wizard-draft-test`
Expected: the base assertions pass; the two `{stick: pointer}` / `{stick: head}` assertions still fail, because `ValueText` has no `Stick`, `Pointer`, `Keys` or `HeadStick` case yet. Task 3 closes them.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-wizard/WizardDraft.h Source/Project64-wizard/WizardDraft.cpp \
        Source/Project64-wizard/WizardDraftTest.cpp
git commit -m "$(cat <<'EOF'
Start a draft from the built-in bindings or a shipped layout

Only the controls a base file names become explicit, so a control that
inherits a pair of built-in inputs keeps them by staying out of the file. The
reader cannot report that set, so the base is read twice: once for values and
once for its keys.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: The six Stick forms and the human-readable description

**Files:**
- Modify: `Source/Project64-wizard/WizardDraft.h`, `Source/Project64-wizard/WizardDraft.cpp`
- Test: `Source/Project64-wizard/WizardDraftTest.cpp`

**Interfaces:**
- Consumes: Task 1's `Replace`, `ValueText`; Task 2's `LoadBase`.
- Produces: `SetStickWhole(bool Right)`, `SetStickPointer()`, `SetStickHead(bool Digital)`, `SetStickKeys(SDL_Scancode, SDL_Scancode, SDL_Scancode, SDL_Scancode)`, `std::string Describe(N64Control) const`, `int WizardStickFormCount()`, `const char * WizardStickFormLabel(int)`.

- [ ] **Step 1: Write the failing tests**

Add to `Source/Project64-wizard/WizardDraftTest.cpp`, before the `Failures != 0` check:

```cpp
    // Each Stick form emits its own syntax and survives the reader.
    {
        WizardDraft D;
        D.SetStickWhole(false);
        CHECK(Has(D.Emit("x"), "Stick:     {stick: left}"));
        CHECK(D.Validate("x"));
        D.SetStickWhole(true);
        CHECK(Has(D.Emit("x"), "Stick:     {stick: right}"));
        CHECK(D.Validate("x"));
        D.SetStickPointer();
        CHECK(Has(D.Emit("x"), "Stick:     {stick: pointer}"));
        CHECK(D.Validate("x"));
        D.SetStickHead(false);
        CHECK(Has(D.Emit("x"), "Stick:     {stick: head}"));
        CHECK(D.Validate("x"));
        D.SetStickHead(true);
        CHECK(Has(D.Emit("x"), "Stick:     {stick: head-digital}"));
        CHECK(D.Validate("x"));
        D.SetStickKeys(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
        CHECK(Has(D.Emit("x"), "Stick:     {keys: {up: Up, down: Down, left: Left, right: Right}}"));
        CHECK(D.Validate("x"));
    }

    // A head stick beside a head-direction gesture is the reader's to reject, and the
    // wizard reports the reader's line.
    {
        WizardDraft D;
        D.SetStickHead(true);
        D.SetGesture(N64Control::A, POINTER_GESTURE_HEAD_LEFT);
        CHECK(!D.Validate("x"));
        CHECK(strstr(D.Error(), "head-left cannot be bound while Stick is head") != NULL);
    }

    // Descriptions read as English, and an inherited control says so.
    {
        WizardDraft D;
        D.SetKey(N64Control::A, SDL_SCANCODE_X);
        CHECK(D.Describe(N64Control::A) == "key X");
        D.SetButton(N64Control::B, SDL_GAMEPAD_BUTTON_SOUTH);
        CHECK(D.Describe(N64Control::B) == "button a");
        D.SetAxis(N64Control::Z, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true);
        CHECK(D.Describe(N64Control::Z) == "axis lefttrigger +");
        D.SetZone(N64Control::Start, 8);
        CHECK(D.Describe(N64Control::Start) == "zone mid1");
        D.SetGesture(N64Control::L, POINTER_GESTURE_MOUTH_OPEN);
        CHECK(D.Describe(N64Control::L) == "gesture mouth-open (Mo)");
        D.SetStickHead(true);
        CHECK(D.Describe(N64Control::Stick) == "stick head-digital");
        D.SetStickKeys(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
        CHECK(D.Describe(N64Control::Stick) == "keys Up/Down/Left/Right");
        D.Clear(N64Control::A);
        CHECK(D.Describe(N64Control::A).compare(0, 11, "inherited: ") == 0);
    }

    // The Stick chooser's six rows.
    CHECK(WizardStickFormCount() == 6);
    CHECK(strcmp(WizardStickFormLabel(0), "gamepad left stick") == 0);
    CHECK(strcmp(WizardStickFormLabel(5), "four keyboard keys") == 0);
    CHECK(strcmp(WizardStickFormLabel(6), "") == 0);
```

- [ ] **Step 2: Run the test to see it fail**

Run: `make wizard-draft-test`
Expected: FAIL — `SetStickWhole` is not a member.

- [ ] **Step 3: Declare the setters, the description and the form list**

In `Source/Project64-wizard/WizardDraft.h`, inside the class after `SetGesture`, add:

```cpp
    // Stick only. Whole: {stick: left} or {stick: right}. Head: {stick: head} or
    // {stick: head-digital}. Keys: four scancodes as a digital stick.
    void SetStickWhole(bool Right);
    void SetStickPointer();
    void SetStickHead(bool Digital);
    void SetStickKeys(SDL_Scancode Up, SDL_Scancode Down, SDL_Scancode Left, SDL_Scancode Right);
```

after `Bindings`:

```cpp
    // What the control is bound to, in English: "key X", "zone mid1",
    // "gesture mouth-open (Mo)", "keys Up/Down/Left/Right". An inherited control reads
    // "inherited: " followed by every built-in input, joined by " or ".
    std::string Describe(N64Control Control) const;
```

and after the free functions at the top of the file:

```cpp
// The six forms the Stick chooser offers, in the order it lists them.
int WizardStickFormCount();
const char * WizardStickFormLabel(int Index);
```

- [ ] **Step 4: Implement them**

In `Source/Project64-wizard/WizardDraft.cpp`, extend `ValueText`'s switch with the four remaining kinds, before `default:`:

```cpp
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
```

Add the form list beside the base list:

```cpp
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
```

Add the setters after `SetGesture`:

```cpp
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
```

and the description, after `Bindings`:

```cpp
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
```

- [ ] **Step 5: Run the test and the suite**

Run: `make wizard-draft-test && make input-config-test && make all`
Expected: `ok: wizard draft` with every assertion from Tasks 1–3 passing, including the two left failing at the end of Task 2; `make all` warning-free.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-wizard/WizardDraft.h Source/Project64-wizard/WizardDraft.cpp \
        Source/Project64-wizard/WizardDraftTest.cpp
git commit -m "$(cat <<'EOF'
Emit the six Stick forms and describe a binding in English

ValueText is the file's voice and DescribeBinding is the screen's, so a
control reads as "gesture mouth-open (Mo)" on screen and {face: mouth-open}
in the file. An inherited control says so, because its built-in pair cannot
be written out.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: The binary, the window and the base screen

**Files:**
- Create: `Source/Project64-wizard/Screens.h`, `Source/Project64-wizard/Screens.cpp`
- Create: `Source/Project64-wizard/main.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: Tasks 1–3's `WizardDraft`, `WizardBaseCount`, `WizardBaseLabel`, `WizardBaseFile`.
- Produces: `enum WizardScreen`, `enum WizardMode`, `struct WizardUi`, `WizardUiInit`, `WizardHandleEvent`, `WizardDrawScreen`, `WizardText`; `make wizard`; `Bin/macOS/Project64-wizard`.

- [ ] **Step 1: Write the screen header**

Create `Source/Project64-wizard/Screens.h`:

```cpp
// Project64 - A Nintendo 64 emulator
// The wizard's screens: one draws, one handles an event. Everything the wizard knows
// about bindings lives in WizardDraft; this file only turns events into calls on it.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once

#include "WizardDraft.h"

#include <SDL3/SDL.h>

enum WizardScreen
{
    WIZARD_BASE = 0,     // pick a starting point
    WIZARD_CONTROL,      // one of the fifteen controls
    WIZARD_REVIEW,       // everything, then where to save
    WIZARD_SAVE,         // the three destinations
};

// Which capture is armed on a control screen. NONE means the screen's own keys are live.
enum WizardMode
{
    WIZARD_MODE_NONE = 0,
    WIZARD_MODE_KEY,
    WIZARD_MODE_BUTTON,
    WIZARD_MODE_AXIS,
    WIZARD_MODE_ZONE,
    WIZARD_MODE_GESTURE,
    WIZARD_MODE_STICK,       // the six-form chooser, Stick only
    WIZARD_MODE_STICK_KEYS,  // capturing up, down, left, right in turn
};

struct WizardUi
{
    WizardScreen Screen;
    int Control;             // 0..14 while Screen is WIZARD_CONTROL
    WizardMode Mode;
    int List;                // highlighted row of whatever list is on screen
    int KeyStep;             // 0..3 while Mode is WIZARD_MODE_STICK_KEYS
    SDL_Scancode Keys[4];    // collected so far in that mode
    char Base[256];          // the base's name, for the emitted header
    char Typed[256];         // the path being typed, when Typing
    bool Typing;
    int SaveChoice;          // 0 default file, 1 beside a ROM, 2 a typed path
    bool ConfirmClobber;     // a second Enter is needed for Config/mouse or Config/face
    char Message[256];       // the line under the screen
    bool WantCamera;         // set the first time gesture mode is entered
    bool Quit;
};

void WizardUiInit(WizardUi * Ui);

// One event. LitGestures is PointerState::Gestures, or 0 with no camera.
void WizardHandleEvent(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft, uint32_t LitGestures);

// Draws the current screen. Gestures and Face are PointerState::Gestures and ::Face.
void WizardDrawScreen(SDL_Renderer * Renderer, int W, int H, const WizardUi & Ui,
                      const WizardDraft & Draft, uint32_t Gestures, uint32_t Face);

// One line of text at window pixels X,Y in the current draw colour. Returns the width the
// line occupied, so a caller can put something after it.
float WizardText(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text);
```

- [ ] **Step 2: Write the screen body for the base screen**

Create `Source/Project64-wizard/Screens.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// See Screens.h. Text is SDL's debug font: eight pixels square, ASCII only, scaled by the
// renderer. It is a utility font for a utility screen, and it spells a scancode name,
// which the emulator's twenty-glyph overlay font cannot.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Screens.h"

#include <Common/PointerLayout.h>
#include <Common/PointerState.h>

#include <stdio.h>
#include <string.h>

// SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE is 8; a scale of 2 reads comfortably at 720x640.
static const int kBody = 2;
static const int kHead = 3;
static const float kLine = 22.0f;

float WizardText(SDL_Renderer * Renderer, float X, float Y, int Scale, const char * Text)
{
    const float S = (float)Scale;
    SDL_SetRenderScale(Renderer, S, S);
    SDL_RenderDebugText(Renderer, X / S, Y / S, Text);
    SDL_SetRenderScale(Renderer, 1.0f, 1.0f);
    return (float)strlen(Text) * (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * S;
}

static void Colour(SDL_Renderer * Renderer, bool Highlight)
{
    if (Highlight) SDL_SetRenderDrawColor(Renderer, 255, 220, 120, 255);
    else SDL_SetRenderDrawColor(Renderer, 220, 220, 220, 255);
}

void WizardUiInit(WizardUi * Ui)
{
    memset(Ui, 0, sizeof(*Ui));
    Ui->Screen = WIZARD_BASE;
    Ui->Mode = WIZARD_MODE_NONE;
    snprintf(Ui->Base, sizeof(Ui->Base), "the built-in bindings");
    snprintf(Ui->Message, sizeof(Ui->Message), "Up and Down to move, Enter to choose.");
}

// Rows of the base screen: the built-in bindings, the shipped layouts, then a typed path.
static int BaseRowCount() { return WizardBaseCount() + 2; }

static const char * BaseRowLabel(int Row)
{
    if (Row == 0) return "The built-in bindings (keyboard and gamepad)";
    if (Row == BaseRowCount() - 1) return "A file I will type the path of";
    return WizardBaseLabel(Row - 1);
}

// Absolute path of a shipped layout: they sit beside the executable, as Config/ does.
static void BasePath(int Row, char * Out, size_t Size)
{
    const char * Dir = SDL_GetBasePath();
    snprintf(Out, Size, "%s%s", Dir != NULL ? Dir : "", WizardBaseFile(Row - 1));
}

static void ChooseBase(WizardUi * Ui, WizardDraft * Draft)
{
    if (Ui->List == 0)
    {
        Draft->LoadDefaults();
        snprintf(Ui->Base, sizeof(Ui->Base), "the built-in bindings");
    }
    else if (Ui->List == BaseRowCount() - 1)
    {
        Ui->Typing = true;
        Ui->Typed[0] = '\0';
        snprintf(Ui->Message, sizeof(Ui->Message), "Type a path, then Enter. Escape cancels.");
        return;
    }
    else
    {
        char Path[512];
        BasePath(Ui->List, Path, sizeof(Path));
        if (!Draft->LoadBase(Path))
        {
            snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
            return;
        }
        snprintf(Ui->Base, sizeof(Ui->Base), "%s", WizardBaseFile(Ui->List - 1));
    }
    Ui->Screen = WIZARD_CONTROL;
    Ui->Control = 0;
    Ui->Mode = WIZARD_MODE_NONE;
    snprintf(Ui->Message, sizeof(Ui->Message), "1-5 to bind, Enter to keep, Delete to inherit.");
}

static void TypedBase(WizardUi * Ui, WizardDraft * Draft)
{
    Ui->Typing = false;
    if (!Draft->LoadBase(Ui->Typed))
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
        return;
    }
    snprintf(Ui->Base, sizeof(Ui->Base), "%s", Ui->Typed);
    Ui->Screen = WIZARD_CONTROL;
    Ui->Control = 0;
    snprintf(Ui->Message, sizeof(Ui->Message), "1-5 to bind, Enter to keep, Delete to inherit.");
}

static void HandleTyping(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type == SDL_EVENT_TEXT_INPUT)
    {
        const size_t Len = strlen(Ui->Typed);
        snprintf(Ui->Typed + Len, sizeof(Ui->Typed) - Len, "%s", Event.text.text);
        return;
    }
    if (Event.type != SDL_EVENT_KEY_DOWN) return;
    if (Event.key.scancode == SDL_SCANCODE_BACKSPACE)
    {
        const size_t Len = strlen(Ui->Typed);
        if (Len > 0) Ui->Typed[Len - 1] = '\0';
    }
    else if (Event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        Ui->Typing = false;
        snprintf(Ui->Message, sizeof(Ui->Message), "Cancelled.");
    }
    else if (Event.key.scancode == SDL_SCANCODE_RETURN)
    {
        if (Ui->Screen == WIZARD_BASE) TypedBase(Ui, Draft);
    }
}

static void HandleBase(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_UP:
        if (Ui->List > 0) Ui->List--;
        break;
    case SDL_SCANCODE_DOWN:
        if (Ui->List < BaseRowCount() - 1) Ui->List++;
        break;
    case SDL_SCANCODE_RETURN:
        ChooseBase(Ui, Draft);
        break;
    case SDL_SCANCODE_ESCAPE:
        Ui->Quit = true;
        break;
    default:
        break;
    }
}

void WizardHandleEvent(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft, uint32_t LitGestures)
{
    (void)LitGestures;
    if (Ui->Typing) { HandleTyping(Event, Ui, Draft); return; }
    if (Ui->Screen == WIZARD_BASE) HandleBase(Event, Ui, Draft);
}

static void DrawBase(SDL_Renderer * Renderer, const WizardUi & Ui)
{
    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, 24.0f, kHead, "Project64 binding wizard");
    WizardText(Renderer, 24.0f, 64.0f, kBody, "Start from:");
    for (int Row = 0; Row < BaseRowCount(); Row++)
    {
        const float Y = 96.0f + kLine * (float)Row;
        Colour(Renderer, Row == Ui.List);
        WizardText(Renderer, 40.0f, Y, kBody, Row == Ui.List ? ">" : " ");
        WizardText(Renderer, 64.0f, Y, kBody, BaseRowLabel(Row));
    }
    if (Ui.Typing)
    {
        Colour(Renderer, true);
        char Line[300];
        snprintf(Line, sizeof(Line), "path: %s_", Ui.Typed);
        WizardText(Renderer, 40.0f, 96.0f + kLine * (float)BaseRowCount() + 16.0f, kBody, Line);
    }
}

void WizardDrawScreen(SDL_Renderer * Renderer, int W, int H, const WizardUi & Ui,
                      const WizardDraft & Draft, uint32_t Gestures, uint32_t Face)
{
    (void)W;
    (void)Draft;
    (void)Gestures;
    (void)Face;
    if (Ui.Screen == WIZARD_BASE) DrawBase(Renderer, Ui);

    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, (float)H - 32.0f, kBody, Ui.Message);
}
```

- [ ] **Step 3: Write the binary**

Create `Source/Project64-wizard/main.cpp`:

```cpp
// Project64 - A Nintendo 64 emulator
// The binding wizard: a window, an event loop and nothing else. It never loads a ROM and
// never starts the camera until a gesture list asks for one.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Screens.h"
#include "WizardDraft.h"

#include <Common/PointerState.h>
#include <Project64-sdl/FaceTracker.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char ** argv)
{
    if (argc >= 2 && strcmp(argv[1], "--version") == 0)
    {
        printf("Project64 binding wizard\n");
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window * Window = SDL_CreateWindow("Project64 binding wizard", 720, 640,
                                           SDL_WINDOW_RESIZABLE);
    if (Window == NULL)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer * Renderer = SDL_CreateRenderer(Window, NULL);
    if (Renderer == NULL)
    {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(Window);
        SDL_Quit();
        return 1;
    }

    PointerState State;
    memset(&State, 0, sizeof(State));

    WizardDraft Draft;
    WizardUi Ui;
    WizardUiInit(&Ui);

    bool CameraStarted = false;
    while (!Ui.Quit)
    {
        SDL_Event Event;
        while (SDL_PollEvent(&Event))
        {
            if (Event.type == SDL_EVENT_QUIT) { Ui.Quit = true; break; }
            WizardHandleEvent(Event, &Ui, &Draft,
                              State.Gestures.load(std::memory_order_relaxed));
        }

        int W = 0, H = 0;
        SDL_GetWindowSize(Window, &W, &H);
        SDL_SetRenderDrawColor(Renderer, 16, 16, 20, 255);
        SDL_RenderClear(Renderer);
        WizardDrawScreen(Renderer, W, H, Ui, Draft,
                         State.Gestures.load(std::memory_order_relaxed),
                         State.Face.load(std::memory_order_relaxed));
        SDL_RenderPresent(Renderer);
        SDL_Delay(16);
    }

    if (CameraStarted) FaceTrackerStop();
    SDL_DestroyRenderer(Renderer);
    SDL_DestroyWindow(Window);
    SDL_Quit();
    return 0;
}
```

`CameraStarted` is written in Task 7, which is where `Ui.WantCamera` is set; leaving the stop call here now means the tracker's lifetime is complete from the first commit.

- [ ] **Step 4: Add the build step**

In `Makefile`, extend `WIZARD_SRC` to the three sources that now exist:

```make
WIZARD_SRC = $(addprefix Project64-wizard/, main.cpp Screens.cpp WizardDraft.cpp)
```

After the Stage 7 frontend link rule, add:

```make
# ── Stage 7c · Binding wizard ─────────────────────────────────────────────────

# A second binary, with no ROM and no OpenGL: SDL_Renderer and SDL's debug font. It shares
# the frontend's InputConfig, FaceGestures and FaceTracker objects rather than its own.
wizard: $(BIN)/Project64-wizard ## [STEP 7c] Build the binding wizard
$(BIN)/Project64-wizard: $(WIZARD_OBJS) $(BUILD)/Project64-sdl/InputConfig.o $(BUILD)/Project64-sdl/FaceGestures.o $(BUILD)/Project64-sdl/FaceTracker.o
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(YAML_LIBS) -framework Foundation -framework AVFoundation -framework Vision -framework CoreMedia -framework CoreVideo -lobjc -lpthread
```

Add `wizard` to the `all:` target's prerequisites, between `frontend` and `config`, and to the `.PHONY` line.

- [ ] **Step 5: Build and run it bounded**

Run: `make wizard && ./Bin/macOS/Project64-wizard --version`
Expected: `Project64 binding wizard`

Run: `perl -e 'alarm 5; exec @ARGV' -- ./Bin/macOS/Project64-wizard || true`
Expected: a window opens listing seven starting points and closes when the alarm fires; nothing on stderr.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-wizard/Screens.h Source/Project64-wizard/Screens.cpp \
        Source/Project64-wizard/main.cpp Makefile
git commit -m "$(cat <<'EOF'
Open the wizard's window on its base screen

SDL_Renderer and SDL's debug font: eight pixels square and ASCII-complete,
which the emulator's twenty-glyph tag font is not. The base screen picks a
starting point and the draft is copied from it.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: The control screens and the three device captures

**Files:**
- Modify: `Source/Project64-wizard/Screens.cpp`

**Interfaces:**
- Consumes: Task 4's `WizardUi`, `WizardHandleEvent`, `WizardDrawScreen`, `WizardText`; Task 3's `Describe`, `WizardStickFormCount`, `WizardStickFormLabel`.
- Produces: the control screen, modes 1–3, and the Stick chooser, all reachable from the base screen.

- [ ] **Step 1: Add the control screen's event handling**

In `Source/Project64-wizard/Screens.cpp`, above `WizardHandleEvent`, add:

```cpp
// The plugin's own gate for "this axis is pushed", so the wizard and the game agree.
static const int kStickThreshold = 16000;

static N64Control CurrentControl(const WizardUi & Ui)
{
    return (N64Control)Ui.Control;
}

static void Advance(WizardUi * Ui)
{
    if (Ui->Control + 1 < (int)N64Control::Count)
    {
        Ui->Control++;
        Ui->Mode = WIZARD_MODE_NONE;
        Ui->List = 0;
    }
    else
    {
        Ui->Screen = WIZARD_REVIEW;
        Ui->Mode = WIZARD_MODE_NONE;
        Ui->List = 0;
        snprintf(Ui->Message, sizeof(Ui->Message), "S to save, Backspace to go back.");
    }
}

static void Bound(WizardUi * Ui, const WizardDraft & Draft)
{
    snprintf(Ui->Message, sizeof(Ui->Message), "%s is %s",
             WizardControlName(CurrentControl(*Ui)),
             Draft.Describe(CurrentControl(*Ui)).c_str());
    Ui->Mode = WIZARD_MODE_NONE;
}

// Mode 1: the next keydown is the binding, whatever it is. There is no cancel, because
// every key is a legal answer — Escape and the arrows included.
static bool CaptureKey(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return false;
    if (Ui->Mode == WIZARD_MODE_STICK_KEYS)
    {
        Ui->Keys[Ui->KeyStep] = Event.key.scancode;
        if (++Ui->KeyStep == 4)
        {
            Draft->SetStickKeys(Ui->Keys[0], Ui->Keys[1], Ui->Keys[2], Ui->Keys[3]);
            Ui->KeyStep = 0;
            Bound(Ui, *Draft);
        }
        return true;
    }
    Draft->SetKey(CurrentControl(*Ui), Event.key.scancode);
    Bound(Ui, *Draft);
    return true;
}

// Modes 2 and 3 do not consume keys, so Escape leaves them.
static bool CapturePad(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type == SDL_EVENT_KEY_DOWN && Event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        Ui->Mode = WIZARD_MODE_NONE;
        snprintf(Ui->Message, sizeof(Ui->Message), "Cancelled.");
        return true;
    }
    if (Ui->Mode == WIZARD_MODE_BUTTON && Event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        Draft->SetButton(CurrentControl(*Ui), (SDL_GamepadButton)Event.gbutton.button);
        Bound(Ui, *Draft);
        return true;
    }
    if (Ui->Mode == WIZARD_MODE_AXIS && Event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
    {
        if (Event.gaxis.value > kStickThreshold || Event.gaxis.value < -kStickThreshold)
        {
            Draft->SetAxis(CurrentControl(*Ui), (SDL_GamepadAxis)Event.gaxis.axis,
                           Event.gaxis.value > 0);
            Bound(Ui, *Draft);
        }
        return true;
    }
    return false;
}

static void ChooseStickForm(WizardUi * Ui, WizardDraft * Draft)
{
    switch (Ui->List)
    {
    case 0: Draft->SetStickWhole(false); break;
    case 1: Draft->SetStickWhole(true); break;
    case 2: Draft->SetStickPointer(); break;
    case 3: Draft->SetStickHead(false); break;
    case 4: Draft->SetStickHead(true); break;
    default:
        Ui->Mode = WIZARD_MODE_STICK_KEYS;
        Ui->KeyStep = 0;
        snprintf(Ui->Message, sizeof(Ui->Message), "Press the key for up.");
        return;
    }
    Bound(Ui, *Draft);
}

static bool CaptureStickForm(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return false;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_UP:
        if (Ui->List > 0) Ui->List--;
        return true;
    case SDL_SCANCODE_DOWN:
        if (Ui->List < WizardStickFormCount() - 1) Ui->List++;
        return true;
    case SDL_SCANCODE_RETURN:
        ChooseStickForm(Ui, Draft);
        return true;
    case SDL_SCANCODE_ESCAPE:
        Ui->Mode = WIZARD_MODE_NONE;
        snprintf(Ui->Message, sizeof(Ui->Message), "Cancelled.");
        return true;
    default:
        return true;
    }
}

static void HandleControl(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    switch (Ui->Mode)
    {
    case WIZARD_MODE_KEY:
    case WIZARD_MODE_STICK_KEYS:
        if (CaptureKey(Event, Ui, Draft)) return;
        break;
    case WIZARD_MODE_BUTTON:
    case WIZARD_MODE_AXIS:
        if (CapturePad(Event, Ui, Draft)) return;
        break;
    case WIZARD_MODE_STICK:
        if (CaptureStickForm(Event, Ui, Draft)) return;
        break;
    default:
        break;
    }
    if (Ui->Mode != WIZARD_MODE_NONE || Event.type != SDL_EVENT_KEY_DOWN) return;

    const bool IsStick = CurrentControl(*Ui) == N64Control::Stick;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_1:
        if (IsStick) { Ui->Mode = WIZARD_MODE_STICK; Ui->List = 0;
                       snprintf(Ui->Message, sizeof(Ui->Message), "Pick a form, Enter to take it."); }
        else { Ui->Mode = WIZARD_MODE_KEY;
               snprintf(Ui->Message, sizeof(Ui->Message), "Press any key. It is taken as it comes."); }
        break;
    case SDL_SCANCODE_2:
        if (IsStick) break;
        Ui->Mode = WIZARD_MODE_BUTTON;
        snprintf(Ui->Message, sizeof(Ui->Message), "Press a gamepad button. Escape cancels.");
        break;
    case SDL_SCANCODE_3:
        if (IsStick) break;
        Ui->Mode = WIZARD_MODE_AXIS;
        snprintf(Ui->Message, sizeof(Ui->Message), "Push a stick or trigger. Escape cancels.");
        break;
    case SDL_SCANCODE_RETURN:
        Advance(Ui);
        break;
    case SDL_SCANCODE_BACKSPACE:
        if (Ui->Control > 0) Ui->Control--;
        else Ui->Screen = WIZARD_BASE;
        Ui->Mode = WIZARD_MODE_NONE;
        break;
    case SDL_SCANCODE_DELETE:
        Draft->Clear(CurrentControl(*Ui));
        Bound(Ui, *Draft);
        break;
    case SDL_SCANCODE_ESCAPE:
        Ui->Screen = WIZARD_REVIEW;
        Ui->List = 0;
        snprintf(Ui->Message, sizeof(Ui->Message), "S to save, Backspace to go back.");
        break;
    default:
        break;
    }
}
```

In `WizardHandleEvent`, add the control screen:

```cpp
    if (Ui->Screen == WIZARD_BASE) HandleBase(Event, Ui, Draft);
    else if (Ui->Screen == WIZARD_CONTROL) HandleControl(Event, Ui, Draft);
```

- [ ] **Step 2: Draw the control screen**

Add above `WizardDrawScreen`:

```cpp
static void DrawControl(SDL_Renderer * Renderer, const WizardUi & Ui, const WizardDraft & Draft)
{
    const N64Control C = (N64Control)Ui.Control;
    char Line[320];

    Colour(Renderer, false);
    snprintf(Line, sizeof(Line), "Control %d of %d", Ui.Control + 1, (int)N64Control::Count);
    WizardText(Renderer, 24.0f, 24.0f, kBody, Line);

    Colour(Renderer, true);
    WizardText(Renderer, 24.0f, 56.0f, kHead, WizardControlName(C));

    Colour(Renderer, false);
    snprintf(Line, sizeof(Line), "now: %s", Draft.Describe(C).c_str());
    WizardText(Renderer, 24.0f, 96.0f, kBody, Line);

    if (C == N64Control::Stick && Ui.Mode == WIZARD_MODE_STICK)
    {
        for (int Row = 0; Row < WizardStickFormCount(); Row++)
        {
            Colour(Renderer, Row == Ui.List);
            WizardText(Renderer, 40.0f, 140.0f + kLine * (float)Row, kBody,
                       Row == Ui.List ? ">" : " ");
            WizardText(Renderer, 64.0f, 140.0f + kLine * (float)Row, kBody,
                       WizardStickFormLabel(Row));
        }
        return;
    }

    Colour(Renderer, false);
    if (C == N64Control::Stick)
    {
        WizardText(Renderer, 24.0f, 140.0f, kBody, "1  choose how the stick is driven");
    }
    else
    {
        WizardText(Renderer, 24.0f, 140.0f, kBody, "1  a keyboard key");
        WizardText(Renderer, 24.0f, 162.0f, kBody, "2  a gamepad button");
        WizardText(Renderer, 24.0f, 184.0f, kBody, "3  a gamepad axis");
        WizardText(Renderer, 24.0f, 206.0f, kBody, "4  a panel slot");
        WizardText(Renderer, 24.0f, 228.0f, kBody, "5  a face gesture");
    }
    WizardText(Renderer, 24.0f, 268.0f, kBody, "Enter keep   Backspace back   Delete inherit   Esc review");
}
```

and dispatch it in `WizardDrawScreen`:

```cpp
    if (Ui.Screen == WIZARD_BASE) DrawBase(Renderer, Ui);
    else if (Ui.Screen == WIZARD_CONTROL) DrawControl(Renderer, Ui, Draft);
```

- [ ] **Step 3: Build and walk it bounded**

Run: `make wizard && perl -e 'alarm 5; exec @ARGV' -- ./Bin/macOS/Project64-wizard || true`
Expected: the base screen appears; nothing on stderr. The interaction itself is proven by Task 9's self-test, which drives these same handlers with synthetic events.

- [ ] **Step 4: Commit**

```bash
git add Source/Project64-wizard/Screens.cpp
git commit -m "$(cat <<'EOF'
Walk the fifteen controls and capture keys, buttons and axes

Numbered modes rather than a bare "press what you want", so that Escape and
the arrows stay bindable: inside mode 1 the next key is the answer whatever
it is, and the modes that ignore keys are the ones Escape can leave.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Binding a panel slot

**Files:**
- Modify: `Source/Project64-wizard/Screens.cpp`

**Interfaces:**
- Consumes: `PointerZoneRect`, `PointerZoneName`, `POINTER_ZONE_GAME` from `Source/Common/PointerLayout.h`; Task 5's mode dispatch.
- Produces: `WIZARD_MODE_ZONE` reachable with `4`, binding by click.

The panel is drawn at the real geometry for a 640x640 window and then scaled into place, so what the player clicks is what the game will show.

- [ ] **Step 1: Draw the panel and take a click**

In `Source/Project64-wizard/Screens.cpp`, add this block **above `HandleControl`** — it
holds both the drawing helpers and `CaptureZone`, and `HandleControl` calls the latter, so
placing it lower would not compile. `DrawControl` sits further down the file and can still
see all of it.

```cpp
// The panel as the game lays it out: a 640x640 window, the game image on top, the
// thirteen slots below. The wizard draws that rectangle scaled into its own window, so a
// slot is exactly where the player will find it in the game.
static const int kPanelW = 640;
static const int kPanelH = 640;
static const float kPanelScale = 0.5f;
static const float kPanelX = 24.0f;
static const float kPanelY = 300.0f;

static SDL_FRect ZoneScreenRect(int Zone)
{
    float X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
    PointerZoneRect(Zone, kPanelW, kPanelH, &X0, &Y0, &X1, &Y1);
    SDL_FRect R;
    R.x = kPanelX + X0 * kPanelScale;
    R.y = kPanelY + Y0 * kPanelScale;
    R.w = (X1 - X0) * kPanelScale;
    R.h = (Y1 - Y0) * kPanelScale;
    return R;
}

// The zone under a click in the wizard's window, or POINTER_ZONE_NONE.
static int ZoneAtPoint(float X, float Y)
{
    for (int Zone = 0; Zone < POINTER_ZONE_COUNT; Zone++)
    {
        const SDL_FRect R = ZoneScreenRect(Zone);
        if (X >= R.x && X < R.x + R.w && Y >= R.y && Y < R.y + R.h) return Zone;
    }
    return POINTER_ZONE_NONE;
}

static void DrawPanel(SDL_Renderer * Renderer, const WizardDraft & Draft)
{
    // The game image first, so the slots sit on top of it.
    for (int Zone = POINTER_ZONE_COUNT - 1; Zone >= 0; Zone--)
    {
        const SDL_FRect R = ZoneScreenRect(Zone);
        SDL_SetRenderDrawColor(Renderer, Zone == POINTER_ZONE_GAME ? 28 : 44,
                               Zone == POINTER_ZONE_GAME ? 28 : 44,
                               Zone == POINTER_ZONE_GAME ? 34 : 52, 255);
        SDL_RenderFillRect(Renderer, &R);

        // Whatever the draft already puts in this slot, so the choice is made in context.
        for (int i = 0; i < (int)N64Control::Count; i++)
        {
            const std::vector<Binding> & B = Draft.Bindings((N64Control)i);
            if (B.empty() || B[0].kind != Binding::Kind::Zone || B[0].code != Zone) continue;
            Colour(Renderer, false);
            WizardText(Renderer, R.x + 4.0f, R.y + 4.0f, 1, WizardControlName((N64Control)i));
            break;
        }
    }
}

static bool CaptureZone(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type == SDL_EVENT_KEY_DOWN && Event.key.scancode == SDL_SCANCODE_ESCAPE)
    {
        Ui->Mode = WIZARD_MODE_NONE;
        snprintf(Ui->Message, sizeof(Ui->Message), "Cancelled.");
        return true;
    }
    if (Event.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return false;
    const int Zone = ZoneAtPoint(Event.button.x, Event.button.y);
    if (Zone == POINTER_ZONE_NONE)
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "That is a gap. Click a slot or the game image.");
        return true;
    }
    Draft->SetZone(CurrentControl(*Ui), Zone);
    Bound(Ui, *Draft);
    return true;
}
```

- [ ] **Step 2: Wire the mode in**

In `HandleControl`'s mode switch, add before `default:`:

```cpp
    case WIZARD_MODE_ZONE:
        if (CaptureZone(Event, Ui, Draft)) return;
        break;
```

and in the numbered keys, replace the `4` case (there is none yet — add it after the `3` case):

```cpp
    case SDL_SCANCODE_4:
        if (IsStick) break;
        Ui->Mode = WIZARD_MODE_ZONE;
        snprintf(Ui->Message, sizeof(Ui->Message), "Click a slot or the game image. Escape cancels.");
        break;
```

In `DrawControl`, draw the panel while the mode is armed, just before the final help line:

```cpp
    if (Ui.Mode == WIZARD_MODE_ZONE) DrawPanel(Renderer, Draft);
```

- [ ] **Step 3: Build and check the geometry by arithmetic**

Run: `make wizard`
Expected: warning-free.

Check by hand and record the numbers in the commit message: at `kPanelScale` 0.5 the panel rectangle is 320x320 at (24, 300), so its bottom edge is 620 and its right edge 344 — inside a 720x640 window. `mid1`'s rect at 640x640 is x 164..220, y 488..536, which lands at x 106..134, y 544..568 on screen.

- [ ] **Step 4: Commit**

```bash
git add Source/Project64-wizard/Screens.cpp
git commit -m "$(cat <<'EOF'
Bind a panel slot by clicking the real geometry

The panel is drawn from PointerZoneRect at the game's own 640x640 layout and
scaled into the wizard's window, so a slot is where the player will find it,
and each slot shows what the draft already puts there.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: The live-lit gesture list and the lazy camera

**Files:**
- Modify: `Source/Project64-wizard/Screens.cpp`, `Source/Project64-wizard/main.cpp`
- Modify: `Source/Project64-sdl/FaceTracker.h`

**Interfaces:**
- Consumes: `PointerGestureName`, `PointerGestureTag`, `POINTER_GESTURE_COUNT`, `FaceStatus` from `Source/Common/PointerState.h`; `FaceTrackerStart`, `FaceTrackerStop`.
- Produces: `WIZARD_MODE_GESTURE` reachable with `5`; `WizardUi::WantCamera` honoured by `main.cpp`.

- [ ] **Step 1: Add the gesture list**

In `Source/Project64-wizard/Screens.cpp`, add this block **above `HandleControl`**, which
calls `CaptureGesture`; `DrawGestures` is used further down by `DrawControl` and is happy
either way.

```cpp
static bool CaptureGesture(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft,
                           uint32_t LitGestures)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return false;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_UP:
        if (Ui->List > 0) Ui->List--;
        return true;
    case SDL_SCANCODE_DOWN:
        if (Ui->List < POINTER_GESTURE_COUNT - 1) Ui->List++;
        return true;
    case SDL_SCANCODE_RETURN:
        Draft->SetGesture(CurrentControl(*Ui), 1u << Ui->List);
        Bound(Ui, *Draft);
        return true;
    case SDL_SCANCODE_SPACE:
    {
        // Only when exactly one gesture is firing: two at once is ambiguous, and taking
        // the lower bit would silently pick for the player.
        int Lit = -1, Count = 0;
        for (int i = 0; i < POINTER_GESTURE_COUNT; i++)
        {
            if ((LitGestures & (1u << i)) != 0) { Lit = i; Count++; }
        }
        if (Count == 1)
        {
            Draft->SetGesture(CurrentControl(*Ui), 1u << Lit);
            Bound(Ui, *Draft);
        }
        else
        {
            snprintf(Ui->Message, sizeof(Ui->Message),
                     Count == 0 ? "Nothing is firing. Hold the expression, or pick a row."
                                : "More than one is firing. Pick a row with Enter.");
        }
        return true;
    }
    case SDL_SCANCODE_ESCAPE:
        Ui->Mode = WIZARD_MODE_NONE;
        snprintf(Ui->Message, sizeof(Ui->Message), "Cancelled.");
        return true;
    default:
        return true;
    }
}

static const char * FaceStatusText(uint32_t Face)
{
    switch (Face)
    {
    case FACE_OFF: return "camera off: the list still works, unlit";
    case FACE_STARTING: return "camera starting";
    case FACE_TRACKING: return "tracking";
    case FACE_NO_FACE: return "no face found";
    case FACE_DENIED: return "camera denied in System Settings > Privacy & Security";
    default: return "camera unavailable";
    }
}

static void DrawGestures(SDL_Renderer * Renderer, const WizardUi & Ui, uint32_t Gestures,
                         uint32_t Face)
{
    for (int Row = 0; Row < POINTER_GESTURE_COUNT; Row++)
    {
        const float Y = 140.0f + kLine * (float)Row;
        const bool Firing = (Gestures & (1u << Row)) != 0;
        Colour(Renderer, Row == Ui.List || Firing);
        WizardText(Renderer, 40.0f, Y, kBody, Row == Ui.List ? ">" : " ");
        WizardText(Renderer, 64.0f, Y, kBody, PointerGestureTag(Row));
        WizardText(Renderer, 112.0f, Y, kBody, PointerGestureName(Row));
        if (Firing) WizardText(Renderer, 320.0f, Y, kBody, "<- now");
    }
    Colour(Renderer, false);
    WizardText(Renderer, 40.0f, 140.0f + kLine * (float)POINTER_GESTURE_COUNT + 12.0f, kBody,
               FaceStatusText(Face));
}
```

- [ ] **Step 2: Wire the mode in**

`CaptureGesture` needs the lit bits, which `WizardHandleEvent` already receives. Give `HandleControl` the same parameter — change its signature to
`static void HandleControl(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft, uint32_t LitGestures)`,
pass `LitGestures` at its call site in `WizardHandleEvent`, and drop the `(void)LitGestures;` line there.

In `HandleControl`'s mode switch add:

```cpp
    case WIZARD_MODE_GESTURE:
        if (CaptureGesture(Event, Ui, Draft, LitGestures)) return;
        break;
```

and in the numbered keys, after the `4` case:

```cpp
    case SDL_SCANCODE_5:
        if (IsStick) break;
        Ui->Mode = WIZARD_MODE_GESTURE;
        Ui->List = 0;
        Ui->WantCamera = true;
        snprintf(Ui->Message, sizeof(Ui->Message),
                 "Arrows and Enter, or Space for the one that is firing.");
        break;
```

In `DrawControl`, beside the panel line:

```cpp
    if (Ui.Mode == WIZARD_MODE_GESTURE) DrawGestures(Renderer, Ui, Gestures, Face);
```

`DrawControl` therefore needs the two words: change its signature to
`static void DrawControl(SDL_Renderer * Renderer, const WizardUi & Ui, const WizardDraft & Draft, uint32_t Gestures, uint32_t Face)`
and pass them from `WizardDrawScreen`.

- [ ] **Step 3: Start the camera only when asked**

In `Source/Project64-wizard/main.cpp`, inside the loop after the event drain, add:

```cpp
        // The camera starts the first time a gesture list asks for one, and never before:
        // opening the wizard is not consent to be filmed. PJ64_FACE=0 keeps it shut.
        if (Ui.WantCamera && !CameraStarted)
        {
            const char * Off = getenv("PJ64_FACE");
            if (Off != NULL && strcmp(Off, "0") == 0)
            {
                State.Face.store(FACE_OFF, std::memory_order_relaxed);
                Ui.WantCamera = false;
            }
            else
            {
                CameraStarted = FaceTrackerStart(&State);
                Ui.WantCamera = false;
            }
        }
```

and add `#include <stdlib.h>` to its includes.

- [ ] **Step 4: Fix the stale tracker comment**

In `Source/Project64-sdl/FaceTracker.h`, replace `the plugin reads the three bits this writes into PointerState` with `the plugin reads the eleven gesture bits and the head stick this writes into PointerState`.

- [ ] **Step 5: Build and prove the camera stays shut**

Run: `make wizard && PJ64_FACE=0 perl -e 'alarm 5; exec @ARGV' -- ./Bin/macOS/Project64-wizard || true`
Expected: the window opens and closes; no camera indicator; nothing on stderr. Do not run it without `PJ64_FACE=0` — a manual run with the camera is the human partner's, in Task 10.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-wizard/Screens.cpp Source/Project64-wizard/main.cpp \
        Source/Project64-sdl/FaceTracker.h
git commit -m "$(cat <<'EOF'
List the eleven gestures, lit by what the face is doing

The camera starts the first time the list is opened and never before, so
launching the wizard is not consent to be filmed, and PJ64_FACE=0 keeps it
shut with the list still pickable. Space takes the firing gesture only when
exactly one is firing.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: Review and save

**Files:**
- Modify: `Source/Project64-wizard/Screens.cpp`

**Interfaces:**
- Consumes: Task 1's `Emit`, `Validate`, `Save`, `Error`; Task 3's `Describe`.
- Produces: the review and save screens; the three destinations and the clobber warning.

- [ ] **Step 1: Add the review screen**

In `Source/Project64-wizard/Screens.cpp`, above `WizardHandleEvent`, add:

```cpp
// Arriving at the review runs the same round-trip that saving will, so a mapping the
// reader refuses says so here — in the reader's words — rather than at the last step.
static void EnterReview(WizardUi * Ui, WizardDraft * Draft)
{
    Ui->Screen = WIZARD_REVIEW;
    Ui->Mode = WIZARD_MODE_NONE;
    Ui->List = 0;
    if (Draft->Validate(Ui->Base))
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "S to save, Backspace to go back.");
    }
    else
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
    }
}

static void HandleReview(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Event.type != SDL_EVENT_KEY_DOWN) return;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_S:
        Ui->Screen = WIZARD_SAVE;
        Ui->SaveChoice = 0;
        Ui->ConfirmClobber = false;
        snprintf(Ui->Message, sizeof(Ui->Message), "1, 2 or 3, then Enter.");
        break;
    case SDL_SCANCODE_BACKSPACE:
        Ui->Screen = WIZARD_CONTROL;
        Ui->Control = (int)N64Control::Count - 1;
        Ui->Mode = WIZARD_MODE_NONE;
        break;
    case SDL_SCANCODE_ESCAPE:
        Ui->Quit = true;
        break;
    default:
        break;
    }
    (void)Draft;
}

// Two explicit controls on the same input. The reader allows it — the N64 can have two
// buttons on one key — so this warns and never blocks.
static bool SharesInput(const WizardDraft & Draft, int Index)
{
    if (!Draft.Explicit((N64Control)Index)) return false;
    const std::vector<Binding> & Mine = Draft.Bindings((N64Control)Index);
    if (Mine.empty()) return false;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        if (i == Index || !Draft.Explicit((N64Control)i)) continue;
        const std::vector<Binding> & Other = Draft.Bindings((N64Control)i);
        if (Other.empty()) continue;
        if (Other[0].kind == Mine[0].kind && Other[0].code == Mine[0].code &&
            Other[0].positive == Mine[0].positive)
        {
            return true;
        }
    }
    return false;
}

static void DrawReview(SDL_Renderer * Renderer, const WizardUi & Ui, const WizardDraft & Draft)
{
    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, 24.0f, kHead, "Review");
    char Line[320];
    int Shared = 0;
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        const N64Control C = (N64Control)i;
        const bool Twice = SharesInput(Draft, i);
        if (Twice) Shared++;
        Colour(Renderer, Draft.Explicit(C));
        snprintf(Line, sizeof(Line), "%-10s %s%s", WizardControlName(C),
                 Draft.Describe(C).c_str(), Twice ? "   (also bound elsewhere)" : "");
        WizardText(Renderer, 24.0f, 64.0f + kLine * (float)i, kBody, Line);
    }
    if (Shared > 0)
    {
        Colour(Renderer, true);
        snprintf(Line, sizeof(Line),
                 "%d controls share an input. That is allowed; press S to save anyway.", Shared);
        WizardText(Renderer, 24.0f, 64.0f + kLine * (float)N64Control::Count + 12.0f, kBody, Line);
    }
    (void)Ui;
}
```

- [ ] **Step 2: Add the save screen**

Add below it:

```cpp
// Where the three destinations put the file.
static void SavePath(const WizardUi & Ui, char * Out, size_t Size)
{
    if (Ui.SaveChoice == 0)
    {
        const char * Dir = SDL_GetBasePath();
        snprintf(Out, Size, "%sConfig/input.yaml", Dir != NULL ? Dir : "");
        return;
    }
    if (Ui.SaveChoice == 1)
    {
        // Beside the ROM, named after it: the per-ROM lookup finds it with no input=.
        snprintf(Out, Size, "%s", Ui.Typed);
        char * Dot = strrchr(Out, '.');
        char * Slash = strrchr(Out, '/');
        if (Dot != NULL && (Slash == NULL || Dot > Slash)) *Dot = '\0';
        const size_t Len = strlen(Out);
        snprintf(Out + Len, Size - Len, ".yaml");
        return;
    }
    snprintf(Out, Size, "%s", Ui.Typed);
}

// make deletes and recopies both of these on every build, so a file saved there is gone
// after the next one.
static bool IsClobbered(const char * Path)
{
    return strstr(Path, "/Config/mouse/") != NULL || strstr(Path, "/Config/face/") != NULL;
}

static void DoSave(WizardUi * Ui, WizardDraft * Draft)
{
    char Path[512];
    SavePath(*Ui, Path, sizeof(Path));
    if (Path[0] == '\0')
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "Type a path first.");
        return;
    }
    if (IsClobbered(Path) && !Ui->ConfirmClobber)
    {
        Ui->ConfirmClobber = true;
        snprintf(Ui->Message, sizeof(Ui->Message),
                 "make deletes that directory on every build. Enter again to save anyway.");
        return;
    }
    if (!Draft->Save(Path, Ui->Base))
    {
        snprintf(Ui->Message, sizeof(Ui->Message), "%s", Draft->Error());
        return;
    }
    snprintf(Ui->Message, sizeof(Ui->Message), "Saved %s. Escape to quit.", Path);
}

static void HandleSave(const SDL_Event & Event, WizardUi * Ui, WizardDraft * Draft)
{
    if (Ui->Typing) { HandleTyping(Event, Ui, Draft); return; }
    if (Event.type != SDL_EVENT_KEY_DOWN) return;
    switch (Event.key.scancode)
    {
    case SDL_SCANCODE_1:
        Ui->SaveChoice = 0;
        Ui->ConfirmClobber = false;
        break;
    case SDL_SCANCODE_2:
    case SDL_SCANCODE_3:
        Ui->SaveChoice = Event.key.scancode == SDL_SCANCODE_2 ? 1 : 2;
        Ui->ConfirmClobber = false;
        Ui->Typing = true;
        Ui->Typed[0] = '\0';
        snprintf(Ui->Message, sizeof(Ui->Message),
                 Ui->SaveChoice == 1 ? "Type the ROM's path, then Enter."
                                     : "Type where to save, then Enter.");
        break;
    case SDL_SCANCODE_RETURN:
        DoSave(Ui, Draft);
        break;
    case SDL_SCANCODE_BACKSPACE:
        Ui->Screen = WIZARD_REVIEW;
        break;
    case SDL_SCANCODE_ESCAPE:
        Ui->Quit = true;
        break;
    default:
        break;
    }
}

static void DrawSave(SDL_Renderer * Renderer, const WizardUi & Ui, const WizardDraft & Draft)
{
    Colour(Renderer, false);
    WizardText(Renderer, 24.0f, 24.0f, kHead, "Save");
    const char * const kRows[3] = {
        "1  the default mapping, Config/input.yaml",
        "2  beside a ROM, so it loads for that game",
        "3  a path I will type",
    };
    for (int Row = 0; Row < 3; Row++)
    {
        Colour(Renderer, Row == Ui.SaveChoice);
        WizardText(Renderer, 24.0f, 72.0f + kLine * (float)Row, kBody, kRows[Row]);
    }
    char Path[512];
    SavePath(Ui, Path, sizeof(Path));
    Colour(Renderer, false);
    char Line[600];
    snprintf(Line, sizeof(Line), "to: %s", Path);
    WizardText(Renderer, 24.0f, 160.0f, kBody, Line);
    if (Ui.Typing)
    {
        Colour(Renderer, true);
        snprintf(Line, sizeof(Line), "path: %s_", Ui.Typed);
        WizardText(Renderer, 24.0f, 188.0f, kBody, Line);
    }
    (void)Draft;
}
```

`HandleTyping` currently finishes a typed path only on the base screen; extend its `SDL_SCANCODE_RETURN` case:

```cpp
    else if (Event.key.scancode == SDL_SCANCODE_RETURN)
    {
        if (Ui->Screen == WIZARD_BASE) TypedBase(Ui, Draft);
        else { Ui->Typing = false; snprintf(Ui->Message, sizeof(Ui->Message), "Enter again to save."); }
    }
```

- [ ] **Step 3: Route the two ways into the review through `EnterReview`**

Task 5 left two places that set `WIZARD_REVIEW` by hand: the tail of `Advance` (after the
last control) and `HandleControl`'s `SDL_SCANCODE_ESCAPE` case. Replace each of those
bodies with a single `EnterReview(Ui, Draft);` call, so both paths validate. `Advance`
therefore needs the draft: change its signature to
`static void Advance(WizardUi * Ui, WizardDraft * Draft)` and pass it at its one call site.

- [ ] **Step 4: Dispatch both screens**

In `WizardHandleEvent`:

```cpp
    else if (Ui->Screen == WIZARD_REVIEW) HandleReview(Event, Ui, Draft);
    else if (Ui->Screen == WIZARD_SAVE) HandleSave(Event, Ui, Draft);
```

In `WizardDrawScreen`:

```cpp
    else if (Ui.Screen == WIZARD_REVIEW) DrawReview(Renderer, Ui, Draft);
    else if (Ui.Screen == WIZARD_SAVE) DrawSave(Renderer, Ui, Draft);
```

- [ ] **Step 5: Build and check**

Run: `make wizard && make wizard-draft-test && make all`
Expected: warning-free; `ok: wizard draft`.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-wizard/Screens.cpp
git commit -m "$(cat <<'EOF'
Review the mapping and save it where it will survive

Three destinations, with Config/mouse and Config/face named as the trap they
are: make deletes and recopies both on every build, so saving there takes a
second Enter. A rejected draft reports the reader's own line.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: The end-to-end self-test

**Files:**
- Modify: `Source/Project64-wizard/main.cpp`
- Create: `Scripts/wizard_selftest.sh`
- Modify: `Makefile`

**Interfaces:**
- Consumes: Tasks 4–8's `WizardUiInit`, `WizardHandleEvent`, `WizardDraft::Save`.
- Produces: `--selftest <path>`; `make wizard-selftest`.

The self-test drives the real handlers with synthetic events and never opens a window, a renderer or a camera, so it runs anywhere and proves the screens agree with the draft.

- [ ] **Step 1: Write the failing script**

Create `Scripts/wizard_selftest.sh` (mode 755, LF):

```sh
#!/bin/sh
# Drives the binding wizard's screens with synthetic events and checks the YAML it writes.
# No window, no ROM and no camera: --selftest never starts the tracker.
set -eu

BIN=Bin/macOS/Project64-wizard
OUT=$(mktemp /tmp/pj64-wizard-selftest-XXXXXX)
trap 'rm -f "$OUT" "$OUT.expected" "$OUT.got"' EXIT

[ -x "$BIN" ] || { echo "not built: $BIN" >&2; exit 1; }

PJ64_FACE=0 "$BIN" --selftest "$OUT" || { echo "selftest exited non-zero" >&2; exit 1; }

cat > "$OUT.expected" <<'YAML'
bindings:
  A:         {key: X}
  B:         {button: a}
  Z:         {zone: mid1}
  Start:     {face: mouth-open}
  Stick:     {stick: head-digital}
YAML

# Compare the bindings block only: the header names the base and is prose.
sed -n '/^bindings:/,$p' "$OUT" > "$OUT.got"
if ! diff -u "$OUT.expected" "$OUT.got"; then
    echo "wizard-selftest: the emitted bindings differ from what the canned run should produce" >&2
    exit 1
fi
echo "ok: wizard selftest"
```

- [ ] **Step 2: Run it to see it fail**

Run: `chmod +x Scripts/wizard_selftest.sh && Scripts/wizard_selftest.sh`
Expected: FAIL — `--selftest` is not a flag yet, so the binary opens a window or exits without writing.

- [ ] **Step 3: Add the flag**

In `Source/Project64-wizard/main.cpp`, above `main`, add:

```cpp
// A keydown as the handlers see it. The wizard's screens only ever read `type` and
// `key.scancode`, so a synthetic event needs nothing else.
static SDL_Event KeyEvent(SDL_Scancode Code)
{
    SDL_Event E;
    memset(&E, 0, sizeof(E));
    E.type = SDL_EVENT_KEY_DOWN;
    E.key.scancode = Code;
    return E;
}

static SDL_Event ClickEvent(float X, float Y)
{
    SDL_Event E;
    memset(&E, 0, sizeof(E));
    E.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    E.button.x = X;
    E.button.y = Y;
    return E;
}

// Walks the real screens with a canned sequence and writes the result to Path. No window,
// no renderer, no camera: this is the end-to-end proof that runs anywhere.
static int Selftest(const char * Path)
{
    WizardDraft Draft;
    WizardUi Ui;
    WizardUiInit(&Ui);

    // Base screen: the built-in bindings, the first row.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // A: a keyboard key.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_1), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_X), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // B: a gamepad button.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_2), &Ui, &Draft, 0);
    SDL_Event Pad;
    memset(&Pad, 0, sizeof(Pad));
    Pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    Pad.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    WizardHandleEvent(Pad, &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // Z: the panel slot mid1, clicked at its centre. PointerZoneRect puts mid1 at
    // x 164..220, y 488..536 in a 640x640 window; the wizard draws that at half scale
    // from (24, 300), so its centre is (24 + 192*0.5, 300 + 512*0.5) = (120, 556).
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_4), &Ui, &Draft, 0);
    WizardHandleEvent(ClickEvent(120.0f, 556.0f), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // Start: the gesture that is firing, with mouth-open held.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_5), &Ui, &Draft, POINTER_GESTURE_MOUTH_OPEN);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_SPACE), &Ui, &Draft, POINTER_GESTURE_MOUTH_OPEN);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    // Everything up to Stick keeps what it has.
    while (Ui.Screen == WIZARD_CONTROL && (N64Control)Ui.Control != N64Control::Stick)
    {
        WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);
    }

    // Stick: the fifth form, a digital head stick.
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_1), &Ui, &Draft, 0);
    for (int i = 0; i < 4; i++) WizardHandleEvent(KeyEvent(SDL_SCANCODE_DOWN), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);
    WizardHandleEvent(KeyEvent(SDL_SCANCODE_RETURN), &Ui, &Draft, 0);

    if (!Draft.Save(Path, Ui.Base))
    {
        fprintf(stderr, "wizard-selftest: %s\n", Draft.Error());
        return 1;
    }
    printf("wizard-selftest wrote %s\n", Path);
    return 0;
}
```

and at the top of `main`, before `SDL_Init`:

```cpp
    if (argc >= 3 && strcmp(argv[1], "--selftest") == 0)
    {
        return Selftest(argv[2]);
    }
```

Add `#include <Common/PointerState.h>` if it is not already there (it is, for `PointerState`).

- [ ] **Step 4: Run the script**

Run: `make wizard && Scripts/wizard_selftest.sh`
Expected: `ok: wizard selftest`.

If the emitted block differs, read the diff before changing either side: the canned sequence is a claim about how the screens behave, and the expected YAML is a claim about what the draft emits. Fix whichever is actually wrong.

- [ ] **Step 5: Add the make target**

In `Makefile`, after `face-selftest`, add:

```make
wizard-selftest: ## Prove the wizard's screens write the mapping they show, with no window and no camera
	Scripts/wizard_selftest.sh
```

Add `wizard-selftest` to the `.PHONY` line.

Run: `make wizard-selftest`
Expected: `ok: wizard selftest`.

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-wizard/main.cpp Scripts/wizard_selftest.sh Makefile
git commit -m "$(cat <<'EOF'
Prove the wizard end to end without a window or a camera

--selftest drives the real event handlers with a canned sequence and writes
the YAML they produce, so the screens and the draft are checked against each
other by make rather than by hand.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: Documentation

**Files:**
- Modify: `README.md`
- Modify: `AGENTS.md`

**Interfaces:** none.

- [ ] **Step 1: README**

In the build list under "## Build", after the `make face-gesture-test` line, add:

```
make wizard-draft-test    # tests for the wizard's draft and the YAML it writes
make wizard-selftest      # drives the wizard's screens and checks the file they produce
```

After the "## Input mapping" section, add a new section:

````markdown
## Building a mapping with the wizard

`make wizard && ./Bin/macOS/Project64-wizard` opens a small window that walks the fifteen
controls and writes a mapping file. It never loads a ROM.

Start from the built-in bindings, one of the five shipped layouts, or a file you name.
Then, for each control, press a number to say how you want to bind it — `1` a keyboard
key, `2` a gamepad button, `3` a gamepad axis, `4` a slot on the mouse panel, `5` a face
gesture — and do the thing. Enter keeps what a control already has, Delete returns it to
its built-in binding, Backspace goes back, Escape jumps to the review.

Inside mode `1` the next key you press *is* the binding, Escape and the arrows included,
so there is no way to cancel it: press `1` again to capture a different key. The other
modes ignore the keyboard, so Escape leaves them.

The gesture list shows all eleven with their panel tags, and lights the ones your face is
firing right now, which also makes it a way to see how the thresholds suit you. The camera
starts the first time you open that list and not before; `PJ64_FACE=0` keeps it shut and
the list still works, just unlit. Frames are never saved, shown or logged.

The review lists all fifteen, marking the ones that inherit their built-in binding rather
than being written to the file. Saving offers `Config/input.yaml`, a path beside a ROM so
the per-game lookup finds it, or a path you type — and warns you if you aim at
`Config/mouse/` or `Config/face/`, which `make` deletes and recopies on every build.

Nothing is written until the emulator's own parser has accepted it: the wizard emits the
file, loads it back through the same reader the plugin uses, and refuses to save when that
reader objects, showing you its exact complaint.
````

In "## What works", after the sentence about the mouse and face layouts, add: `A mapping can be built interactively with `make wizard` (see "Building a mapping with the wizard").`

- [ ] **Step 2: AGENTS.md**

In the command list, after the `make face-selftest` line, add:

```
make wizard-selftest      # the wizard's screens, driven by synthetic events
```

In the architecture notes, after the paragraph describing the frontend, add:

```markdown
`Bin/macOS/Project64-wizard` is a second binary from `Source/Project64-wizard/`. It shares
`InputConfig.o`, `FaceGestures.o` and `FaceTracker.o` with the frontend but links no
OpenGL: it draws with `SDL_Renderer` and SDL's 8x8 debug font, because the overlay's font
has twenty glyphs and cannot spell a scancode name. `WizardDraft` holds the mapping and
emits the YAML with no SDL window in sight, which is why `make wizard-draft-test` can test
it headlessly; `Screens.cpp` turns one event into one call on the draft, which is why
`--selftest` can drive the real screens with synthetic events.
```

Add one trap to the traps list:

```markdown
- **A control that inherits its built-in binding must stay out of the file.** The grammar
  allows exactly one input per control, and several built-in bindings are a *pair* (a key
  and a gamepad input), so a pair can only survive by the control being omitted. This is
  why `WizardDraft` tracks explicit-versus-inherited instead of just writing all fifteen
  lines, and why loading a base reads the file twice — `InputConfig` merges over the
  defaults and cannot say which controls a file named.
```

- [ ] **Step 3: Check wrap and line endings**

Run: `file README.md AGENTS.md && awk 'length > 92 {print FILENAME ":" FNR ": " length}' README.md AGENTS.md`
Expected: no CRLF; no new lines over 92 columns (pre-existing long lines in tables are unchanged).

- [ ] **Step 4: Commit**

```bash
git add README.md AGENTS.md
git commit -m "$(cat <<'EOF'
Document the binding wizard

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Manual verification (human partner)

Not dispatched to an agent: this is the run with a real face, a real gamepad and a real game.

- [ ] **Step 1: Walk it once from the built-in bindings** — `make wizard && ./Bin/macOS/Project64-wizard`. Confirm: the base list shows seven rows; a key capture takes the key you press; a gamepad button and an axis capture work if you have a pad; clicking `mid1` on the drawn panel binds it; the gesture list lights up as you move your face, and Space takes the one that is firing.
- [ ] **Step 2: Save and play it** — save to `Config/input.yaml`, then `make run rom=Roms/sm64.z64` and confirm the mapping is the one you built.
- [ ] **Step 3: Save beside a ROM** — save with destination 2 pointed at `Roms/sm64.z64`, then `make run rom=Roms/sm64.z64` with no `input=` and confirm the frontend prints `input layout: Roms/sm64.yaml`.
- [ ] **Step 4: Judge the interaction** — five numbered modes is the design's guess. If binding feels clumsy, say where: the likely candidates are mode 1 having no cancel, and the panel being half-size.
- [ ] **Step 5: Commit any change** with a message naming which run decided it, ending with the standard trailer.
