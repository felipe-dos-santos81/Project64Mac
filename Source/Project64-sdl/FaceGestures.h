// Project64 - A Nintendo 64 emulator
// Turns two face measures into held gesture bits. Pure C++ so it can be tested without a
// camera; FaceTracker.mm feeds it one sample per frame.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#pragma once
#include <Common/PointerState.h>
#include <stdint.h>

// The measures the tracker derives each frame. Landmark measures are in Vision's
// face-box-normalised units; the three angles are radians.
enum FaceMeasure
{
    FACE_BROW,       // eyebrow mean y minus eye mean y
    FACE_YAW,        // negative is the player's left
    FACE_PITCH,      // positive is nose up
    FACE_ROLL,       // negative is left ear down
    FACE_MOUTH,      // inner-lip vertical extent
    FACE_SMILE,      // outer-lip horizontal extent
    FACE_EYE_LEFT,   // eye height over eye width
    FACE_EYE_RIGHT,
    FACE_MEASURE_COUNT
};

struct GestureSample
{
    bool FaceFound;
    float M[FACE_MEASURE_COUNT];
    double Time;        // seconds, monotonic
};

struct GestureThresholds
{
    float Brow = 0.035f;            // rise above baseline that sets eyebrows
    float Yaw = 0.25f;              // radians from baseline that set a head-left/right bit
    float Pitch = 0.20f;            // radians from baseline that set head-up/down
    float Roll = 0.25f;             // radians from baseline that set tilt-left/right
    float Mouth = 0.06f;            // inner-lip gap above baseline that sets mouth-open
    float Smile = 0.05f;            // lip width above baseline that sets smile
    float Eye = 0.12f;              // aperture drop below baseline that sets a wink
    float StickYaw = 0.26f;         // radians of yaw for full head-stick tilt
    float StickPitch = 0.17f;       // radians of pitch for full head-stick tilt
    float ReleaseFraction = 0.6f;   // release below this fraction of the set threshold
    double BaselineSeconds = 5.0;   // EMA time constant for the rest baseline
    int DebounceFrames = 2;         // consecutive frames to set, and to clear
    double NoFaceSeconds = 0.5;     // clear everything after this long without a face
};

class GestureClassifier
{
public:
    explicit GestureClassifier(const GestureThresholds & Thresholds);

    // Returns the current PointerGesture bits and updates the head stick. HeadStickInUse
    // (the layout binds {stick: head} or head-digital) makes the yaw and pitch baselines
    // hold while the stick is outside its dead zone.
    uint32_t Update(const GestureSample & Sample, bool HeadStickInUse = false);

    // Head stick after the last Update, -80..80; left negative, nose-up positive.
    int8_t StickX() const { return m_StickX; }
    int8_t StickY() const { return m_StickY; }

    float Baseline(FaceMeasure Measure) const { return m_Baseline[Measure]; }
    float BrowBaseline() const { return m_Baseline[FACE_BROW]; }
    float YawBaseline() const { return m_Baseline[FACE_YAW]; }

    // Forgets the resting pose: the next sample with a face becomes the new rest, as at
    // start-up. The emulator actions menu's Recentre.
    void Rebaseline() { m_HaveBaseline = false; }

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
    float Threshold(FaceMeasure Measure) const;
    uint32_t Bits() const;

    GestureThresholds m_T;
    bool m_HaveBaseline;
    float m_Baseline[FACE_MEASURE_COUNT];
    double m_LastTime;
    double m_LastFaceTime;
    Channel m_Channel[POINTER_GESTURE_COUNT];
    int8_t m_StickX, m_StickY;
};
