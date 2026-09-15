// Project64 - A Nintendo 64 emulator
// Webcam face gestures: AVFoundation capture -> Vision face landmarks -> GestureClassifier.
// Frames are handled in the capture callback and released with it; nothing is stored,
// previewed or written. Only PJ64_FACE_DEBUG prints the two measures, once a second.
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

static GestureThresholds ThresholdsFromEnv(void)
{
    GestureThresholds T;
    const char * Brow = getenv("PJ64_FACE_BROW");
    if (Brow != nullptr && atof(Brow) > 0.0) T.Brow = (float)atof(Brow);
    const char * Yaw = getenv("PJ64_FACE_YAW");
    if (Yaw != nullptr && atof(Yaw) > 0.0) T.Yaw = (float)atof(Yaw);
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

@interface PJ64FaceDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
{
    PointerState * m_State;
    GestureClassifier * m_Classifier;
    VNDetectFaceLandmarksRequest * m_Request;
    bool m_Debug;
    double m_LastDebug;
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
    GestureSample S = { false, 0.0f, 0.0f, MonotonicSeconds() };
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
            const float BrowY = 0.5f * (RegionMeanY(L.leftEyebrow) + RegionMeanY(L.rightEyebrow));
            const float EyeY = 0.5f * (RegionMeanY(L.leftEye) + RegionMeanY(L.rightEye));
            S.FaceFound = true;
            S.BrowHeight = BrowY - EyeY;
            // Vision reports yaw for the face as seen by the camera. A front camera is not
            // mirrored, so the player's left turn arrives as a positive yaw; negate so the
            // classifier's "negative is head-left" holds. Confirm on first run (spec Part 6).
            S.Yaw = Best.yaw != nil ? -Best.yaw.floatValue : 0.0f;
        }
    }

    const uint32_t Bits = m_Classifier->Update(S);
    m_State->Gestures.store(Bits, std::memory_order_relaxed);
    m_State->Face.store(S.FaceFound ? FACE_TRACKING : FACE_NO_FACE, std::memory_order_relaxed);

    if (m_Debug && S.Time - m_LastDebug >= 1.0)
    {
        m_LastDebug = S.Time;
        fprintf(stderr, "face: found=%d brow=%.4f (base %.4f) yaw=%.3f (base %.3f) bits=%u\n",
            S.FaceFound ? 1 : 0, S.BrowHeight, m_Classifier->BrowBaseline(),
            S.Yaw, m_Classifier->YawBaseline(), Bits);
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
