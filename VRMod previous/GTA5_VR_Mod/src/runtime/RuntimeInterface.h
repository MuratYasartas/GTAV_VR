#pragma once

#include <d3d11.h>
#include <string>
#include <vector>
#include <cstdint>

// OpenXR types forward declaration
typedef uint64_t XrTime;
struct XrPosef;
struct XrFovf;
struct XrView;
struct XrFrameState;

namespace GTA5VR {

// Eye indices
enum class Eye : uint32_t {
    Left = 0,
    Right = 1
};

// VR hand
enum class VRHand : uint32_t {
    Left = 0,
    Right = 1
};

// View data for an eye
struct EyeView {
    float position[3];
    float orientation[4]; // Quaternion (x, y, z, w)
    float fovLeft;
    float fovRight;
    float fovUp;
    float fovDown;
};

// Controller pose data
struct ControllerPose {
    float position[3];
    float orientation[4];
    float linearVelocity[3];
    float angularVelocity[3];
    bool isValid;
};

// Controller input state
struct ControllerState {
    float trigger;
    float grip;
    float thumbstickX;
    float thumbstickY;
    bool thumbstickClick;
    bool primaryButton;   // A/X
    bool secondaryButton; // B/Y
    bool menuButton;
    bool systemButton;
};

// Abstract VR runtime interface
class IRuntimeInterface {
public:
    virtual ~IRuntimeInterface() = default;

    // Lifecycle
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsInitialized() const = 0;

    // Session management
    virtual bool CreateSession(ID3D11Device* device) = 0;
    virtual bool DestroySession() = 0;
    virtual bool IsSessionRunning() const = 0;

    // Swapchain management
    virtual bool CreateSwapchains(uint32_t width, uint32_t height) = 0;
    virtual bool AcquireSwapchainImage(uint32_t eyeIndex, uint32_t* imageIndex) = 0;
    virtual bool ReleaseSwapchainImage(uint32_t eyeIndex) = 0;
    virtual ID3D11Texture2D* GetSwapchainTexture(uint32_t eyeIndex, uint32_t imageIndex) = 0;

    // Frame lifecycle
    virtual bool WaitFrame() = 0;
    virtual bool BeginFrame() = 0;
    virtual bool EndFrame() = 0;
    virtual bool SubmitFrame(ID3D11Texture2D* leftEye, ID3D11Texture2D* rightEye) = 0;

    // Tracking
    virtual bool GetHeadPose(XrPosef* pose) = 0;
    virtual bool GetEyeViews(std::vector<EyeView>& views) = 0;
    virtual bool GetControllerPose(VRHand hand, ControllerPose& pose) = 0;
    virtual bool GetControllerState(VRHand hand, ControllerState& state) = 0;

    // Input
    virtual bool SyncActions() = 0;
    virtual void TriggerHaptic(VRHand hand, float amplitude, float duration) = 0;

    // Configuration
    virtual void GetRecommendedRenderSize(uint32_t* width, uint32_t* height) const = 0;
    virtual float GetRefreshRate() const = 0;
    virtual bool SupportsDepthSubmission() const = 0;

    // Runtime info
    virtual std::string GetRuntimeName() const = 0;
    virtual std::string GetRuntimeVersion() const = 0;
    virtual std::string GetHMDName() const = 0;

    // Utility
    virtual XrTime GetPredictedDisplayTime() const = 0;
    virtual bool IsHMDMounted() const = 0;
};

} // namespace GTA5VR
