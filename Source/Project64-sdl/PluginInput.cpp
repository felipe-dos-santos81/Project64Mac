// Project64 - A Nintendo 64 emulator
// SDL3 keyboard + gamepad input plugin for the macOS/SDL frontend.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
//
// GetKeys runs on the emulation thread. It never pumps events; it reads
// SDL's keyboard state array (updated by the frontend's main-thread event
// loop) and polls the first gamepad, both of which SDL3 documents as safe
// from any thread. The mouse is sampled by the frontend on its main thread
// (SDL3 documents the mouse state functions as main-thread only) and read
// here through the shared PointerState.
#include <Project64-plugin-spec/Input.h>
#include <Common/GridKeys.h>
#include <Common/PointerLayout.h>
#include <Common/PointerState.h>
#include "InputConfig.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PLUGIN_NAME "Project64 SDL3 input 1.0"

// N64 stick range is -80..80; the SDL axis range is -32768..32767.
static const int16_t STICK_DEAD_ZONE = 4000;  // below this an axis reads as centred
static const int16_t STICK_THRESHOLD = 16000; // above this an axis counts as a button press
static const int N64_AXIS_MAX = 80;

static SDL_Gamepad * g_Gamepad = nullptr;
static GridKeys * g_GridKeys = nullptr;
static bool g_GridKeysChecked = false;
static PointerState * g_Pointer = nullptr;
static bool g_PointerChecked = false;
// Latch: the zone under the cursor when the button went down stays pressed until release.
static bool g_PointerPrevButton = false;
static int g_PointerLatched = POINTER_ZONE_NONE;
// Flick gate: a cursor that jumps more than g_PointerFlick px between polls keeps the
// previous poll's tilt, so reaching for the panel never reads as a tilt on the way.
// PJ64_POINTER_FLICK overrides the threshold; 0 disables the gate.
static PointerGate g_PointerGate = { false, 0.0f, 0.0f, 0, 0 };
static float g_PointerFlick = POINTER_FLICK_PX;

// The orchestrator maps a GridKeys and passes its descriptor in PJ64_GRID_KEYS_ENV.
// Reading it here means every tile sees the one keyboard owned by the control strip.
// Without the variable this is a normal single-ROM run and SDL's own state is used.
static void OpenGridKeys(void)
{
    if (g_GridKeysChecked)
    {
        return;
    }
    g_GridKeysChecked = true;
    const char * FdEnv = getenv(PJ64_GRID_KEYS_ENV);
    if (FdEnv == nullptr)
    {
        return;
    }
    void * Mapped = mmap(nullptr, sizeof(GridKeys), PROT_READ, MAP_SHARED, atoi(FdEnv), 0);
    if (Mapped == MAP_FAILED)
    {
        fprintf(stderr, "input: could not map %s=%s\n", PJ64_GRID_KEYS_ENV, FdEnv);
        return;
    }
    g_GridKeys = (GridKeys *)Mapped;
}

// The frontend maps a PointerState and passes its descriptor in PJ64_POINTER_ENV. It is
// mapped read-write: the plugin writes labels and the latched zone back for the overlay.
static void OpenPointerState(void)
{
    if (g_PointerChecked)
    {
        return;
    }
    g_PointerChecked = true;
    const char * FdEnv = getenv(PJ64_POINTER_ENV);
    if (FdEnv == nullptr)
    {
        return;
    }
    void * Mapped = mmap(nullptr, sizeof(PointerState), PROT_READ | PROT_WRITE, MAP_SHARED, atoi(FdEnv), 0);
    if (Mapped == MAP_FAILED)
    {
        fprintf(stderr, "input: could not map %s=%s; pointer bindings are inactive\n", PJ64_POINTER_ENV, FdEnv);
        return;
    }
    g_Pointer = (PointerState *)Mapped;
    const char * Flick = getenv("PJ64_POINTER_FLICK");
    if (Flick != nullptr)
    {
        g_PointerFlick = (float)atof(Flick);
    }
}

// Verification only. With PJ64_GRID_SELFTEST set, report once both that the snapshot
// carried the strip's pattern (recv) and what the controller produced from it (a, start),
// so Scripts/grid_selftest.sh proves delivery and mapping together.
static void SelftestReport(const bool * Raw, const BUTTONS * Out)
{
    static bool Reported = false;
    if (Reported || getenv("PJ64_GRID_SELFTEST") == nullptr)
    {
        return;
    }
    Reported = true;
    const int Recv = (Raw[SDL_SCANCODE_X] && Raw[SDL_SCANCODE_RETURN]) ? 1 : 0;
    fprintf(stderr, "grid-selftest pid=%d recv=%d a=%d start=%d\n",
        (int)getpid(), Recv, Out->A_BUTTON ? 1 : 0, Out->START_BUTTON ? 1 : 0);
}

// Verification only. With PJ64_POINTER_SELFTEST set, report once, on the first frame that
// sees the button down or any face input (a gesture bit, or a head-stick axis), which zone
// latched and what the controller produced, so Scripts/pointer_selftest.sh and
// Scripts/face_selftest.sh prove delivery, geometry and mapping together.
static void PointerSelftestReport(bool Button, int Latched, uint32_t Gestures, const BUTTONS * Out)
{
    static bool Reported = false;
    if (Reported || getenv("PJ64_POINTER_SELFTEST") == nullptr)
    {
        return;
    }
    if (!Button && Gestures == 0 && Out->X_AXIS == 0 && Out->Y_AXIS == 0)
    {
        return;
    }
    Reported = true;
    fprintf(stderr, "pointer-selftest zone=%d a=%d start=%d z=%d x=%d y=%d\n",
        Latched, Out->A_BUTTON ? 1 : 0, Out->START_BUTTON ? 1 : 0, Out->Z_TRIG ? 1 : 0,
        (int)Out->X_AXIS, (int)Out->Y_AXIS);
}

static void OpenFirstGamepad(void)
{
    if (g_Gamepad != nullptr)
    {
        return;
    }
    int count = 0;
    SDL_JoystickID * ids = SDL_GetGamepads(&count);
    if (ids != nullptr)
    {
        if (count > 0)
        {
            g_Gamepad = SDL_OpenGamepad(ids[0]);
        }
        SDL_free(ids);
    }
}

static void CloseGamepad(void)
{
    if (g_Gamepad != nullptr)
    {
        SDL_CloseGamepad(g_Gamepad);
        g_Gamepad = nullptr;
    }
}

static int8_t AxisToN64(int16_t value)
{
    if (value > -STICK_DEAD_ZONE && value < STICK_DEAD_ZONE)
    {
        return 0;
    }
    return (int8_t)((value * N64_AXIS_MAX) / 32767);
}

// Clamps a published head-stick coordinate to the N64 axis range before the narrowing cast
// to int8_t, so a value outside -80..80 (the producers already clamp, but the consumer
// should not take that on faith) cannot wrap into the opposite direction.
static int8_t ClampToN64Axis(int32_t value)
{
    if (value > N64_AXIS_MAX) return (int8_t)N64_AXIS_MAX;
    if (value < -N64_AXIS_MAX) return (int8_t)-N64_AXIS_MAX;
    return (int8_t)value;
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO * PluginInfo)
{
    PluginInfo->Version = CONTROLLER_SPECS_VERSION;
    PluginInfo->Type = PLUGIN_TYPE_CONTROLLER;
    snprintf(PluginInfo->Name, sizeof(PluginInfo->Name), "%s", PLUGIN_NAME);
    PluginInfo->Reserved1 = false;
    PluginInfo->Reserved2 = true;
}

EXPORT void CALL InitiateControllers(CONTROL_INFO * ControlInfo)
{
    // Controls points at the core's own array, so these writes land there directly.
    for (int i = 0; i < 4; i++)
    {
        ControlInfo->Controls[i].Present = PRESENT_NONE;
        ControlInfo->Controls[i].RawData = false;
        ControlInfo->Controls[i].Plugin = PLUGIN_NONE;
    }
    ControlInfo->Controls[0].Present = PRESENT_CONT;
    ControlInfo->Controls[0].Plugin = PLUGIN_MEMPAK;
}

static void SetControl(BUTTONS * Keys, N64Control Control)
{
    switch (Control)
    {
    case N64Control::A: Keys->A_BUTTON = 1; break;
    case N64Control::B: Keys->B_BUTTON = 1; break;
    case N64Control::Z: Keys->Z_TRIG = 1; break;
    case N64Control::Start: Keys->START_BUTTON = 1; break;
    case N64Control::L: Keys->L_TRIG = 1; break;
    case N64Control::R: Keys->R_TRIG = 1; break;
    case N64Control::CUp: Keys->U_CBUTTON = 1; break;
    case N64Control::CDown: Keys->D_CBUTTON = 1; break;
    case N64Control::CLeft: Keys->L_CBUTTON = 1; break;
    case N64Control::CRight: Keys->R_CBUTTON = 1; break;
    case N64Control::DPadUp: Keys->U_DPAD = 1; break;
    case N64Control::DPadDown: Keys->D_DPAD = 1; break;
    case N64Control::DPadLeft: Keys->L_DPAD = 1; break;
    case N64Control::DPadRight: Keys->R_DPAD = 1; break;
    case N64Control::Stick: break;
    default: break;
    }
}

EXPORT void CALL GetKeys(int32_t Control, BUTTONS * Keys)
{
    Keys->Value = 0;
    if (Control != 0)
    {
        return;
    }

    bool Snapshot[SDL_SCANCODE_COUNT];
    bool Raw[SDL_SCANCODE_COUNT] = { false };
    const bool * k = SDL_GetKeyboardState(nullptr);
    OpenGridKeys();
    if (g_GridKeys != nullptr)
    {
        GridKeysSnapshot(g_GridKeys, Snapshot);
        memcpy(Raw, Snapshot, sizeof(Raw));
        k = Snapshot;
    }

    const InputConfig & Config = InputConfig::Get();
    bool StickFromKeys = false;

    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        for (const Binding & B : Config.Bindings((N64Control)i))
        {
            if (B.kind == Binding::Kind::Key)
            {
                if (k != nullptr && k[B.code])
                {
                    SetControl(Keys, (N64Control)i);
                }
            }
            else if (B.kind == Binding::Kind::Keys && k != nullptr)
            {
                int x = 0, y = 0;
                if (k[B.LeftKey]) x -= N64_AXIS_MAX;
                if (k[B.RightKey]) x += N64_AXIS_MAX;
                if (k[B.DownKey]) y -= N64_AXIS_MAX;
                if (k[B.UpKey]) y += N64_AXIS_MAX;
                Keys->X_AXIS = (int8_t)x;
                Keys->Y_AXIS = (int8_t)y;
                if (x != 0 || y != 0)
                {
                    StickFromKeys = true;
                }
            }
        }
    }

    SelftestReport(Raw, Keys);

    OpenPointerState();
    if (g_Pointer != nullptr)
    {
        PointerSample S;
        PointerSnapshot(g_Pointer, &S);
        PointerEval E = PointerLayoutEvaluate(S.X, S.Y, S.W, S.H, S.Inside);
        PointerGateStick(&g_PointerGate, &E, S.X, S.Y, g_PointerFlick);
        if (S.Button && !g_PointerPrevButton)
        {
            g_PointerLatched = E.Zone; // press edge: latch whatever is under the cursor now
        }
        if (!S.Button)
        {
            g_PointerLatched = POINTER_ZONE_NONE;
        }
        g_PointerPrevButton = S.Button;
        g_Pointer->LatchedZone.store(g_PointerLatched, std::memory_order_relaxed);
        g_Pointer->Quadrant.store(PointerQuadrant(E.StickX, E.StickY), std::memory_order_relaxed);
        const uint32_t Gestures = g_Pointer->Gestures.load(std::memory_order_relaxed);

        for (int i = 0; i < (int)N64Control::Count; i++)
        {
            for (const Binding & B : Config.Bindings((N64Control)i))
            {
                if (B.kind == Binding::Kind::Zone)
                {
                    if (g_PointerLatched == B.code)
                    {
                        SetControl(Keys, (N64Control)i);
                    }
                }
                else if (B.kind == Binding::Kind::Face)
                {
                    if ((Gestures & (uint32_t)B.code) != 0)
                    {
                        SetControl(Keys, (N64Control)i);
                    }
                }
                else if (B.kind == Binding::Kind::Pointer)
                {
                    Keys->X_AXIS = E.StickX;
                    Keys->Y_AXIS = E.StickY;
                    StickFromKeys = true; // the pointer owns the stick; the gamepad must not overwrite it
                }
                else if (B.kind == Binding::Kind::HeadStick)
                {
                    // The tracker publishes the stick already scaled; head-digital snaps it
                    // by the quadrant rule the mouse guide uses (vertical wins ties).
                    int8_t X = ClampToN64Axis(g_Pointer->HeadX.load(std::memory_order_relaxed));
                    int8_t Y = ClampToN64Axis(g_Pointer->HeadY.load(std::memory_order_relaxed));
                    const int Q = PointerQuadrant(X, Y);
                    if (B.code == 1)
                    {
                        X = (int8_t)(Q == 1 ? N64_AXIS_MAX : Q == 3 ? -N64_AXIS_MAX : 0);
                        Y = (int8_t)(Q == 0 ? N64_AXIS_MAX : Q == 2 ? -N64_AXIS_MAX : 0);
                    }
                    Keys->X_AXIS = X;
                    Keys->Y_AXIS = Y;
                    StickFromKeys = true; // the head owns the stick; the gamepad must not overwrite it
                    g_Pointer->Quadrant.store(Q, std::memory_order_relaxed);
                }
            }
        }
        PointerSelftestReport(S.Button, g_PointerLatched, Gestures, Keys);
    }

    OpenFirstGamepad();
    if (g_Gamepad != nullptr)
    {
        SDL_UpdateGamepads();
        if (!SDL_GamepadConnected(g_Gamepad))
        {
            CloseGamepad();
            return;
        }
        for (int i = 0; i < (int)N64Control::Count; i++)
        {
            for (const Binding & B : Config.Bindings((N64Control)i))
            {
                if (B.kind == Binding::Kind::Button)
                {
                    if (SDL_GetGamepadButton(g_Gamepad, (SDL_GamepadButton)B.code))
                    {
                        SetControl(Keys, (N64Control)i);
                    }
                }
                else if (B.kind == Binding::Kind::Axis)
                {
                    const int16_t v = SDL_GetGamepadAxis(g_Gamepad, (SDL_GamepadAxis)B.code);
                    if (B.positive ? v > STICK_THRESHOLD : v < -STICK_THRESHOLD)
                    {
                        SetControl(Keys, (N64Control)i);
                    }
                }
                else if (B.kind == Binding::Kind::Stick && !StickFromKeys)
                {
                    Keys->X_AXIS = AxisToN64(SDL_GetGamepadAxis(g_Gamepad, (SDL_GamepadAxis)B.code));
                    Keys->Y_AXIS = (int8_t)-AxisToN64(SDL_GetGamepadAxis(g_Gamepad, (SDL_GamepadAxis)(B.code + 1)));
                }
            }
        }
    }
}

EXPORT void CALL ControllerCommand(int32_t /*Control*/, uint8_t * /*Command*/)
{
}

EXPORT void CALL ReadController(int32_t /*Control*/, uint8_t * /*Command*/)
{
}

EXPORT void CALL RomOpen(void)
{
    // The frontend owns the gamepad subsystem's lifetime. SDL3 documents subsystem init
    // and quit as not thread safe, and RomOpen can run on either thread, so do not
    // touch it here.
    OpenFirstGamepad();
}

EXPORT void CALL RomClosed(void)
{
    CloseGamepad();
}

EXPORT void CALL CloseDLL(void)
{
    CloseGamepad();
}

EXPORT void CALL DllAbout(void * /*hParent*/)
{
}

EXPORT void CALL DllConfig(void * /*hParent*/)
{
}

EXPORT void CALL WM_KeyDown(uint32_t /*wParam*/, uint32_t /*lParam*/)
{
}

EXPORT void CALL WM_KeyUp(uint32_t /*wParam*/, uint32_t /*lParam*/)
{
}

// Copies the resolved layout into the shared struct so the overlay can label cells. Runs
// once at dylib load, before any ROM, so plain stores are enough.
static void PublishPointerLabels(void)
{
    OpenPointerState();
    if (g_Pointer == nullptr)
    {
        return;
    }
    const InputConfig & Config = InputConfig::Get();
    Config.PointerLabels(g_Pointer->Labels, g_Pointer->GestureLabels);
    g_Pointer->LatchedZone.store(POINTER_ZONE_NONE);
    g_Pointer->OverlayWanted.store(Config.UsesPointer() ? 1u : 0u);
    g_Pointer->HeadStickWanted.store(Config.UsesHeadStick() ? 1u : 0u, std::memory_order_release);
    // The frontend's main loop polls this to start the camera; it may run on another
    // thread than the one loading the dylib, hence release here and acquire there.
    g_Pointer->FaceWanted.store(Config.UsesFace() ? 1u : 0u, std::memory_order_release);
}

EXPORT void CALL PluginLoaded(void)
{
    const char * Env = getenv("PJ64_INPUT_YAML");
    if (Env != nullptr && Env[0] != '\0')
    {
        InputConfig::Get().Load(Env);
    }
    else
    {
        char Path[PATH_MAX];
        if (DefaultConfigPath(Path, sizeof(Path)) && access(Path, R_OK) == 0)
        {
            InputConfig::Get().Load(Path);
        }
    }
    PublishPointerLabels();
}
