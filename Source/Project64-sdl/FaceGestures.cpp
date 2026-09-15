// Project64 - A Nintendo 64 emulator
// GestureClassifier: rest baseline, hysteresis, debounce, no-face timeout.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "FaceGestures.h"

GestureClassifier::GestureClassifier(const GestureThresholds & Thresholds) :
    m_T(Thresholds),
    m_HaveBaseline(false),
    m_BrowBaseline(0.0f),
    m_YawBaseline(0.0f),
    m_LastTime(0.0),
    m_LastFaceTime(-1e9)
{
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

// Exponential moving average with time constant BaselineSeconds. Frozen while the
// gesture that uses this baseline is held, so a long hold never becomes the new rest.
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

uint32_t GestureClassifier::Update(const GestureSample & S)
{
    const double Dt = m_HaveBaseline ? S.Time - m_LastTime : 0.0;
    m_LastTime = S.Time;

    if (!S.FaceFound)
    {
        if (S.Time - m_LastFaceTime > m_T.NoFaceSeconds)
        {
            m_Brows = Channel();
            m_Left = Channel();
            m_Right = Channel();
        }
        return (m_Brows.Active ? POINTER_GESTURE_EYEBROWS : 0)
             | (m_Left.Active ? POINTER_GESTURE_HEAD_LEFT : 0)
             | (m_Right.Active ? POINTER_GESTURE_HEAD_RIGHT : 0);
    }
    m_LastFaceTime = S.Time;

    if (!m_HaveBaseline)
    {
        m_HaveBaseline = true;
        m_BrowBaseline = S.BrowHeight;
        m_YawBaseline = S.Yaw;
    }

    const float BrowSet = m_T.Brow;
    const float BrowRelease = m_T.Brow * m_T.ReleaseFraction;
    const float BrowDelta = S.BrowHeight - m_BrowBaseline;
    m_Brows.Step(BrowDelta > (m_Brows.Active ? BrowRelease : BrowSet), m_T.DebounceFrames);

    const float YawSet = m_T.Yaw;
    const float YawRelease = m_T.Yaw * m_T.ReleaseFraction;
    const float YawDelta = S.Yaw - m_YawBaseline;
    m_Right.Step(YawDelta > (m_Right.Active ? YawRelease : YawSet), m_T.DebounceFrames);
    m_Left.Step(-YawDelta > (m_Left.Active ? YawRelease : YawSet), m_T.DebounceFrames);

    // Baselines move only at rest. Also hold them while a channel is counting toward a
    // set, so the rise that is about to fire does not get partly absorbed.
    Track(m_BrowBaseline, S.BrowHeight, Dt, m_Brows.Active || m_Brows.Count > 0);
    Track(m_YawBaseline, S.Yaw, Dt, m_Left.Active || m_Right.Active || m_Left.Count > 0 || m_Right.Count > 0);

    return (m_Brows.Active ? POINTER_GESTURE_EYEBROWS : 0)
         | (m_Left.Active ? POINTER_GESTURE_HEAD_LEFT : 0)
         | (m_Right.Active ? POINTER_GESTURE_HEAD_RIGHT : 0);
}
