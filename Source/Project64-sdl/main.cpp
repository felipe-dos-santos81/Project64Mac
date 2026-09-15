// Project64 - A Nintendo 64 emulator
// SDL3 frontend for macOS. Windowed only; the cursor is never grabbed or hidden. The main
// loop publishes the mouse for the input plugin (see Common/PointerState.h).
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "SdlNotification.h"
#include "SdlRenderWindow.h"
#include "GridHost.h"
#include "FaceTracker.h"
#include "GameConfig.h"
#include "InputConfig.h"
#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/SettingsID.h>
#include <SDL3/SDL.h>
#include <OpenGL/OpenGL.h>
#include <Common/PointerState.h>
#include <Common/PointerLayout.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <mach-o/dyld.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

static std::string ExecutableDirectory(void)
{
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
    {
        return ".";
    }
    char resolved[4096];
    if (realpath(buf, resolved) == nullptr)
    {
        return ".";
    }
    std::string dir(resolved);
    size_t slash = dir.rfind('/');
    return slash == std::string::npos ? "." : dir.substr(0, slash);
}

// Tears down whatever SDL state exists and returns ExitCode, so each failure path is one
// line and a new one cannot forget a step. Null arguments are skipped.
static int ShutdownSdl(SDL_Window * Window, SDL_GLContext Context, int ExitCode)
{
    if (Context != nullptr)
    {
        SDL_GL_DestroyContext(Context);
    }
    if (Window != nullptr)
    {
        SDL_DestroyWindow(Window);
    }
    SDL_Quit();
    return ExitCode;
}

// A tile is a child of the orchestrator. macOS has no PDEATHSIG, so if the orchestrator is
// killed hard the tile is reparented; exit then rather than lingering as a stray emulator.
static int SDLCALL ParentWatchThread(void * /*data*/)
{
    pid_t Parent = getppid();
    for (;;)
    {
        SDL_Delay(1000);
        if (getppid() != Parent)
        {
            _exit(0);
        }
    }
    return 0;
}

// One mapped PointerState for this process: the main loop writes the mouse sample, the
// face tracker writes gesture bits, the input plugin reads both and writes labels back.
// The plugin is a dylib in this process, so the descriptor is passed by number in the
// environment exactly as the grid passes its key snapshot. The name is unlinked at once.
static PointerState * CreatePointerState(void)
{
    char ShmName[64];
    snprintf(ShmName, sizeof(ShmName), "/pj64ptr-%d", (int)getpid());
    const int Fd = shm_open(ShmName, O_CREAT | O_RDWR, 0600);
    if (Fd < 0 || ftruncate(Fd, (off_t)sizeof(PointerState)) != 0)
    {
        fprintf(stderr, "could not create the pointer state; mouse bindings are inactive\n");
        if (Fd >= 0) { shm_unlink(ShmName); close(Fd); }
        return nullptr;
    }
    void * Mapped = mmap(nullptr, sizeof(PointerState), PROT_READ | PROT_WRITE, MAP_SHARED, Fd, 0);
    shm_unlink(ShmName);
    if (Mapped == MAP_FAILED)
    {
        fprintf(stderr, "could not map the pointer state; mouse bindings are inactive\n");
        close(Fd);
        return nullptr;
    }
    PointerState * State = (PointerState *)Mapped;
    memset(State, 0, sizeof(PointerState));
    State->LatchedZone.store(-1);
    State->Quadrant.store(-1);
    char FdText[16];
    snprintf(FdText, sizeof(FdText), "%d", Fd);
    setenv(PJ64_POINTER_ENV, FdText, 1);
    return State;
}

// PJ64_POINTER_INJECT=x,y,button replaces the sampled mouse with a fixed sample so the
// pointer path can be proven without a mouse (Scripts/pointer_selftest.sh).
static bool ParsePointerInject(PointerSample * Out)
{
    const char * Env = getenv("PJ64_POINTER_INJECT");
    if (Env == nullptr)
    {
        return false;
    }
    int Button = 0;
    if (sscanf(Env, "%f,%f,%d", &Out->X, &Out->Y, &Button) != 3)
    {
        fprintf(stderr, "bad PJ64_POINTER_INJECT: %s (want x,y,button)\n", Env);
        return false;
    }
    Out->Inside = true;
    Out->Button = Button != 0;
    return true;
}

// PJ64_FACE_INJECT=<gesture>[,<x>,<y>] publishes that gesture's bit and that head stick
// instead of running the tracker, so the face path can be proven without a camera
// (Scripts/face_selftest.sh). With it set the camera is never opened, whatever PJ64_FACE says.
struct FaceInject { uint32_t Bits; int X, Y; };

static bool ParseFaceInject(FaceInject * Out)
{
    const char * Env = getenv("PJ64_FACE_INJECT");
    if (Env == nullptr)
    {
        return false;
    }
    char Name[32] = { 0 };
    Out->X = 0;
    Out->Y = 0;
    const int N = sscanf(Env, "%31[^,],%d,%d", Name, &Out->X, &Out->Y);
    Out->Bits = N >= 1 ? PointerGestureFromName(Name) : 0;
    if (Out->Bits == 0 || N == 2 || Out->X < -80 || Out->X > 80 || Out->Y < -80 || Out->Y > 80)
    {
        fprintf(stderr, "bad PJ64_FACE_INJECT: %s (want gesture[,x,y] with x,y in -80..80)\n", Env);
        return false;
    }
    return true;
}

static void PublishFaceInject(PointerState * State, const FaceInject & F)
{
    State->Gestures.store(F.Bits, std::memory_order_relaxed);
    State->HeadX.store(F.X, std::memory_order_relaxed);
    State->HeadY.store(F.Y, std::memory_order_relaxed);
    State->Face.store(FACE_TRACKING, std::memory_order_relaxed);
}

// Main thread only: SDL3 documents SDL_GetMouseState as main-thread only.
static void PublishMouse(PointerState * State, SDL_Window * Window, const PointerSample * Inject)
{
    PointerSample S;
    SDL_GetWindowSize(Window, &S.W, &S.H);
    if (Inject != nullptr)
    {
        S.X = Inject->X;
        S.Y = Inject->Y;
        S.Inside = true;
        S.Button = Inject->Button;
    }
    else
    {
        const SDL_MouseButtonFlags Buttons = SDL_GetMouseState(&S.X, &S.Y);
        S.Inside = SDL_GetMouseFocus() == Window;
        S.Button = (Buttons & SDL_BUTTON_LMASK) != 0;
    }
    PointerPublish(State, S);
}

// PJ64_FACE: unset or empty means "start the camera once the plugin reports that the
// layout binds a gesture"; 0 means never; anything else, or --face, means start at once.
enum class FaceMode { Auto, On, Off };

static FaceMode FaceModeFromEnv(void)
{
    const char * Env = getenv("PJ64_FACE");
    if (Env == nullptr || Env[0] == '\0') return FaceMode::Auto;
    return strcmp(Env, "0") == 0 ? FaceMode::Off : FaceMode::On;
}

// Whether the layout the plugin is about to load binds the pointer. The frontend reads the
// same file with the same code, quietly (the plugin reports a bad file), because the
// window and the video plugin's viewport must be sized before either exists. The default
// path is the frontend's own: the plugin's DefaultConfigPath resolves from its dylib.
static bool LayoutUsesPointer(const std::string & ExeDir)
{
    const char * Env = getenv("PJ64_INPUT_YAML");
    const std::string Path = (Env != nullptr && Env[0] != '\0') ? std::string(Env) : ExeDir + "/Config/input.yaml";
    InputConfig & Config = InputConfig::Get();
    return Config.Load(Path.c_str(), true) && Config.UsesPointer();
}

// Plugin directory is the core default: <base dir>/Plugin/ (Directory_PluginInitial).
static void ConfigurePlugins(void)
{
    g_Settings->SaveString(Plugin_GFX_Current, "GFX/Project64-video.dylib");
    g_Settings->SaveString(Plugin_AUDIO_Current, "Audio/Project64-audio.dylib");
    g_Settings->SaveString(Plugin_RSP_Current, "RSP/Project64-rsp.dylib");
    g_Settings->SaveString(Plugin_CONT_Current, "Input/Project64-input-sdl.dylib");
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, true);
}

int main(int argc, char ** argv)
{
    if (argc >= 2 && strcmp(argv[1], "--version") == 0)
    {
        printf("Project64 macOS SDL3 frontend\n");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--grid") == 0)
    {
        return GridHostRun(argc, argv);
    }
    bool TileMode = false;
    FaceMode Face = FaceModeFromEnv();
    SDL_Rect TileRect = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    const char * RomPath = nullptr;
    // Strip --face wherever it appears; everything else keeps its position.
    int Argc = 0;
    char * Argv[16];
    for (int i = 0; i < argc && Argc < 16; i++)
    {
        if (strcmp(argv[i], "--face") == 0) { Face = FaceMode::On; continue; }
        Argv[Argc++] = argv[i];
    }
    if (Argc >= 2 && strcmp(Argv[1], "--tile") == 0)
    {
        if (Argc < 5 || strcmp(Argv[3], "--tile-rect") != 0)
        {
            fprintf(stderr, "usage: %s --tile <rom> --tile-rect X,Y,W,H\n", Argv[0]);
            return 2;
        }
        RomPath = Argv[2];
        if (sscanf(Argv[4], "%d,%d,%d,%d", &TileRect.x, &TileRect.y, &TileRect.w, &TileRect.h) != 4
            || TileRect.w <= 0 || TileRect.h <= 0)
        {
            fprintf(stderr, "bad --tile-rect: %s\n", Argv[4]);
            return 2;
        }
        TileMode = true;
    }
    else if (Argc >= 2)
    {
        RomPath = Argv[1];
    }
    if (RomPath == nullptr)
    {
        fprintf(stderr, "usage: %s <rom file> [--face]\n", argv[0]);
        return 2;
    }

    // A YAML named after the ROM, beside it or under Config/mouse/, is that game's layout.
    // A non-empty PJ64_INPUT_YAML already in the environment wins, and tiles skip the
    // lookup: a mouse layout would replace the keyboard bindings the grid strip broadcasts.
    // That only skips the lookup, though - GridHost execs tiles with the parent
    // environment, so an explicit PJ64_INPUT_YAML still reaches every tile. The branch
    // below loads it quietly to see whether it binds the pointer, and clears it only then,
    // so a tile cannot inherit a mouse layout it would evaluate against its own small
    // window and go inert on; a keyboard YAML passed deliberately across a grid is left
    // alone and keeps working.
    const char * ExplicitLayout = getenv("PJ64_INPUT_YAML");
    if (!TileMode && (ExplicitLayout == nullptr || ExplicitLayout[0] == '\0'))
    {
        char Layout[PATH_MAX];
        if (GameConfigPath(RomPath, ExecutableDirectory().c_str(), Layout, sizeof(Layout)))
        {
            setenv("PJ64_INPUT_YAML", Layout, 1);
            fprintf(stderr, "input layout: %s\n", Layout);
        }
    }
    else if (TileMode && ExplicitLayout != nullptr && ExplicitLayout[0] != '\0')
    {
        InputConfig & Config = InputConfig::Get();
        if (Config.Load(ExplicitLayout, /*Quiet=*/true) && Config.UsesPointer())
        {
            unsetenv("PJ64_INPUT_YAML");
        }
    }

    // A layout that uses the pointer gets the panel: the window grows by its height and the
    // video plugin lifts the game by the same amount, so the game renders unscaled at the
    // top (Design: Docs/superpowers/specs/2026-09-15-mouse-panel-design.md). Tiles never
    // get one. The variable is cleared otherwise, so a value inherited from the caller
    // cannot lift a keyboard run.
    if (!TileMode && LayoutUsesPointer(ExecutableDirectory()))
    {
        TileRect.h += POINTER_PANEL_HEIGHT;
        char Offset[16];
        snprintf(Offset, sizeof(Offset), "%d", POINTER_PANEL_HEIGHT);
        setenv("PJ64_VIEWPORT_OFFSET", Offset, 1);
    }
    else
    {
        unsetenv("PJ64_VIEWPORT_OFFSET");
    }
    if (!TileMode)
    {
        // PJ64_TILE_SIZE is set only in the TileMode branch below, right before the video
        // plugin loads. Clear it here too, so a value inherited from the caller cannot make
        // a non-tile run's video plugin render at a stale tile size while this window is
        // WINDOW_WIDTH x WINDOW_HEIGHT (or that plus the panel, above).
        unsetenv("PJ64_TILE_SIZE");
    }

    SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
    // The emulation thread drives GL. Without this, a context update dispatched from
    // that thread blocks until the main thread processes it, and the main thread is
    // waiting on the emulation thread - a deadlock inside the first buffer swap.
    SDL_SetHint(SDL_HINT_MAC_OPENGL_ASYNC_DISPATCH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Legacy (compatibility) profile: the Glide64 renderer uses fixed-function GL and GLSL 1.10.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    Uint64 WindowFlags = SDL_WINDOW_OPENGL;
    if (TileMode)
    {
        WindowFlags |= SDL_WINDOW_NOT_FOCUSABLE;
    }
    SDL_Window * window = SDL_CreateWindow("Project64", TileRect.w, TileRect.h, WindowFlags);
    if (window == nullptr)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return ShutdownSdl(nullptr, nullptr, 1);
    }
    if (TileMode)
    {
        SDL_SetWindowPosition(window, TileRect.x, TileRect.y);
        // The video plugin renders at PJ64_TILE_SIZE; keep it equal to the window so the
        // game cannot land unscaled in a corner.
        char Size[32];
        snprintf(Size, sizeof(Size), "%dx%d", TileRect.w, TileRect.h);
        setenv("PJ64_TILE_SIZE", Size, 1);
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr)
    {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return ShutdownSdl(window, nullptr, 1);
    }
    // Release the context on the main thread; the emulation thread takes it in GfxThreadInit.
    // Capture the underlying CGL context while it is current here on the main thread;
    // the emulation thread binds it directly rather than going through SDL.
    CGLContextObj cglContext = CGLGetCurrentContext();
    SDL_GL_MakeCurrent(window, nullptr);

    PointerState * pointer = CreatePointerState();
    FaceInject faceInject;
    const bool faceInjecting = ParseFaceInject(&faceInject);
    bool FaceStarted = faceInjecting; // an injected face stands in for the tracker
    if (Face == FaceMode::On && pointer != nullptr && !TileMode && !faceInjecting)
    {
        FaceTrackerStart(pointer);
        FaceStarted = true;
    }
    PointerSample inject;
    const bool injecting = ParsePointerInject(&inject);

    CSdlNotification notify;
    std::string baseDir = ExecutableDirectory();
    if (!AppInit(&notify, baseDir.c_str(), 0, nullptr))
    {
        fprintf(stderr, "AppInit failed\n");
        return ShutdownSdl(window, context, 1);
    }
    ConfigurePlugins();

    CSdlRenderWindow renderWindow(window, context, cglContext, pointer);
    g_Plugins->SetRenderWindows(&renderWindow, nullptr);

    if (!CN64System::RunFileImage(RomPath))
    {
        fprintf(stderr, "Failed to load ROM: %s\n", RomPath);
        AppCleanup();
        return ShutdownSdl(window, context, 1);
    }

    if (TileMode)
    {
        SDL_CreateThread(ParentWatchThread, "pj64-parent-watch", nullptr);
    }

    bool running = true;
    while (running)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT || ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                running = false;
            }
        }
        if (pointer != nullptr)
        {
            PublishMouse(pointer, window, injecting ? &inject : nullptr);
            if (faceInjecting)
            {
                PublishFaceInject(pointer, faceInject);
            }
            // The plugin stores FaceWanted at dylib load, on whichever thread loads it.
            // FaceTrackerStop at the bottom is safe whether or not this ever fires.
            if (!FaceStarted && Face == FaceMode::Auto && !TileMode
                && pointer->FaceWanted.load(std::memory_order_acquire) != 0)
            {
                FaceTrackerStart(pointer);
                FaceStarted = true;
            }
        }
        if (g_BaseSystem == nullptr)
        {
            running = false; // emulation ended on its own
        }
        SDL_Delay(10);
    }

    FaceTrackerStop();
    CN64System::CloseSystem(); // stops the CPU thread and deletes g_BaseSystem
    AppCleanup();
    return ShutdownSdl(window, context, 0);
}
