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

// Held face gestures, one bit each, in the order of the label table, the overlay strip and
// the YAML names below. Left and right are the player's own.
enum PointerGesture : uint32_t
{
    POINTER_GESTURE_EYEBROWS   = 1u << 0,
    POINTER_GESTURE_HEAD_LEFT  = 1u << 1,
    POINTER_GESTURE_HEAD_RIGHT = 1u << 2,
    POINTER_GESTURE_HEAD_UP    = 1u << 3,
    POINTER_GESTURE_HEAD_DOWN  = 1u << 4,
    POINTER_GESTURE_TILT_LEFT  = 1u << 5,
    POINTER_GESTURE_TILT_RIGHT = 1u << 6,
    POINTER_GESTURE_MOUTH_OPEN = 1u << 7,
    POINTER_GESTURE_SMILE      = 1u << 8,
    POINTER_GESTURE_WINK_LEFT  = 1u << 9,
    POINTER_GESTURE_WINK_RIGHT = 1u << 10,
};

// The four gestures a head stick consumes: a layout cannot bind them beside {stick: head}.
#define POINTER_GESTURE_HEAD_DIRECTIONS \
    (POINTER_GESTURE_HEAD_LEFT | POINTER_GESTURE_HEAD_RIGHT | POINTER_GESTURE_HEAD_UP | POINTER_GESTURE_HEAD_DOWN)

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
#define POINTER_GESTURE_COUNT 11

// What the frontend samples on its main thread. X,Y in launch-size pixels from the top
// left -- the size the window was created at; PointerFitToBase (PointerLayout.h) maps a
// resized or full-screen window's real cursor back into them. Inside is "the window has
// mouse focus and the cursor is over the picture": a cursor over a full-screen or
// maximised bar is not inside. Grid tiles and injected samples never resize, so their
// launch size is just the window size.
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
    std::atomic<int32_t> HeadX;          // head stick, -80..80, written by the tracker every frame
    std::atomic<int32_t> HeadY;
    // Plugin at load -> overlay and frontend. Written before the ROM opens.
    std::atomic<uint32_t> OverlayWanted;
    std::atomic<uint32_t> FaceWanted;    // 1 when the layout binds a face gesture
    std::atomic<uint32_t> HeadStickWanted; // 1 when Stick is {stick: head} or {stick: head-digital}
    std::atomic<uint32_t> ToggleZones;   // one bit per zone: the toggle slots and the hold slot (corner mark)
    char Labels[POINTER_ZONE_COUNT][POINTER_LABEL_SIZE];
    char GestureLabels[POINTER_GESTURE_COUNT][POINTER_LABEL_SIZE];
    // Plugin each GetKeys -> overlay.
    std::atomic<int32_t> LatchedZone;    // POINTER_ZONE_NONE when nothing is held
    std::atomic<int32_t> Quadrant;       // PointerQuadrant of the stick the game got; -1 neutral
    std::atomic<uint32_t> ToggledZones;  // one bit per zone: toggles that are on, and the hold slot while holding
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

// YAML name of gesture bit 1 << Index; "" out of range.
inline const char * PointerGestureName(int Index)
{
    static const char * const kNames[POINTER_GESTURE_COUNT] = {
        "eyebrows", "head-left", "head-right", "head-up", "head-down",
        "tilt-left", "tilt-right", "mouth-open", "smile", "wink-left", "wink-right",
    };
    return (Index >= 0 && Index < POINTER_GESTURE_COUNT) ? kNames[Index] : "";
}

// Two-glyph overlay tag of gesture bit 1 << Index; "" out of range.
inline const char * PointerGestureTag(int Index)
{
    static const char * const kTags[POINTER_GESTURE_COUNT] = {
        "Br", "H<", "H>", "H^", "Hv", "T<", "T>", "Mo", "Sm", "W<", "W>",
    };
    return (Index >= 0 && Index < POINTER_GESTURE_COUNT) ? kTags[Index] : "";
}

inline uint32_t PointerGestureFromName(const char * Name)
{
    for (int i = 0; i < POINTER_GESTURE_COUNT; i++)
    {
        if (strcmp(Name, PointerGestureName(i)) == 0) return 1u << i;
    }
    return 0;
}

// Index 0..10 of a single gesture bit, for the GestureLabels table. Callers pass one valid
// bit; anything else maps to 0 so a table write can never go out of range.
inline int PointerGestureIndex(uint32_t Bit)
{
    for (int i = 0; i < POINTER_GESTURE_COUNT; i++)
    {
        if (Bit == (1u << i)) return i;
    }
    return 0;
}
