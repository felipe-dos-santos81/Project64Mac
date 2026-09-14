// Project64 - A Nintendo 64 emulator
// Shared by the grid orchestrator (only writer) and the input plugin (readers).
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <stdint.h>
#include <string.h>

// Environment variable holding the descriptor number of a mapped GridKeys. The
// orchestrator sets it on each tile; it is absent for a normal single-ROM run.
#define PJ64_GRID_KEYS_ENV "PJ64_GRID_KEYS_FD"

// Seq is a seqlock: odd while a write is in flight, even when Keys is stable.
struct GridKeys
{
    volatile uint32_t Seq;
    bool Keys[SDL_SCANCODE_COUNT];
};

inline void GridKeysPublish(GridKeys * Keys, const bool * State)
{
    Keys->Seq = Keys->Seq + 1; // odd: a write is in flight
    std::atomic_thread_fence(std::memory_order_release); // Seq=odd before the data stores
    memcpy(Keys->Keys, State, sizeof Keys->Keys);
    std::atomic_thread_fence(std::memory_order_release); // data stores before Seq=even
    Keys->Seq = Keys->Seq + 1; // even: stable
}

inline void GridKeysSnapshot(const GridKeys * Keys, bool * Out)
{
    for (;;)
    {
        uint32_t Before = Keys->Seq;
        if ((Before & 1u) != 0)
        {
            continue; // a writer is mid-publish
        }
        std::atomic_thread_fence(std::memory_order_acquire); // Seq=even before the data loads
        memcpy(Out, Keys->Keys, sizeof Keys->Keys);
        std::atomic_thread_fence(std::memory_order_acquire); // data loads before re-reading Seq
        if (Keys->Seq == Before)
        {
            return;
        }
    }
}
