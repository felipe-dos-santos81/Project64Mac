// Project64 - A Nintendo 64 emulator
// SDL3 audio stream driver for the macOS/SDL frontend
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include "SoundBase.h"
#include <SDL3/SDL.h>
#include <atomic>

class SdlAudioDriver : public SoundDriverBase
{
public:
    SdlAudioDriver();
    ~SdlAudioDriver();

    bool Initialize();
    void SetFrequency(uint32_t Frequency, uint32_t BufferSize);
    void StartAudio();
    void StopAudio();

private:
    static void SDLCALL StreamCallback(void * userdata, SDL_AudioStream * stream, int additional_amount, int total_amount);
    void OpenStream(uint32_t Frequency);
    void CloseStream();
    // When no audio device can be opened, the mixer's ring buffer would never drain and
    // the emulation thread would block forever inside AI_LenChanged. This consumes the
    // ring at roughly real time so the game keeps running, silently.
    void StartSilenceDrain();
    void StopSilenceDrain();
    static int SDLCALL SilenceDrainThread(void * data);
    void SilenceDrainLoop();

    SDL_AudioStream * m_Stream;
    uint32_t m_Frequency;
    uint8_t m_Scratch[MAX_SIZE];
    uint8_t m_DrainScratch[MAX_SIZE];
    SDL_Thread * m_DrainThread;
    std::atomic<bool> m_DrainRunning;
    // Opening a device that cannot start costs seconds inside CoreAudio, and the N64
    // changes sample rate often, so one failure is taken as final.
    bool m_DeviceUnavailable;
};
