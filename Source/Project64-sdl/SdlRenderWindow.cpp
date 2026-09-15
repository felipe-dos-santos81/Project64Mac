#include "SdlRenderWindow.h"
#include "Overlay.h"
#include <OpenGL/gl.h>
#include <Common/PointerState.h>
#include <Common/Trace.h>
#include <Project64-core/TraceModulesProject64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// How often DumpFrame re-checks the back buffer while waiting for a non-black frame.
// Reading every frame would slow the emulation being waited on.
static const uint32_t DumpCheckInterval = 30;

CSdlRenderWindow::CSdlRenderWindow(SDL_Window * Window, SDL_GLContext Context, CGLContextObj Cgl, const PointerState * Pointer) :
    m_Window(Window),
    m_Context(Context),
    m_Cgl(Cgl),
    m_Pointer(Pointer),
    m_OverlayHidden(false),
    m_DumpAt(300),
    m_FrameCount(0),
    m_FrameDumped(false),
    m_MinNonBlack(0.0),
    m_MaxFrame(3600),
    m_BestNonBlack(0.0)
{
    const char * Path = getenv("PJ64_FRAME_DUMP");
    if (Path != nullptr)
    {
        m_DumpPath = Path;
    }
    const char * FrameEnv = getenv("PJ64_FRAME_DUMP_AT");
    if (FrameEnv != nullptr)
    {
        m_DumpAt = (uint32_t)atoi(FrameEnv);
    }
    const char * MinEnv = getenv("PJ64_FRAME_DUMP_MIN_NONBLACK");
    if (MinEnv != nullptr)
    {
        m_MinNonBlack = atof(MinEnv);
    }
    const char * MaxEnv = getenv("PJ64_FRAME_DUMP_MAX");
    if (MaxEnv != nullptr)
    {
        m_MaxFrame = (uint32_t)atoi(MaxEnv);
    }
    const char * OverlayEnv = getenv("PJ64_OVERLAY");
    if (OverlayEnv != nullptr && strcmp(OverlayEnv, "0") == 0)
    {
        m_OverlayHidden = true;
    }
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
//
// With PJ64_FRAME_DUMP_MIN_NONBLACK set, the dump instead waits for the first frame whose
// non-black share clears that percentage, so a boot-time black frame is never the one
// captured. PJ64_FRAME_DUMP_MAX bounds the wait; at the cap the best frame seen is written.
void CSdlRenderWindow::DumpFrame()
{
    if (m_FrameDumped || m_DumpPath.empty())
    {
        return;
    }
    m_FrameCount += 1;
    if (m_FrameCount < m_DumpAt)
    {
        return;
    }

    // No threshold: write the first frame at PJ64_FRAME_DUMP_AT, exactly as before.
    if (m_MinNonBlack <= 0.0)
    {
        std::vector<uint8_t> Pixels;
        int Width = 0, Height = 0;
        if (ReadBackBuffer(Pixels, Width, Height))
        {
            WriteFrame(Pixels, Width, Height);
        }
        return;
    }

    bool AtCap = m_FrameCount >= m_MaxFrame;
    if (!AtCap && (m_FrameCount - m_DumpAt) % DumpCheckInterval != 0)
    {
        return;
    }

    std::vector<uint8_t> Pixels;
    int Width = 0, Height = 0;
    if (!ReadBackBuffer(Pixels, Width, Height))
    {
        return;
    }

    double Share = NonBlackShare(Pixels, Width, Height);
    if (Share >= m_MinNonBlack)
    {
        WriteFrame(Pixels, Width, Height);
        return;
    }
    if (Share > m_BestNonBlack)
    {
        m_BestNonBlack = Share;
        m_BestPixels = Pixels;
    }
    if (AtCap)
    {
        WriteTrace(TraceUserInterface, TraceInfo,
            "Frame cap %u reached with %.1f%% non-black (wanted %.1f%%)",
            (unsigned)m_MaxFrame, Share, m_MinNonBlack);
        WriteFrame(m_BestPixels.empty() ? Pixels : m_BestPixels, Width, Height);
    }
}

bool CSdlRenderWindow::ReadBackBuffer(std::vector<uint8_t> & Pixels, int & Width, int & Height)
{
    GLint Viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, Viewport);
    Width = (int)Viewport[2];
    Height = (int)Viewport[3];
    if (Width <= 0 || Height <= 0)
    {
        return false;
    }
    Pixels.resize((size_t)Width * (size_t)Height * 3);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, Width, Height, GL_RGB, GL_UNSIGNED_BYTE, &Pixels[0]);
    return true;
}

// Share of sampled pixels that are not black, over a sparse grid (every 8th pixel on each
// axis) so the check stays cheap. A pixel is non-black when any channel exceeds 16, which
// ignores low-level dither noise on an otherwise black frame.
double CSdlRenderWindow::NonBlackShare(const std::vector<uint8_t> & Pixels, int Width, int Height) const
{
    const int Stride = 8;
    const uint8_t Threshold = 16;
    size_t Total = 0, NonBlack = 0;
    for (int y = 0; y < Height; y += Stride)
    {
        for (int x = 0; x < Width; x += Stride)
        {
            size_t i = ((size_t)y * (size_t)Width + (size_t)x) * 3;
            Total += 1;
            if (Pixels[i] > Threshold || Pixels[i + 1] > Threshold || Pixels[i + 2] > Threshold)
            {
                NonBlack += 1;
            }
        }
    }
    return Total == 0 ? 0.0 : 100.0 * (double)NonBlack / (double)Total;
}

void CSdlRenderWindow::WriteFrame(const std::vector<uint8_t> & Pixels, int Width, int Height)
{
    const char * Path = m_DumpPath.c_str();
    FILE * File = fopen(Path, "wb");
    if (File == nullptr)
    {
        WriteTrace(TraceUserInterface, TraceError, "Could not open %s for the frame dump", Path);
        m_FrameDumped = true;
        return;
    }
    fprintf(File, "P6\n%d %d\n255\n", Width, Height);
    for (int Row = Height - 1; Row >= 0; Row--) // GL's origin is bottom left, a PPM's is top left
    {
        fwrite(&Pixels[(size_t)Row * (size_t)Width * 3], 1, (size_t)Width * 3, File);
    }
    fclose(File);
    m_FrameDumped = true;
    WriteTrace(TraceUserInterface, TraceInfo, "Wrote frame %u (%dx%d) to %s", (unsigned)m_FrameCount, Width, Height, Path);
}

void CSdlRenderWindow::SwapWindow()
{
    DumpFrame(); // before the flush, while the back buffer still holds this frame

    // After the dump so PJ64_FRAME_DUMP measurements are of the game alone.
    if (m_Pointer != nullptr && !m_OverlayHidden && m_Pointer->OverlayWanted.load(std::memory_order_relaxed) != 0)
    {
        GLint Viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, Viewport);
        // Drawn twice: on this GL 2.1 compatibility context (Apple Silicon, CGL-backed), the
        // first fixed-function draw issued right after the video plugin's shader-based
        // rendering does not reach the framebuffer - confirmed by reading the back buffer
        // back (PJ64_FRAME_DUMP) with the draw temporarily moved before the dump. A second,
        // otherwise-redundant call (each call is self-contained via glPushAttrib/glPopAttrib,
        // so this is safe) reliably makes it visible without disturbing the game's own frame.
        OverlayDraw(m_Pointer, (int)Viewport[2], (int)Viewport[3]);
        OverlayDraw(m_Pointer, (int)Viewport[2], (int)Viewport[3]);
    }

    // SDL_GL_SwapWindow marshals the swap to the main thread on macOS and waits for it.
    // The context is current on this thread, so the main thread blocks trying to flush a
    // context it does not own, and this thread waits on the main thread: a deadlock on the
    // very first swap. Presenting is exactly CGLFlushDrawable on the context that is
    // current here, so do that directly and leave SDL out of it.
    // m_Cgl was captured on the main thread and bound here by GfxThreadInit, so it is
    // already the current context - no need to ask GL for it again every frame.
    if (m_Cgl != nullptr)
    {
        CGLFlushDrawable(m_Cgl);
        return;
    }
    // No context current on this thread. SDL_GL_SwapWindow is the only option left, but
    // it is also what deadlocks when called off the main thread, so say so rather than
    // hanging silently.
    WriteTrace(TraceUserInterface, TraceError, "SwapWindow with no current GL context; falling back to SDL_GL_SwapWindow");
    SDL_GL_SwapWindow(m_Window);
}
