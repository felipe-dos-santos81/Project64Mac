// Project64 - A Nintendo 64 emulator
// SDL3 frontend for macOS. Windowed only; the cursor is never grabbed or hidden.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "SdlNotification.h"
#include "SdlRenderWindow.h"
#include "GridHost.h"
#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/SettingsID.h>
#include <SDL3/SDL.h>
#include <OpenGL/OpenGL.h>
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
    SDL_Rect TileRect = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    const char * RomPath = nullptr;
    if (argc >= 2 && strcmp(argv[1], "--tile") == 0)
    {
        if (argc < 5 || strcmp(argv[3], "--tile-rect") != 0)
        {
            fprintf(stderr, "usage: %s --tile <rom> --tile-rect X,Y,W,H\n", argv[0]);
            return 2;
        }
        RomPath = argv[2];
        if (sscanf(argv[4], "%d,%d,%d,%d", &TileRect.x, &TileRect.y, &TileRect.w, &TileRect.h) != 4
            || TileRect.w <= 0 || TileRect.h <= 0)
        {
            fprintf(stderr, "bad --tile-rect: %s\n", argv[4]);
            return 2;
        }
        TileMode = true;
    }
    else if (argc >= 2)
    {
        RomPath = argv[1];
    }
    if (RomPath == nullptr)
    {
        fprintf(stderr, "usage: %s <rom file>\n", argv[0]);
        return 2;
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

    CSdlNotification notify;
    std::string baseDir = ExecutableDirectory();
    if (!AppInit(&notify, baseDir.c_str(), 0, nullptr))
    {
        fprintf(stderr, "AppInit failed\n");
        return ShutdownSdl(window, context, 1);
    }
    ConfigurePlugins();

    CSdlRenderWindow renderWindow(window, context, cglContext);
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
        if (g_BaseSystem == nullptr)
        {
            running = false; // emulation ended on its own
        }
        SDL_Delay(10);
    }

    CN64System::CloseSystem(); // stops the CPU thread and deletes g_BaseSystem
    AppCleanup();
    return ShutdownSdl(window, context, 0);
}
