#include "SdlRenderWindow.h"
#include <OpenGL/OpenGL.h> // CGLGetCurrentContext, CGLFlushDrawable
#include <OpenGL/gl.h>
#include <Common/Trace.h>
#include <Project64-core/TraceModulesProject64.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

CSdlRenderWindow::CSdlRenderWindow(SDL_Window * Window, SDL_GLContext Context, CGLContextObj Cgl) :
    m_Window(Window),
    m_Context(Context),
    m_Cgl(Cgl),
    m_FrameCount(0),
    m_FrameDumped(false)
{
}

void CSdlRenderWindow::GfxThreadInit()
{
    // Bind the context to this thread directly. SDL_GL_MakeCurrent is documented main-thread
    // only and marshals there, which is the same contract that made the buffer swap deadlock.
    if (m_Cgl != nullptr && CGLSetCurrentContext(m_Cgl) == kCGLNoError)
    {
        return;
    }
    if (!SDL_GL_MakeCurrent(m_Window, m_Context))
    {
        WriteTrace(TraceUserInterface, TraceError, "SDL_GL_MakeCurrent failed: %s", SDL_GetError());
    }
}

void CSdlRenderWindow::GfxThreadDone()
{
    if (m_Cgl != nullptr)
    {
        CGLSetCurrentContext(nullptr);
        return;
    }
    SDL_GL_MakeCurrent(m_Window, nullptr);
}

// Writes one frame to the file named by PJ64_FRAME_DUMP, as a binary PPM, once the frame
// counter reaches PJ64_FRAME_DUMP_AT (default 300, late enough to pass the black frames a
// game shows while it boots). This frontend has no UI, so reading the back buffer is the
// only way to see what was drawn without capturing the whole screen.
void CSdlRenderWindow::DumpFrame()
{
    const char * Path = getenv("PJ64_FRAME_DUMP");
    if (Path == nullptr || Path[0] == '\0' || m_FrameDumped)
    {
        return;
    }
    const char * FrameEnv = getenv("PJ64_FRAME_DUMP_AT");
    uint32_t DumpAt = FrameEnv != nullptr ? (uint32_t)atoi(FrameEnv) : 300;
    m_FrameCount += 1;
    if (m_FrameCount < DumpAt)
    {
        return;
    }

    GLint Viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, Viewport);
    GLint Width = Viewport[2], Height = Viewport[3];
    if (Width <= 0 || Height <= 0)
    {
        return;
    }

    std::vector<uint8_t> Pixels((size_t)Width * (size_t)Height * 3);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, Width, Height, GL_RGB, GL_UNSIGNED_BYTE, &Pixels[0]);

    FILE * File = fopen(Path, "wb");
    if (File == nullptr)
    {
        WriteTrace(TraceUserInterface, TraceError, "Could not open %s for the frame dump", Path);
        m_FrameDumped = true;
        return;
    }
    fprintf(File, "P6\n%d %d\n255\n", (int)Width, (int)Height);
    for (GLint Row = Height - 1; Row >= 0; Row--) // GL's origin is bottom left, a PPM's is top left
    {
        fwrite(&Pixels[(size_t)Row * (size_t)Width * 3], 1, (size_t)Width * 3, File);
    }
    fclose(File);
    m_FrameDumped = true;
    WriteTrace(TraceUserInterface, TraceInfo, "Wrote frame %u (%dx%d) to %s", (unsigned)m_FrameCount, (int)Width, (int)Height, Path);
}

void CSdlRenderWindow::SwapWindow()
{
    DumpFrame(); // before the flush, while the back buffer still holds this frame

    // SDL_GL_SwapWindow marshals the swap to the main thread on macOS and waits for it.
    // The context is current on this thread, so the main thread blocks trying to flush a
    // context it does not own, and this thread waits on the main thread: a deadlock on the
    // very first swap. Presenting is exactly CGLFlushDrawable on the context that is
    // current here, so do that directly and leave SDL out of it.
    CGLContextObj cgl = CGLGetCurrentContext();
    if (cgl != nullptr)
    {
        CGLFlushDrawable(cgl);
        return;
    }
    // No context current on this thread. SDL_GL_SwapWindow is the only option left, but
    // it is also what deadlocks when called off the main thread, so say so rather than
    // hanging silently.
    WriteTrace(TraceUserInterface, TraceError, "SwapWindow with no current GL context; falling back to SDL_GL_SwapWindow");
    SDL_GL_SwapWindow(m_Window);
}
