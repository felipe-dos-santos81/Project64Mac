// Project64 - A Nintendo 64 emulator
// SDL3 audio stream driver for the macOS/SDL frontend
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "SdlAudio.h"
#include <Project64-audio/trace.h>
#include <Common/CriticalSection.h>
#include <string.h>


SdlAudioDriver::SdlAudioDriver() :
    m_Stream(nullptr),
    m_Frequency(44100),
    m_DrainRunning(false),
    m_DrainThread(nullptr),
    m_DeviceUnavailable(false)
{
    WriteTrace(TraceAudioInitShutdown, TraceDebug, "Start");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        WriteTrace(TraceAudioInitShutdown, TraceError, "SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
    }
    WriteTrace(TraceAudioInitShutdown, TraceDebug, "Done");
}

SdlAudioDriver::~SdlAudioDriver()
{
    StopSilenceDrain();
    CloseStream();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool SdlAudioDriver::Initialize()
{
    if (!SoundDriverBase::Initialize())
    {
        return false;
    }
    OpenStream(m_Frequency);
    return m_Stream != nullptr;
}

void SdlAudioDriver::OpenStream(uint32_t Frequency)
{
    CloseStream();
    if (m_DeviceUnavailable)
    {
        StartSilenceDrain();
        return;
    }
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = (int)Frequency;
    m_Stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, StreamCallback, this);
    if (m_Stream == nullptr)
    {
        WriteTrace(TraceAudioInitShutdown, TraceError, "SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        m_DeviceUnavailable = true;
        StartSilenceDrain();
        return;
    }
    StopSilenceDrain();
    WriteTrace(TraceAudioInitShutdown, TraceDebug, "Opened SDL audio stream at %u Hz", Frequency);
}

void SdlAudioDriver::CloseStream()
{
    if (m_Stream != nullptr)
    {
        SDL_DestroyAudioStream(m_Stream); // also closes the device it opened
        m_Stream = nullptr;
    }
}

void SdlAudioDriver::SetFrequency(uint32_t Frequency, uint32_t /*BufferSize*/)
{
    WriteTrace(TraceAudioInitShutdown, TraceDebug, "Frequency: %u", Frequency);
    if (Frequency == 0 || Frequency == m_Frequency)
    {
        return;
    }
    m_Frequency = Frequency;
    bool wasPlaying = m_Stream != nullptr && !SDL_AudioStreamDevicePaused(m_Stream);
    OpenStream(m_Frequency);
    if (wasPlaying)
    {
        StartAudio();
    }
}

void SdlAudioDriver::StartAudio()
{
    if (m_Stream != nullptr)
    {
        SDL_ResumeAudioStreamDevice(m_Stream);
    }
}

void SdlAudioDriver::StopAudio()
{
    if (m_Stream != nullptr)
    {
        SDL_PauseAudioStreamDevice(m_Stream);
    }
}

// Runs on SDL's audio thread whenever the device needs more data.
void SDLCALL SdlAudioDriver::StreamCallback(void * userdata, SDL_AudioStream * stream, int additional_amount, int /*total_amount*/)
{
    SdlAudioDriver * _this = (SdlAudioDriver *)userdata;
    if (additional_amount <= 0)
    {
        return;
    }
    uint32_t bytes = (uint32_t)additional_amount;
    if (bytes > MAX_SIZE)
    {
        bytes = MAX_SIZE;
    }
    bytes &= ~3u; // whole 16-bit stereo frames
    {
        CGuard guard(_this->m_CS);
        _this->LoadAiBuffer(_this->m_Scratch, bytes);
    }
    SDL_PutAudioStreamData(stream, _this->m_Scratch, (int)bytes);
}

void SdlAudioDriver::StartSilenceDrain()
{
    if (m_DrainThread != nullptr)
    {
        return;
    }
    WriteTrace(TraceAudioInitShutdown, TraceWarning, "No audio device; draining silently so emulation continues");
    m_DrainRunning.store(true);
    m_DrainThread = SDL_CreateThread(SilenceDrainThread, "pj64-audio-drain", this);
    if (m_DrainThread == nullptr)
    {
        WriteTrace(TraceAudioInitShutdown, TraceError, "SDL_CreateThread failed: %s", SDL_GetError());
        m_DrainRunning.store(false);
    }
}

void SdlAudioDriver::StopSilenceDrain()
{
    m_DrainRunning.store(false);
    if (m_DrainThread != nullptr)
    {
        SDL_WaitThread(m_DrainThread, nullptr);
        m_DrainThread = nullptr;
    }
}

int SDLCALL SdlAudioDriver::SilenceDrainThread(void * data)
{
    ((SdlAudioDriver *)data)->SilenceDrainLoop();
    return 0;
}

void SdlAudioDriver::SilenceDrainLoop()
{
    {
        // AI_LenChanged waits while m_BufferRemaining == m_MaxBufferSize. When the game has
        // not set a buffer size yet both are zero, which reads as "full" forever and wedges
        // the emulation thread before it can ever write audio. With no device to pace
        // against, give it the whole scratch size so the comparison can move.
        //
        // Done once here rather than every pass: AI_LenChanged reads m_MaxBufferSize without
        // holding m_CS, so storing it repeatedly would race with that read on every
        // iteration. One store keeps the window as small as it can be without changing the
        // shared base class.
        CGuard guard(m_CS);
        if (m_MaxBufferSize == 0)
        {
            m_MaxBufferSize = MAX_SIZE;
        }
    }
    while (m_DrainRunning.load())
    {
        // Empty the whole ring every pass. AI_LenChanged blocks the emulation thread while
        // the ring is full, and with no device there is nothing to pace against, so the ring
        // must never be allowed to stay full. The emulator has its own speed limiter.
        uint32_t bytes = m_MaxBufferSize;
        if (bytes > MAX_SIZE)
        {
            bytes = MAX_SIZE;
        }
        bytes &= ~3u;
        if (bytes != 0)
        {
            CGuard guard(m_CS);
            LoadAiBuffer(m_DrainScratch, bytes);
        }
        SDL_Delay(1);
    }
}
