#include "OpenXRRuntime.h"
#include "../core/Logger.h"
#include <algorithm>

#pragma comment(lib, "openxr_loader.lib")

namespace GTA5VR {

OpenXRRuntime::OpenXRRuntime() {
    m_frameState.type = XR_TYPE_FRAME_STATE;
    m_views.resize(2);
    for (auto& view : m_views) {
        view.type = XR_TYPE_VIEW;
    }
}

OpenXRRuntime::~OpenXRRuntime() {
    Shutdown();
}

bool OpenXRRuntime::Initialize() {
    if (m_initialized) {
        return true;
    }

    LOG_INFO("Initializing OpenXR runtime...");

    // Enumerate extensions
    uint32_t extensionCount = 0;
    XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    if (XR_FAILED(result)) {
        LOG_ERROR("Failed to enumerate extensions: " + XrResultToString(result));
        return false;
    }

    m_supportedExtensions.resize(extensionCount);
    for (auto& ext : m_supportedExtensions) {
        ext.type = XR_TYPE_EXTENSION_PROPERTIES;
    }
    result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, m_supportedExtensions.data());
    if (XR_FAILED(result)) {
        LOG_ERROR("Failed to get extensions: " + XrResultToString(result));
        return false;
    }

    // Log available extensions
    LOG_VERBOSE("Available OpenXR extensions:");
    for (const auto& ext : m_supportedExtensions) {
        LOG_VERBOSE("  - " + std::string(ext.extensionName));
    }

    // Check for depth extension
    m_depthExtensionSupported = CheckExtensionSupport(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);

    // Create instance
    if (!CreateInstance()) {
        return false;
    }

    // Get system
    if (!GetSystem()) {
        return false;
    }

    m_initialized = true;
    LOG_INFO("OpenXR runtime initialized successfully");
    return true;
}

void OpenXRRuntime::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("Shutting down OpenXR runtime...");

    DestroySession();

    if (m_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }

    m_initialized = false;
    LOG_INFO("OpenXR runtime shut down");
}

bool OpenXRRuntime::IsInitialized() const {
    return m_initialized;
}

bool OpenXRRuntime::CreateSession(ID3D11Device* device) {
    if (!m_initialized || m_session != XR_NULL_HANDLE) {
        return m_session != XR_NULL_HANDLE;
    }

    m_d3dDevice = device;
    LOG_INFO("Creating OpenXR session...");

    // Get graphics requirements
    XrGraphicsRequirementsD3D11KHR graphicsReqs{};
    graphicsReqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;

    PFN_xrGetD3D11GraphicsRequirementsKHR xrGetD3D11GraphicsRequirementsKHR = nullptr;
    XrResult result = xrGetInstanceProcAddr(m_instance, "xrGetD3D11GraphicsRequirementsKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrGetD3D11GraphicsRequirementsKHR));
    if (XR_FAILED(result) || !xrGetD3D11GraphicsRequirementsKHR) {
        LOG_ERROR("Failed to get xrGetD3D11GraphicsRequirementsKHR");
        return false;
    }

    result = xrGetD3D11GraphicsRequirementsKHR(m_instance, m_systemId, &graphicsReqs);
    if (XR_FAILED(result)) {
        LOG_ERROR("Failed to get graphics requirements: " + XrResultToString(result));
        return false;
    }

    // Create graphics binding
    XrGraphicsBindingD3D11KHR graphicsBinding{};
    graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;
    graphicsBinding.device = device;

    // Create session
    XrSessionCreateInfo sessionInfo{};
    sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
    sessionInfo.next = &graphicsBinding;
    sessionInfo.systemId = m_systemId;

    result = xrCreateSession(m_instance, &sessionInfo, &m_session);
    if (XR_FAILED(result)) {
        LOG_ERROR("Failed to create session: " + XrResultToString(result));
        return false;
    }

    // Create reference space
    if (!CreateReferenceSpace()) {
        return false;
    }

    // Get view configuration
    uint32_t viewCount = 0;
    result = xrEnumerateViewConfigurationViews(m_instance, m_systemId, m_viewConfigType,
        0, &viewCount, nullptr);
    if (XR_FAILED(result) || viewCount != 2) {
        LOG_ERROR("Failed to enumerate view configuration views");
        return false;
    }

    m_configViews.resize(viewCount);
    for (auto& view : m_configViews) {
        view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }

    result = xrEnumerateViewConfigurationViews(m_instance, m_systemId, m_viewConfigType,
        viewCount, &viewCount, m_configViews.data());
    if (XR_FAILED(result)) {
        LOG_ERROR("Failed to get view configuration views");
        return false;
    }

    m_recommendedWidth = m_configViews[0].recommendedImageRectWidth;
    m_recommendedHeight = m_configViews[0].recommendedImageRectHeight;
    LOG_INFO("Recommended render size: " + std::to_string(m_recommendedWidth) + "x" + std::to_string(m_recommendedHeight));

    // Create swapchains
    if (!CreateSwapchains(m_recommendedWidth, m_recommendedHeight)) {
        return false;
    }

    // Setup actions
    if (!SetupActions()) {
        LOG_WARNING("Failed to setup actions, input may not work");
    }

    LOG_INFO("OpenXR session created successfully");
    return true;
}

bool OpenXRRuntime::DestroySession() {
    // Destroy hand spaces
    for (int i = 0; i < 2; ++i) {
        if (m_handSpaces[i] != XR_NULL_HANDLE) {
            xrDestroySpace(m_handSpaces[i]);
            m_handSpaces[i] = XR_NULL_HANDLE;
        }
    }

    // Destroy action set
    if (m_actionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(m_actionSet);
        m_actionSet = XR_NULL_HANDLE;
    }

    // Destroy swapchains
    for (int eye = 0; eye < 2; ++eye) {
        if (m_colorSwapchains[eye].swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(m_colorSwapchains[eye].swapchain);
            m_colorSwapchains[eye].swapchain = XR_NULL_HANDLE;
        }
        if (m_depthSwapchains[eye].swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(m_depthSwapchains[eye].swapchain);
            m_depthSwapchains[eye].swapchain = XR_NULL_HANDLE;
        }
    }

    // Destroy spaces
    if (m_viewSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_viewSpace);
        m_viewSpace = XR_NULL_HANDLE;
    }
    if (m_referenceSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_referenceSpace);
        m_referenceSpace = XR_NULL_HANDLE;
    }

    // Destroy session
    if (m_session != XR_NULL_HANDLE) {
        xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }

    m_sessionRunning = false;
    return true;
}

bool OpenXRRuntime::IsSessionRunning() const {
    return m_sessionRunning;
}

bool OpenXRRuntime::CreateSwapchains(uint32_t width, uint32_t height) {
    if (m_session == XR_NULL_HANDLE) {
        return false;
    }

    // Get supported formats
    uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr);

    std::vector<int64_t> formats(formatCount);
    xrEnumerateSwapchainFormats(m_session, formatCount, &formatCount, formats.data());

    // Prefer SRGB formats
    int64_t selectedFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    for (int64_t format : formats) {
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
            selectedFormat = format;
            break;
        }
    }

    // Create swapchains for each eye
    for (int eye = 0; eye < 2; ++eye) {
        XrSwapchainCreateInfo swapchainInfo{};
        swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        swapchainInfo.format = selectedFormat;
        swapchainInfo.sampleCount = 1;
        swapchainInfo.width = width;
        swapchainInfo.height = height;
        swapchainInfo.faceCount = 1;
        swapchainInfo.arraySize = 1;
        swapchainInfo.mipCount = 1;

        XrResult result = xrCreateSwapchain(m_session, &swapchainInfo, &m_colorSwapchains[eye].swapchain);
        if (XR_FAILED(result)) {
            LOG_ERROR("Failed to create color swapchain for eye " + std::to_string(eye));
            return false;
        }

        m_colorSwapchains[eye].width = width;
        m_colorSwapchains[eye].height = height;
        m_colorSwapchains[eye].format = selectedFormat;

        // Get swapchain images
        uint32_t imageCount = 0;
        xrEnumerateSwapchainImages(m_colorSwapchains[eye].swapchain, 0, &imageCount, nullptr);

        m_colorSwapchains[eye].images.resize(imageCount);
        for (auto& image : m_colorSwapchains[eye].images) {
            image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        }

        result = xrEnumerateSwapchainImages(m_colorSwapchains[eye].swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(m_colorSwapchains[eye].images.data()));
        if (XR_FAILED(result)) {
            LOG_ERROR("Failed to enumerate swapchain images for eye " + std::to_string(eye));
            return false;
        }

        // Create depth swapchain if supported
        if (m_depthExtensionSupported) {
            swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            swapchainInfo.format = DXGI_FORMAT_D32_FLOAT;

            result = xrCreateSwapchain(m_session, &swapchainInfo, &m_depthSwapchains[eye].swapchain);
            if (XR_SUCCEEDED(result)) {
                m_depthSwapchains[eye].width = width;
                m_depthSwapchains[eye].height = height;
                m_depthSwapchains[eye].format = DXGI_FORMAT_D32_FLOAT;

                xrEnumerateSwapchainImages(m_depthSwapchains[eye].swapchain, 0, &imageCount, nullptr);
                m_depthSwapchains[eye].images.resize(imageCount);
                for (auto& image : m_depthSwapchains[eye].images) {
                    image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
                }
                xrEnumerateSwapchainImages(m_depthSwapchains[eye].swapchain, imageCount, &imageCount,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(m_depthSwapchains[eye].images.data()));
            }
        }
    }

    LOG_INFO("Created OpenXR swapchains: " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

bool OpenXRRuntime::AcquireSwapchainImage(uint32_t eyeIndex, uint32_t* imageIndex) {
    if (eyeIndex >= 2 || m_colorSwapchains[eyeIndex].swapchain == XR_NULL_HANDLE) {
        return false;
    }

    XrSwapchainImageAcquireInfo acquireInfo{};
    acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;

    XrResult result = xrAcquireSwapchainImage(m_colorSwapchains[eyeIndex].swapchain, &acquireInfo, imageIndex);
    if (XR_FAILED(result)) {
        return false;
    }

    XrSwapchainImageWaitInfo waitInfo{};
    waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    waitInfo.timeout = XR_INFINITE_DURATION;

    result = xrWaitSwapchainImage(m_colorSwapchains[eyeIndex].swapchain, &waitInfo);
    return XR_SUCCEEDED(result);
}

bool OpenXRRuntime::ReleaseSwapchainImage(uint32_t eyeIndex) {
    if (eyeIndex >= 2 || m_colorSwapchains[eyeIndex].swapchain == XR_NULL_HANDLE) {
        return false;
    }

    XrSwapchainImageReleaseInfo releaseInfo{};
    releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;

    XrResult result = xrReleaseSwapchainImage(m_colorSwapchains[eyeIndex].swapchain, &releaseInfo);
    return XR_SUCCEEDED(result);
}

ID3D11Texture2D* OpenXRRuntime::GetSwapchainTexture(uint32_t eyeIndex, uint32_t imageIndex) {
    if (eyeIndex >= 2 || imageIndex >= m_colorSwapchains[eyeIndex].images.size()) {
        return nullptr;
    }
    return m_colorSwapchains[eyeIndex].images[imageIndex].texture;
}

bool OpenXRRuntime::WaitFrame() {
    if (m_session == XR_NULL_HANDLE) {
        return false;
    }

    PollEvents();

    if (!m_sessionRunning) {
        return false;
    }

    XrFrameWaitInfo waitInfo{};
    waitInfo.type = XR_TYPE_FRAME_WAIT_INFO;

    m_frameState.type = XR_TYPE_FRAME_STATE;
    XrResult result = xrWaitFrame(m_session, &waitInfo, &m_frameState);
    if (XR_FAILED(result)) {
        LOG_ERROR("xrWaitFrame failed: " + XrResultToString(result));
        return false;
    }

    m_frameWaited = true;
    return true;
}

bool OpenXRRuntime::BeginFrame() {
    if (!m_frameWaited || m_session == XR_NULL_HANDLE) {
        return false;
    }

    XrFrameBeginInfo beginInfo{};
    beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;

    XrResult result = xrBeginFrame(m_session, &beginInfo);
    if (XR_FAILED(result)) {
        LOG_ERROR("xrBeginFrame failed: " + XrResultToString(result));
        return false;
    }

    m_frameBegun = true;

    // Locate views for this frame
    LocateViews(m_frameState.predictedDisplayTime);

    return true;
}

bool OpenXRRuntime::EndFrame() {
    if (!m_frameBegun || m_session == XR_NULL_HANDLE) {
        return false;
    }

    m_frameWaited = false;
    m_frameBegun = false;
    return true;
}

bool OpenXRRuntime::SubmitFrame(ID3D11Texture2D* leftEye, ID3D11Texture2D* rightEye) {
    if (m_session == XR_NULL_HANDLE || !m_frameState.shouldRender) {
        // Submit empty frame
        XrFrameEndInfo endInfo{};
        endInfo.type = XR_TYPE_FRAME_END_INFO;
        endInfo.displayTime = m_frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = 0;
        endInfo.layers = nullptr;
        xrEndFrame(m_session, &endInfo);
        return true;
    }

    // Build projection views
    std::vector<XrCompositionLayerProjectionView> projectionViews(2);
    for (int eye = 0; eye < 2; ++eye) {
        projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projectionViews[eye].pose = m_views[eye].pose;
        projectionViews[eye].fov = m_views[eye].fov;
        projectionViews[eye].subImage.swapchain = m_colorSwapchains[eye].swapchain;
        projectionViews[eye].subImage.imageRect.offset = { 0, 0 };
        projectionViews[eye].subImage.imageRect.extent = {
            static_cast<int32_t>(m_colorSwapchains[eye].width),
            static_cast<int32_t>(m_colorSwapchains[eye].height)
        };
        projectionViews[eye].subImage.imageArrayIndex = 0;
    }

    XrCompositionLayerProjection projectionLayer{};
    projectionLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    projectionLayer.space = m_referenceSpace;
    projectionLayer.viewCount = 2;
    projectionLayer.views = projectionViews.data();

    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionLayer)
    };

    XrFrameEndInfo endInfo{};
    endInfo.type = XR_TYPE_FRAME_END_INFO;
    endInfo.displayTime = m_frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 1;
    endInfo.layers = layers;

    XrResult result = xrEndFrame(m_session, &endInfo);
    if (XR_FAILED(result)) {
        LOG_ERROR("xrEndFrame failed: " + XrResultToString(result));
        return false;
    }

    return true;
}

bool OpenXRRuntime::GetHeadPose(XrPosef* pose) {
    if (!pose || m_views.size() < 2) {
        return false;
    }

    // Return average of both eye poses (roughly head center)
    *pose = m_views[0].pose;
    return true;
}

bool OpenXRRuntime::GetEyeViews(std::vector<EyeView>& views) {
    views.resize(2);
    for (int eye = 0; eye < 2; ++eye) {
        views[eye].position[0] = m_views[eye].pose.position.x;
        views[eye].position[1] = m_views[eye].pose.position.y;
        views[eye].position[2] = m_views[eye].pose.position.z;
        views[eye].orientation[0] = m_views[eye].pose.orientation.x;
        views[eye].orientation[1] = m_views[eye].pose.orientation.y;
        views[eye].orientation[2] = m_views[eye].pose.orientation.z;
        views[eye].orientation[3] = m_views[eye].pose.orientation.w;
        views[eye].fovLeft = m_views[eye].fov.angleLeft;
        views[eye].fovRight = m_views[eye].fov.angleRight;
        views[eye].fovUp = m_views[eye].fov.angleUp;
        views[eye].fovDown = m_views[eye].fov.angleDown;
    }
    return true;
}

bool OpenXRRuntime::GetControllerPose(VRHand hand, ControllerPose& pose) {
    int handIndex = static_cast<int>(hand);
    if (m_handSpaces[handIndex] == XR_NULL_HANDLE) {
        pose.isValid = false;
        return false;
    }

    XrSpaceLocation spaceLocation{};
    spaceLocation.type = XR_TYPE_SPACE_LOCATION;

    XrResult result = xrLocateSpace(m_handSpaces[handIndex], m_referenceSpace,
        m_frameState.predictedDisplayTime, &spaceLocation);

    if (XR_FAILED(result) ||
        !(spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
        !(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        pose.isValid = false;
        return false;
    }

    pose.position[0] = spaceLocation.pose.position.x;
    pose.position[1] = spaceLocation.pose.position.y;
    pose.position[2] = spaceLocation.pose.position.z;
    pose.orientation[0] = spaceLocation.pose.orientation.x;
    pose.orientation[1] = spaceLocation.pose.orientation.y;
    pose.orientation[2] = spaceLocation.pose.orientation.z;
    pose.orientation[3] = spaceLocation.pose.orientation.w;
    pose.isValid = true;

    return true;
}

bool OpenXRRuntime::GetControllerState(VRHand hand, ControllerState& state) {
    int handIndex = static_cast<int>(hand);
    memset(&state, 0, sizeof(state));

    if (m_actionSet == XR_NULL_HANDLE) {
        return false;
    }

    XrActionStateGetInfo getInfo{};
    getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.subactionPath = m_handPaths[handIndex];

    // Get trigger
    getInfo.action = m_triggerAction;
    XrActionStateFloat floatState{};
    floatState.type = XR_TYPE_ACTION_STATE_FLOAT;
    if (XR_SUCCEEDED(xrGetActionStateFloat(m_session, &getInfo, &floatState))) {
        state.trigger = floatState.currentState;
    }

    // Get grip
    getInfo.action = m_gripAction;
    if (XR_SUCCEEDED(xrGetActionStateFloat(m_session, &getInfo, &floatState))) {
        state.grip = floatState.currentState;
    }

    // Get thumbstick
    getInfo.action = m_thumbstickAction;
    XrActionStateVector2f vec2State{};
    vec2State.type = XR_TYPE_ACTION_STATE_VECTOR2F;
    if (XR_SUCCEEDED(xrGetActionStateVector2f(m_session, &getInfo, &vec2State))) {
        state.thumbstickX = vec2State.currentState.x;
        state.thumbstickY = vec2State.currentState.y;
    }

    // Get buttons
    XrActionStateBoolean boolState{};
    boolState.type = XR_TYPE_ACTION_STATE_BOOLEAN;

    getInfo.action = m_thumbstickClickAction;
    if (XR_SUCCEEDED(xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        state.thumbstickClick = boolState.currentState;
    }

    getInfo.action = m_primaryButtonAction;
    if (XR_SUCCEEDED(xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        state.primaryButton = boolState.currentState;
    }

    getInfo.action = m_secondaryButtonAction;
    if (XR_SUCCEEDED(xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        state.secondaryButton = boolState.currentState;
    }

    getInfo.action = m_menuButtonAction;
    if (XR_SUCCEEDED(xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        state.menuButton = boolState.currentState;
    }

    return true;
}

bool OpenXRRuntime::SyncActions() {
    if (m_actionSet == XR_NULL_HANDLE || m_session == XR_NULL_HANDLE) {
        return false;
    }

    XrActiveActionSet activeActionSet{};
    activeActionSet.actionSet = m_actionSet;
    activeActionSet.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo syncInfo{};
    syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;

    XrResult result = xrSyncActions(m_session, &syncInfo);
    return XR_SUCCEEDED(result);
}

void OpenXRRuntime::TriggerHaptic(VRHand hand, float amplitude, float duration) {
    if (m_hapticAction == XR_NULL_HANDLE || m_session == XR_NULL_HANDLE) {
        return;
    }

    int handIndex = static_cast<int>(hand);

    XrHapticVibration vibration{};
    vibration.type = XR_TYPE_HAPTIC_VIBRATION;
    vibration.amplitude = amplitude;
    vibration.duration = static_cast<XrDuration>(duration * 1000000000.0f); // Convert to nanoseconds
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;

    XrHapticActionInfo hapticInfo{};
    hapticInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
    hapticInfo.action = m_hapticAction;
    hapticInfo.subactionPath = m_handPaths[handIndex];

    xrApplyHapticFeedback(m_session, &hapticInfo, reinterpret_cast<XrHapticBaseHeader*>(&vibration));
}

void OpenXRRuntime::GetRecommendedRenderSize(uint32_t* width, uint32_t* height) const {
    if (width) *width = m_recommendedWidth;
    if (height) *height = m_recommendedHeight;
}

float OpenXRRuntime::GetRefreshRate() const {
    return m_refreshRate;
}

bool OpenXRRuntime::SupportsDepthSubmission() const {
    return m_depthExtensionSupported;
}

std::string OpenXRRuntime::GetRuntimeName() const {
    return m_runtimeName;
}

std::string OpenXRRuntime::GetRuntimeVersion() const {
    return m_runtimeVersion;
}

std::string OpenXRRuntime::GetHMDName() const {
    return m_hmdName;
}

XrTime OpenXRRuntime::GetPredictedDisplayTime() const {
    return m_frameState.predictedDisplayTime;
}

bool OpenXRRuntime::IsHMDMounted() const {
    // Could check XR_SESSION_STATE_VISIBLE or use user presence extension
    return m_sessionRunning && m_sessionState >= XR_SESSION_STATE_VISIBLE;
}

// Private methods

bool OpenXRRuntime::CreateInstance() {
    std::vector<const char*> extensions = GetRequiredExtensions();

    XrInstanceCreateInfo createInfo{};
    createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
    strcpy_s(createInfo.applicationInfo.applicationName, "GTA5 VR Mod");
    createInfo.applicationInfo.applicationVersion = 1;
    strcpy_s(createInfo.applicationInfo.engineName, "GTA5VR");
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.enabledExtensionNames = extensions.data();

    XrResult result = xrCreateInstance(&createInfo, &m_instance);
    if (XR_FAILED(result)) {
        LOG_ERROR("Failed to create OpenXR instance: " + XrResultToString(result));
        return false;
    }

    // Get instance properties
    XrInstanceProperties instanceProps{};
    instanceProps.type = XR_TYPE_INSTANCE_PROPERTIES;
    xrGetInstanceProperties(m_instance, &instanceProps);

    m_runtimeName = instanceProps.runtimeName;
    m_runtimeVersion = std::to_string(XR_VERSION_MAJOR(instanceProps.runtimeVersion)) + "." +
                       std::to_string(XR_VERSION_MINOR(instanceProps.runtimeVersion)) + "." +
                       std::to_string(XR_VERSION_PATCH(instanceProps.runtimeVersion));

    LOG_INFO("OpenXR Runtime: " + m_runtimeName + " v" + m_runtimeVersion);
    return true;
}

bool OpenXRRuntime::GetSystem() {
    XrSystemGetInfo systemInfo{};
    systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = xrGetSystem(m_instance, &systemInfo, &m_systemId);
    if (XR_FAILED(result)) {
        LOG_ERROR("Failed to get OpenXR system: " + XrResultToString(result));
        return false;
    }

    // Get system properties
    XrSystemProperties systemProps{};
    systemProps.type = XR_TYPE_SYSTEM_PROPERTIES;
    xrGetSystemProperties(m_instance, m_systemId, &systemProps);

    m_hmdName = systemProps.systemName;
    LOG_INFO("HMD: " + m_hmdName);

    return true;
}

bool OpenXRRuntime::CreateReferenceSpace() {
    // Create local reference space (seated)
    XrReferenceSpaceCreateInfo refSpaceInfo{};
    refSpaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    refSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    refSpaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

    XrResult result = xrCreateReferenceSpace(m_session, &refSpaceInfo, &m_referenceSpace);
    if (XR_FAILED(result)) {
        LOG_ERROR("Failed to create reference space: " + XrResultToString(result));
        return false;
    }

    // Create view space for tracking
    refSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    result = xrCreateReferenceSpace(m_session, &refSpaceInfo, &m_viewSpace);
    if (XR_FAILED(result)) {
        LOG_ERROR("Failed to create view space: " + XrResultToString(result));
        return false;
    }

    return true;
}

bool OpenXRRuntime::SetupActions() {
    // Create action set
    XrActionSetCreateInfo actionSetInfo{};
    actionSetInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    strcpy_s(actionSetInfo.actionSetName, "gameplay");
    strcpy_s(actionSetInfo.localizedActionSetName, "Gameplay");

    XrResult result = xrCreateActionSet(m_instance, &actionSetInfo, &m_actionSet);
    if (XR_FAILED(result)) {
        return false;
    }

    // Get hand paths
    xrStringToPath(m_instance, "/user/hand/left", &m_handPaths[0]);
    xrStringToPath(m_instance, "/user/hand/right", &m_handPaths[1]);

    // Create actions
    XrActionCreateInfo actionInfo{};
    actionInfo.type = XR_TYPE_ACTION_CREATE_INFO;
    actionInfo.countSubactionPaths = 2;
    actionInfo.subactionPaths = m_handPaths;

    // Pose action
    actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
    strcpy_s(actionInfo.actionName, "hand_pose");
    strcpy_s(actionInfo.localizedActionName, "Hand Pose");
    xrCreateAction(m_actionSet, &actionInfo, &m_poseAction);

    // Trigger action
    actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
    strcpy_s(actionInfo.actionName, "trigger");
    strcpy_s(actionInfo.localizedActionName, "Trigger");
    xrCreateAction(m_actionSet, &actionInfo, &m_triggerAction);

    // Grip action
    strcpy_s(actionInfo.actionName, "grip");
    strcpy_s(actionInfo.localizedActionName, "Grip");
    xrCreateAction(m_actionSet, &actionInfo, &m_gripAction);

    // Thumbstick action
    actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    strcpy_s(actionInfo.actionName, "thumbstick");
    strcpy_s(actionInfo.localizedActionName, "Thumbstick");
    xrCreateAction(m_actionSet, &actionInfo, &m_thumbstickAction);

    // Boolean actions
    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;

    strcpy_s(actionInfo.actionName, "thumbstick_click");
    strcpy_s(actionInfo.localizedActionName, "Thumbstick Click");
    xrCreateAction(m_actionSet, &actionInfo, &m_thumbstickClickAction);

    strcpy_s(actionInfo.actionName, "primary_button");
    strcpy_s(actionInfo.localizedActionName, "Primary Button");
    xrCreateAction(m_actionSet, &actionInfo, &m_primaryButtonAction);

    strcpy_s(actionInfo.actionName, "secondary_button");
    strcpy_s(actionInfo.localizedActionName, "Secondary Button");
    xrCreateAction(m_actionSet, &actionInfo, &m_secondaryButtonAction);

    strcpy_s(actionInfo.actionName, "menu_button");
    strcpy_s(actionInfo.localizedActionName, "Menu Button");
    xrCreateAction(m_actionSet, &actionInfo, &m_menuButtonAction);

    // Haptic action
    actionInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
    strcpy_s(actionInfo.actionName, "haptic");
    strcpy_s(actionInfo.localizedActionName, "Haptic");
    xrCreateAction(m_actionSet, &actionInfo, &m_hapticAction);

    // Suggest bindings for common controllers
    // Oculus Touch
    XrPath oculusTouchPath;
    xrStringToPath(m_instance, "/interaction_profiles/oculus/touch_controller", &oculusTouchPath);

    std::vector<XrActionSuggestedBinding> bindings;
    XrPath path;

    // Left hand bindings
    xrStringToPath(m_instance, "/user/hand/left/input/aim/pose", &path);
    bindings.push_back({ m_poseAction, path });
    xrStringToPath(m_instance, "/user/hand/left/input/trigger/value", &path);
    bindings.push_back({ m_triggerAction, path });
    xrStringToPath(m_instance, "/user/hand/left/input/squeeze/value", &path);
    bindings.push_back({ m_gripAction, path });
    xrStringToPath(m_instance, "/user/hand/left/input/thumbstick", &path);
    bindings.push_back({ m_thumbstickAction, path });
    xrStringToPath(m_instance, "/user/hand/left/input/thumbstick/click", &path);
    bindings.push_back({ m_thumbstickClickAction, path });
    xrStringToPath(m_instance, "/user/hand/left/input/x/click", &path);
    bindings.push_back({ m_primaryButtonAction, path });
    xrStringToPath(m_instance, "/user/hand/left/input/y/click", &path);
    bindings.push_back({ m_secondaryButtonAction, path });
    xrStringToPath(m_instance, "/user/hand/left/input/menu/click", &path);
    bindings.push_back({ m_menuButtonAction, path });
    xrStringToPath(m_instance, "/user/hand/left/output/haptic", &path);
    bindings.push_back({ m_hapticAction, path });

    // Right hand bindings
    xrStringToPath(m_instance, "/user/hand/right/input/aim/pose", &path);
    bindings.push_back({ m_poseAction, path });
    xrStringToPath(m_instance, "/user/hand/right/input/trigger/value", &path);
    bindings.push_back({ m_triggerAction, path });
    xrStringToPath(m_instance, "/user/hand/right/input/squeeze/value", &path);
    bindings.push_back({ m_gripAction, path });
    xrStringToPath(m_instance, "/user/hand/right/input/thumbstick", &path);
    bindings.push_back({ m_thumbstickAction, path });
    xrStringToPath(m_instance, "/user/hand/right/input/thumbstick/click", &path);
    bindings.push_back({ m_thumbstickClickAction, path });
    xrStringToPath(m_instance, "/user/hand/right/input/a/click", &path);
    bindings.push_back({ m_primaryButtonAction, path });
    xrStringToPath(m_instance, "/user/hand/right/input/b/click", &path);
    bindings.push_back({ m_secondaryButtonAction, path });
    xrStringToPath(m_instance, "/user/hand/right/output/haptic", &path);
    bindings.push_back({ m_hapticAction, path });

    XrInteractionProfileSuggestedBinding suggestedBindings{};
    suggestedBindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    suggestedBindings.interactionProfile = oculusTouchPath;
    suggestedBindings.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggestedBindings.suggestedBindings = bindings.data();

    xrSuggestInteractionProfileBindings(m_instance, &suggestedBindings);

    // Attach action set to session
    XrSessionActionSetsAttachInfo attachInfo{};
    attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &m_actionSet;

    result = xrAttachSessionActionSets(m_session, &attachInfo);
    if (XR_FAILED(result)) {
        LOG_WARNING("Failed to attach action sets");
        return false;
    }

    // Create hand spaces
    for (int hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo spaceInfo{};
        spaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
        spaceInfo.action = m_poseAction;
        spaceInfo.subactionPath = m_handPaths[hand];
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;

        xrCreateActionSpace(m_session, &spaceInfo, &m_handSpaces[hand]);
    }

    return true;
}

bool OpenXRRuntime::LocateViews(XrTime displayTime) {
    XrViewState viewState{};
    viewState.type = XR_TYPE_VIEW_STATE;

    XrViewLocateInfo locateInfo{};
    locateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
    locateInfo.viewConfigurationType = m_viewConfigType;
    locateInfo.displayTime = displayTime;
    locateInfo.space = m_referenceSpace;

    uint32_t viewCount = 2;
    XrResult result = xrLocateViews(m_session, &locateInfo, &viewState, viewCount, &viewCount, m_views.data());

    return XR_SUCCEEDED(result);
}

bool OpenXRRuntime::HandleSessionStateChange(XrEventDataSessionStateChanged* stateEvent) {
    m_sessionState = stateEvent->state;

    switch (m_sessionState) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo beginInfo{};
            beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
            beginInfo.primaryViewConfigurationType = m_viewConfigType;
            xrBeginSession(m_session, &beginInfo);
            m_sessionRunning = true;
            LOG_INFO("OpenXR session started");
            break;
        }
        case XR_SESSION_STATE_STOPPING: {
            xrEndSession(m_session);
            m_sessionRunning = false;
            LOG_INFO("OpenXR session stopped");
            break;
        }
        case XR_SESSION_STATE_LOSS_PENDING:
        case XR_SESSION_STATE_EXITING: {
            m_sessionRunning = false;
            LOG_INFO("OpenXR session ending");
            break;
        }
        default:
            break;
    }

    return true;
}

bool OpenXRRuntime::PollEvents() {
    XrEventDataBuffer event{};
    event.type = XR_TYPE_EVENT_DATA_BUFFER;

    while (xrPollEvent(m_instance, &event) == XR_SUCCESS) {
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
                HandleSessionStateChange(stateEvent);
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                LOG_WARNING("OpenXR instance loss pending");
                break;
            }
            default:
                break;
        }

        event.type = XR_TYPE_EVENT_DATA_BUFFER;
    }

    return true;
}

std::vector<const char*> OpenXRRuntime::GetRequiredExtensions() const {
    std::vector<const char*> extensions = {
        XR_KHR_D3D11_ENABLE_EXTENSION_NAME
    };

    if (m_depthExtensionSupported) {
        extensions.push_back(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
    }

    return extensions;
}

bool OpenXRRuntime::CheckExtensionSupport(const char* extensionName) const {
    for (const auto& ext : m_supportedExtensions) {
        if (strcmp(ext.extensionName, extensionName) == 0) {
            return true;
        }
    }
    return false;
}

std::string OpenXRRuntime::XrResultToString(XrResult result) const {
    char buffer[XR_MAX_RESULT_STRING_SIZE];
    if (m_instance != XR_NULL_HANDLE) {
        xrResultToString(m_instance, result, buffer);
        return std::string(buffer);
    }
    return "XR_ERROR_" + std::to_string(static_cast<int>(result));
}

} // namespace GTA5VR
