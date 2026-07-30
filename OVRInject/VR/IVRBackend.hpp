#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <memory>

using namespace DirectX;

#ifdef OVRINJECT_EXPORTS
#define OVR_API __declspec(dllexport)
#else
#define OVR_API __declspec(dllimport)
#endif

namespace OVRInject {
namespace VR {

/**
 * Eye enumeration
 */
enum class Eye {
    Left = 0,
    Right = 1,
    Count = 2
};

/**
 * Hand enumeration
 */
enum class Hand {
    Left = 0,
    Right = 1,
    Count = 2
};

/**
 * Controller button state - unified for all backends
 */
struct ControllerButtonState {
    // Thumbstick/touchpad position
    float thumbstickX = 0.0f;
    float thumbstickY = 0.0f;

    // Thumbstick/touchpad states
    bool thumbstickTouched = false;
    bool thumbstickPressed = false;
    bool thumbstickJustPressed = false;
    bool thumbstickJustReleased = false;

    // Grip
    float gripValue = 0.0f;
    bool gripPressed = false;
    bool gripJustPressed = false;
    bool gripJustReleased = false;

    // Trigger
    float triggerValue = 0.0f;
    bool triggerPressed = false;
    bool triggerJustPressed = false;
    bool triggerJustReleased = false;

    // Primary button (A/X)
    bool primaryPressed = false;
    bool primaryJustPressed = false;
    bool primaryJustReleased = false;

    // Secondary button (B/Y)
    bool secondaryPressed = false;
    bool secondaryJustPressed = false;
    bool secondaryJustReleased = false;

    // Menu button
    bool menuPressed = false;
    bool menuJustPressed = false;
    bool menuJustReleased = false;

    // State validity
    bool valid = false;
};

/**
 * Controller tracking state
 */
struct ControllerState {
    XMMATRIX poseMatrix = {};
    XMFLOAT3 position = {0, 0, 0};
    XMFLOAT4 rotation = {0, 0, 0, 1};  // Quaternion

    ControllerButtonState buttons;

    bool isTracked = false;
};

/**
 * IVRBackend - Abstract interface for VR runtime backends
 *
 * Provides a unified API that can be implemented by:
 * - OpenVR (SteamVR)
 * - OpenXR (Quest Link, WMR, SteamVR OpenXR, etc.)
 */
class IVRBackend {
public:
    virtual ~IVRBackend() = default;

    //-------------------------------------------------------------------------
    // Lifecycle
    //-------------------------------------------------------------------------

    /**
     * Initialize the VR backend
     * @param device D3D11 device from the game
     * @return true on success
     */
    virtual bool Initialize(ID3D11Device* device) = 0;

    /**
     * Provide the game's swap chain when available (optional for some backends)
     * @param swap_chain Game's DXGI swap chain
     */
    virtual void SetSwapChain(IDXGISwapChain* swap_chain) { (void)swap_chain; }

    /**
     * Shutdown and cleanup
     */
    virtual void Shutdown() = 0;

    /**
     * Check if initialized
     */
    virtual bool IsInitialized() const = 0;

    //-------------------------------------------------------------------------
    // Frame Lifecycle
    //-------------------------------------------------------------------------

    /**
     * Begin a new VR frame
     * Waits for compositor timing and updates tracking
     * @return true if we should render this frame
     */
    virtual bool BeginFrame() = 0;

    /**
     * End the VR frame
     * Submits composition layers
     */
    virtual void EndFrame() = 0;

    /**
     * Submit an eye texture to the compositor
     * @param eye Which eye
     * @param texture D3D11 texture containing rendered view
     */
    virtual void SubmitEyeTexture(
        Eye eye,
        ID3D11Texture2D* texture,
        const XMMATRIX* renderedPose = nullptr) = 0;

    /**
     * Refresh tracking at the latest safe point before the camera write.
     * OpenXR already locates predicted views in BeginFrame. OpenVR overrides
     * this so WaitGetPoses happens after submitting the image rendered from
     * the previous pose, keeping compositor timing and game rendering paired.
     */
    virtual bool PrepareForCameraWrite() { return false; }

    //-------------------------------------------------------------------------
    // Tracking - Head
    //-------------------------------------------------------------------------

    /**
     * Get head pose matrix
     */
    virtual XMMATRIX GetHeadPoseMatrix() const = 0;

    /**
     * Get head position
     */
    virtual XMFLOAT3 GetHeadPosition() const = 0;

    /**
     * Get head forward vector
     */
    virtual XMFLOAT3 GetHeadForward() const = 0;

    /**
     * Get head up vector
     */
    virtual XMFLOAT3 GetHeadUp() const = 0;

    /**
     * Get head rotation as euler angles (degrees)
     */
    virtual XMFLOAT3 GetHeadRotation() const = 0;

    //-------------------------------------------------------------------------
    // Tracking - Controllers
    //-------------------------------------------------------------------------

    /**
     * Update controller tracking and input
     */
    virtual void UpdateControllers() = 0;

    /**
     * Get controller state
     */
    virtual const ControllerState& GetControllerState(Hand hand) const = 0;

    /**
     * Check if controller is being tracked
     */
    virtual bool IsControllerTracked(Hand hand) const = 0;

    //-------------------------------------------------------------------------
    // Haptics
    //-------------------------------------------------------------------------

    /**
     * Trigger haptic feedback
     * @param hand Which controller
     * @param duration Duration in seconds
     * @param frequency Frequency in Hz (0 for default)
     * @param amplitude Amplitude 0.0 to 1.0
     */
    virtual void TriggerHaptic(Hand hand, float duration, float frequency, float amplitude) = 0;

    //-------------------------------------------------------------------------
    // View Configuration
    //-------------------------------------------------------------------------

    /**
     * Get recommended render width per eye
     */
    virtual uint32_t GetRecommendedWidth() const = 0;

    /**
     * Get recommended render height per eye
     */
    virtual uint32_t GetRecommendedHeight() const = 0;

    /**
     * Raw runtime-recommended size (unclamped, independent of the current
     * swapchain). Eye-texture sizing must use this SAME base as the XR
     * swapchain sizing, otherwise the two drift to different sizes and the
     * submit copy crops (the "renderScale deforms the image" bug).
     */
    virtual uint32_t GetRawRecommendedWidth() const { return GetRecommendedWidth(); }
    virtual uint32_t GetRawRecommendedHeight() const { return GetRecommendedHeight(); }

    /**
     * Get projection matrix for an eye
     * @param nearZ Near clip plane
     * @param farZ Far clip plane
     */
    virtual XMMATRIX GetProjectionMatrix(Eye eye, float nearZ, float farZ) const = 0;

    /**
     * Get view matrix for an eye
     */
    virtual XMMATRIX GetViewMatrix(Eye eye) const = 0;

    //-------------------------------------------------------------------------
    // Overlay
    //-------------------------------------------------------------------------

    /**
     * Show the settings/menu overlay
     */
    virtual void ShowOverlay() = 0;

    /**
     * Hide the settings/menu overlay
     */
    virtual void HideOverlay() = 0;

    /**
     * Toggle overlay visibility
     */
    virtual void ToggleOverlay() = 0;

    /**
     * Render overlay content
     */
    virtual void RenderOverlay() = 0;

    /**
     * Check if overlay is visible
     */
    virtual bool IsOverlayVisible() const { return false; }

    /**
     * Check if overlay wants to capture desktop input
     */
    virtual bool WantsOverlayInputCapture() const { return false; }

    //-------------------------------------------------------------------------
    // Info
    //-------------------------------------------------------------------------

    /**
     * Get the runtime name (e.g., "OpenVR", "OpenXR")
     */
    virtual const char* GetRuntimeName() const = 0;

    /**
     * Get the HMD/system name
     */
    virtual const char* GetSystemName() const = 0;

    virtual ID3D11Device* GetDevice() const { return nullptr; }
    virtual void* GetSession() const { return nullptr; }
    virtual XMMATRIX GetEyeMatrix(Eye eye) const { return XMMatrixIdentity(); }
    virtual DXGI_FORMAT GetPreferredSwapchainFormat() const { return DXGI_FORMAT_UNKNOWN; }

    virtual void Recenter() {}
};

} // namespace VR
} // namespace OVRInject
