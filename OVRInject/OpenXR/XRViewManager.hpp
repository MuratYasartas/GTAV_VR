#pragma once

#include "XRCore.hpp"
#include "XRInstance.hpp"
#include "XRSession.hpp"
#include <array>

namespace OVRInject {
namespace XR {

/**
 * ViewInfo - Contains all view information for a single eye
 */
struct ViewInfo {
    XrViewConfigurationView config;   // Static configuration (resolution, etc.)
    XrView view;                       // Dynamic pose and FOV (updated each frame)
    XMMATRIX projection_matrix;        // Projection matrix for this eye
    XMMATRIX view_matrix;              // View matrix (inverted pose)
};

/**
 * XRViewManager - Manages view configuration, reference space, and pose location
 *
 * Handles:
 * - Enumerating view configurations
 * - Creating and managing reference space
 * - Locating views (per-eye pose and FOV) each frame
 * - Computing projection and view matrices
 */
class XRViewManager {
public:
    XRViewManager(XRInstance* instance, XRSession* session);
    ~XRViewManager();

    // Disable copy
    XRViewManager(const XRViewManager&) = delete;
    XRViewManager& operator=(const XRViewManager&) = delete;

    /**
     * Initialize view configuration and reference space
     * @return true on success
     */
    bool Initialize();

    /**
     * Shutdown and cleanup
     */
    void Shutdown();

    /**
     * Check if initialized
     */
    bool IsInitialized() const { return reference_space_ != XR_NULL_HANDLE; }

    //-------------------------------------------------------------------------
    // View Configuration
    //-------------------------------------------------------------------------

    /**
     * Get recommended render width for a single eye
     */
    uint32_t GetRecommendedWidth() const;

    /**
     * Get recommended render height for a single eye
     */
    uint32_t GetRecommendedHeight() const;

    /**
     * Get maximum render width
     */
    uint32_t GetMaxWidth() const;

    /**
     * Get maximum render height
     */
    uint32_t GetMaxHeight() const;

    //-------------------------------------------------------------------------
    // Reference Space
    //-------------------------------------------------------------------------

    /**
     * Create reference space of specified type
     * @param type Reference space type (STAGE for room-scale, LOCAL for seated)
     * @return true on success
     */
    bool CreateReferenceSpace(XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_STAGE);

    /**
     * Get the reference space handle
     */
    XrSpace GetReferenceSpace() const { return reference_space_; }

    /**
     * Get the render space handle (may differ when view-locked)
     */
    XrSpace GetRenderSpace() const { return render_space_; }

    /**
     * Get reference space type
     */
    XrReferenceSpaceType GetReferenceSpaceType() const { return reference_space_type_; }

    /**
     * Enable or disable view-locked rendering (render space = VIEW)
     */
    void SetViewLockEnabled(bool enabled);

    //-------------------------------------------------------------------------
    // View Location
    //-------------------------------------------------------------------------

    /**
     * Locate views for the given display time
     * Updates pose and FOV for both eyes
     *
     * @param display_time Predicted display time from frame state
     * @param view_state Output: view state flags
     * @return true if views were successfully located
     */
    bool LocateViews(XrTime display_time, XrViewState& view_state);

    /**
     * Get view info for specific eye
     */
    const ViewInfo& GetView(Eye eye) const;

    /**
     * Get left eye view
     */
    const ViewInfo& GetLeftView() const { return GetView(Eye::Left); }

    /**
     * Get right eye view
     */
    const ViewInfo& GetRightView() const { return GetView(Eye::Right); }

    //-------------------------------------------------------------------------
    // Head Pose Utilities
    //-------------------------------------------------------------------------

    /**
     * Get combined head pose matrix (average of both eyes)
     */
    XMMATRIX GetHeadPoseMatrix() const;

    /**
     * Get head position
     */
    XMFLOAT3 GetHeadPosition() const;

    /**
     * Get head forward vector
     */
    XMFLOAT3 GetHeadForward() const;

    /**
     * Get head up vector
     */
    XMFLOAT3 GetHeadUp() const;

    /**
     * Get head rotation as euler angles (pitch, yaw, roll in degrees)
     */
    XMFLOAT3 GetHeadRotation() const;

    /**
     * Get head pose (position + orientation)
     */
    XrPosef GetHeadPose() const;

    /**
     * Recenter the current view
     * @param yaw_only If true, only recenter yaw (keeps position)
     */
    void Recenter(bool yaw_only = true);

    //-------------------------------------------------------------------------
    // Projection Settings
    //-------------------------------------------------------------------------

    /**
     * Set near and far clip planes
     */
    void SetClipPlanes(float nearZ, float farZ);

    float GetNearZ() const { return near_z_; }
    float GetFarZ() const { return far_z_; }

    /**
     * Check if views have been located (FOV/pose valid) at least once
     */
    bool ViewsValid() const { return views_valid_; }

private:
    /**
     * Enumerate view configurations
     */
    bool EnumerateViewConfigs();

    /**
     * Create a reference space of the specified type
     */
    bool CreateSpace(XrReferenceSpaceType type, XrSpace& outSpace);

    /**
     * Update projection matrices for both eyes
     */
    void UpdateProjectionMatrices();

    /**
     * Update view matrices from poses
     */
    void UpdateViewMatrices();

    //-------------------------------------------------------------------------
    // Members
    //-------------------------------------------------------------------------

    XRInstance* instance_;
    XRSession* session_;

    XrViewConfigurationType view_config_type_ = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    std::array<ViewInfo, static_cast<size_t>(Eye::Count)> views_;

    XrSpace reference_space_ = XR_NULL_HANDLE;
    XrReferenceSpaceType reference_space_type_ = XR_REFERENCE_SPACE_TYPE_LOCAL;
    XrSpace render_space_ = XR_NULL_HANDLE;
    XrReferenceSpaceType render_space_type_ = XR_REFERENCE_SPACE_TYPE_LOCAL;

    // Combined head pose (for convenience)
    XrPosef head_pose_ = IdentityPose();
    XrPosef recenter_pose_ = IdentityPose();
    bool recenter_active_ = false;

    // Clip planes
    float near_z_ = 0.1f;
    float far_z_ = 1000.0f;

    bool views_valid_ = false;
};

} // namespace XR
} // namespace OVRInject
