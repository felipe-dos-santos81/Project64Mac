#pragma once
#include <Project64-core/Plugins/Plugin.h>
#include <SDL3/SDL.h>
#include <OpenGL/OpenGL.h>

// Owns nothing; the SDL_Window and SDL_GLContext belong to main().
// GfxThreadInit/SwapWindow/GfxThreadDone are called by the core on the
// emulation thread, matching the Android bridge's use of these hooks.
class CSdlRenderWindow : public RenderWindow
{
public:
    CSdlRenderWindow(SDL_Window * Window, SDL_GLContext Context, CGLContextObj Cgl);

    void GfxThreadInit();
    void GfxThreadDone();
    void SwapWindow();

private:
    void DumpFrame();

    SDL_Window * m_Window;
    SDL_GLContext m_Context;
    // Captured on the main thread while the context was current there. SDL3 documents
    // SDL_GL_MakeCurrent as main-thread-only, and it marshals, so the emulation thread
    // binds the underlying CGL context itself instead.
    CGLContextObj m_Cgl;
    // Frame dumping, driven by PJ64_FRAME_DUMP; see DumpFrame.
    uint32_t m_FrameCount;
    bool m_FrameDumped;
};
