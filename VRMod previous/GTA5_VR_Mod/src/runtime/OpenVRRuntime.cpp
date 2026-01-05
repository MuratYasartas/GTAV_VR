#include "OpenVRRuntime.h"
#include "../core/Logger.h"
#include <cstring>

#pragma comment(lib, "openvr_api.lib")

namespace GTA5VR {

OpenVRRuntime::OpenVRRuntime() {
    memset(m_trackedPoses, 0, sizeof(m_trackedPoses));
    memset(m_gamePoses, 0, sizeof(m_gamePoses));
}

OpenVRRuntime::~OpenVRRuntime() {
    Shutdown();
}

bool OpenVRRuntime::Initialize() {
    if (m_initialized) {
        return true;
    }

    LOG_INFO("Initializing OpenVR runtime...");

    // Check if OpenVR is available
    if (!vr::VR_IsRuntimeInstalled()) {
        LOG_ERROR("OpenVR runtime not installed");
        return false;
    }

    if (!vr::VR_IsHmdPresent()) {
        LOG_ERROR("No VR headset detected");
        return false;
    }

    // Initialize OpenVR
    vr::EVRInitError error = vr::VRInitError_None;
    m_vrSystem = vr::VR_Init(&error, vr::VRApplication_Scene);

    if (error != vr::VRInitError_None || !m_vrSystem) {
        LOG_ERROR("Failed to initialize OpenVR: " + std::string(vr::VR_GetVRInitErrorAsEnglishDescription(error)));
        return false;
    }

    // Get compositor
    m_compositor = vr::VRCompositor();
    if (!m_compositor) {
        LOG_ERROR("Failed to get VR compositor");
        vr::VR_Shutdown();
        m_vrSystem = nullptr;
        return false;
    }

    // Get input interface
    m_vrInput = vr::VRInput();

    // Get HMD info
    char buffer[256];
    m_vrSystem->GetStringTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
        vr::Prop_ModelNumber_String, buffer, sizeof(buffer));
    m_hmdName = buffer;

    // Get refresh rate
    m_refreshRate = m_vrSystem->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd,
        vr::Prop_DisplayFrequency_Float);

    // Get render target size
    m_vrSystem->GetRecommendedRenderTargetSize(&m_renderWidth, &m_renderHeight);

    LOG_INFO("OpenVR HMD: " + m_hmdName);
    LOG_INFO("Render size: " + std::to_string(m_renderWidth) + "x" + std::to_string(m_renderHeight));
    LOG_INFO("Refresh rate: " + std::to_string(m_refreshRate) + " Hz");

    m_initialized = true;
    LOG_INFO("OpenVR runtime initialized successfully");
    return true;
}

void OpenVRRuntime::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down OpenVR runtime...");

    DestroySession();

    if (m_vrSystem) {
        vr::VR_Shutdown();
        m_vrSystem = nullptr;
        m_compositor = nullptr;
        m_vrInput = nullptr;
    }

    m_initialized = false;
    LOG_INFO("OpenVR runtime shut down");
}

bool OpenVRRuntime::IsInitialized() const {
    return m_initialized;
}

bool OpenVRRuntime::CreateSession(ID3D11Device* device) {
    if (!m_initialized) {
        return false;
    }

    m_d3dDevice = device;
    m_sessionRunning = true;

    LOG_INFO("OpenVR session created");
    return true;
}

bool OpenVRRuntime::DestroySession() {
    // Release eye textures
    for (int eye = 0; eye < 2; ++eye) {
        if (m_eyeTextures[eye]) {
            m_eyeTextures[eye]->Release();
            m_eyeTextures[eye] = nullptr;
        }
    }

    m_sessionRunning = false;
    return true;
}

bool OpenVRRuntime::IsSessionRunning() const {
    return m_sessionRunning;
}

bool OpenVRRuntime::CreateSwapchains(uint32_t width, uint32_t height) {
    if (!m_d3dDevice) {
        return false;
    }

    // OpenVR doesn't use swapchains like OpenXR
    // We create our own render targets and submit them
    for (int eye = 0; eye < 2; ++eye) {
        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr, &m_eyeTextures[eye]);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create eye texture " + std::to_string(eye));
            return false;
        }
    }

    m_renderWidth = width;
    m_renderHeight = height;

    LOG_INFO("Created OpenVR render textures: " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

bool OpenVRRuntime::AcquireSwapchainImage(uint32_t eyeIndex, uint32_t* imageIndex) {
    // OpenVR doesn't have swapchain image acquisition
    if (imageIndex) *imageIndex = 0;
    return true;
}

bool OpenVRRuntime::ReleaseSwapchainImage(uint32_t eyeIndex) {
    // OpenVR doesn't need explicit release
    return true;
}

ID3D11Texture2D* OpenVRRuntime::GetSwapchainTexture(uint32_t eyeIndex, uint32_t imageIndex) {
    if (eyeIndex >= 2) return nullptr;
    return m_eyeTextures[eyeIndex];
}

bool OpenVRRuntime::WaitFrame() {
    if (!m_compositor) {
        return false;
    }

    // Wait for poses
    m_compositor->WaitGetPoses(m_trackedPoses, vr::k_unMaxTrackedDeviceCount,
        m_gamePoses, vr::k_unMaxTrackedDeviceCount);

    // Update controller indices
    m_leftControllerIndex = m_vrSystem->GetTrackedDeviceIndexForControllerRole(
        vr::TrackedControllerRole_LeftHand);
    m_rightControllerIndex = m_vrSystem->GetTrackedDeviceIndexForControllerRole(
        vr::TrackedControllerRole_RightHand);

    return true;
}

bool OpenVRRuntime::BeginFrame() {
    return m_sessionRunning;
}

bool OpenVRRuntime::EndFrame() {
    return true;
}

bool OpenVRRuntime::SubmitFrame(ID3D11Texture2D* leftEye, ID3D11Texture2D* rightEye) {
    if (!m_compositor) {
        return false;
    }

    vr::Texture_t leftTexture{};
    leftTexture.handle = leftEye ? leftEye : m_eyeTextures[0];
    leftTexture.eType = vr::TextureType_DirectX;
    leftTexture.eColorSpace = vr::ColorSpace_Gamma;

    vr::Texture_t rightTexture{};
    rightTexture.handle = rightEye ? rightEye : m_eyeTextures[1];
    rightTexture.eType = vr::TextureType_DirectX;
    rightTexture.eColorSpace = vr::ColorSpace_Gamma;

    vr::EVRCompositorError error;

    error = m_compositor->Submit(vr::Eye_Left, &leftTexture);
    if (error != vr::VRCompositorError_None) {
        LOG_ERROR("Failed to submit left eye: " + std::to_string(error));
        return false;
    }

    error = m_compositor->Submit(vr::Eye_Right, &rightTexture);
    if (error != vr::VRCompositorError_None) {
        LOG_ERROR("Failed to submit right eye: " + std::to_string(error));
        return false;
    }

    return true;
}

bool OpenVRRuntime::GetHeadPose(XrPosef* pose) {
    if (!pose || !m_trackedPoses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid) {
        return false;
    }

    const auto& hmdPose = m_trackedPoses[vr::k_unTrackedDeviceIndex_Hmd];
    const auto& mat = hmdPose.mDeviceToAbsoluteTracking;

    // Extract position
    pose->position.x = mat.m[0][3];
    pose->position.y = mat.m[1][3];
    pose->position.z = mat.m[2][3];

    // Extract rotation as quaternion
    float trace = mat.m[0][0] + mat.m[1][1] + mat.m[2][2];
    if (trace > 0) {
        float s = 0.5f / sqrtf(trace + 1.0f);
        pose->orientation.w = 0.25f / s;
        pose->orientation.x = (mat.m[2][1] - mat.m[1][2]) * s;
        pose->orientation.y = (mat.m[0][2] - mat.m[2][0]) * s;
        pose->orientation.z = (mat.m[1][0] - mat.m[0][1]) * s;
    } else if (mat.m[0][0] > mat.m[1][1] && mat.m[0][0] > mat.m[2][2]) {
        float s = 2.0f * sqrtf(1.0f + mat.m[0][0] - mat.m[1][1] - mat.m[2][2]);
        pose->orientation.w = (mat.m[2][1] - mat.m[1][2]) / s;
        pose->orientation.x = 0.25f * s;
        pose->orientation.y = (mat.m[0][1] + mat.m[1][0]) / s;
        pose->orientation.z = (mat.m[0][2] + mat.m[2][0]) / s;
    } else if (mat.m[1][1] > mat.m[2][2]) {
        float s = 2.0f * sqrtf(1.0f + mat.m[1][1] - mat.m[0][0] - mat.m[2][2]);
        pose->orientation.w = (mat.m[0][2] - mat.m[2][0]) / s;
        pose->orientation.x = (mat.m[0][1] + mat.m[1][0]) / s;
        pose->orientation.y = 0.25f * s;
        pose->orientation.z = (mat.m[1][2] + mat.m[2][1]) / s;
    } else {
        float s = 2.0f * sqrtf(1.0f + mat.m[2][2] - mat.m[0][0] - mat.m[1][1]);
        pose->orientation.w = (mat.m[1][0] - mat.m[0][1]) / s;
        pose->orientation.x = (mat.m[0][2] + mat.m[2][0]) / s;
        pose->orientation.y = (mat.m[1][2] + mat.m[2][1]) / s;
        pose->orientation.z = 0.25f * s;
    }

    return true;
}

bool OpenVRRuntime::GetEyeViews(std::vector<EyeView>& views) {
    views.resize(2);

    XrPosef headPose;
    if (!GetHeadPose(&headPose)) {
        return false;
    }

    for (int eye = 0; eye < 2; ++eye) {
        vr::EVREye vrEye = (eye == 0) ? vr::Eye_Left : vr::Eye_Right;

        // Get eye to head transform
        vr::HmdMatrix34_t eyeToHead = GetEyeToHeadTransform(vrEye);

        // Eye position is head position + eye offset
        views[eye].position[0] = headPose.position.x + eyeToHead.m[0][3];
        views[eye].position[1] = headPose.position.y + eyeToHead.m[1][3];
        views[eye].position[2] = headPose.position.z + eyeToHead.m[2][3];

        views[eye].orientation[0] = headPose.orientation.x;
        views[eye].orientation[1] = headPose.orientation.y;
        views[eye].orientation[2] = headPose.orientation.z;
        views[eye].orientation[3] = headPose.orientation.w;

        // Get projection
        float left, right, top, bottom;
        m_vrSystem->GetProjectionRaw(vrEye, &left, &right, &top, &bottom);

        views[eye].fovLeft = atanf(left);
        views[eye].fovRight = atanf(right);
        views[eye].fovUp = atanf(-bottom);
        views[eye].fovDown = atanf(-top);
    }

    return true;
}

bool OpenVRRuntime::GetControllerPose(VRHand hand, ControllerPose& pose) {
    vr::TrackedDeviceIndex_t index = GetControllerIndex(hand);
    if (index == vr::k_unTrackedDeviceIndexInvalid) {
        pose.isValid = false;
        return false;
    }

    if (!m_trackedPoses[index].bPoseIsValid) {
        pose.isValid = false;
        return false;
    }

    ConvertPose(m_trackedPoses[index], pose);
    return true;
}

bool OpenVRRuntime::GetControllerState(VRHand hand, ControllerState& state) {
    vr::TrackedDeviceIndex_t index = GetControllerIndex(hand);
    if (index == vr::k_unTrackedDeviceIndexInvalid) {
        memset(&state, 0, sizeof(state));
        return false;
    }

    vr::VRControllerState_t controllerState;
    if (!m_vrSystem->GetControllerState(index, &controllerState, sizeof(controllerState))) {
        memset(&state, 0, sizeof(state));
        return false;
    }

    // Map controller state
    state.trigger = controllerState.rAxis[1].x;  // Trigger axis
    state.grip = (controllerState.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_Grip)) ? 1.0f : 0.0f;
    state.thumbstickX = controllerState.rAxis[0].x;  // Touchpad/thumbstick X
    state.thumbstickY = controllerState.rAxis[0].y;  // Touchpad/thumbstick Y
    state.thumbstickClick = (controllerState.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) != 0;
    state.primaryButton = (controllerState.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_A)) != 0;
    state.secondaryButton = (controllerState.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) != 0;
    state.menuButton = (controllerState.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) != 0;
    state.systemButton = (controllerState.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_System)) != 0;

    return true;
}

bool OpenVRRuntime::SyncActions() {
    // OpenVR doesn't have an action sync like OpenXR
    return true;
}

void OpenVRRuntime::TriggerHaptic(VRHand hand, float amplitude, float duration) {
    vr::TrackedDeviceIndex_t index = GetControllerIndex(hand);
    if (index == vr::k_unTrackedDeviceIndexInvalid) {
        return;
    }

    // OpenVR haptics are limited - duration is in microseconds
    uint16_t durationMicroseconds = static_cast<uint16_t>(duration * 1000000.0f);
    m_vrSystem->TriggerHapticPulse(index, 0, durationMicroseconds);
}

void OpenVRRuntime::GetRecommendedRenderSize(uint32_t* width, uint32_t* height) const {
    if (width) *width = m_renderWidth;
    if (height) *height = m_renderHeight;
}

float OpenVRRuntime::GetRefreshRate() const {
    return m_refreshRate;
}

bool OpenVRRuntime::SupportsDepthSubmission() const {
    return false; // OpenVR doesn't support depth submission in the same way
}

std::string OpenVRRuntime::GetRuntimeName() const {
    return "SteamVR/OpenVR";
}

std::string OpenVRRuntime::GetRuntimeVersion() const {
    return vr::IVRSystem_Version;
}

std::string OpenVRRuntime::GetHMDName() const {
    return m_hmdName;
}

XrTime OpenVRRuntime::GetPredictedDisplayTime() const {
    // Return current time in nanoseconds
    float secondsSinceLastVsync;
    m_vrSystem->GetTimeSinceLastVsync(&secondsSinceLastVsync, nullptr);
    return static_cast<XrTime>(secondsSinceLastVsync * 1000000000.0);
}

bool OpenVRRuntime::IsHMDMounted() const {
    if (!m_vrSystem) {
        return false;
    }

    vr::EDeviceActivityLevel activity = m_vrSystem->GetTrackedDeviceActivityLevel(vr::k_unTrackedDeviceIndex_Hmd);
    return activity == vr::k_EDeviceActivityLevel_UserInteraction ||
           activity == vr::k_EDeviceActivityLevel_UserInteraction_Timeout;
}

// Private helper methods

vr::HmdMatrix34_t OpenVRRuntime::GetEyeToHeadTransform(vr::EVREye eye) const {
    return m_vrSystem->GetEyeToHeadTransform(eye);
}

vr::HmdMatrix44_t OpenVRRuntime::GetProjectionMatrix(vr::EVREye eye, float nearZ, float farZ) const {
    return m_vrSystem->GetProjectionMatrix(eye, nearZ, farZ);
}

void OpenVRRuntime::ConvertPose(const vr::TrackedDevicePose_t& vrPose, ControllerPose& pose) {
    const auto& mat = vrPose.mDeviceToAbsoluteTracking;

    // Position
    pose.position[0] = mat.m[0][3];
    pose.position[1] = mat.m[1][3];
    pose.position[2] = mat.m[2][3];

    // Quaternion from rotation matrix
    float trace = mat.m[0][0] + mat.m[1][1] + mat.m[2][2];
    if (trace > 0) {
        float s = 0.5f / sqrtf(trace + 1.0f);
        pose.orientation[3] = 0.25f / s;
        pose.orientation[0] = (mat.m[2][1] - mat.m[1][2]) * s;
        pose.orientation[1] = (mat.m[0][2] - mat.m[2][0]) * s;
        pose.orientation[2] = (mat.m[1][0] - mat.m[0][1]) * s;
    } else if (mat.m[0][0] > mat.m[1][1] && mat.m[0][0] > mat.m[2][2]) {
        float s = 2.0f * sqrtf(1.0f + mat.m[0][0] - mat.m[1][1] - mat.m[2][2]);
        pose.orientation[3] = (mat.m[2][1] - mat.m[1][2]) / s;
        pose.orientation[0] = 0.25f * s;
        pose.orientation[1] = (mat.m[0][1] + mat.m[1][0]) / s;
        pose.orientation[2] = (mat.m[0][2] + mat.m[2][0]) / s;
    } else if (mat.m[1][1] > mat.m[2][2]) {
        float s = 2.0f * sqrtf(1.0f + mat.m[1][1] - mat.m[0][0] - mat.m[2][2]);
        pose.orientation[3] = (mat.m[0][2] - mat.m[2][0]) / s;
        pose.orientation[0] = (mat.m[0][1] + mat.m[1][0]) / s;
        pose.orientation[1] = 0.25f * s;
        pose.orientation[2] = (mat.m[1][2] + mat.m[2][1]) / s;
    } else {
        float s = 2.0f * sqrtf(1.0f + mat.m[2][2] - mat.m[0][0] - mat.m[1][1]);
        pose.orientation[3] = (mat.m[1][0] - mat.m[0][1]) / s;
        pose.orientation[0] = (mat.m[0][2] + mat.m[2][0]) / s;
        pose.orientation[1] = (mat.m[1][2] + mat.m[2][1]) / s;
        pose.orientation[2] = 0.25f * s;
    }

    // Velocity
    pose.linearVelocity[0] = vrPose.vVelocity.v[0];
    pose.linearVelocity[1] = vrPose.vVelocity.v[1];
    pose.linearVelocity[2] = vrPose.vVelocity.v[2];
    pose.angularVelocity[0] = vrPose.vAngularVelocity.v[0];
    pose.angularVelocity[1] = vrPose.vAngularVelocity.v[1];
    pose.angularVelocity[2] = vrPose.vAngularVelocity.v[2];

    pose.isValid = vrPose.bPoseIsValid;
}

vr::TrackedDeviceIndex_t OpenVRRuntime::GetControllerIndex(VRHand hand) const {
    return (hand == VRHand::Left) ? m_leftControllerIndex : m_rightControllerIndex;
}

} // namespace GTA5VR
