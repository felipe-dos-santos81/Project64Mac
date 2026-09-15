# Face Expressions and Head Stick Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eight new facial gestures join the three the tracker knows, and head pose becomes the analog stick, so Super Mario 64 and Mario Kart 64 can be played from the webcam alone.

**Architecture:** The pure `GestureClassifier` generalises from two named measures to an eight-measure array with one rest baseline each and one channel per gesture, and computes the head stick from the yaw and pitch baselines. The tracker derives the measures from Vision's landmark regions and publishes bits and stick into `PointerState`; the input plugin copies the stick under two new `Stick` forms; the overlay lists bound gestures in a strip. Two face-only layouts ship under `Config/face/`, and an injection variable proves the path end to end without a camera.

**Tech Stack:** C++14, Objective-C++ (AVFoundation, Vision), SDL3, yaml-cpp, fixed-function OpenGL 2.1, GNU Make, POSIX sh.

**Spec:** `Docs/superpowers/specs/2026-09-15-face-expressions-design.md`

## Global Constraints

- **Never capture the screen.** Verify pixels only from inside the program (`PJ64_FRAME_DUMP`).
- **Automated runs never start the camera.** Every scripted or test run sets `PJ64_FACE=0`; `PJ64_FACE_INJECT` must never open the camera whatever `PJ64_FACE` says. Only the human partner's manual runs start it.
- **Privacy:** frames are never stored, shown or written; nothing but `PJ64_FACE_DEBUG`'s once-a-second measures is ever logged.
- **Windowed runs in scripts are bounded:** `perl -e 'alarm N; exec @ARGV' -- <cmd> || true`, or the script's own timeout loop.
- **Line endings:** run `file <path>` before editing any tracked file; every file this plan touches is LF today and must stay LF. New files are LF.
- **Language level:** `-std=c++14`; no new frameworks, no new dependencies.
- **YAML rule:** a bad file is rejected whole with one line `input: <path>:<line>:<col>: <message>; using built-in defaults`, and the instance is left exactly as it was.
- **Left and right are the player's own** for every gesture and for the stick (a left turn is negative X).
- **Bit order is fixed:** eyebrows 0, head-left 1, head-right 2, head-up 3, head-down 4, tilt-left 5, tilt-right 6, mouth-open 7, smile 8, wink-left 9, wink-right 10.
- **Commit messages** end with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- **Build check per task:** `make all` must stay warning-free apart from the pre-existing macOS `-Wdeprecated-declarations` notices from the GL 2.1 overlay.

---

## File structure

| File | Responsibility | Change |
|---|---|---|
| `Source/Common/PointerState.h` | shared struct and gesture vocabulary | eleven bits, names, tags, `HeadX`/`HeadY`, `HeadStickWanted` |
| `Source/Project64-sdl/FaceGestures.{h,cpp}` | pure classifier | measure array, channel table, wink rule, head stick |
| `Source/Project64-sdl/FaceGesturesTest.cpp` | classifier tests | new helpers and cases |
| `Source/Project64-sdl/FaceTracker.mm` | camera and Vision | eight measures, stick publish, overrides, debug |
| `Source/Project64-sdl/InputConfig.{h,cpp}` | YAML reader | `head`/`head-digital`, head-direction rule, `UsesHeadStick` |
| `Source/Project64-sdl/InputConfigTest.cpp` | reader tests | new forms, rejections, shipped face layouts |
| `Config/face/super_mario_64_usa.yaml`, `Config/face/mario_kart_64_u.yaml` | shipped layouts | new |
| `Makefile` | build, install, targets | install `Config/face`, `face-selftest` |
| `Source/Project64-sdl/PluginInput.cpp` | evaluator | `HeadStick`, flag publish, wider self-test report |
| `Source/Project64-sdl/main.cpp` | frontend | `PJ64_FACE_INJECT` |
| `Scripts/pointer_selftest.sh`, `Scripts/face_selftest.sh` | end-to-end proofs | report format, new script |
| `Source/Project64-sdl/Overlay.cpp` | panel | gesture strip, new glyphs |
| `README.md`, `AGENTS.md` | docs | face section, architecture note, traps |

---

### Task 1: Eleven-gesture vocabulary and the measure-array classifier

**Files:**
- Modify: `Source/Common/PointerState.h`
- Modify: `Source/Project64-sdl/FaceGestures.h`
- Modify: `Source/Project64-sdl/FaceGestures.cpp`
- Modify: `Source/Project64-sdl/FaceTracker.mm:86-105` (sample construction only)
- Test: `Source/Project64-sdl/FaceGesturesTest.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces (later tasks rely on these exact names):
  - `enum PointerGesture` with eleven bits; `POINTER_GESTURE_COUNT 11`; `POINTER_GESTURE_HEAD_DIRECTIONS` mask.
  - `const char * PointerGestureName(int Index)`, `const char * PointerGestureTag(int Index)`, `uint32_t PointerGestureFromName(const char *)`, `int PointerGestureIndex(uint32_t Bit)`.
  - `PointerState::HeadX`, `HeadY` (`std::atomic<int32_t>`), `HeadStickWanted` (`std::atomic<uint32_t>`).
  - `enum FaceMeasure { FACE_BROW, FACE_YAW, FACE_PITCH, FACE_ROLL, FACE_MOUTH, FACE_SMILE, FACE_EYE_LEFT, FACE_EYE_RIGHT, FACE_MEASURE_COUNT }`.
  - `struct GestureSample { bool FaceFound; float M[FACE_MEASURE_COUNT]; double Time; }`.
  - `GestureThresholds` with fields `Brow, Yaw, Pitch, Roll, Mouth, Smile, Eye, StickYaw, StickPitch, ReleaseFraction, BaselineSeconds, DebounceFrames, NoFaceSeconds`.
  - `uint32_t GestureClassifier::Update(const GestureSample &, bool HeadStickInUse = false)`, `int8_t StickX() const`, `int8_t StickY() const`, `float Baseline(FaceMeasure) const`, plus the existing `BrowBaseline()`/`YawBaseline()`.

This task changes the shapes and keeps the three existing gestures working through a channel table. The eight new channels arrive in Task 2 and the stick in Task 3; here `StickX()`/`StickY()` return 0 and `HeadStickInUse` is accepted and ignored.

- [ ] **Step 1: Check line endings**

Run: `file Source/Common/PointerState.h Source/Project64-sdl/FaceGestures.h Source/Project64-sdl/FaceGestures.cpp Source/Project64-sdl/FaceGesturesTest.cpp Source/Project64-sdl/FaceTracker.mm`
Expected: none reports CRLF.

- [ ] **Step 2: Update the test to the new sample shape and the new defaults**

Replace the `Feed` helper and the defaults check in `Source/Project64-sdl/FaceGesturesTest.cpp` (lines 15-31) with:

```cpp
// A sample with every measure at zero.
static GestureSample Sample(bool Face, double T)
{
    GestureSample S;
    S.FaceFound = Face;
    for (int i = 0; i < FACE_MEASURE_COUNT; i++) S.M[i] = 0.0f;
    S.Time = T;
    return S;
}

// Feed N frames of one brow/yaw pair, every other measure at zero, returning the last result.
static uint32_t Feed(GestureClassifier & C, double & T, int Frames, bool Face, float Brow, float Yaw)
{
    uint32_t Last = 0;
    for (int i = 0; i < Frames; i++)
    {
        T += kFrame;
        GestureSample S = Sample(Face, T);
        S.M[FACE_BROW] = Brow;
        S.M[FACE_YAW] = Yaw;
        Last = C.Update(S);
    }
    return Last;
}

// Feed N frames of one full sample (its Time is overwritten per frame), returning the last result.
static uint32_t FeedSample(GestureClassifier & C, double & T, int Frames, GestureSample S, bool HeadStick = false)
{
    uint32_t Last = 0;
    for (int i = 0; i < Frames; i++)
    {
        T += kFrame;
        S.Time = T;
        Last = C.Update(S, HeadStick);
    }
    return Last;
}

int main()
{
    GestureThresholds Th;
    CHECK(Th.Brow == 0.035f && Th.Yaw == 0.25f && Th.ReleaseFraction == 0.6f);
    CHECK(Th.Pitch == 0.20f && Th.Roll == 0.25f && Th.Mouth == 0.06f && Th.Smile == 0.05f && Th.Eye == 0.12f);
    CHECK(Th.StickYaw == 0.26f && Th.StickPitch == 0.17f);
    CHECK(Th.BaselineSeconds == 5.0 && Th.DebounceFrames == 2 && Th.NoFaceSeconds == 0.5);
    CHECK(POINTER_GESTURE_COUNT == 11);
    CHECK(PointerGestureFromName("wink-right") == POINTER_GESTURE_WINK_RIGHT);
    CHECK(PointerGestureFromName("mouth-open") == (1u << 7));
    CHECK(PointerGestureFromName("wink") == 0);
    CHECK(PointerGestureIndex(POINTER_GESTURE_TILT_LEFT) == 5);
    CHECK(strcmp(PointerGestureName(3), "head-up") == 0);
    CHECK(strcmp(PointerGestureName(11), "") == 0);
    CHECK(strcmp(PointerGestureTag(0), "Br") == 0 && strcmp(PointerGestureTag(10), "W>") == 0);
    CHECK((POINTER_GESTURE_HEAD_DIRECTIONS & POINTER_GESTURE_HEAD_UP) != 0);
    CHECK((POINTER_GESTURE_HEAD_DIRECTIONS & POINTER_GESTURE_TILT_LEFT) == 0);
    {
        // Baseline(FaceMeasure) is the general accessor; the two named ones still work.
        GestureClassifier C(Th);
        double T = 0;
        Feed(C, T, 60, true, 0.10f, 0.0f);
        CHECK(C.Baseline(FACE_BROW) == C.BrowBaseline());
        CHECK(C.Baseline(FACE_YAW) == C.YawBaseline());
        CHECK(C.StickX() == 0 && C.StickY() == 0);
    }
```

Add `#include <string.h>` after `#include <stdio.h>`. Leave every existing block from "Debounce" onward as it is; they use `Feed` and still compile.

- [ ] **Step 3: Run the test to see it fail to compile**

Run: `make face-gesture-test`
Expected: compile errors about `FACE_BROW`, `FACE_MEASURE_COUNT`, `M`, `Baseline`, `StickX`.

- [ ] **Step 4: Widen the vocabulary in `PointerState.h`**

Replace lines 14-19 (the `PointerGesture` enum) with:

```cpp
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
```

Change line 33 to `#define POINTER_GESTURE_COUNT 11`.

In `struct PointerState`, after the `Face` atomic (line 52) add:

```cpp
    std::atomic<int32_t> HeadX;          // head stick, -80..80, written by the tracker every frame
    std::atomic<int32_t> HeadY;
```

and after `FaceWanted` (line 55) add:

```cpp
    std::atomic<uint32_t> HeadStickWanted; // 1 when Stick is {stick: head} or {stick: head-digital}
```

Replace lines 91-105 (`PointerGestureFromName` and `PointerGestureIndex`) with:

```cpp
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
```

- [ ] **Step 5: Reshape the classifier header**

Replace `Source/Project64-sdl/FaceGestures.h` from line 9 to the end with:

```cpp
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
```

- [ ] **Step 6: Rewrite the classifier body as a channel table**

Replace `Source/Project64-sdl/FaceGestures.cpp` from line 6 to the end with:

```cpp
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

// Task 2 enables the eight new channels; until then only the first three are evaluated.
static const int kActiveChannels = 3;

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

    for (int g = 0; g < kActiveChannels; g++)
    {
        const ChannelSpec & Spec = kChannels[g];
        const float Set = Threshold(Spec.Measure);
        const float Release = Set * m_T.ReleaseFraction;
        const float Delta = (S.M[Spec.Measure] - m_Baseline[Spec.Measure]) * Spec.Sign;
        m_Channel[g].Step(Delta > (m_Channel[g].Active ? Release : Set), m_T.DebounceFrames);
    }

    // Baselines move only at rest. Also hold them while a channel is counting toward a
    // set, so the rise that is about to fire does not get partly absorbed.
    for (int i = 0; i < FACE_MEASURE_COUNT; i++)
    {
        bool Frozen = false;
        for (int g = 0; g < kActiveChannels; g++)
        {
            if (kChannels[g].Measure == i && (m_Channel[g].Active || m_Channel[g].Count > 0)) Frozen = true;
        }
        Track(m_Baseline[i], S.M[i], Dt, Frozen);
    }

    return Bits();
}
```

- [ ] **Step 7: Fill the new sample shape in the tracker**

In `Source/Project64-sdl/FaceTracker.mm`, replace line 86 (`GestureSample S = { false, 0.0f, 0.0f, MonotonicSeconds() };`) with:

```cpp
    GestureSample S;
    S.FaceFound = false;
    for (int i = 0; i < FACE_MEASURE_COUNT; i++) S.M[i] = 0.0f;
    S.Time = MonotonicSeconds();
```

Replace lines 100 and 104 so the two measures land in the array:

```cpp
            S.M[FACE_BROW] = BrowY - EyeY;
```
```cpp
            S.M[FACE_YAW] = Best.yaw != nil ? -Best.yaw.floatValue : 0.0f;
```

And in the debug print (lines 115-117) replace `S.BrowHeight` with `S.M[FACE_BROW]` and `S.Yaw` with `S.M[FACE_YAW]`. Task 4 rewrites this block; the aim here is only that the frontend compiles.

- [ ] **Step 8: Run the classifier test and the other tests**

Run: `make face-gesture-test && make input-config-test && make all 2>&1 | grep -c "warning: 'gl" ; make test`
Expected: `ok: face gestures`, `ok: input config`, the warning count unchanged from before this task (record it in the report), `make test` green. `input-config-test` passes unchanged because `GestureLabels` is sized by `POINTER_GESTURE_COUNT` and its indices 0-2 keep their meaning.

- [ ] **Step 9: Commit**

```bash
git add Source/Common/PointerState.h Source/Project64-sdl/FaceGestures.h Source/Project64-sdl/FaceGestures.cpp Source/Project64-sdl/FaceGesturesTest.cpp Source/Project64-sdl/FaceTracker.mm
git commit -m "Widen the gesture vocabulary to eleven bits and make the classifier a channel table

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: The eight new channels and the blink rule

**Files:**
- Modify: `Source/Project64-sdl/FaceGestures.cpp`
- Test: `Source/Project64-sdl/FaceGesturesTest.cpp`

**Interfaces:**
- Consumes: Task 1's `kChannels`, `FeedSample`, `Sample`.
- Produces: all eleven bits from `Update`.

- [ ] **Step 1: Write the failing tests**

Add before the `if (Failures != 0)` line in `FaceGesturesTest.cpp`:

```cpp
    // A face at rest for the new measures: mouth nearly shut, a resting lip width, both
    // eyes open at the same aperture.
    auto Rest = [](double T) {
        GestureSample S = Sample(true, T);
        S.M[FACE_BROW] = 0.10f;
        S.M[FACE_MOUTH] = 0.02f;
        S.M[FACE_SMILE] = 0.50f;
        S.M[FACE_EYE_LEFT] = 0.30f;
        S.M[FACE_EYE_RIGHT] = 0.30f;
        return S;
    };
    {
        // Mouth and smile each set after two frames and clear after two at rest.
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0));
        GestureSample S = Rest(0);
        S.M[FACE_MOUTH] = 0.09f;                                  // +0.07 > 0.06
        CHECK(FeedSample(C, T, 1, S) == 0);
        CHECK(FeedSample(C, T, 1, S) == POINTER_GESTURE_MOUTH_OPEN);
        S.M[FACE_SMILE] = 0.56f;                                  // +0.06 > 0.05
        CHECK(FeedSample(C, T, 2, S) == (POINTER_GESTURE_MOUTH_OPEN | POINTER_GESTURE_SMILE));
        CHECK(FeedSample(C, T, 2, Rest(0)) == 0);
        // The mouth baseline was frozen during the hold.
        CHECK(C.Baseline(FACE_MOUTH) < 0.025f);
    }
    {
        // Pitch: nose up sets head-up, nose down sets head-down. Roll: left ear down is
        // tilt-left, right ear down is tilt-right.
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0));
        GestureSample S = Rest(0);
        S.M[FACE_PITCH] = 0.25f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_HEAD_UP);
        S.M[FACE_PITCH] = -0.25f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_HEAD_DOWN);
        S.M[FACE_PITCH] = 0.0f;
        S.M[FACE_ROLL] = -0.30f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_TILT_LEFT);
        S.M[FACE_ROLL] = 0.30f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_TILT_RIGHT);
        // Roll and yaw are independent: a tilt with a turn reports both.
        S.M[FACE_YAW] = -0.30f;
        CHECK(FeedSample(C, T, 2, S) == (POINTER_GESTURE_TILT_RIGHT | POINTER_GESTURE_HEAD_LEFT));
    }
    {
        // Winks: one eye closing while the other stays open. A blink is neither.
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0));
        GestureSample S = Rest(0);
        S.M[FACE_EYE_LEFT] = 0.15f;                               // -0.15 beyond 0.12
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_WINK_LEFT);
        CHECK(FeedSample(C, T, 2, Rest(0)) == 0);
        S = Rest(0);
        S.M[FACE_EYE_RIGHT] = 0.15f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_WINK_RIGHT);
        CHECK(FeedSample(C, T, 2, Rest(0)) == 0);
        S = Rest(0);
        S.M[FACE_EYE_LEFT] = 0.15f;
        S.M[FACE_EYE_RIGHT] = 0.15f;                              // blink
        CHECK(FeedSample(C, T, 5, S) == 0);
        CHECK(FeedSample(C, T, 2, Rest(0)) == 0);
        // A wink that turns into a blink releases: the other eye closing past its release
        // level (0.12 * 0.6 = 0.072 below baseline) ends the wink within two frames.
        S = Rest(0);
        S.M[FACE_EYE_LEFT] = 0.15f;
        CHECK(FeedSample(C, T, 2, S) == POINTER_GESTURE_WINK_LEFT);
        S.M[FACE_EYE_RIGHT] = 0.20f;                              // -0.10: past release
        CHECK(FeedSample(C, T, 2, S) == 0);
        // The eye baselines were frozen through all of that.
        CHECK(C.Baseline(FACE_EYE_LEFT) > 0.29f && C.Baseline(FACE_EYE_RIGHT) > 0.29f);
    }
    {
        // A custom mouth threshold is honoured.
        GestureThresholds Loose;
        Loose.Mouth = 0.01f;
        Loose.DebounceFrames = 1;
        GestureClassifier C(Loose);
        double T = 0;
        FeedSample(C, T, 60, Rest(0));
        GestureSample S = Rest(0);
        S.M[FACE_MOUTH] = 0.035f;
        CHECK(FeedSample(C, T, 1, S) == POINTER_GESTURE_MOUTH_OPEN);
    }
```

- [ ] **Step 2: Run the test to see it fail**

Run: `make face-gesture-test`
Expected: FAIL lines for the mouth, pitch, wink blocks (the new channels are not evaluated).

- [ ] **Step 3: Enable the channels and add the blink rule**

In `FaceGestures.cpp`, delete the `kActiveChannels` constant and its comment, and replace the channel loop and the freeze loop in `Update` with:

```cpp
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
```

- [ ] **Step 4: Run the test to see it pass**

Run: `make face-gesture-test`
Expected: `ok: face gestures`.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-sdl/FaceGestures.cpp Source/Project64-sdl/FaceGesturesTest.cpp
git commit -m "Classify the eight new gestures, with a blink firing neither wink

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: The head stick in the classifier

**Files:**
- Modify: `Source/Project64-sdl/FaceGestures.cpp`
- Test: `Source/Project64-sdl/FaceGesturesTest.cpp`

**Interfaces:**
- Consumes: Task 1's `StickX()`, `StickY()`, `Update(..., HeadStickInUse)`, `StickYaw`, `StickPitch`.
- Produces: the stick values later published by the tracker (Task 4).

- [ ] **Step 1: Write the failing tests**

Add before the `if (Failures != 0)` line:

```cpp
    {
        // Head stick: yaw is X, pitch is Y, each divided by its full-tilt angle. Dead zone
        // at 20 % of full tilt, clamped to the unit disc, scaled to +-80.
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0), true);
        CHECK(C.StickX() == 0 && C.StickY() == 0);
        GestureSample S = Rest(0);
        S.M[FACE_YAW] = -0.13f;                                   // half of 0.26, to the left
        FeedSample(C, T, 1, S, true);
        CHECK(C.StickX() == -40 && C.StickY() == 0);
        S.M[FACE_YAW] = -0.04f;                                   // 0.15 of full: inside the dead zone
        FeedSample(C, T, 1, S, true);
        CHECK(C.StickX() == 0 && C.StickY() == 0);
        S.M[FACE_YAW] = 0.0f;
        S.M[FACE_PITCH] = 0.17f;                                  // full tilt up
        FeedSample(C, T, 1, S, true);
        CHECK(C.StickX() == 0 && C.StickY() == 80);
        S.M[FACE_PITCH] = -0.085f;
        FeedSample(C, T, 1, S, true);
        CHECK(C.StickY() == -40);
        S.M[FACE_YAW] = 0.52f;                                    // twice full: clamped
        S.M[FACE_PITCH] = 0.0f;
        FeedSample(C, T, 1, S, true);
        CHECK(C.StickX() == 80 && C.StickY() == 0);
        S.M[FACE_YAW] = 0.26f;                                    // a full diagonal is normalised
        S.M[FACE_PITCH] = 0.17f;
        FeedSample(C, T, 1, S, true);
        CHECK(C.StickX() == 57 && C.StickY() == 57);
        // Gestures on other measures are unaffected by a tilted stick.
        S.M[FACE_MOUTH] = 0.09f;
        CHECK(FeedSample(C, T, 2, S, true) == POINTER_GESTURE_MOUTH_OPEN);
    }
    {
        // With the stick in use, the yaw and pitch baselines hold while it is tilted, even
        // below the head-turn threshold, so a long steer never becomes the new rest.
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0), true);
        GestureSample S = Rest(0);
        S.M[FACE_YAW] = 0.13f;                                    // below Yaw 0.25: no channel counts
        S.M[FACE_PITCH] = 0.08f;
        FeedSample(C, T, 300, S, true);                           // 10 s
        CHECK(C.Baseline(FACE_YAW) < 0.005f && C.Baseline(FACE_PITCH) < 0.005f);
        CHECK(C.StickX() == 40);
    }
    {
        // Without a head stick in the layout, the same turn moves the yaw baseline as it
        // always did (10 s at tau 5 s: 86 % of the way).
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0), false);
        GestureSample S = Rest(0);
        S.M[FACE_YAW] = 0.13f;
        FeedSample(C, T, 300, S, false);
        CHECK(C.Baseline(FACE_YAW) > 0.10f);
    }
    {
        // No face for longer than the timeout centres the stick; a brief dropout keeps it.
        GestureClassifier C(Th);
        double T = 0;
        FeedSample(C, T, 60, Rest(0), true);
        GestureSample S = Rest(0);
        S.M[FACE_YAW] = 0.26f;
        FeedSample(C, T, 1, S, true);
        CHECK(C.StickX() == 80);
        FeedSample(C, T, 5, Sample(false, 0), true);
        CHECK(C.StickX() == 80);
        FeedSample(C, T, 15, Sample(false, 0), true);
        CHECK(C.StickX() == 0);
    }
    {
        // Custom full-tilt angles are honoured.
        GestureThresholds Wide;
        Wide.StickYaw = 0.52f;
        GestureClassifier C(Wide);
        double T = 0;
        FeedSample(C, T, 60, Rest(0), true);
        GestureSample S = Rest(0);
        S.M[FACE_YAW] = 0.26f;
        FeedSample(C, T, 1, S, true);
        CHECK(C.StickX() == 40);
    }
```

- [ ] **Step 2: Run the test to see it fail**

Run: `make face-gesture-test`
Expected: FAIL lines from the stick blocks (`StickX() == -40` and so on).

- [ ] **Step 3: Compute the stick and the extra freeze rule**

In `FaceGestures.cpp`, add `#include <math.h>` after the `FaceGestures.h` include. Rename the parameter in `Update`'s definition from `/*HeadStickInUse*/` to `HeadStickInUse`. Insert, immediately after the `m_Channel[g].Step(...)` loop and before the baseline loop:

```cpp
    // Head stick from the yaw and pitch baselines: dead zone at 20 % of full tilt, clamped
    // to the unit disc, +-80. Computed whether or not the layout uses it; only the freeze
    // below depends on that.
    {
        float X = (S.M[FACE_YAW] - m_Baseline[FACE_YAW]) / m_T.StickYaw;
        float Y = (S.M[FACE_PITCH] - m_Baseline[FACE_PITCH]) / m_T.StickPitch;
        const float Len = sqrtf(X * X + Y * Y);
        if (Len < 0.2f)
        {
            X = 0.0f;
            Y = 0.0f;
        }
        else if (Len > 1.0f)
        {
            X /= Len;
            Y /= Len;
        }
        m_StickX = (int8_t)lrintf(X * 80.0f);
        m_StickY = (int8_t)lrintf(Y * 80.0f);
    }
    const bool StickTilted = HeadStickInUse && (m_StickX != 0 || m_StickY != 0);
```

and in the baseline loop, after the inner `for (int g ...)` loop, add:

```cpp
        if (StickTilted && (i == FACE_YAW || i == FACE_PITCH)) Frozen = true;
```

- [ ] **Step 4: Run the test to see it pass**

Run: `make face-gesture-test`
Expected: `ok: face gestures`.

- [ ] **Step 5: Commit**

```bash
git add Source/Project64-sdl/FaceGestures.cpp Source/Project64-sdl/FaceGesturesTest.cpp
git commit -m "Compute the head stick from the yaw and pitch baselines

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Derive the eight measures in the tracker and publish the stick

**Files:**
- Modify: `Source/Project64-sdl/FaceTracker.mm`

**Interfaces:**
- Consumes: Task 1's `FaceMeasure`, `GestureSample::M`, `PointerState::HeadX/HeadY/HeadStickWanted`; Task 3's `StickX()/StickY()`.
- Produces: `HeadX`, `HeadY` and eleven `Gestures` bits written every frame; env overrides `PJ64_FACE_PITCH`, `PJ64_FACE_ROLL`, `PJ64_FACE_MOUTH`, `PJ64_FACE_SMILE`, `PJ64_FACE_EYE`, `PJ64_FACE_STICK_YAW`, `PJ64_FACE_STICK_PITCH`.

There is no automated test for Objective-C++ (Global Constraints: automated runs never start the camera). The gate is a warning-free compile and a review of the derivations against the spec's Part 2 table; the human partner's manual run (Task 9) confirms the two signs.

- [ ] **Step 1: Read the env overrides**

Replace `ThresholdsFromEnv` (lines 23-31) with:

```cpp
// One override per threshold, in the units the field uses (radians for the angles,
// face-box-normalised units for the landmark measures). Zero or negative is ignored.
static void OverrideFromEnv(float * Field, const char * Name)
{
    const char * Text = getenv(Name);
    if (Text != nullptr && atof(Text) > 0.0) *Field = (float)atof(Text);
}

static GestureThresholds ThresholdsFromEnv(void)
{
    GestureThresholds T;
    OverrideFromEnv(&T.Brow, "PJ64_FACE_BROW");
    OverrideFromEnv(&T.Yaw, "PJ64_FACE_YAW");
    OverrideFromEnv(&T.Pitch, "PJ64_FACE_PITCH");
    OverrideFromEnv(&T.Roll, "PJ64_FACE_ROLL");
    OverrideFromEnv(&T.Mouth, "PJ64_FACE_MOUTH");
    OverrideFromEnv(&T.Smile, "PJ64_FACE_SMILE");
    OverrideFromEnv(&T.Eye, "PJ64_FACE_EYE");
    OverrideFromEnv(&T.StickYaw, "PJ64_FACE_STICK_YAW");
    OverrideFromEnv(&T.StickPitch, "PJ64_FACE_STICK_PITCH");
    return T;
}
```

- [ ] **Step 2: Add the region-extent helper and the sign constants**

After `RegionMeanY` (line 41) add:

```cpp
// Bounding extent of a landmark region in face-box-normalised units. Returns false when
// Vision returned no points, in which case the caller keeps the previous frame's value.
static bool RegionExtent(VNFaceLandmarkRegion2D * Region, float * Width, float * Height)
{
    if (Region == nil || Region.pointCount == 0) return false;
    const CGPoint * Points = Region.normalizedPoints;
    double MinX = Points[0].x, MaxX = Points[0].x, MinY = Points[0].y, MaxY = Points[0].y;
    for (NSUInteger i = 1; i < Region.pointCount; i++)
    {
        if (Points[i].x < MinX) MinX = Points[i].x;
        if (Points[i].x > MaxX) MaxX = Points[i].x;
        if (Points[i].y < MinY) MinY = Points[i].y;
        if (Points[i].y > MaxY) MaxY = Points[i].y;
    }
    *Width = (float)(MaxX - MinX);
    *Height = (float)(MaxY - MinY);
    return true;
}

// Eye aperture: height over width, so it stays comparable as the head turns.
static bool EyeAperture(VNFaceLandmarkRegion2D * Eye, float * Out)
{
    float W = 0.0f, H = 0.0f;
    if (!RegionExtent(Eye, &W, &H) || W <= 0.0f) return false;
    *Out = H / W;
    return true;
}

// Vision reports the angles for the face as seen by the camera. Yaw is negated so the
// player's left turn is negative (confirmed by manual run). Pitch and roll follow the same
// path: the classifier wants nose-up positive and left-ear-down negative, and these two
// constants are the one place to flip them once the first manual run of the face layouts
// says which way Vision's values go (spec Part 2). Never fix a sign in a YAML file.
static const float kPitchSign = 1.0f;
static const float kRollSign = 1.0f;
```

- [ ] **Step 3: Keep last-frame measures in the delegate**

In the `@interface PJ64FaceDelegate` ivars (lines 44-50) add `float m_LastM[FACE_MEASURE_COUNT];`, and in `initWithState:` after `m_LastDebug = 0.0;` add:

```objc
        for (int i = 0; i < FACE_MEASURE_COUNT; i++) m_LastM[i] = 0.0f;
```

- [ ] **Step 4: Derive the measures and publish the stick**

Replace the body of `captureOutput:didOutputSampleBuffer:fromConnection:` from the `GestureSample S` declaration through the end of the debug print with:

```objc
    GestureSample S;
    S.FaceFound = false;
    for (int i = 0; i < FACE_MEASURE_COUNT; i++) S.M[i] = m_LastM[i];
    S.Time = MonotonicSeconds();
    if ([Handler performRequests:@[ m_Request ] error:&Error])
    {
        VNFaceObservation * Best = nil;
        for (VNFaceObservation * Face in m_Request.results)
        {
            if (Best == nil || Face.boundingBox.size.width > Best.boundingBox.size.width) Best = Face;
        }
        if (Best != nil && Best.landmarks != nil)
        {
            VNFaceLandmarks2D * L = Best.landmarks;
            S.FaceFound = true;
            S.M[FACE_BROW] = 0.5f * (RegionMeanY(L.leftEyebrow) + RegionMeanY(L.rightEyebrow))
                           - 0.5f * (RegionMeanY(L.leftEye) + RegionMeanY(L.rightEye));
            S.M[FACE_YAW] = Best.yaw != nil ? -Best.yaw.floatValue : 0.0f;
            S.M[FACE_PITCH] = Best.pitch != nil ? kPitchSign * Best.pitch.floatValue : 0.0f;
            S.M[FACE_ROLL] = Best.roll != nil ? kRollSign * Best.roll.floatValue : 0.0f;
            // A region with no points keeps the previous frame's value (spec Part 2).
            float W = 0.0f, H = 0.0f, A = 0.0f;
            if (RegionExtent(L.innerLips, &W, &H)) S.M[FACE_MOUTH] = H;
            if (RegionExtent(L.outerLips, &W, &H)) S.M[FACE_SMILE] = W;
            if (EyeAperture(L.leftEye, &A)) S.M[FACE_EYE_LEFT] = A;
            if (EyeAperture(L.rightEye, &A)) S.M[FACE_EYE_RIGHT] = A;
            for (int i = 0; i < FACE_MEASURE_COUNT; i++) m_LastM[i] = S.M[i];
        }
    }

    const bool HeadStick = m_State->HeadStickWanted.load(std::memory_order_acquire) != 0;
    const uint32_t Bits = m_Classifier->Update(S, HeadStick);
    m_State->Gestures.store(Bits, std::memory_order_relaxed);
    m_State->HeadX.store(m_Classifier->StickX(), std::memory_order_relaxed);
    m_State->HeadY.store(m_Classifier->StickY(), std::memory_order_relaxed);
    m_State->Face.store(S.FaceFound ? FACE_TRACKING : FACE_NO_FACE, std::memory_order_relaxed);

    if (m_Debug && S.Time - m_LastDebug >= 1.0)
    {
        m_LastDebug = S.Time;
        static const char * const kNames[FACE_MEASURE_COUNT] = {
            "brow", "yaw", "pitch", "roll", "mouth", "smile", "eyeL", "eyeR"
        };
        fprintf(stderr, "face: found=%d", S.FaceFound ? 1 : 0);
        for (int i = 0; i < FACE_MEASURE_COUNT; i++)
        {
            fprintf(stderr, " %s=%.3f/%.3f", kNames[i], S.M[i], m_Classifier->Baseline((FaceMeasure)i));
        }
        fprintf(stderr, " bits=%u stick=%d,%d\n", Bits, (int)m_Classifier->StickX(), (int)m_Classifier->StickY());
    }
```

Update the file's header comment (line 4) to: `// previewed or written. Only PJ64_FACE_DEBUG prints the eight measures, once a second.`

- [ ] **Step 5: Build and smoke-test without a camera**

Run: `make all 2>&1 | grep -E "FaceTracker|error" ; make test`
Expected: no diagnostics mentioning `FaceTracker.mm`; `make test` green. Then confirm the camera is untouched by a normal run: `PJ64_FACE=0 perl -e 'alarm 8; exec @ARGV' -- ./Bin/macOS/Project64 Roms/sm64.z64 || true` prints no `face:` line. (Skip if no ROM is present, and say so in the report.)

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-sdl/FaceTracker.mm
git commit -m "Derive the eight face measures from Vision's regions and publish the head stick

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: The YAML forms, the head-direction rule and the shipped layouts

**Files:**
- Modify: `Source/Project64-sdl/InputConfig.h`
- Modify: `Source/Project64-sdl/InputConfig.cpp`
- Create: `Config/face/super_mario_64_usa.yaml`, `Config/face/mario_kart_64_u.yaml`
- Modify: `Makefile:435-448` (config target)
- Test: `Source/Project64-sdl/InputConfigTest.cpp`

**Interfaces:**
- Consumes: Task 1's names, `POINTER_GESTURE_HEAD_DIRECTIONS`, `PointerGestureIndex`.
- Produces: `Binding::Kind::HeadStick` (`code` 0 analog, 1 digital); `bool InputConfig::UsesHeadStick() const`; `UsesPointer()`/`UsesFace()` true for a head stick.

- [ ] **Step 1: Write the failing tests**

In `InputConfigTest.cpp`, insert before the line `CHECK(C.Load(WriteTemp(Valid)));                  // establish a known good state`:

```cpp
    const char * FaceOnly =
        "bindings:\n"
        "  Stick: {stick: head}\n"
        "  A: {face: mouth-open}\n"
        "  B: {face: smile}\n"
        "  CLeft: {face: wink-left}\n"
        "  CRight: {face: wink-right}\n"
        "  R: {face: tilt-left}\n"
        "  Start: {face: tilt-right}\n"
        "  Z: {face: head-up}\n";
    CHECK(!C.Load(WriteTemp(FaceOnly)));              // head-up beside a head stick is rejected...
    const char * FaceOnlyOk =
        "bindings:\n"
        "  Stick: {stick: head}\n"
        "  A: {face: mouth-open}\n"
        "  B: {face: smile}\n"
        "  CLeft: {face: wink-left}\n"
        "  CRight: {face: wink-right}\n"
        "  R: {face: tilt-left}\n"
        "  Start: {face: tilt-right}\n";
    CHECK(C.Load(WriteTemp(FaceOnlyOk)));             // ...and the same file without it loads
    CHECK(C.UsesPointer() && C.UsesFace() && C.UsesHeadStick());
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::HeadStick);
    CHECK(C.Bindings(N64Control::Stick)[0].code == 0);
    CHECK(C.Bindings(N64Control::A)[0].code == POINTER_GESTURE_MOUTH_OPEN);
    CHECK(C.Bindings(N64Control::CLeft)[0].code == POINTER_GESTURE_WINK_LEFT);
    CHECK(C.Bindings(N64Control::Start)[0].code == POINTER_GESTURE_TILT_RIGHT);
    C.PointerLabels(Labels, GestureLabels);
    CHECK(strcmp(GestureLabels[7], "A") == 0);        // mouth-open is bit 7
    CHECK(strcmp(GestureLabels[9], "C<") == 0);       // wink-left is bit 9
    CHECK(strcmp(GestureLabels[1], "") == 0);         // head-left unbound

    CHECK(C.Load(WriteTemp("bindings:\n  Stick: {stick: head-digital}\n  Z: {face: eyebrows}\n")));
    CHECK(C.Bindings(N64Control::Stick)[0].kind == Binding::Kind::HeadStick);
    CHECK(C.Bindings(N64Control::Stick)[0].code == 1);
    CHECK(C.UsesHeadStick());

    // A pointer stick beside head-left is fine: only a head stick consumes the head turns.
    CHECK(C.Load(WriteTemp("bindings:\n  Stick: {stick: pointer}\n  B: {face: head-left}\n")));
    CHECK(!C.UsesHeadStick());
    // The rule holds in either order and for either head-stick form.
    CHECK(!C.Load(WriteTemp("bindings:\n  B: {face: head-down}\n  Stick: {stick: head}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {stick: head-digital}\n  B: {face: head-right}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  A: {stick: head}\n")));
    CHECK(!C.Load(WriteTemp("bindings:\n  Stick: {stick: head-analog}\n")));
```

Change the existing rejection line `CHECK(!C.Load(WriteTemp("bindings:\n  A: {face: wink}\n")));` to keep it (a bare `wink` is still unknown) and add after it:

```cpp
    CHECK(C.Load(WriteTemp("bindings:\n  A: {face: wink-left}\n  B: {face: head-down}\n  Z: {face: tilt-right}\n")));
    CHECK(C.Bindings(N64Control::B)[0].code == POINTER_GESTURE_HEAD_DOWN);
    CHECK(C.Load(WriteTemp(Valid)));                  // back to the known good state
```

Before the `if (Failures != 0)` line add:

```cpp
    CHECK(C.Load("Config/face/super_mario_64_usa.yaml"));   // every shipped face layout must parse
    CHECK(C.UsesPointer() && C.UsesFace() && C.UsesHeadStick());
    CHECK(C.Bindings(N64Control::A)[0].code == POINTER_GESTURE_MOUTH_OPEN);
    CHECK(C.Bindings(N64Control::L).size() == 2);     // unbound: keeps keyboard and gamepad
    CHECK(C.Load("Config/face/mario_kart_64_u.yaml"));
    CHECK(C.UsesPointer() && C.UsesFace() && C.UsesHeadStick());
    CHECK(C.Bindings(N64Control::R)[0].code == POINTER_GESTURE_EYEBROWS);
    CHECK(C.Load("Config/mouse/super_mario_64_usa.yaml"));
    CHECK(!C.UsesHeadStick());                        // the mouse layouts have no head stick
```

- [ ] **Step 2: Run the test to see it fail**

Run: `make input-config-test`
Expected: compile error on `Binding::Kind::HeadStick` / `UsesHeadStick`.

- [ ] **Step 3: Extend the header**

In `InputConfig.h`, change the `Kind` enum (line 28) to:

```cpp
    enum class Kind { Key, Button, Axis, Stick, Keys, Zone, Face, Pointer, HeadStick };
```

Extend the `code` comment (lines 31-34) with `// HeadStick: 0 for {stick: head}, 1 for {stick: head-digital}`. After `UsesFace()` add:

```cpp
    // True when Stick is {stick: head} or {stick: head-digital}: the tracker's yaw and
    // pitch baselines then hold while the stick is tilted (PointerState::HeadStickWanted).
    bool UsesHeadStick() const;
```

- [ ] **Step 4: Implement the forms and the rule**

In `InputConfig.cpp`:

In `UsesPointer()` line 46, add `|| B.kind == Binding::Kind::HeadStick` to the condition. In `UsesFace()` line 61, change to `if (B.kind == Binding::Kind::Face || B.kind == Binding::Kind::HeadStick)`. After `UsesFace()` add:

```cpp
bool InputConfig::UsesHeadStick() const
{
    for (const Binding & B : m_Bindings[(int)N64Control::Stick])
    {
        if (B.kind == Binding::Kind::HeadStick)
        {
            return true;
        }
    }
    return false;
}
```

After `MakePointer()` add:

```cpp
static Binding MakeHeadStick(bool Digital)
{
    return Binding{ Binding::Kind::HeadStick, Digital ? 1 : 0, true, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN };
}
```

Change line 252's message to `Form + " cannot drive Stick; use {keys:}, {stick: left/right}, {stick: pointer} or {stick: head/head-digital}"`.

In the `stick` branch (lines 304-313), after the `pointer` line add:

```cpp
        if (Name == "head") { Out = MakeHeadStick(false); return true; }
        if (Name == "head-digital") { Out = MakeHeadStick(true); return true; }
        ConfigError(Path, Value[Form], "stick must be left, right, pointer, head or head-digital");
        return false;
```

(replacing the old `stick must be left, right or pointer` line and its return).

In `Load`, the rule needs the offending node, which only exists inside the loop, and the stick form, which may come before or after it. Declare before the `try`:

```cpp
    // The head-direction rule (spec Part 1): remembered during the loop, checked after it,
    // so it holds whichever order the file names Stick and the gesture in.
    bool StickIsHead = false;
    YAML::Node HeadGestureNode;
    std::string HeadGestureName;
```

Inside the loop, after `Next[Index].push_back(B);` add:

```cpp
                if (B.kind == Binding::Kind::HeadStick) StickIsHead = true;
                if (B.kind == Binding::Kind::Face && ((uint32_t)B.code & POINTER_GESTURE_HEAD_DIRECTIONS) != 0 && HeadGestureName.empty())
                {
                    HeadGestureNode = Entry.second["face"];
                    HeadGestureName = PointerGestureName(PointerGestureIndex((uint32_t)B.code));
                }
```

After the closing brace of the `if (Bindings)` block, still inside the `try`, add:

```cpp
        if (StickIsHead && !HeadGestureName.empty())
        {
            ConfigError(Path, HeadGestureNode, HeadGestureName + " cannot be bound while Stick is head");
            return false;
        }
```

- [ ] **Step 5: Write the shipped layouts**

Create `Config/face/super_mario_64_usa.yaml`:

```yaml
# Face-only layout for Super Mario 64. Select with
#   make run rom=... input=Config/face/super_mario_64_usa.yaml
# Head pose is the stick; every button here is a held facial gesture. See README
# "Playing with your face". Controls not listed keep their keyboard keys.

bindings:
  Stick:     {stick: head}
  A:         {face: mouth-open}    # hold longer for a higher jump
  B:         {face: smile}         # punch, and dive while running
  Z:         {face: eyebrows}      # crouch; brows + mouth while tilted = long jump
  CLeft:     {face: wink-left}     # camera
  CRight:    {face: wink-right}
  R:         {face: tilt-left}
  Start:     {face: tilt-right}
```

Create `Config/face/mario_kart_64_u.yaml`:

```yaml
# Face-only layout for Mario Kart 64. Select with
#   make run rom=... input=Config/face/mario_kart_64_u.yaml
# The analog head stick steers; hold the mouth open to accelerate. See README
# "Playing with your face". Controls not listed keep their keyboard keys.

bindings:
  Stick:     {stick: head}
  A:         {face: mouth-open}    # accelerate (held)
  R:         {face: eyebrows}      # hop, and drift while held
  Z:         {face: smile}         # use item
  B:         {face: tilt-left}     # brake
  Start:     {face: tilt-right}
```

- [ ] **Step 6: Install them**

In the `Makefile` `config` target, after the `cp -f Config/mouse/*.yaml` line add:

```make
	@# Same rule for the face layouts. They are outside the per-ROM lookup on purpose.
	@mkdir -p $(BIN)/Config/face
	@rm -f $(BIN)/Config/face/*.yaml
	@cp -f Config/face/*.yaml $(BIN)/Config/face/
```

Change the target's help text to `## [STEP 7b] Install ROM database, enhancements, input mappings and language files beside the binary (refreshes Config/mouse and Config/face)`.

- [ ] **Step 7: Run the tests**

Run: `make input-config-test && make config && ls Bin/macOS/Config/face/`
Expected: `ok: input config`; both YAML files listed.

- [ ] **Step 8: Commit**

```bash
git add Source/Project64-sdl/InputConfig.h Source/Project64-sdl/InputConfig.cpp Source/Project64-sdl/InputConfigTest.cpp Config/face/super_mario_64_usa.yaml Config/face/mario_kart_64_u.yaml Makefile
git commit -m "Read the head-stick forms, reject head turns beside them, and ship two face layouts

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Evaluate the head stick in the plugin and prove the path without a camera

**Files:**
- Modify: `Source/Project64-sdl/PluginInput.cpp:113-126, 261-308, 390-406`
- Modify: `Source/Project64-sdl/main.cpp:118-136, 344-352, 389-400`
- Modify: `Scripts/pointer_selftest.sh:54-55, 65`
- Create: `Scripts/face_selftest.sh`
- Modify: `Makefile` (`.PHONY`, a `face-selftest` target after `pointer-selftest`)

**Interfaces:**
- Consumes: Task 1's `HeadX/HeadY/HeadStickWanted`, `PointerGestureFromName`; Task 5's `Binding::Kind::HeadStick`, `UsesHeadStick()`; `PointerQuadrant` from `PointerLayout.h`.
- Produces: `PJ64_FACE_INJECT=<gesture>[,<x>,<y>]`; the self-test report line `pointer-selftest zone=%d a=%d start=%d z=%d x=%d y=%d`; `make face-selftest rom=…`.

- [ ] **Step 1: Write the failing end-to-end script**

Create `Scripts/face_selftest.sh` (mode 755):

```sh
#!/bin/sh
# Prove the face path end to end without a camera: the frontend publishes an injected
# gesture and head stick, the plugin evaluates them against
# Config/face/super_mario_64_usa.yaml, and the N64 bits and axes come out right. Two runs:
# mouth-open must set A; eyebrows with a full right tilt must set Z, not A, and put 80 on
# the X axis, which proves the head stick reached the mapped output. PJ64_FACE=0 is set as
# well: the injection never opens the camera, and neither may this script.
# Design: Docs/superpowers/specs/2026-09-15-face-expressions-design.md
set -eu

ROM="${1:?usage: face_selftest.sh <rom>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64"
YAML="$ROOT/Config/face/super_mario_64_usa.yaml"
TIMEOUT=20

[ -x "$BIN" ] || { echo "frontend not built: $BIN" >&2; exit 1; }

# $1 = face inject spec, $2 = expected report tail. The pointer is pinned to the window's
# corner with no button so the real mouse cannot trip the report early.
one_run() {
    LOG="$(mktemp)"
    PJ64_INPUT_YAML="$YAML" PJ64_FACE=0 PJ64_POINTER_SELFTEST=1 PJ64_POINTER_INJECT="0,0,0" \
        PJ64_FACE_INJECT="$1" "$BIN" "$ROM" >"$LOG" 2>&1 &
    PID=$!
    I=0
    while [ "$I" -lt "$TIMEOUT" ]; do
        grep -q '^pointer-selftest ' "$LOG" 2>/dev/null && break
        sleep 1
        I=$((I + 1))
    done
    kill -TERM "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    if grep -q '^face: tracking started' "$LOG"; then
        echo "FAIL: inject $1 opened the camera (log: $LOG)" >&2
        return 1
    fi
    REPORT="$(grep '^pointer-selftest ' "$LOG" || true)"
    if [ "$REPORT" = "pointer-selftest $2" ]; then
        rm -f "$LOG"
        return 0
    fi
    echo "FAIL: inject $1: got '$REPORT', wanted 'pointer-selftest $2' (log: $LOG)" >&2
    return 1
}

FAIL=0
one_run "mouth-open" "zone=-1 a=1 start=0 z=0 x=0 y=0" || FAIL=1
one_run "eyebrows,80,0" "zone=-1 a=0 start=0 z=1 x=80 y=0" || FAIL=1

if [ "$FAIL" -eq 0 ]; then
    echo "ok: face path maps mouth-open to A, eyebrows to Z, and the head stick to the X axis"
fi
exit "$FAIL"
```

Add to the Makefile after the `pointer-selftest` target:

```make
face-selftest: ## Prove the injected-face path maps mouth-open to A and the head stick to the X axis, with no camera (usage: make face-selftest rom=/path/to/game.z64)
	@test -n "$(rom)" || { echo "usage: make face-selftest rom=/path/to/game.z64"; exit 1; }
	Scripts/face_selftest.sh "$(rom)"
```

and add `face-selftest` to the `.PHONY` list after `pointer-selftest`.

- [ ] **Step 2: Run it to see it fail**

Run: `make all && make face-selftest rom=Roms/sm64.z64`
Expected: both runs FAIL with an empty report (nothing publishes face bits, and the report only fires on a button). If no ROM is present, note it and continue; Step 7 is then a documented gap in the report.

- [ ] **Step 3: Widen the self-test report**

Replace `PointerSelftestReport` (lines 113-126 of `PluginInput.cpp`) with:

```cpp
// Verification only. With PJ64_POINTER_SELFTEST set, report once, on the first frame that
// sees the button down or any face input (a gesture bit, or a head-stick axis), which zone
// latched and what the controller produced, so Scripts/pointer_selftest.sh and
// Scripts/face_selftest.sh prove delivery, geometry and mapping together.
static void PointerSelftestReport(bool Button, int Latched, uint32_t Gestures, const BUTTONS * Out)
{
    static bool Reported = false;
    if (Reported || getenv("PJ64_POINTER_SELFTEST") == nullptr)
    {
        return;
    }
    if (!Button && Gestures == 0 && Out->X_AXIS == 0 && Out->Y_AXIS == 0)
    {
        return;
    }
    Reported = true;
    fprintf(stderr, "pointer-selftest zone=%d a=%d start=%d z=%d x=%d y=%d\n",
        Latched, Out->A_BUTTON ? 1 : 0, Out->START_BUTTON ? 1 : 0, Out->Z_TRIG ? 1 : 0,
        (int)Out->X_AXIS, (int)Out->Y_AXIS);
}
```

Update the call at line 307 to `PointerSelftestReport(S.Button, g_PointerLatched, Gestures, Keys);`.

Update the two expected strings in `Scripts/pointer_selftest.sh` (lines 54, 55, 65) to `"zone=13 a=1 start=0 z=0 x=0 y=0"` and `"zone=8 a=0 start=1 z=0 x=0 y=0"` (the click at the game centre is inside the dead zone; the click in mid1 is outside the game image, so both sticks are neutral).

- [ ] **Step 4: Evaluate the head stick**

In the pointer block of `GetKeys`, after the `Binding::Kind::Pointer` branch (line 304) add:

```cpp
                else if (B.kind == Binding::Kind::HeadStick)
                {
                    // The tracker publishes the stick already scaled; head-digital snaps it
                    // by the quadrant rule the mouse guide uses (vertical wins ties).
                    int8_t X = (int8_t)g_Pointer->HeadX.load(std::memory_order_relaxed);
                    int8_t Y = (int8_t)g_Pointer->HeadY.load(std::memory_order_relaxed);
                    const int Q = PointerQuadrant(X, Y);
                    if (B.code == 1)
                    {
                        X = (int8_t)(Q == 1 ? N64_AXIS_MAX : Q == 3 ? -N64_AXIS_MAX : 0);
                        Y = (int8_t)(Q == 0 ? N64_AXIS_MAX : Q == 2 ? -N64_AXIS_MAX : 0);
                    }
                    Keys->X_AXIS = X;
                    Keys->Y_AXIS = Y;
                    StickFromKeys = true; // the head owns the stick; the gamepad must not overwrite it
                    g_Pointer->Quadrant.store(Q, std::memory_order_relaxed);
                }
```

In `PublishPointerLabels`, before the `FaceWanted` store add:

```cpp
    g_Pointer->HeadStickWanted.store(Config.UsesHeadStick() ? 1u : 0u, std::memory_order_release);
```

- [ ] **Step 5: Add the injection to the frontend**

In `main.cpp`, after `ParsePointerInject` (line 136) add:

```cpp
// PJ64_FACE_INJECT=<gesture>[,<x>,<y>] publishes that gesture's bit and that head stick
// instead of running the tracker, so the face path can be proven without a camera
// (Scripts/face_selftest.sh). With it set the camera is never opened, whatever PJ64_FACE says.
struct FaceInject { uint32_t Bits; int X, Y; };

static bool ParseFaceInject(FaceInject * Out)
{
    const char * Env = getenv("PJ64_FACE_INJECT");
    if (Env == nullptr)
    {
        return false;
    }
    char Name[32] = { 0 };
    Out->X = 0;
    Out->Y = 0;
    const int N = sscanf(Env, "%31[^,],%d,%d", Name, &Out->X, &Out->Y);
    Out->Bits = N >= 1 ? PointerGestureFromName(Name) : 0;
    if (Out->Bits == 0 || N == 2 || Out->X < -80 || Out->X > 80 || Out->Y < -80 || Out->Y > 80)
    {
        fprintf(stderr, "bad PJ64_FACE_INJECT: %s (want gesture[,x,y] with x,y in -80..80)\n", Env);
        return false;
    }
    return true;
}

static void PublishFaceInject(PointerState * State, const FaceInject & F)
{
    State->Gestures.store(F.Bits, std::memory_order_relaxed);
    State->HeadX.store(F.X, std::memory_order_relaxed);
    State->HeadY.store(F.Y, std::memory_order_relaxed);
    State->Face.store(FACE_TRACKING, std::memory_order_relaxed);
}
```

Replace lines 344-352 with:

```cpp
    PointerState * pointer = CreatePointerState();
    FaceInject faceInject;
    const bool faceInjecting = ParseFaceInject(&faceInject);
    bool FaceStarted = faceInjecting; // an injected face stands in for the tracker
    if (Face == FaceMode::On && pointer != nullptr && !TileMode && !faceInjecting)
    {
        FaceTrackerStart(pointer);
        FaceStarted = true;
    }
    PointerSample inject;
    const bool injecting = ParsePointerInject(&inject);
```

In the main loop, after `PublishMouse(...)` (line 391) add:

```cpp
            if (faceInjecting)
            {
                PublishFaceInject(pointer, faceInject);
            }
```

The `Auto` start below it is already gated on `!FaceStarted`, which `faceInjecting` set true.

- [ ] **Step 6: Build and run the unit suite**

Run: `make all && make test && make input-config-test && make face-gesture-test && make pointer-layout-test`
Expected: all green.

- [ ] **Step 7: Run both end-to-end scripts**

Run: `make pointer-selftest rom=Roms/sm64.z64 && make face-selftest rom=Roms/sm64.z64`
Expected: `ok: pointer path …` and `ok: face path maps mouth-open to A, eyebrows to Z, and the head stick to the X axis`. Neither log contains `face: tracking started`.

- [ ] **Step 8: Commit**

```bash
chmod 755 Scripts/face_selftest.sh
git add Source/Project64-sdl/PluginInput.cpp Source/Project64-sdl/main.cpp Scripts/pointer_selftest.sh Scripts/face_selftest.sh Makefile
git commit -m "Drive the stick from the head in the plugin and prove the face path by injection

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: The gesture strip in the overlay

**Files:**
- Modify: `Source/Project64-sdl/Overlay.cpp:20-76, 111-137, 158`
- Modify: `Docs/superpowers/specs/2026-09-15-face-expressions-design.md` (Part 3, overlay paragraph: dot position)

**Interfaces:**
- Consumes: Task 1's `PointerGestureTag`, `POINTER_GESTURE_COUNT`, `GestureLabels`.
- Produces: nothing downstream.

No automated test reaches what the overlay draws (the frame dump is captured before `OverlayDraw`); the gate is a clean build, an in-program frame check that the panel still renders, and the human partner's visual check in Task 9.

- [ ] **Step 1: Add the glyphs the tags need**

In `kGlyphs` (lines 22-36) add, keeping the table sorted as it is (by convenience, append before the closing brace):

```cpp
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } },
    { 'm', { 0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15 } },
    { 'o', { 0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E } },
    { 'r', { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10 } },
    { '=', { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 } },
```

Update the comment above the table to `// 5x7 glyphs, one byte per row, bit 4 is the left column. The slot labels, the guide arrows and the gesture tags.`

- [ ] **Step 2: Give the text drawer a scale and an alignment**

Replace `DrawText` (lines 47-76) with:

```cpp
// Draws Text with its left edge at X and its vertical centre at Cy, at Scale pixels per
// font pixel. Returns the width drawn.
static float DrawTextLeft(const char * Text, float X, float Cy, float Alpha, int Scale)
{
    const int Len = (int)strlen(Text);
    if (Len == 0) return 0.0f;
    const float GlyphW = 6.0f * Scale; // 5 columns plus one of spacing
    const float GlyphH = 7.0f * Scale;
    const float Y = Cy - GlyphH / 2.0f;
    glColor4f(1.0f, 1.0f, 1.0f, Alpha);
    glBegin(GL_QUADS);
    for (int i = 0; i < Len; i++, X += GlyphW)
    {
        const Glyph * G = FindGlyph(Text[i]);
        if (G == nullptr) continue;
        for (int Row = 0; Row < 7; Row++)
        {
            for (int Col = 0; Col < 5; Col++)
            {
                if ((G->Rows[Row] & (0x10 >> Col)) == 0) continue;
                const float Px = X + Col * Scale, Py = Y + Row * Scale;
                glVertex2f(Px, Py);
                glVertex2f(Px + Scale, Py);
                glVertex2f(Px + Scale, Py + Scale);
                glVertex2f(Px, Py + Scale);
            }
        }
    }
    glEnd();
    return Len * GlyphW - Scale;
}

// Draws Text with its centre at (Cx, Cy) at the label scale.
static void DrawText(const char * Text, float Cx, float Cy, float Alpha)
{
    const float Width = (float)strlen(Text) * 6.0f * kScale - kScale;
    DrawTextLeft(Text, Cx - Width / 2.0f, Cy, Alpha, kScale);
}
```

- [ ] **Step 3: Replace the three labels with the strip**

Replace `DrawFaceStatus` (lines 111-137) with:

```cpp
// The tracker mark, then one entry per bound gesture in bit order: the gesture's two-glyph
// tag, '=', the bound control's label; bright while held. Drawn at kStripScale in the free
// band under the middle slots (x 164..484, y from the mark's row down), wrapping to a new
// row when an entry would cross the band's right edge, so eleven entries fit.
static const int kStripScale = 2;

static void DrawFaceStatus(const PointerState * State, float Cx, float Cy)
{
    const uint32_t Face = State->Face.load(std::memory_order_relaxed);
    if (Face == FACE_OFF) return;
    const float R = 6.0f;
    if (Face == FACE_TRACKING)
    {
        DrawCircle(Cx, Cy, R, kBright, true);
    }
    else
    {
        DrawCircle(Cx, Cy, R, kDim, false);
        if (Face == FACE_DENIED || Face == FACE_ERROR)
        {
            DrawLine(Cx - R, Cy - R, Cx + R, Cy + R, kDim);
            DrawLine(Cx - R, Cy + R, Cx + R, Cy - R, kDim);
        }
    }
    const uint32_t Bits = State->Gestures.load(std::memory_order_relaxed);
    const float BandLeft = 164.0f, BandRight = 484.0f, Gap = 10.0f, RowH = 22.0f;
    float X = Cx + R + Gap, Y = Cy;
    for (int i = 0; i < POINTER_GESTURE_COUNT; i++)
    {
        if (State->GestureLabels[i][0] == '\0') continue;
        char Entry[8];
        snprintf(Entry, sizeof(Entry), "%s=%s", PointerGestureTag(i), State->GestureLabels[i]);
        const float Width = (float)strlen(Entry) * 6.0f * kStripScale - kStripScale;
        if (X + Width > BandRight)
        {
            X = BandLeft;
            Y += RowH;
        }
        DrawTextLeft(Entry, X, Y, (Bits & (1u << i)) != 0 ? kBright : kDim, kStripScale);
        X += Width + Gap;
    }
}
```

Add `#include <stdio.h>` after `#include <math.h>`.

Change the call in `DrawPanel` (line 158) to `DrawFaceStatus(State, 178.0f, (float)H - 88.0f);` so the mark sits at the top of the free band (`Top + 72`) and up to three strip rows fit below it inside the panel.

- [ ] **Step 4: Amend the spec's dot sentence**

In `Docs/superpowers/specs/2026-09-15-face-expressions-design.md`, Part 3, replace the sentence `The tracker dot stays where it is.` with `The tracker dot moves up to the top of the free band under the middle slots (x 164..484, y from 72 px into the panel), and the strip flows right of it and wraps onto up to two more rows below.` Replace `drawn with the existing bitmap font at scale 2 (the slot labels use 3) so that eleven entries fit the panel's width` with `drawn with the existing bitmap font at scale 2 (the slot labels use 3), wrapping within the band so that eleven entries fit`. In Part 2, replace `A region that Vision returns with zero points makes the frame count as no face for that measure's channels only; the other channels still update.` with `A region that Vision returns with zero points keeps that measure at its previous frame's value; the other measures update normally.`

- [ ] **Step 5: Build and check the panel still renders**

Run: `make all 2>&1 | grep -E "Overlay|error"` then, with a ROM present:

```sh
PJ64_FACE=0 PJ64_INPUT_YAML=Config/face/super_mario_64_usa.yaml PJ64_FACE_INJECT=mouth-open \
PJ64_FRAME_DUMP=/tmp/pj64-face.ppm PJ64_FRAME_DUMP_AT=400 PJ64_TRACE=info \
perl -e 'alarm 25; exec @ARGV' -- ./Bin/macOS/Project64 Roms/sm64.z64 2>&1 | grep -E "Wrote frame|face:" || true
head -c 20 /tmp/pj64-face.ppm | head -2
```

Expected: no diagnostics naming `Overlay.cpp`; the trace says `Wrote frame 400 (640x480) at (0,160)`, proving the window grew for a face layout; no `face: tracking started`. The strip itself is only visible to a human (Task 9).

- [ ] **Step 6: Commit**

```bash
git add Source/Project64-sdl/Overlay.cpp Docs/superpowers/specs/2026-09-15-face-expressions-design.md
git commit -m "List the bound gestures in a strip under the middle slots

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: Documentation

**Files:**
- Modify: `README.md:15-27, 71-129, 131-137`
- Modify: `AGENTS.md:19-30, 105-126, 169-195`

**Interfaces:** none.

- [ ] **Step 1: README**

In the build list (lines 20-27) add after the `face-gesture-test` line:

```
make face-selftest rom=Roms/a.z64   # end-to-end face path, no camera
```

(keep the columns aligned with the lines around it). In the mouse section's last paragraph (line 128) change to: `` `make pointer-selftest rom=Roms/a.z64` proves the mouse path end to end, and `make face-selftest rom=Roms/a.z64` the face path, the same way `make grid-selftest` proves the grid's key broadcast. Neither opens the camera. ``

Insert a new section after the "Playing with a mouse" section (before "## What works"):

````markdown
## Playing with your face

A layout can bind every button to a facial gesture and the stick to your head, so the
webcam is the whole controller. Two ship under `Config/face/`, chosen explicitly (they are
never picked up by ROM name):

```sh
make run rom=Roms/sm64.z64 input=Config/face/super_mario_64_usa.yaml
make run rom=Roms/mk64.z64 input=Config/face/mario_kart_64_u.yaml
```

The camera starts by itself for these layouts, with the same permission prompt and privacy
rules as above. Controls a layout leaves out keep their keyboard keys.

- **Your head is the stick.** `Stick: {stick: head}` turns yaw into X and pitch into Y:
  full tilt at 15° of turn or 10° of nod from your resting pose, with a dead zone at a fifth
  of that. `{stick: head-digital}` snaps the same motion to one of four full tilts. The
  resting pose is learned over a few seconds and holds while you are tilted, so sit still
  for a moment to recentre. `PJ64_FACE_STICK_YAW` and `PJ64_FACE_STICK_PITCH` set the
  full-tilt angles in radians.
- **Eleven gestures**, each `{face: <name>}` on any button: `eyebrows`, `head-left`,
  `head-right`, `head-up`, `head-down`, `tilt-left`, `tilt-right`, `mouth-open`, `smile`,
  `wink-left`, `wink-right`. Left and right are yours. A blink is not a wink. A file with a
  head stick cannot also bind the four `head-*` turns, since they are the stick.
- **The panel lists what is bound.** Beside the tracker dot, each bound gesture shows as
  its tag and its button (`Mo=A`, `W<=C<`), lit while held. Tags: `Br` brows, `H<` `H>`
  `H^` `Hv` head, `T<` `T>` tilt, `Mo` mouth, `Sm` smile, `W<` `W>` winks.

Thresholds are per measure, each with an override: `PJ64_FACE_BROW` (0.035),
`PJ64_FACE_YAW` (0.25), `PJ64_FACE_PITCH` (0.20), `PJ64_FACE_ROLL` (0.25),
`PJ64_FACE_MOUTH` (0.06), `PJ64_FACE_SMILE` (0.05), `PJ64_FACE_EYE` (0.12); angles in
radians, the rest in Vision's face-box units. `PJ64_FACE_DEBUG=1` prints all eight measures
against their baselines once a second, which is how to tune them to your face and camera.
````

In "What works" (line 133-137) change `and a one-button mouse with optional face gestures works with a layout from `Config/mouse/` (see "Playing with a mouse")` to `a one-button mouse with optional face gestures works with a layout from `Config/mouse/`, and the face alone with one from `Config/face/` (see the two sections above)`.

- [ ] **Step 2: AGENTS.md**

In the command list near line 24 add `make face-selftest rom=Roms/a.z64          # face path end to end, camera never opened` beside the other self-tests, aligned.

Replace the "*Then each frame.*" paragraph (lines 116-124) with:

```markdown
*Then each frame.* The frontend's main loop samples the mouse (SDL3's mouse state
functions are main-thread only) and publishes it, and polls `FaceWanted` to start the
camera when `PJ64_FACE` is unset (`0` never, anything else at once).
`Source/Project64-sdl/FaceTracker.mm` runs AVFoundation and Vision on a private queue,
derives eight measures from the landmark regions (brow height, yaw, pitch, roll, inner-lip
gap, outer-lip width, each eye's aperture) and feeds them to the pure classifier in
`FaceGestures.{h,cpp}`: one rest baseline per measure, one hysteresis-and-debounce channel
per gesture bit (eleven, in `PointerState.h`'s order), and the head stick from the yaw and
pitch baselines. The tracker writes the bits and the stick (`HeadX`, `HeadY`) into the
struct. The plugin's `GetKeys` evaluates slots, the flick gate, gestures, the pointer stick
and the head stick (`{stick: head}` copies, `head-digital` snaps by quadrant) using the
pure geometry in `Source/Common/PointerLayout.h`, then writes the labels, the latched zone
and the lit quadrant back for `Overlay.cpp`, which reads the GL viewport to find the game
rectangle and paints the panel below it in `CSdlRenderWindow::SwapWindow` before the flush.
```

Replace line 126 with:

```markdown
Layouts are the `{zone:}`, `{face:}`, `{stick: pointer}` and `{stick: head|head-digital}`
YAML forms, in `Config/mouse/` and `Config/face/`. `PJ64_FACE_INJECT=<gesture>[,<x>,<y>]`
stands in for the tracker so `Scripts/face_selftest.sh` proves the path without a camera.
```

Add three traps after the "Installed mouse layouts" trap (after line 195):

```markdown
- **`Config/face/` is outside the per-ROM lookup on purpose.** `GameConfigPath` searches
  beside the ROM and then `Config/mouse/` only, so a face layout is never picked by ROM
  name and cannot shadow the mouse layout of the same base name; select it with `input=`.
  `make config` replaces the installed copies like the mouse ones.
- **Pitch and roll signs are fixed in code, not YAML.** `kPitchSign` and `kRollSign` in
  `FaceTracker.mm` are the one place each is flipped, decided by the first manual run of
  a face layout the way the yaw negation was. A tilt or nod that reads backwards is a code
  fix there; never swap gesture names in a layout to compensate.
- **The yaw baseline freezes differently once a head stick is bound.** With
  `{stick: head}` the classifier holds the yaw and pitch baselines whenever the stick is
  outside its dead zone, not only past the head-turn threshold; the plugin publishes
  `HeadStickWanted` so a mouse layout keeps the old behaviour. Read
  `FaceGesturesTest.cpp`'s two freeze cases before touching `Track`.
```

- [ ] **Step 3: Check wrap and line endings**

Run: `file README.md AGENTS.md && awk 'length > 92 { print FILENAME ":" FNR ": " length }' README.md AGENTS.md`
Expected: both LF; no new lines over the width the files already use (pre-existing overages are fine; new ones are not).

- [ ] **Step 4: Commit**

```bash
git add README.md AGENTS.md
git commit -m "Document face-only play, the head stick and the gesture strip

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: Manual verification (human partner)

This task is the human partner's and is not dispatched to a subagent: it starts the camera.

- [ ] **Step 1: Super Mario 64** — `make run rom=Roms/sm64.z64 input=Config/face/super_mario_64_usa.yaml PJ64_FACE_DEBUG=1`. Confirm: the window is 640x640; the panel shows the dot and seven strip entries; a head turn left moves Mario left (if reversed, the yaw negation is wrong; it was confirmed once already, so expect it right); a nod up moves him forward — if it moves him back, flip `kPitchSign` in `FaceTracker.mm`; a left-ear-down tilt lights `T<=R` — if `T>=St` lights instead, flip `kRollSign`; mouth open jumps; brows crouch; winks turn the camera; a blink does nothing.
- [ ] **Step 2: Mario Kart 64** — `make run rom=Roms/mk64.z64 input=Config/face/mario_kart_64_u.yaml`. Steer a lap on the analog head stick, hop on brows, and decide whether holding the mouth open to accelerate is bearable. If not, move `A` in `Config/face/mario_kart_64_u.yaml` to `{face: smile}` or `{face: eyebrows}` and swap the displaced gesture; it is one line either way.
- [ ] **Step 3: Tune** — the defaults for `PJ64_FACE_MOUTH`, `PJ64_FACE_SMILE`, `PJ64_FACE_EYE`, `PJ64_FACE_PITCH` and `PJ64_FACE_ROLL` are guesses; use the debug print to pick values that set cleanly and rest cleanly for your face, then change the defaults in `GestureThresholds` and the numbers in the README and the spec's Part 2 table, with the tests' `Th.*` checks.
- [ ] **Step 4: Commit any sign flip or default change** with a message naming which run decided it, ending with the standard trailer.
