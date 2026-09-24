// Project64 - A Nintendo 64 emulator
// Webcam face gestures: AVFoundation capture -> Vision face landmarks -> GestureClassifier.
// Frames are handled in the capture callback and released with it; nothing is stored,
// previewed or written. Only PJ64_FACE_DEBUG prints the eight measures, once a second.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#import "FaceTracker.h"
#import "FaceGestures.h"
#import <Common/PointerState.h>

#import <AVFoundation/AVFoundation.h>
#import <Vision/Vision.h>
#import <mach/mach_time.h>
#import <stdio.h>
#import <stdlib.h>

static double MonotonicSeconds(void)
{
    static mach_timebase_info_data_t Info = { 0, 0 };
    if (Info.denom == 0) mach_timebase_info(&Info);
    return (double)mach_absolute_time() * (double)Info.numer / (double)Info.denom / 1e9;
}

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

// Mean y of a landmark region in face-box-normalised coordinates (origin bottom-left).
static float RegionMeanY(VNFaceLandmarkRegion2D * Region)
{
    if (Region == nil || Region.pointCount == 0) return 0.0f;
    const CGPoint * Points = Region.normalizedPoints;
    double Sum = 0.0;
    for (NSUInteger i = 0; i < Region.pointCount; i++) Sum += Points[i].y;
    return (float)(Sum / (double)Region.pointCount);
}

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

@interface PJ64FaceDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
{
    PointerState * m_State;
    GestureClassifier * m_Classifier;
    VNDetectFaceLandmarksRequest * m_Request;
    bool m_Debug;
    double m_LastDebug;
    float m_LastM[FACE_MEASURE_COUNT];
}
- (instancetype)initWithState:(PointerState *)State;
@end

@implementation PJ64FaceDelegate

- (instancetype)initWithState:(PointerState *)State
{
    self = [super init];
    if (self)
    {
        m_State = State;
        m_Classifier = new GestureClassifier(ThresholdsFromEnv());
        m_Request = [[VNDetectFaceLandmarksRequest alloc] init];
        m_Debug = getenv("PJ64_FACE_DEBUG") != nullptr;
        m_LastDebug = 0.0;
        for (int i = 0; i < FACE_MEASURE_COUNT; i++) m_LastM[i] = 0.0f;
    }
    return self;
}

- (void)dealloc
{
    delete m_Classifier;
}

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection
{
    CVPixelBufferRef Pixels = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (Pixels == nullptr) return;

    VNImageRequestHandler * Handler = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:Pixels
                                                                              orientation:kCGImagePropertyOrientationUp
                                                                                  options:@{}];
    NSError * Error = nil;
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

    // The menu's Recentre, requested from the main thread; handled here, on this queue,
    // where the classifier lives.
    if (m_State->RecentreFace.exchange(0, std::memory_order_acq_rel) != 0)
    {
        m_Classifier->Rebaseline();
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
}

@end

static AVCaptureSession * g_Session = nil;
static PJ64FaceDelegate * g_Delegate = nil;
static dispatch_queue_t g_Queue = nil;
static bool g_Started = false;

static bool StartSession(PointerState * State)
{
    AVCaptureDevice * Device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (Device == nil)
    {
        fprintf(stderr, "face: no camera found; gestures are off\n");
        State->Face.store(FACE_ERROR);
        return false;
    }
    NSError * Error = nil;
    AVCaptureDeviceInput * Input = [AVCaptureDeviceInput deviceInputWithDevice:Device error:&Error];
    if (Input == nil)
    {
        fprintf(stderr, "face: could not open the camera: %s; gestures are off\n",
            Error.localizedDescription.UTF8String);
        State->Face.store(FACE_ERROR);
        return false;
    }
    g_Session = [[AVCaptureSession alloc] init];
    g_Session.sessionPreset = AVCaptureSessionPreset640x480;
    if (![g_Session canAddInput:Input])
    {
        fprintf(stderr, "face: camera input rejected; gestures are off\n");
        State->Face.store(FACE_ERROR);
        g_Session = nil;
        return false;
    }
    [g_Session addInput:Input];

    AVCaptureVideoDataOutput * Output = [[AVCaptureVideoDataOutput alloc] init];
    Output.alwaysDiscardsLateVideoFrames = YES;
    Output.videoSettings = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
    g_Queue = dispatch_queue_create("pj64.face", DISPATCH_QUEUE_SERIAL);
    g_Delegate = [[PJ64FaceDelegate alloc] initWithState:State];
    [Output setSampleBufferDelegate:g_Delegate queue:g_Queue];
    if (![g_Session canAddOutput:Output])
    {
        fprintf(stderr, "face: camera output rejected; gestures are off\n");
        State->Face.store(FACE_ERROR);
        g_Session = nil;
        return false;
    }
    [g_Session addOutput:Output];
    [g_Session startRunning];
    State->Face.store(FACE_NO_FACE);
    fprintf(stderr, "face: tracking started (%s)\n", Device.localizedName.UTF8String);
    return true;
}

bool FaceTrackerStart(PointerState * State)
{
    if (g_Started || State == nullptr) return false;
    g_Started = true;
    State->Face.store(FACE_STARTING);

    const AVAuthorizationStatus Status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    if (Status == AVAuthorizationStatusAuthorized)
    {
        return StartSession(State);
    }
    if (Status == AVAuthorizationStatusDenied || Status == AVAuthorizationStatusRestricted)
    {
        fprintf(stderr, "face: camera access denied for the launching app; allow it in System Settings > Privacy & Security > Camera\n");
        State->Face.store(FACE_DENIED);
        return false;
    }
    // Not determined: macOS prompts, attributed to the terminal or IDE that launched us.
    // The answer arrives on an arbitrary queue; start there, since AVCaptureSession may be
    // started from any thread.
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL Granted) {
        if (Granted)
        {
            StartSession(State);
        }
        else
        {
            fprintf(stderr, "face: camera access denied; gestures are off\n");
            State->Face.store(FACE_DENIED);
        }
    }];
    return true;
}

void FaceTrackerStop(void)
{
    if (g_Session != nil)
    {
        [g_Session stopRunning];
        g_Session = nil;
        fprintf(stderr, "face: tracking stopped\n");
    }
    g_Delegate = nil;
    g_Queue = nil;
}
