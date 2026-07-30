#pragma once

#include "IVRBackend.hpp"
#include <array>
#include <cstdint>
#include <openvr.h>

namespace OVRInject {
namespace VR {

/**
 * OpenVRBackend - IVRBackend implementation wrapping existing OpenVR code
 *
 * Wraps the existing HMDSupport class to provide the unified IVRBackend interface.
 * This allows the existing OpenVR code to work alongside the new OpenXR backend.
 */
class OpenVRBackend : public IVRBackend {
public:
    OpenVRBackend();
    ~OpenVRBackend() override;

    //-------------------------------------------------------------------------
    // IVRBackend Implementation
    //-------------------------------------------------------------------------

    bool Initialize(ID3D11Device* device) override;
    void Shutdown() override;
    bool IsInitialized() const override;

    bool BeginFrame() override;
    void EndFrame() override;
    void SubmitEyeTexture(
        Eye eye,
        ID3D11Texture2D* texture,
        const XMMATRIX* renderedPose = nullptr) override;
    bool PrepareForCameraWrite() override;

    XMMATRIX GetHeadPoseMatrix() const override;
    XMFLOAT3 GetHeadPosition() const override;
    XMFLOAT3 GetHeadForward() const override;
    XMFLOAT3 GetHeadUp() const override;
    XMFLOAT3 GetHeadRotation() const override;

    void UpdateControllers() override;
    const ControllerState& GetControllerState(Hand hand) const override;
    bool IsControllerTracked(Hand hand) const override;

    void TriggerHaptic(Hand hand, float duration, float frequency, float amplitude) override;

    uint32_t GetRecommendedWidth() const override;
    uint32_t GetRecommendedHeight() const override;
    XMMATRIX GetProjectionMatrix(Eye eye, float nearZ, float farZ) const override;
    XMMATRIX GetEyeMatrix(Eye eye) const override;
    XMMATRIX GetViewMatrix(Eye eye) const override;

    void ShowOverlay() override;
    void HideOverlay() override;
    void ToggleOverlay() override;
    void RenderOverlay() override;
    bool IsOverlayVisible() const override { return false; }
    bool WantsOverlayInputCapture() const override { return false; }

    const char* GetRuntimeName() const override;
    const char* GetSystemName() const override;

    ID3D11Device* GetDevice() const override { return device_; }
    void* GetSession() const override { return hmd_; }
    void Recenter() override;

private:
    void UpdateAsyncReprojectionSetting();

    /**
     * Refresh the cached tracked device poses.
     * WaitGetPoses blocks on compositor timing, so it runs at most once per
     * rendered frame. frame_counter_ advances in PrepareForCameraWrite after
     * submission; all pose getters otherwise read that stable snapshot.
     */
    void UpdatePoseCache() const;

    ID3D11Device* device_ = nullptr;

    std::array<ControllerState, 2> controller_states_;
    std::array<ControllerButtonState, 2> previous_buttons_{};
    std::array<vr::TrackedDeviceIndex_t, 2> controller_indices_{
        vr::k_unTrackedDeviceIndexInvalid,
        vr::k_unTrackedDeviceIndexInvalid
    };

    bool initialized_ = false;
    bool async_reprojection_last_ = true;
    bool async_reprojection_last_valid_ = false;
    bool async_disable_original_ = false;
    bool async_disable_original_valid_ = false;

    // Cached system name
    mutable std::string system_name_;

    // Per-frame pose snapshot (see UpdatePoseCache)
    mutable vr::TrackedDevicePose_t cached_poses_[vr::k_unMaxTrackedDeviceCount] = {};
    mutable uint64_t pose_cache_frame_ = 0;
    mutable bool pose_cache_valid_ = false;
    uint64_t frame_counter_ = 0;

	vr::IVRSystem* hmd_ = nullptr;
};

} // namespace VR
} // namespace OVRInject
