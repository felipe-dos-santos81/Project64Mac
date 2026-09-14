// Project64 - A Nintendo 64 emulator
// Shared by the grid orchestrator (only writer) and the input plugin (readers).
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <SDL3/SDL.h>
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
    memcpy(Keys->Keys, State, sizeof Keys->Keys);
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
        memcpy(Out, Keys->Keys, sizeof Keys->Keys);
        if (Keys->Seq == Before)
        {
            return;
        }
    }
}
