// Project64 - A Nintendo 64 emulator
// SDL3 keyboard + gamepad input plugin for the macOS/SDL frontend.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
//
// GetKeys runs on the emulation thread. It never pumps events; it reads
// SDL's keyboard state array (updated by the frontend's main-thread event
// loop) and polls the first gamepad, both of which SDL3 documents as safe
// from any thread.
#include <Project64-plugin-spec/Input.h>
#include <Common/GridKeys.h>
#include "InputConfig.h"
#include <SDL3/SDL.h>
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

EXPORT void CALL PluginLoaded(void)
{
}
