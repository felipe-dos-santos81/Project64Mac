// Project64 - A Nintendo 64 emulator
// Shared between the SDL frontend (mouse and face writers), the input plugin (evaluator)
// and the overlay (reader). One writer per field group; see the table in the design spec.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <atomic>
#include <stdint.h>
#include <string.h>

// Environment variable holding the descriptor number of a mapped PointerState. The
// frontend sets it before the plugins load; it is absent under any other frontend.
#define PJ64_POINTER_ENV "PJ64_POINTER_FD"

enum PointerGesture : uint32_t
{
    POINTER_GESTURE_EYEBROWS = 1u << 0,
    POINTER_GESTURE_HEAD_LEFT = 1u << 1,
    POINTER_GESTURE_HEAD_RIGHT = 1u << 2,
};

enum FaceStatus : uint32_t
{
    FACE_OFF = 0,
    FACE_STARTING,
    FACE_TRACKING,
    FACE_NO_FACE,
    FACE_DENIED,
    FACE_ERROR,
};

#define POINTER_ZONE_COUNT 14   // 13 panel slots + the game image; see PointerLayout.h
#define POINTER_LABEL_SIZE 3    // up to two characters plus NUL
#define POINTER_GESTURE_COUNT 3

// What the frontend samples on its main thread. X,Y in window pixels from the top left;
// Inside is "cursor over this window and the window has mouse focus".
struct PointerSample
{
    float X, Y;
    int32_t W, H;
    bool Inside;
    bool Button;
};

struct PointerState
{
    // Frontend main loop -> plugin. Seq is a seqlock as in GridKeys.
    volatile uint32_t Seq;
    PointerSample Sample;
    // Face tracker -> plugin and overlay.
    std::atomic<uint32_t> Gestures;      // PointerGesture bits
    std::atomic<uint32_t> Face;          // FaceStatus
    // Plugin at load -> overlay and frontend. Written before the ROM opens.
    std::atomic<uint32_t> OverlayWanted;
    std::atomic<uint32_t> FaceWanted;    // 1 when the layout binds a face gesture
    char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE];
    char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE];
    // Plugin each GetKeys -> overlay.
    std::atomic<int32_t> LatchedZone;    // POINTER_ZONE_NONE when nothing is held
    std::atomic<int32_t> Quadrant;       // PointerQuadrant of the stick the game got; -1 neutral
};

inline void PointerPublish(PointerState * State, const PointerSample & Sample)
{
    State->Seq = State->Seq + 1; // odd: a write is in flight
    std::atomic_thread_fence(std::memory_order_release);
    State->Sample = Sample;
    std::atomic_thread_fence(std::memory_order_release);
    State->Seq = State->Seq + 1; // even: stable
}

inline void PointerSnapshot(const PointerState * State, PointerSample * Out)
{
    for (;;)
    {
        uint32_t Before = State->Seq;
        if ((Before & 1u) != 0)
        {
            continue;
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        *Out = State->Sample;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (State->Seq == Before)
        {
            return;
        }
    }
}

inline uint32_t PointerGestureFromName(const char * Name)
{
    if (strcmp(Name, "eyebrows") == 0) return POINTER_GESTURE_EYEBROWS;
    if (strcmp(Name, "head-left") == 0) return POINTER_GESTURE_HEAD_LEFT;
    if (strcmp(Name, "head-right") == 0) return POINTER_GESTURE_HEAD_RIGHT;
    return 0;
}

// Index 0..2 of a single gesture bit, for the GestureLabels table.
inline int PointerGestureIndex(uint32_t Bit)
{
    if (Bit == POINTER_GESTURE_EYEBROWS) return 0;
    if (Bit == POINTER_GESTURE_HEAD_LEFT) return 1;
    return 2;
}
