#include "VRCore.h"
#include "VRConfig.h"
#include "Logger.h"
#include "MemoryManager.h"
#include "../runtime/RuntimeInterface.h"
#include "../runtime/OpenXRRuntime.h"
#include "../runtime/OpenVRRuntime.h"
#include "../runtime/RuntimeDetector.h"
#include "../rendering/RenderModes/RenderModeInterface.h"
#include "../rendering/RenderModes/NativeStereo.h"
#include "../rendering/RenderModes/SynchronizedSequential.h"
#include "../rendering/RenderModes/AlternatingEyeRendering.h"
#include "../camera/VRCamera.h"
#include "../input/VRInputManager.h"
#include "../ui/VROverlay.h"

namespace GTA5VR {

VRCore& VRCore::GetInstance() {
    static VRCore instance;
    return instance;
}

bool VRCore::Initialize(HWND gameWindow, ID3D11Device* device, ID3D11DeviceContext* context) {
    if (m_state != VRState::Uninitialized) {
        LOG_WARNING("VRCore already initialized");
        return m_state == VRState::Ready || m_state == VRState::Running;
    }

    LOG_INFO("Initializing VRCore...");
    m_state = VRState::Initializing;

    // Store D3D11 resources
    m_gameWindow = gameWindow;
    m_device = device;
    m_context = context;

    if (!m_device || !m_context) {
        LOG_ERROR("Invalid D3D11 device or context");
        m_state = VRState::Error;
        return false;
    }

    // Initialize timing
    QueryPerformanceFrequency(&m_frequency);
    QueryPerformanceCounter(&m_startTime);
    m_lastFrameTime = m_startTime;

    // Initialize VR runtime
    if (!InitializeRuntime()) {
        LOG_ERROR("Failed to initialize VR runtime");
        m_state = VRState::Error;
        return false;
    }

    // Get recommended render size from runtime
    m_runtime->GetRecommendedRenderSize(&m_renderWidth, &m_renderHeight);
    LOG_INFO("Recommended render size: " + std::to_string(m_renderWidth) + "x" + std::to_string(m_renderHeight));

    // Create render targets
    if (!CreateRenderTargets()) {
        LOG_ERROR("Failed to create render targets");
        m_state = VRState::Error;
        return false;
    }

    // Initialize render mode
    if (!InitializeRenderMode()) {
        LOG_ERROR("Failed to initialize render mode");
        m_state = VRState::Error;
        return false;
    }

    // Initialize components
    if (!InitializeComponents()) {
        LOG_ERROR("Failed to initialize VR components");
        m_state = VRState::Error;
        return false;
    }

    m_state = VRState::Ready;
    LOG_INFO("VRCore initialized successfully");
    return true;
}

void VRCore::Shutdown() {
    if (m_state == VRState::Uninitialized) {
        return;
    }

    LOG_INFO("Shutting down VRCore...");
    m_state = VRState::ShuttingDown;

    // Destroy components
    m_overlay.reset();
    m_inputManager.reset();
    m_camera.reset();
    m_renderMode.reset();
    m_runtime.reset();

    // Destroy render targets
    DestroyRenderTargets();

    // Clear callbacks
    m_preFrameCallbacks.clear();
    m_postFrameCallbacks.clear();

    m_state = VRState::Uninitialized;
    LOG_INFO("VRCore shutdown complete");
}

bool VRCore::IsInitialized() const {
    return m_state == VRState::Ready || m_state == VRState::Running || m_state == VRState::Paused;
}

void VRCore::BeginFrame() {
    if (m_state != VRState::Ready && m_state != VRState::Running) {
        return;
    }

    // Update timing
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    m_deltaTime = static_cast<float>(currentTime.QuadPart - m_lastFrameTime.QuadPart) /
                  static_cast<float>(m_frequency.QuadPart);
    m_lastFrameTime = currentTime;
    m_timeSinceStart = static_cast<float>(currentTime.QuadPart - m_startTime.QuadPart) /
                       static_cast<float>(m_frequency.QuadPart);

    // Call pre-frame callbacks
    for (const auto& callback : m_preFrameCallbacks) {
        callback();
    }

    // Begin VR frame
    if (m_runtime) {
        m_runtime->WaitFrame();
        m_runtime->BeginFrame();
    }

    // Update input
    if (m_inputManager) {
        m_inputManager->Update();
    }

    // Update camera with tracking data
    if (m_camera && m_runtime) {
        XrPosef headPose;
        if (m_runtime->GetHeadPose(&headPose)) {
            m_camera->ApplyHeadTracking(headPose);
        }
    }

    // Begin render mode frame
    if (m_renderMode) {
        m_renderMode->BeginFrame();
    }

    m_state = VRState::Running;
    m_currentEye = 0;
}

void VRCore::EndFrame() {
    if (m_state != VRState::Running) {
        return;
    }

    // End render mode frame
    if (m_renderMode) {
        m_renderMode->EndFrame();
    }

    // Call post-frame callbacks
    for (const auto& callback : m_postFrameCallbacks) {
        callback();
    }

    // Update performance stats
    UpdatePerformanceStats();

    m_frameCount++;
}

void VRCore::SubmitFrame() {
    if (m_state != VRState::Running || !m_runtime) {
        return;
    }

    // Submit eye textures to VR runtime
    m_runtime->SubmitFrame(m_eyeTextures[0], m_eyeTextures[1]);
    m_runtime->EndFrame();

    m_state = VRState::Ready;
}

VRState VRCore::GetState() const {
    return m_state;
}

void VRCore::SetState(VRState state) {
    m_state = state;
}

bool VRCore::IsPaused() const {
    return m_state == VRState::Paused;
}

void VRCore::Pause() {
    if (m_state == VRState::Running || m_state == VRState::Ready) {
        m_state = VRState::Paused;
        LOG_INFO("VR paused");
    }
}

void VRCore::Resume() {
    if (m_state == VRState::Paused) {
        m_state = VRState::Ready;
        LOG_INFO("VR resumed");
    }
}

IRuntimeInterface* VRCore::GetRuntime() const {
    return m_runtime.get();
}

bool VRCore::SwitchRuntime(const std::string& runtimeType) {
    LOG_INFO("Switching runtime to: " + runtimeType);

    // Create new runtime
    std::unique_ptr<IRuntimeInterface> newRuntime;

    if (runtimeType == "openxr" || runtimeType == "OpenXR") {
        newRuntime = std::make_unique<OpenXRRuntime>();
    } else if (runtimeType == "openvr" || runtimeType == "OpenVR") {
        newRuntime = std::make_unique<OpenVRRuntime>();
    } else {
        LOG_ERROR("Unknown runtime type: " + runtimeType);
        return false;
    }

    // Initialize new runtime
    if (!newRuntime->Initialize() || !newRuntime->CreateSession(m_device)) {
        LOG_ERROR("Failed to initialize new runtime");
        return false;
    }

    // Swap runtimes
    m_runtime = std::move(newRuntime);

    // Update config
    auto& config = VRConfig::GetInstance().GetConfig();
    if (runtimeType == "openxr") {
        config.preferredRuntime = VRRuntimeType::OpenXR;
    } else {
        config.preferredRuntime = VRRuntimeType::OpenVR;
    }

    return true;
}

IRenderMode* VRCore::GetRenderMode() const {
    return m_renderMode.get();
}

bool VRCore::SetRenderMode(const std::string& mode) {
    LOG_INFO("Switching render mode to: " + mode);

    std::unique_ptr<IRenderMode> newMode;

    if (mode == "native_stereo" || mode == "NativeStereo") {
        newMode = std::make_unique<NativeStereoMode>();
    } else if (mode == "synchronized_sequential" || mode == "SynchronizedSequential") {
        newMode = std::make_unique<SynchronizedSequentialMode>();
    } else if (mode == "alternating_eye" || mode == "AlternatingEye") {
        newMode = std::make_unique<AlternatingEyeMode>();
    } else {
        LOG_ERROR("Unknown render mode: " + mode);
        return false;
    }

    if (!newMode->Initialize(this)) {
        LOG_ERROR("Failed to initialize new render mode");
        return false;
    }

    m_renderMode = std::move(newMode);
    return true;
}

VRCamera* VRCore::GetCamera() const {
    return m_camera.get();
}

VRInputManager* VRCore::GetInputManager() const {
    return m_inputManager.get();
}

VROverlay* VRCore::GetOverlay() const {
    return m_overlay.get();
}

ID3D11Device* VRCore::GetDevice() const {
    return m_device;
}

ID3D11DeviceContext* VRCore::GetContext() const {
    return m_context;
}

ID3D11RenderTargetView* VRCore::GetEyeRenderTarget(uint32_t eyeIndex) const {
    if (eyeIndex >= 2) return nullptr;
    return m_eyeRTVs[eyeIndex];
}

ID3D11DepthStencilView* VRCore::GetEyeDepthStencil(uint32_t eyeIndex) const {
    if (eyeIndex >= 2) return nullptr;
    return m_eyeDSVs[eyeIndex];
}

void VRCore::SetEyeRenderTarget(uint32_t eyeIndex) {
    if (eyeIndex >= 2 || !m_context) return;

    m_currentEye = eyeIndex;
    m_context->OMSetRenderTargets(1, &m_eyeRTVs[eyeIndex], m_eyeDSVs[eyeIndex]);

    // Set viewport
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(m_renderWidth);
    viewport.Height = static_cast<float>(m_renderHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
}

void VRCore::ClearEyeRenderTarget(uint32_t eyeIndex, const float* clearColor) {
    if (eyeIndex >= 2 || !m_context) return;

    static const float defaultClear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const float* color = clearColor ? clearColor : defaultClear;

    m_context->ClearRenderTargetView(m_eyeRTVs[eyeIndex], color);
    m_context->ClearDepthStencilView(m_eyeDSVs[eyeIndex], D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

uint32_t VRCore::GetCurrentEye() const {
    return m_currentEye;
}

void VRCore::GetRecommendedRenderSize(uint32_t* width, uint32_t* height) const {
    if (width) *width = m_renderWidth;
    if (height) *height = m_renderHeight;
}

float VRCore::GetDeltaTime() const {
    return m_deltaTime;
}

float VRCore::GetTimeSinceStart() const {
    return m_timeSinceStart;
}

uint64_t VRCore::GetFrameCount() const {
    return m_frameCount;
}

const VRPerformanceStats& VRCore::GetPerformanceStats() const {
    return m_perfStats;
}

void VRCore::ResetPerformanceStats() {
    m_perfStats = VRPerformanceStats{};
}

void VRCore::RegisterPreFrameCallback(FrameCallback callback) {
    m_preFrameCallbacks.push_back(callback);
}

void VRCore::RegisterPostFrameCallback(FrameCallback callback) {
    m_postFrameCallbacks.push_back(callback);
}

void VRCore::ApplyConfigChanges() {
    auto& config = VRConfig::GetInstance().GetConfig();

    // Apply render mode
    switch (config.renderingMode) {
        case RenderingMode::NativeStereo:
            SetRenderMode("native_stereo");
            break;
        case RenderingMode::AlternatingEye:
            SetRenderMode("alternating_eye");
            break;
        default:
            SetRenderMode("synchronized_sequential");
            break;
    }

    // Apply camera settings
    if (m_camera) {
        m_camera->SetWorldScale(config.worldScale);
        if (config.ipdOverride) {
            m_camera->SetIPD(config.ipdValue);
        }
        m_camera->SetSnapTurning(config.enableSnapTurn, config.snapTurnDegrees);
        m_camera->SetVignetteOnTurn(config.enableComfortVignette);
    }

    // Apply overlay settings
    if (m_overlay) {
        m_overlay->SetHUDDistance(config.hudDistance);
        m_overlay->SetHUDScale(config.hudScale);
        m_overlay->SetHUDCurvature(config.hudCurvature);
    }

    LOG_INFO("Applied configuration changes");
}

void VRCore::Recenter() {
    if (m_camera) {
        m_camera->Recenter();
        LOG_INFO("View recentered");
    }
}

void VRCore::EnableDebugMode(bool enable) {
    m_debugMode = enable;
    LOG_INFO("Debug mode " + std::string(enable ? "enabled" : "disabled"));
}

bool VRCore::IsDebugModeEnabled() const {
    return m_debugMode;
}

void VRCore::DumpDebugInfo() {
    LOG_INFO("=== VRCore Debug Info ===");
    LOG_INFO("State: " + std::to_string(static_cast<int>(m_state)));
    LOG_INFO("Frame count: " + std::to_string(m_frameCount));
    LOG_INFO("Render size: " + std::to_string(m_renderWidth) + "x" + std::to_string(m_renderHeight));
    LOG_INFO("FPS: " + std::to_string(m_perfStats.fps));
    LOG_INFO("Frame time: " + std::to_string(m_perfStats.frameTime) + "ms");
    if (m_runtime) {
        LOG_INFO("Runtime: " + m_runtime->GetRuntimeName());
    }
    if (m_renderMode) {
        LOG_INFO("Render mode: " + m_renderMode->GetName());
    }
    LOG_INFO("=========================");
}

bool VRCore::InitializeRuntime() {
    auto& config = VRConfig::GetInstance().GetConfig();

    // Auto-detect or use preferred runtime
    std::string runtimeType;

    if (config.preferredRuntime == VRRuntimeType::Auto) {
        runtimeType = RuntimeDetector::DetectBestRuntime();
    } else if (config.preferredRuntime == VRRuntimeType::OpenXR) {
        runtimeType = "openxr";
    } else {
        runtimeType = "openvr";
    }

    LOG_INFO("Using VR runtime: " + runtimeType);

    if (runtimeType == "openxr") {
        m_runtime = std::make_unique<OpenXRRuntime>();
    } else {
        m_runtime = std::make_unique<OpenVRRuntime>();
    }

    if (!m_runtime->Initialize()) {
        LOG_ERROR("Failed to initialize VR runtime");
        return false;
    }

    if (!m_runtime->CreateSession(m_device)) {
        LOG_ERROR("Failed to create VR session");
        return false;
    }

    return true;
}

bool VRCore::InitializeRenderMode() {
    auto& config = VRConfig::GetInstance().GetConfig();

    std::string mode;
    switch (config.renderingMode) {
        case RenderingMode::NativeStereo:
            mode = "native_stereo";
            break;
        case RenderingMode::AlternatingEye:
            mode = "alternating_eye";
            break;
        default:
            mode = "synchronized_sequential";
            break;
    }

    return SetRenderMode(mode);
}

bool VRCore::InitializeComponents() {
    // Initialize camera
    m_camera = std::make_unique<VRCamera>();
    if (!m_camera->Initialize(m_runtime.get())) {
        LOG_ERROR("Failed to initialize VR camera");
        return false;
    }

    // Initialize input manager
    m_inputManager = std::make_unique<VRInputManager>();
    if (!m_inputManager->Initialize(m_runtime.get())) {
        LOG_ERROR("Failed to initialize VR input manager");
        return false;
    }

    // Initialize overlay
    m_overlay = std::make_unique<VROverlay>();
    if (!m_overlay->Initialize(m_runtime.get(), m_device)) {
        LOG_ERROR("Failed to initialize VR overlay");
        return false;
    }

    return true;
}

bool VRCore::CreateRenderTargets() {
    for (int eye = 0; eye < 2; ++eye) {
        // Create color texture
        D3D11_TEXTURE2D_DESC texDesc{};
        texDesc.Width = m_renderWidth;
        texDesc.Height = m_renderHeight;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, &m_eyeTextures[eye]);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create eye texture " + std::to_string(eye));
            return false;
        }

        // Create render target view
        hr = m_device->CreateRenderTargetView(m_eyeTextures[eye], nullptr, &m_eyeRTVs[eye]);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create eye RTV " + std::to_string(eye));
            return false;
        }

        // Create depth texture
        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = m_renderWidth;
        depthDesc.Height = m_renderHeight;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        hr = m_device->CreateTexture2D(&depthDesc, nullptr, &m_eyeDepthTextures[eye]);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create eye depth texture " + std::to_string(eye));
            return false;
        }

        // Create depth stencil view
        hr = m_device->CreateDepthStencilView(m_eyeDepthTextures[eye], nullptr, &m_eyeDSVs[eye]);
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create eye DSV " + std::to_string(eye));
            return false;
        }
    }

    LOG_INFO("Created eye render targets");
    return true;
}

void VRCore::DestroyRenderTargets() {
    for (int eye = 0; eye < 2; ++eye) {
        if (m_eyeDSVs[eye]) {
            m_eyeDSVs[eye]->Release();
            m_eyeDSVs[eye] = nullptr;
        }
        if (m_eyeDepthTextures[eye]) {
            m_eyeDepthTextures[eye]->Release();
            m_eyeDepthTextures[eye] = nullptr;
        }
        if (m_eyeRTVs[eye]) {
            m_eyeRTVs[eye]->Release();
            m_eyeRTVs[eye] = nullptr;
        }
        if (m_eyeTextures[eye]) {
            m_eyeTextures[eye]->Release();
            m_eyeTextures[eye] = nullptr;
        }
    }
}

void VRCore::UpdatePerformanceStats() {
    // Calculate FPS
    static float fpsAccumulator = 0.0f;
    static int frameAccumulator = 0;
    static LARGE_INTEGER lastFpsTime{};

    frameAccumulator++;
    fpsAccumulator += m_deltaTime;

    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);

    float elapsed = static_cast<float>(currentTime.QuadPart - lastFpsTime.QuadPart) /
                   static_cast<float>(m_frequency.QuadPart);

    if (elapsed >= 1.0f) {
        m_perfStats.fps = static_cast<float>(frameAccumulator) / elapsed;
        m_perfStats.frameTime = fpsAccumulator / static_cast<float>(frameAccumulator) * 1000.0f;

        frameAccumulator = 0;
        fpsAccumulator = 0.0f;
        lastFpsTime = currentTime;
    }
}

} // namespace GTA5VR
