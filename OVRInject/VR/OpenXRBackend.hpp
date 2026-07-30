#pragma once

#include "IVRBackend.hpp"
#include "../OpenXR/XRHMDSupport.hpp"
#include <array>

namespace OVRInject {
namespace VR {

/**
 * OpenXRBackend - IVRBackend implementation using OpenXR
 *
 * Wraps XRHMDSupport to provide the unified IVRBackend interface.
 */
class OpenXRBackend : public IVRBackend {
public:
    OpenXRBackend();
    ~OpenXRBackend() override;

    //-------------------------------------------------------------------------
    // IVRBackend Implementation
    //-------------------------------------------------------------------------

    bool Initialize(ID3D11Device* device) override;
    void SetSwapChain(IDXGISwapChain* swap_chain) override;
    void Shutdown() override;
    bool IsInitialized() const override;

    bool BeginFrame() override;
    void EndFrame() override;
    void SubmitEyeTexture(
        Eye eye,
        ID3D11Texture2D* texture,
        const XMMATRIX* renderedPose = nullptr) override;

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
    uint32_t GetRawRecommendedWidth() const override;
    uint32_t GetRawRecommendedHeight() const override;
    XMMATRIX GetProjectionMatrix(Eye eye, float nearZ, float farZ) const override;
    XMMATRIX GetViewMatrix(Eye eye) const override;

    void ShowOverlay() override;
    void HideOverlay() override;
    void ToggleOverlay() override;
    void RenderOverlay() override;
    bool IsOverlayVisible() const override;
    bool WantsOverlayInputCapture() const override;

    const char* GetRuntimeName() const override;
    const char* GetSystemName() const override;
    void* GetSession() const override;
    ID3D11Device* GetDevice() const override;
    XMMATRIX GetEyeMatrix(Eye eye) const override;
    DXGI_FORMAT GetPreferredSwapchainFormat() const override;
    void Recenter() override;

private:
    /**
     * Convert internal eye enum to OpenXR eye enum
     */
    static XR::Eye ToXREye(Eye eye);

    /**
     * Convert internal hand enum to OpenXR hand enum
     */
    static XR::Hand ToXRHand(Hand hand);

    /**
     * Update controller state from XR controller
     */
    void UpdateControllerStateFromXR(Hand hand);

    std::unique_ptr<XRHMDSupport> hmd_support_;
    std::array<ControllerState, 2> controller_states_;
    DXGI_FORMAT preferred_swapchain_format_ = DXGI_FORMAT_UNKNOWN;
};

} // namespace VR
} // namespace OVRInject
