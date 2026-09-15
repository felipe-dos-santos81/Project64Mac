// Project64 - A Nintendo 64 emulator
// GestureClassifier: rest baseline, hysteresis, debounce, no-face timeout.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "FaceGestures.h"

// One entry per gesture bit, in bit order: which measure drives it and which way.
struct ChannelSpec
{
    FaceMeasure Measure;
    float Sign;     // +1 fires above baseline, -1 below
};

static const ChannelSpec kChannels[POINTER_GESTURE_COUNT] = {
    { FACE_BROW, +1.0f },       // eyebrows
    { FACE_YAW, -1.0f },        // head-left
    { FACE_YAW, +1.0f },        // head-right
    { FACE_PITCH, +1.0f },      // head-up
    { FACE_PITCH, -1.0f },      // head-down
    { FACE_ROLL, -1.0f },       // tilt-left
    { FACE_ROLL, +1.0f },       // tilt-right
    { FACE_MOUTH, +1.0f },      // mouth-open
    { FACE_SMILE, +1.0f },      // smile
    { FACE_EYE_LEFT, -1.0f },   // wink-left
    { FACE_EYE_RIGHT, -1.0f },  // wink-right
};

GestureClassifier::GestureClassifier(const GestureThresholds & Thresholds) :
    m_T(Thresholds),
    m_HaveBaseline(false),
    m_LastTime(0.0),
    m_LastFaceTime(-1e9),
    m_StickX(0),
    m_StickY(0)
{
    for (int i = 0; i < FACE_MEASURE_COUNT; i++) m_Baseline[i] = 0.0f;
}

void GestureClassifier::Channel::Step(bool Raw, int Debounce)
{
    if (Raw == Active)
    {
        Count = 0;
        return;
    }
    Count++;
    if (Count >= Debounce)
    {
        Active = Raw;
        Count = 0;
    }
}

// Exponential moving average with time constant BaselineSeconds. Frozen while a gesture
// that uses this baseline is held, so a long hold never becomes the new rest.
void GestureClassifier::Track(float & Baseline, float Value, double Dt, bool Frozen)
{
    if (Frozen || Dt <= 0.0)
    {
        return;
    }
    double Alpha = Dt / m_T.BaselineSeconds;
    if (Alpha > 1.0) Alpha = 1.0;
    Baseline = (float)(Baseline + (Value - Baseline) * Alpha);
}

float GestureClassifier::Threshold(FaceMeasure Measure) const
{
    switch (Measure)
    {
    case FACE_BROW: return m_T.Brow;
    case FACE_YAW: return m_T.Yaw;
    case FACE_PITCH: return m_T.Pitch;
    case FACE_ROLL: return m_T.Roll;
    case FACE_MOUTH: return m_T.Mouth;
    case FACE_SMILE: return m_T.Smile;
    default: return m_T.Eye;   // both eyes
    }
}

uint32_t GestureClassifier::Bits() const
{
    uint32_t Out = 0;
    for (int g = 0; g < POINTER_GESTURE_COUNT; g++)
    {
        if (m_Channel[g].Active) Out |= 1u << g;
    }
    return Out;
}

uint32_t GestureClassifier::Update(const GestureSample & S, bool /*HeadStickInUse*/)
{
    const double Dt = m_HaveBaseline ? S.Time - m_LastTime : 0.0;
    m_LastTime = S.Time;

    if (!S.FaceFound)
    {
        if (S.Time - m_LastFaceTime > m_T.NoFaceSeconds)
        {
            for (int g = 0; g < POINTER_GESTURE_COUNT; g++) m_Channel[g] = Channel();
            m_StickX = 0;
            m_StickY = 0;
        }
        return Bits();
    }
    m_LastFaceTime = S.Time;

    if (!m_HaveBaseline)
    {
        m_HaveBaseline = true;
        for (int i = 0; i < FACE_MEASURE_COUNT; i++) m_Baseline[i] = S.M[i];
    }

    // Raw conditions first, so the winks can look at the other eye's state before any
    // channel steps.
    bool Raw[POINTER_GESTURE_COUNT];
    for (int g = 0; g < POINTER_GESTURE_COUNT; g++)
    {
        const ChannelSpec & Spec = kChannels[g];
        const float Set = Threshold(Spec.Measure);
        const float Release = Set * m_T.ReleaseFraction;
        const float Delta = (S.M[Spec.Measure] - m_Baseline[Spec.Measure]) * Spec.Sign;
        Raw[g] = Delta > (m_Channel[g].Active ? Release : Set);
    }
    // A blink is not a wink: each wink needs the other eye open, meaning not closed past
    // its own release level.
    const float EyeRelease = m_T.Eye * m_T.ReleaseFraction;
    const bool LeftClosed = (m_Baseline[FACE_EYE_LEFT] - S.M[FACE_EYE_LEFT]) > EyeRelease;
    const bool RightClosed = (m_Baseline[FACE_EYE_RIGHT] - S.M[FACE_EYE_RIGHT]) > EyeRelease;
    const int WinkLeft = PointerGestureIndex(POINTER_GESTURE_WINK_LEFT);
    const int WinkRight = PointerGestureIndex(POINTER_GESTURE_WINK_RIGHT);
    Raw[WinkLeft] = Raw[WinkLeft] && !RightClosed;
    Raw[WinkRight] = Raw[WinkRight] && !LeftClosed;

    for (int g = 0; g < POINTER_GESTURE_COUNT; g++)
    {
        m_Channel[g].Step(Raw[g], m_T.DebounceFrames);
    }

    // Baselines move only at rest. Also hold them while a channel is counting toward a
    // set, so the rise that is about to fire does not get partly absorbed.
    for (int i = 0; i < FACE_MEASURE_COUNT; i++)
    {
        bool Frozen = false;
        for (int g = 0; g < POINTER_GESTURE_COUNT; g++)
        {
            if (kChannels[g].Measure == i && (m_Channel[g].Active || m_Channel[g].Count > 0)) Frozen = true;
        }
        Track(m_Baseline[i], S.M[i], Dt, Frozen);
    }

    return Bits();
}
