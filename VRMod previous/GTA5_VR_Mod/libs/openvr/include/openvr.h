// OpenVR API header placeholder
// This is a minimal placeholder for compilation
// Download actual SDK from https://github.com/ValveSoftware/openvr

#ifndef OPENVR_H
#define OPENVR_H

#include <stdint.h>

namespace vr {

// OpenVR version
static const uint32_t k_nSteamVRVersionMajor = 1;
static const uint32_t k_nSteamVRVersionMinor = 23;
static const uint32_t k_nSteamVRVersionBuild = 7;

// Eye constants
enum EVREye {
    Eye_Left = 0,
    Eye_Right = 1
};

// Tracking result
enum ETrackingResult {
    TrackingResult_Uninitialized = 1,
    TrackingResult_Calibrating_InProgress = 100,
    TrackingResult_Calibrating_OutOfRange = 101,
    TrackingResult_Running_OK = 200,
    TrackingResult_Running_OutOfRange = 201,
    TrackingResult_Fallback_RotationOnly = 300,
};

// Device class
enum ETrackedDeviceClass {
    TrackedDeviceClass_Invalid = 0,
    TrackedDeviceClass_HMD = 1,
    TrackedDeviceClass_Controller = 2,
    TrackedDeviceClass_GenericTracker = 3,
    TrackedDeviceClass_TrackingReference = 4,
    TrackedDeviceClass_DisplayRedirect = 5,
    TrackedDeviceClass_Max
};

// Controller role
enum ETrackedControllerRole {
    TrackedControllerRole_Invalid = 0,
    TrackedControllerRole_LeftHand = 1,
    TrackedControllerRole_RightHand = 2,
    TrackedControllerRole_OptOut = 3,
    TrackedControllerRole_Treadmill = 4,
    TrackedControllerRole_Stylus = 5,
    TrackedControllerRole_Max = 5
};

// Button IDs
enum EVRButtonId {
    k_EButton_System = 0,
    k_EButton_ApplicationMenu = 1,
    k_EButton_Grip = 2,
    k_EButton_DPad_Left = 3,
    k_EButton_DPad_Up = 4,
    k_EButton_DPad_Right = 5,
    k_EButton_DPad_Down = 6,
    k_EButton_A = 7,
    k_EButton_ProximitySensor = 31,
    k_EButton_Axis0 = 32,
    k_EButton_Axis1 = 33,
    k_EButton_Axis2 = 34,
    k_EButton_Axis3 = 35,
    k_EButton_Axis4 = 36,
    k_EButton_SteamVR_Touchpad = k_EButton_Axis0,
    k_EButton_SteamVR_Trigger = k_EButton_Axis1,
    k_EButton_Dashboard_Back = k_EButton_Grip,
    k_EButton_IndexController_A = k_EButton_Grip,
    k_EButton_IndexController_B = k_EButton_ApplicationMenu,
    k_EButton_IndexController_JoyStick = k_EButton_Axis3,
    k_EButton_Max = 64
};

// VR Event types
enum EVREventType {
    VREvent_None = 0,
    VREvent_TrackedDeviceActivated = 100,
    VREvent_TrackedDeviceDeactivated = 101,
    VREvent_TrackedDeviceUpdated = 102,
    VREvent_TrackedDeviceUserInteractionStarted = 103,
    VREvent_TrackedDeviceUserInteractionEnded = 104,
    VREvent_ButtonPress = 200,
    VREvent_ButtonUnpress = 201,
    VREvent_ButtonTouch = 202,
    VREvent_ButtonUntouch = 203,
    VREvent_Quit = 700,
    VREvent_ProcessQuit = 701,
};

// Texture types
enum ETextureType {
    TextureType_Invalid = -1,
    TextureType_DirectX = 0,
    TextureType_OpenGL = 1,
    TextureType_Vulkan = 2,
    TextureType_IOSurface = 3,
    TextureType_DirectX12 = 4,
    TextureType_DXGISharedHandle = 5,
    TextureType_Metal = 6
};

// Color space
enum EColorSpace {
    ColorSpace_Auto = 0,
    ColorSpace_Gamma = 1,
    ColorSpace_Linear = 2
};

// Submit flags
enum EVRSubmitFlags {
    Submit_Default = 0x00,
    Submit_LensDistortionAlreadyApplied = 0x01,
    Submit_GlRenderBuffer = 0x02,
    Submit_Reserved = 0x04,
    Submit_TextureWithPose = 0x08,
    Submit_TextureWithDepth = 0x10,
    Submit_FrameDiscontinuty = 0x20,
    Submit_VulkanTextureWithArrayData = 0x40,
    Submit_GlArrayTexture = 0x80,
    Submit_Reserved2 = 0x8000,
};

// Compositor error
enum EVRCompositorError {
    VRCompositorError_None = 0,
    VRCompositorError_RequestFailed = 1,
    VRCompositorError_IncompatibleVersion = 100,
    VRCompositorError_DoNotHaveFocus = 101,
    VRCompositorError_InvalidTexture = 102,
    VRCompositorError_IsNotSceneApplication = 103,
    VRCompositorError_TextureIsOnWrongDevice = 104,
    VRCompositorError_TextureUsesUnsupportedFormat = 105,
    VRCompositorError_SharedTexturesNotSupported = 106,
    VRCompositorError_IndexOutOfRange = 107,
    VRCompositorError_AlreadySubmitted = 108,
    VRCompositorError_InvalidBounds = 109,
    VRCompositorError_AlreadySet = 110,
};

// Init error
enum EVRInitError {
    VRInitError_None = 0,
    VRInitError_Unknown = 1,
    VRInitError_Init_InstallationNotFound = 100,
    VRInitError_Init_InstallationCorrupt = 101,
    VRInitError_Init_VRClientDLLNotFound = 102,
    VRInitError_Init_FileNotFound = 103,
    VRInitError_Init_FactoryNotFound = 104,
    VRInitError_Init_InterfaceNotFound = 105,
    VRInitError_Init_InvalidInterface = 106,
    VRInitError_Init_UserConfigDirectoryInvalid = 107,
    VRInitError_Init_HmdNotFound = 108,
    VRInitError_Init_NotInitialized = 109,
    VRInitError_Init_PathRegistryNotFound = 110,
    VRInitError_Init_NoConfigPath = 111,
    VRInitError_Init_NoLogPath = 112,
    VRInitError_Init_PathRegistryNotWritable = 113,
    VRInitError_Init_AppInfoInitFailed = 114,
};

// Application type
enum EVRApplicationType {
    VRApplication_Other = 0,
    VRApplication_Scene = 1,
    VRApplication_Overlay = 2,
    VRApplication_Background = 3,
    VRApplication_Utility = 4,
    VRApplication_VRMonitor = 5,
    VRApplication_SteamWatchdog = 6,
    VRApplication_Bootstrapper = 7,
    VRApplication_WebHelper = 8,
    VRApplication_OpenXRRuntime = 9,
    VRApplication_Max
};

// Tracked device index
typedef uint32_t TrackedDeviceIndex_t;
static const TrackedDeviceIndex_t k_unTrackedDeviceIndex_Hmd = 0;
static const TrackedDeviceIndex_t k_unTrackedDeviceIndexInvalid = 0xFFFFFFFF;
static const TrackedDeviceIndex_t k_unMaxTrackedDeviceCount = 64;

// Matrix types
struct HmdMatrix34_t {
    float m[3][4];
};

struct HmdMatrix44_t {
    float m[4][4];
};

struct HmdVector3_t {
    float v[3];
};

struct HmdVector2_t {
    float v[2];
};

struct HmdQuaternion_t {
    double w, x, y, z;
};

struct HmdQuaternionf_t {
    float w, x, y, z;
};

// Tracking pose
struct TrackedDevicePose_t {
    HmdMatrix34_t mDeviceToAbsoluteTracking;
    HmdVector3_t vVelocity;
    HmdVector3_t vAngularVelocity;
    ETrackingResult eTrackingResult;
    bool bPoseIsValid;
    bool bDeviceIsConnected;
};

// Texture
struct Texture_t {
    void* handle;
    ETextureType eType;
    EColorSpace eColorSpace;
};

// Texture bounds
struct VRTextureBounds_t {
    float uMin, vMin;
    float uMax, vMax;
};

// Controller state
struct VRControllerAxis_t {
    float x;
    float y;
};

struct VRControllerState001_t {
    uint32_t unPacketNum;
    uint64_t ulButtonPressed;
    uint64_t ulButtonTouched;
    VRControllerAxis_t rAxis[5];
};
typedef VRControllerState001_t VRControllerState_t;

// VR Event data
struct VREvent_Controller_t {
    uint32_t button;
};

struct VREvent_Data_t {
    VREvent_Controller_t controller;
    // Other event data types would go here
};

struct VREvent_t {
    uint32_t eventType;
    TrackedDeviceIndex_t trackedDeviceIndex;
    float eventAgeSeconds;
    VREvent_Data_t data;
};

// Interface class declarations (forward declarations)
class IVRSystem;
class IVRCompositor;
class IVRInput;
class IVROverlay;

// Interface accessor functions
IVRSystem* VR_GetVRSystem();
IVRCompositor* VR_GetVRCompositor();
IVRInput* VR_GetVRInput();

// IVRSystem interface
class IVRSystem {
public:
    virtual void GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) = 0;
    virtual HmdMatrix44_t GetProjectionMatrix(EVREye eEye, float fNearZ, float fFarZ) = 0;
    virtual void GetProjectionRaw(EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom) = 0;
    virtual bool ComputeDistortion(EVREye eEye, float fU, float fV, struct DistortionCoordinates_t* pDistortionCoordinates) = 0;
    virtual HmdMatrix34_t GetEyeToHeadTransform(EVREye eEye) = 0;
    virtual bool GetTimeSinceLastVsync(float* pfSecondsSinceLastVsync, uint64_t* pulFrameCounter) = 0;
    virtual void GetDeviceToAbsoluteTrackingPose(uint32_t eOrigin, float fPredictedSecondsToPhotonsFromNow,
        TrackedDevicePose_t* pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount) = 0;
    virtual bool PollNextEvent(VREvent_t* pEvent, uint32_t uncbVREvent) = 0;
    virtual bool GetControllerState(TrackedDeviceIndex_t unControllerDeviceIndex,
        VRControllerState_t* pControllerState, uint32_t unControllerStateSize) = 0;
    virtual void TriggerHapticPulse(TrackedDeviceIndex_t unControllerDeviceIndex,
        uint32_t unAxisId, unsigned short usDurationMicroSec) = 0;
    virtual ETrackedDeviceClass GetTrackedDeviceClass(TrackedDeviceIndex_t unDeviceIndex) = 0;
    virtual bool IsTrackedDeviceConnected(TrackedDeviceIndex_t unDeviceIndex) = 0;
    virtual ETrackedControllerRole GetControllerRoleForTrackedDeviceIndex(TrackedDeviceIndex_t unDeviceIndex) = 0;
};

// IVRCompositor interface
class IVRCompositor {
public:
    virtual void SetTrackingSpace(uint32_t eOrigin) = 0;
    virtual uint32_t GetTrackingSpace() = 0;
    virtual EVRCompositorError WaitGetPoses(TrackedDevicePose_t* pRenderPoseArray, uint32_t unRenderPoseArrayCount,
        TrackedDevicePose_t* pGamePoseArray, uint32_t unGamePoseArrayCount) = 0;
    virtual EVRCompositorError Submit(EVREye eEye, const Texture_t* pTexture,
        const VRTextureBounds_t* pBounds = nullptr, EVRSubmitFlags nSubmitFlags = Submit_Default) = 0;
    virtual void ClearLastSubmittedFrame() = 0;
    virtual void PostPresentHandoff() = 0;
    virtual bool GetFrameTiming(void* pTiming, uint32_t unFramesAgo = 0) = 0;
    virtual uint32_t GetFrameTimings(void* pTiming, uint32_t nFrames) = 0;
    virtual float GetFrameTimeRemaining() = 0;
};

// Initialization functions
inline IVRSystem* VR_Init(EVRInitError* peError, EVRApplicationType eType) {
    // Placeholder - actual implementation in openvr_api.dll
    if (peError) *peError = VRInitError_Init_HmdNotFound;
    return nullptr;
}

inline void VR_Shutdown() {
    // Placeholder
}

inline bool VR_IsHmdPresent() {
    // Placeholder
    return false;
}

inline bool VR_IsRuntimeInstalled() {
    // Placeholder
    return false;
}

inline const char* VR_GetVRInitErrorAsSymbol(EVRInitError error) {
    // Placeholder
    return "Unknown";
}

inline const char* VR_GetVRInitErrorAsEnglishDescription(EVRInitError error) {
    // Placeholder
    return "Unknown error";
}

// Interface version strings
static const char* const IVRSystem_Version = "IVRSystem_022";
static const char* const IVRCompositor_Version = "IVRCompositor_027";
static const char* const IVRInput_Version = "IVRInput_010";

} // namespace vr

#endif // OPENVR_H
