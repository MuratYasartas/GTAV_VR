#pragma once

#include "RuntimeInterface.h"
#include <openvr.h>
#include <vector>
#include <string>

namespace GTA5VR {

class OpenVRRuntime : public IRuntimeInterface {
public:
    OpenVRRuntime();
    ~OpenVRRuntime() override;

    // IRuntimeInterface implementation
    bool Initialize() override;
    void Shutdown() override;
    bool IsInitialized() const override;

    bool CreateSession(ID3D11Device* device) override;
    bool DestroySession() override;
    bool IsSessionRunning() const override;

    bool CreateSwapchains(uint32_t width, uint32_t height) override;
    bool AcquireSwapchainImage(uint32_t eyeIndex, uint32_t* imageIndex) override;
    bool ReleaseSwapchainImage(uint32_t eyeIndex) override;
    ID3D11Texture2D* GetSwapchainTexture(uint32_t eyeIndex, uint32_t imageIndex) override;

    bool WaitFrame() override;
    bool BeginFrame() override;
    bool EndFrame() override;
    bool SubmitFrame(ID3D11Texture2D* leftEye, ID3D11Texture2D* rightEye) override;

    bool GetHeadPose(XrPosef* pose) override;
    bool GetEyeViews(std::vector<EyeView>& views) override;
    bool GetControllerPose(VRHand hand, ControllerPose& pose) override;
    bool GetControllerState(VRHand hand, ControllerState& state) override;

    bool SyncActions() override;
    void TriggerHaptic(VRHand hand, float amplitude, float duration) override;

    void GetRecommendedRenderSize(uint32_t* width, uint32_t* height) const override;
    float GetRefreshRate() const override;
    bool SupportsDepthSubmission() const override;

    std::string GetRuntimeName() const override;
    std::string GetRuntimeVersion() const override;
    std::string GetHMDName() const override;

    XrTime GetPredictedDisplayTime() const override;
    bool IsHMDMounted() const override;

    // OpenVR-specific methods
    vr::IVRSystem* GetVRSystem() const { return m_vrSystem; }
    vr::IVRCompositor* GetCompositor() const { return m_compositor; }

private:
    // Helper methods
    vr::HmdMatrix34_t GetEyeToHeadTransform(vr::EVREye eye) const;
    vr::HmdMatrix44_t GetProjectionMatrix(vr::EVREye eye, float nearZ, float farZ) const;
    void ConvertPose(const vr::TrackedDevicePose_t& vrPose, ControllerPose& pose);
    vr::TrackedDeviceIndex_t GetControllerIndex(VRHand hand) const;

    // OpenVR interfaces
    vr::IVRSystem* m_vrSystem = nullptr;
    vr::IVRCompositor* m_compositor = nullptr;
    vr::IVRInput* m_vrInput = nullptr;

    // D3D11
    ID3D11Device* m_d3dDevice = nullptr;

    // Eye textures (we manage our own for consistency with interface)
    ID3D11Texture2D* m_eyeTextures[2] = { nullptr, nullptr };
    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;

    // Tracking
    vr::TrackedDevicePose_t m_trackedPoses[vr::k_unMaxTrackedDeviceCount];
    vr::TrackedDevicePose_t m_gamePoses[vr::k_unMaxTrackedDeviceCount];

    // Controller indices
    vr::TrackedDeviceIndex_t m_leftControllerIndex = vr::k_unTrackedDeviceIndexInvalid;
    vr::TrackedDeviceIndex_t m_rightControllerIndex = vr::k_unTrackedDeviceIndexInvalid;

    // State
    bool m_initialized = false;
    bool m_sessionRunning = false;

    // Cached info
    std::string m_hmdName;
    float m_refreshRate = 90.0f;
};

} // namespace GTA5VR
