#pragma once

#include "XRCore.hpp"
#include "XRInstance.hpp"
#include "XRGraphicsBinding.hpp"
#include "XRSession.hpp"
#include "XRSwapchain.hpp"
#include "XRViewManager.hpp"
#include "XRFrameManager.hpp"
#include "XRActionManager.hpp"
#include "XROverlay.hpp"
#include "XROverlayUI.hpp"

#include "../Vive/Math/Matrices.h"

#include <memory>
#include <d3d11.h>
#include <dxgi.h>

#ifdef OVRINJECT_EXPORTS
#define OVR_API __declspec(dllexport)
#else
#define OVR_API __declspec(dllimport)
#endif

namespace OVRInject {

// Forward declaration
class XRTrackedController;

/**
 * XRHMDSupport - High-level OpenXR wrapper matching HMDSupport API
 *
 * Provides a unified interface for VR functionality:
 * - Initialization and shutdown
 * - Frame submission
 * - Head and controller tracking
 * - Overlay management
 * - Desktop mirroring
 */
class XRHMDSupport {
public:
    OVR_API XRHMDSupport();
    OVR_API virtual ~XRHMDSupport();

    /**
     * Initialize OpenXR with the game's D3D11 device
     * @param swap_chain Game's swap chain (for compatibility, may not be used)
     * @param device Game's D3D11 device
     * @return true on success
     */
    OVR_API bool Initialize(IDXGISwapChain* swap_chain, ID3D11Device* device);

    /**
     * Shutdown OpenXR
     */
    OVR_API void Shutdown();

    /**
     * Check if initialized
     */
    OVR_API bool IsInitialized() const;

    //-------------------------------------------------------------------------
    // Frame Submission
    //-------------------------------------------------------------------------

    /**
     * Submit a texture to an eye
     * @param eye_index Eye index (0=left, 1=right)
     * @param texture D3D11 texture containing the eye view
     * @param time Frame time (for timing/synchronization)
     */
    OVR_API void SubmitFrameTexture(int eye_index, ID3D11Texture2D* texture, const unsigned int& time);

    /**
     * Begin a new VR frame
     * Waits for compositor and updates tracking
     * @return true if we should render this frame
     */
    OVR_API bool BeginFrame();

    /**
     * End the current VR frame
     * Submits composition layers to the headset
     */
    OVR_API void EndFrame();

    //-------------------------------------------------------------------------
    // Tracking
    //-------------------------------------------------------------------------

    /**
     * Sync poses with the runtime
     * Updates head and controller tracking data
     */
    OVR_API void SyncOnPoses();

    /**
     * Get head pose matrix
     */
    inline Matrix4 GetHeadMatrix() const {
        return head_matrix_;
    }

    /**
     * Get head position with optional yaw adjustment
     */
    OVR_API XMFLOAT3 GetHeadPosition(float yaw = 0.0f);

    /**
     * Get head forward vector
     */
    inline XMFLOAT3 GetHeadForwardVector() const {
        if (!view_manager_) return XMFLOAT3(0, 0, -1);
        return view_manager_->GetHeadForward();
    }

    /**
     * Get head up vector
     */
    inline XMFLOAT3 GetHeadUpVector() const {
        if (!view_manager_) return XMFLOAT3(0, 1, 0);
        return view_manager_->GetHeadUp();
    }

    /**
     * Get head rotation as euler angles (pitch, yaw, roll in degrees)
     */
    inline XMFLOAT3 GetHeadRotation() const {
        if (!view_manager_) return XMFLOAT3(0, 0, 0);
        return view_manager_->GetHeadRotation();
    }

    //-------------------------------------------------------------------------
    // Controllers
    //-------------------------------------------------------------------------

    /**
     * Get left hand controller
     */
    OVR_API XRTrackedController* GetLeftHand();

    /**
     * Get right hand controller
     */
    OVR_API XRTrackedController* GetRightHand();

    /**
     * Update controller button states
     */
    OVR_API void UpdateControllers();

    //-------------------------------------------------------------------------
    // View Information
    //-------------------------------------------------------------------------

    /**
     * Get recommended render width for a single eye
     */
    OVR_API uint32_t GetRecommendedWidth() const;
    OVR_API uint32_t GetRawRecommendedWidth() const;
    OVR_API uint32_t GetRawRecommendedHeight() const;

    /**
     * Get recommended render height for a single eye
     */
    OVR_API uint32_t GetRecommendedHeight() const;

    /**
     * Get projection matrix for an eye
     */
    OVR_API XMMATRIX GetProjectionMatrix(XR::Eye eye) const;

    /**
     * Get view matrix for an eye
     */
    OVR_API XMMATRIX GetViewMatrix(XR::Eye eye) const;

    /**
     * Get eye offset matrix for an eye
     */
    OVR_API XMMATRIX GetEyeMatrix(XR::Eye eye) const;

    /**
     * Get the D3D11 device
     */
    ID3D11Device* GetDevice() const { return device_; }

    /**
     * Get the swapchain format used for eye textures
     */
    OVR_API DXGI_FORMAT GetSwapchainFormat() const;

    /**
     * Force swapchain format to match the game's backbuffer
     */
    OVR_API void SetSwapchainFormat(DXGI_FORMAT format);

    //-------------------------------------------------------------------------
    // Overlay
    //-------------------------------------------------------------------------

    /**
     * Get overlay manager
     */
    XR::XROverlayManager* GetOverlayManager() { return overlay_manager_.get(); }

    /**
     * Toggle overlay visibility
     */
    OVR_API void ToggleOverlay();

    /**
     * Show/hide overlay
     */
    OVR_API void SetOverlayVisible(bool visible);

    /**
     * Check overlay state
     */
    OVR_API bool IsOverlayVisible() const;
    OVR_API bool WantsOverlayInputCapture() const;

    //-------------------------------------------------------------------------
    // Settings
    //-------------------------------------------------------------------------

    OVR_API void SetDesktopMirroring(bool enable);
    OVR_API bool GetDesktopMirroring() const { return desktop_mirroring_; }

    OVR_API void SetFrameHueristic(int frameHuer);
    OVR_API void SetSpinlock(bool enabled);

    OVR_API void SetHS(float hs);
    OVR_API void SetVS(float vs);
    OVR_API void SetZS(float zs);

    //-------------------------------------------------------------------------
    // Singleton Access
    //-------------------------------------------------------------------------

    OVR_API static XRHMDSupport* Singleton();

    //-------------------------------------------------------------------------
    // Component Access
    //-------------------------------------------------------------------------

    XR::XRInstance* GetInstance() { return instance_.get(); }
    XR::XRSession* GetSession() { return session_.get(); }
    XR::XRViewManager* GetViewManager() { return view_manager_.get(); }
    XR::XRFrameManager* GetFrameManager() { return frame_manager_.get(); }
    XR::XRActionManager* GetActionManager() { return action_manager_.get(); }

private:
    /**
     * Handle session state changes
     */
    void OnSessionStateChanged(XR::SessionState oldState, XR::SessionState newState);

    /**
     * Copy texture to swapchain image
     */
    void CopyTextureToSwapchain(ID3D11Texture2D* source, XR::Eye eye);

    /**
     * Keyboard overlay toggle (Delete/Insert/F10), polled every frame
     * regardless of session visibility so the menu always opens.
     */
    void PollOverlayKeyboardToggle();

    /**
     * Update overlay input state and visibility
     */
    void UpdateOverlayUI(const XR::ControllerState& leftState,
                         const XR::ControllerState& rightState);

    /**
     * Render overlay UI into its swapchain
     */
    void RenderOverlayUI();

    /**
     * Apply overlay-related settings
     */
    void ApplyOverlaySettings(const XR::VRSettings& settings);

    /**
     * Resize OpenXR swapchains to match render scale
     */
    bool RecreateSwapchains(uint32_t width, uint32_t height);
    void RequestSwapchainResize(float scale);

    //-------------------------------------------------------------------------
    // Members
    //-------------------------------------------------------------------------

    // Core OpenXR components
    std::unique_ptr<XR::XRInstance> instance_;
    std::unique_ptr<XR::XRGraphicsBinding> graphics_;
    std::unique_ptr<XR::XRSession> session_;
    std::unique_ptr<XR::XRStereoSwapchains> swapchains_;
    std::unique_ptr<XR::XRViewManager> view_manager_;
    std::unique_ptr<XR::XRFrameManager> frame_manager_;
    std::unique_ptr<XR::XRActionManager> action_manager_;
    std::unique_ptr<XR::XROverlayManager> overlay_manager_;
    std::unique_ptr<XR::XROverlayUI> overlay_ui_;
    XR::XROverlay* settings_overlay_ = nullptr;

    // Controllers
    std::unique_ptr<XRTrackedController> left_controller_;
    std::unique_ptr<XRTrackedController> right_controller_;

    // D3D11 resources
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* device_context_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;

    // Tracking state
    Matrix4 head_matrix_;

    // Frame state
    bool frame_in_progress_ = false;
    bool should_render_ = false;
    bool views_valid_ = false;

    // Settings
    bool desktop_mirroring_ = true;
    float horizontal_scale_ = 1.0f;
    float vertical_scale_ = 1.0f;
    float z_scale_ = 1.0f;
    float render_scale_ = 1.0f;
    DXGI_FORMAT swapchain_format_ = DXGI_FORMAT_UNKNOWN;
    uint32_t swapchain_width_ = 0;
    uint32_t swapchain_height_ = 0;
    bool pending_swapchain_resize_ = false;
    float pending_render_scale_ = 1.0f;

    // Projection views for frame submission
    std::array<XrCompositionLayerProjectionView, 2> projection_views_;
    XrCompositionLayerProjection projection_layer_;

    // Singleton instance
    static XRHMDSupport* singleton_;
};

/**
 * XRTrackedController - OpenXR controller wrapper matching TrackedController API
 */
class XRTrackedController {
public:
    struct ButtonState {
        ButtonState(bool valid = true) : valid_(valid) {}

        float touchX = 0.0f;
        float touchY = 0.0f;

        bool touchContact = false;
        bool touchJustContacted = false;
        bool touchJustReleased = false;

        bool padPressed = false;
        bool padJustPressed = false;
        bool padJustReleased = false;

        bool gripPressed = false;
        bool gripJustPressed = false;
        bool gripJustReleased = false;

        float triggerMargin = 0.0f;

        bool triggerPressed = false;
        bool triggerJustPressed = false;
        bool triggerJustReleased = false;

        bool menuPressed = false;
        bool menuJustPressed = false;
        bool menuJustReleased = false;

        bool valid_ = true;
    };

    XRTrackedController(XR::Hand hand);
    ~XRTrackedController() = default;

    void SetMatrix(const Matrix4& matrix);
    OVR_API Matrix4 GetMatrix() const;

    OVR_API XMFLOAT3 GetPosition() const;
    OVR_API XMFLOAT3 GetRotation(float rx = 0.0f, float ry = 0.0f, float rz = 0.0f) const;

    void UpdateFromState(const XR::ControllerState& state);
    OVR_API ButtonState GetButtonState() const { return button_state_; }

    XR::Hand GetHand() const { return hand_; }

private:
    XR::Hand hand_;
    Matrix4 matrix_;
    ButtonState button_state_;
};

} // namespace OVRInject
