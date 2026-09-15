#pragma once
#include <Project64-core/Plugins/Plugin.h>
#include <SDL3/SDL.h>
#include <OpenGL/OpenGL.h>
#include <string>
#include <vector>

struct PointerState;

// Owns nothing; the SDL_Window and SDL_GLContext belong to main().
// GfxThreadInit/SwapWindow/GfxThreadDone are called by the core on the
// emulation thread, matching the Android bridge's use of these hooks.
class CSdlRenderWindow : public RenderWindow
{
public:
    CSdlRenderWindow(SDL_Window * Window, SDL_GLContext Context, CGLContextObj Cgl, const PointerState * Pointer);

    void GfxThreadInit();
    void GfxThreadDone();
    void SwapWindow();

private:
    void DumpFrame();
    bool ReadBackBuffer(std::vector<uint8_t> & Pixels, int & Width, int & Height);
    double NonBlackShare(const std::vector<uint8_t> & Pixels, int Width, int Height) const;
    void WriteFrame(const std::vector<uint8_t> & Pixels, int Width, int Height);

    SDL_Window * m_Window;
    SDL_GLContext m_Context;
    // Captured on the main thread while the context was current there. SDL3 documents
    // SDL_GL_MakeCurrent as main-thread-only, and it marshals, so the emulation thread
    // binds the underlying CGL context itself instead.
    CGLContextObj m_Cgl;
    // The overlay's data, owned by main(); null under a failed shm setup. PJ64_OVERLAY=0
    // hides the overlay for a player who has memorised the layout.
    const PointerState * m_Pointer;
    bool m_OverlayHidden;
    // Frame dumping, driven by PJ64_FRAME_DUMP; see DumpFrame. Both environment
    // variables are read once at construction - they cannot change mid-run, and
    // DumpFrame is on the per-frame present path.
    std::string m_DumpPath;
    uint32_t m_DumpAt;
    uint32_t m_FrameCount;
    bool m_FrameDumped;
    // Non-black wait, driven by PJ64_FRAME_DUMP_MIN_NONBLACK. 0 keeps the old
    // write-once-at-DumpAt behavior; PJ64_FRAME_DUMP_MAX caps the wait.
    double m_MinNonBlack;
    uint32_t m_MaxFrame;
    double m_BestNonBlack;
    std::vector<uint8_t> m_BestPixels;
};
