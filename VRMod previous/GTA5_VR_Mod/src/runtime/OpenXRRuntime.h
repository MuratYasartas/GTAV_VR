#pragma once

#include "RuntimeInterface.h"
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vector>
#include <string>
#include <map>

namespace GTA5VR {

class OpenXRRuntime : public IRuntimeInterface {
public:
    OpenXRRuntime();
    ~OpenXRRuntime() override;

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

    // OpenXR-specific methods
    XrInstance GetInstance() const { return m_instance; }
    XrSession GetSession() const { return m_session; }
    XrSpace GetReferenceSpace() const { return m_referenceSpace; }

private:
    // Internal helper methods
    bool CreateInstance();
    bool GetSystem();
    bool CreateReferenceSpace();
    bool SetupActions();
    bool LocateViews(XrTime displayTime);
    bool HandleSessionStateChange(XrEventDataSessionStateChanged* stateEvent);
    bool PollEvents();

    std::vector<const char*> GetRequiredExtensions() const;
    bool CheckExtensionSupport(const char* extensionName) const;
    std::string XrResultToString(XrResult result) const;

    // OpenXR handles
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_referenceSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;

    // Swapchain data
    struct SwapchainData {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageD3D11KHR> images;
        uint32_t width = 0;
        uint32_t height = 0;
        int64_t format = 0;
    };
    SwapchainData m_colorSwapchains[2];
    SwapchainData m_depthSwapchains[2];

    // View configuration
    XrViewConfigurationType m_viewConfigType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    std::vector<XrViewConfigurationView> m_configViews;
    std::vector<XrView> m_views;

    // Frame state
    XrFrameState m_frameState{};
    bool m_frameWaited = false;
    bool m_frameBegun = false;

    // Action system
    XrActionSet m_actionSet = XR_NULL_HANDLE;
    XrAction m_poseAction = XR_NULL_HANDLE;
    XrAction m_triggerAction = XR_NULL_HANDLE;
    XrAction m_gripAction = XR_NULL_HANDLE;
    XrAction m_thumbstickAction = XR_NULL_HANDLE;
    XrAction m_thumbstickClickAction = XR_NULL_HANDLE;
    XrAction m_primaryButtonAction = XR_NULL_HANDLE;
    XrAction m_secondaryButtonAction = XR_NULL_HANDLE;
    XrAction m_menuButtonAction = XR_NULL_HANDLE;
    XrAction m_hapticAction = XR_NULL_HANDLE;
    XrSpace m_handSpaces[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
    XrPath m_handPaths[2] = { XR_NULL_PATH, XR_NULL_PATH };

    // State
    bool m_initialized = false;
    bool m_sessionRunning = false;
    XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;

    // D3D11
    ID3D11Device* m_d3dDevice = nullptr;

    // Cached info
    uint32_t m_recommendedWidth = 0;
    uint32_t m_recommendedHeight = 0;
    float m_refreshRate = 90.0f;
    std::string m_runtimeName;
    std::string m_runtimeVersion;
    std::string m_hmdName;

    // Extension support
    bool m_depthExtensionSupported = false;
    std::vector<XrExtensionProperties> m_supportedExtensions;
};

} // namespace GTA5VR
