#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <memory>
#include <functional>

namespace GTA5VR {

// Forward declarations
class IRuntimeInterface;
class IRenderMode;
class VRCamera;
class VRInputManager;
class VROverlay;

// VR state
enum class VRState {
    Uninitialized,
    Initializing,
    Ready,
    Running,
    Paused,
    Error,
    ShuttingDown
};

// Performance statistics
struct VRPerformanceStats {
    float fps;
    float frameTime;
    float gpuTime;
    float cpuTime;
    uint64_t droppedFrames;
    float reprojectionRatio;
    uint64_t vramUsed;
    uint64_t vramTotal;
    bool reprojectionActive;
};

// Main VR system class
class VRCore {
public:
    static VRCore& GetInstance();

    // Lifecycle
    bool Initialize(HWND gameWindow, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    bool IsInitialized() const;

    // Frame lifecycle
    void BeginFrame();
    void EndFrame();
    void SubmitFrame();

    // State management
    VRState GetState() const;
    void SetState(VRState state);
    bool IsPaused() const;
    void Pause();
    void Resume();

    // Runtime management
    IRuntimeInterface* GetRuntime() const;
    bool SwitchRuntime(const std::string& runtimeType);

    // Render mode management
    IRenderMode* GetRenderMode() const;
    bool SetRenderMode(const std::string& mode);

    // Components
    VRCamera* GetCamera() const;
    VRInputManager* GetInputManager() const;
    VROverlay* GetOverlay() const;

    // D3D11 resources
    ID3D11Device* GetDevice() const;
    ID3D11DeviceContext* GetContext() const;
    ID3D11RenderTargetView* GetEyeRenderTarget(uint32_t eyeIndex) const;
    ID3D11DepthStencilView* GetEyeDepthStencil(uint32_t eyeIndex) const;

    // Rendering
    void SetEyeRenderTarget(uint32_t eyeIndex);
    void ClearEyeRenderTarget(uint32_t eyeIndex, const float* clearColor);
    uint32_t GetCurrentEye() const;
    void GetRecommendedRenderSize(uint32_t* width, uint32_t* height) const;

    // Timing
    float GetDeltaTime() const;
    float GetTimeSinceStart() const;
    uint64_t GetFrameCount() const;

    // Performance
    const VRPerformanceStats& GetPerformanceStats() const;
    void ResetPerformanceStats();

    // Callbacks
    using FrameCallback = std::function<void()>;
    void RegisterPreFrameCallback(FrameCallback callback);
    void RegisterPostFrameCallback(FrameCallback callback);

    // Settings sync
    void ApplyConfigChanges();
    void Recenter();

    // Debug
    void EnableDebugMode(bool enable);
    bool IsDebugModeEnabled() const;
    void DumpDebugInfo();

private:
    VRCore() = default;
    ~VRCore() = default;
    VRCore(const VRCore&) = delete;
    VRCore& operator=(const VRCore&) = delete;

    // Internal initialization
    bool InitializeRuntime();
    bool InitializeRenderMode();
    bool InitializeComponents();
    bool CreateRenderTargets();
    void DestroyRenderTargets();
    void UpdatePerformanceStats();

    // State
    VRState m_state = VRState::Uninitialized;
    bool m_debugMode = false;

    // D3D11 resources
    HWND m_gameWindow = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Eye render targets
    ID3D11Texture2D* m_eyeTextures[2] = { nullptr, nullptr };
    ID3D11RenderTargetView* m_eyeRTVs[2] = { nullptr, nullptr };
    ID3D11Texture2D* m_eyeDepthTextures[2] = { nullptr, nullptr };
    ID3D11DepthStencilView* m_eyeDSVs[2] = { nullptr, nullptr };

    // Components
    std::unique_ptr<IRuntimeInterface> m_runtime;
    std::unique_ptr<IRenderMode> m_renderMode;
    std::unique_ptr<VRCamera> m_camera;
    std::unique_ptr<VRInputManager> m_inputManager;
    std::unique_ptr<VROverlay> m_overlay;

    // Timing
    uint64_t m_frameCount = 0;
    float m_deltaTime = 0.0f;
    float m_timeSinceStart = 0.0f;
    LARGE_INTEGER m_lastFrameTime{};
    LARGE_INTEGER m_startTime{};
    LARGE_INTEGER m_frequency{};

    // Current rendering state
    uint32_t m_currentEye = 0;
    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;

    // Performance
    VRPerformanceStats m_perfStats{};

    // Callbacks
    std::vector<FrameCallback> m_preFrameCallbacks;
    std::vector<FrameCallback> m_postFrameCallbacks;
};

// Global convenience function
inline VRCore& GetVRCore() {
    return VRCore::GetInstance();
}

} // namespace GTA5VR
