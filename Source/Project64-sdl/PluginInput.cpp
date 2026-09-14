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
    if (k != nullptr)
    {
        Keys->A_BUTTON = k[SDL_SCANCODE_X];
        Keys->B_BUTTON = k[SDL_SCANCODE_C];
        Keys->Z_TRIG = k[SDL_SCANCODE_Z];
        Keys->START_BUTTON = k[SDL_SCANCODE_RETURN];
        Keys->L_TRIG = k[SDL_SCANCODE_Q];
        Keys->R_TRIG = k[SDL_SCANCODE_E];
        Keys->U_CBUTTON = k[SDL_SCANCODE_W];
        Keys->D_CBUTTON = k[SDL_SCANCODE_S];
        Keys->L_CBUTTON = k[SDL_SCANCODE_A];
        Keys->R_CBUTTON = k[SDL_SCANCODE_D];
        Keys->U_DPAD = k[SDL_SCANCODE_I];
        Keys->D_DPAD = k[SDL_SCANCODE_K];
        Keys->L_DPAD = k[SDL_SCANCODE_J];
        Keys->R_DPAD = k[SDL_SCANCODE_L];
        int x = 0, y = 0;
        if (k[SDL_SCANCODE_LEFT]) x -= N64_AXIS_MAX;
        if (k[SDL_SCANCODE_RIGHT]) x += N64_AXIS_MAX;
        if (k[SDL_SCANCODE_DOWN]) y -= N64_AXIS_MAX;
        if (k[SDL_SCANCODE_UP]) y += N64_AXIS_MAX;
        Keys->X_AXIS = (int8_t)x;
        Keys->Y_AXIS = (int8_t)y;
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
        Keys->A_BUTTON |= SDL_GetGamepadButton(g_Gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        Keys->B_BUTTON |= SDL_GetGamepadButton(g_Gamepad, SDL_GAMEPAD_BUTTON_WEST);
        Keys->START_BUTTON |= SDL_GetGamepadButton(g_Gamepad, SDL_GAMEPAD_BUTTON_START);
        Keys->L_TRIG |= SDL_GetGamepadButton(g_Gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        Keys->R_TRIG |= SDL_GetGamepadButton(g_Gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        Keys->Z_TRIG |= SDL_GetGamepadAxis(g_Gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > STICK_THRESHOLD;
        Keys->U_DPAD |= SDL_GetGamepadButton(g_Gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        Keys->D_DPAD |= SDL_GetGamepadButton(g_Gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        Keys->L_DPAD |= SDL_GetGamepadButton(g_Gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        Keys->R_DPAD |= SDL_GetGamepadButton(g_Gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        int16_t rx = SDL_GetGamepadAxis(g_Gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        int16_t ry = SDL_GetGamepadAxis(g_Gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
        Keys->R_CBUTTON |= rx > STICK_THRESHOLD;
        Keys->L_CBUTTON |= rx < -STICK_THRESHOLD;
        Keys->D_CBUTTON |= ry > STICK_THRESHOLD;
        Keys->U_CBUTTON |= ry < -STICK_THRESHOLD;
        if (Keys->X_AXIS == 0 && Keys->Y_AXIS == 0)
        {
            Keys->X_AXIS = AxisToN64(SDL_GetGamepadAxis(g_Gamepad, SDL_GAMEPAD_AXIS_LEFTX));
            Keys->Y_AXIS = (int8_t)-AxisToN64(SDL_GetGamepadAxis(g_Gamepad, SDL_GAMEPAD_AXIS_LEFTY));
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
