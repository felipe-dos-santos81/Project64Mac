// Project64 - A Nintendo 64 emulator
// Turns two face measures into held gesture bits. Pure C++ so it can be tested without a
// camera; FaceTracker.mm feeds it one sample per frame.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <Common/PointerState.h>
#include <stdint.h>

struct GestureSample
{
    bool FaceFound;
    float BrowHeight;   // eyebrow mean y minus eye mean y, face-box-normalised units
    float Yaw;          // radians; negative is head-left
    double Time;        // seconds, monotonic
};

struct GestureThresholds
{
    float Brow = 0.035f;            // rise above baseline that sets eyebrows
    float Yaw = 0.25f;              // radians from baseline that set a head bit
    float ReleaseFraction = 0.6f;   // release below this fraction of the set threshold
    double BaselineSeconds = 5.0;   // EMA time constant for the rest baseline
    int DebounceFrames = 2;         // consecutive frames to set, and to clear
    double NoFaceSeconds = 0.5;     // clear everything after this long without a face
};

class GestureClassifier
{
public:
    explicit GestureClassifier(const GestureThresholds & Thresholds);

    // Returns the current PointerGesture bits.
    uint32_t Update(const GestureSample & Sample);

    float BrowBaseline() const { return m_BrowBaseline; }
    float YawBaseline() const { return m_YawBaseline; }

private:
    // One held bit with debounce and hysteresis.
    struct Channel
    {
        bool Active = false;
        int Count = 0;
        // Raw true means the measure is beyond the set level (or, while active, still
        // above the release level).
        void Step(bool Raw, int Debounce);
    };

    void Track(float & Baseline, float Value, double Dt, bool Frozen);

    GestureThresholds m_T;
    bool m_HaveBaseline;
    float m_BrowBaseline;
    float m_YawBaseline;
    double m_LastTime;
    double m_LastFaceTime;
    Channel m_Brows, m_Left, m_Right;
};
