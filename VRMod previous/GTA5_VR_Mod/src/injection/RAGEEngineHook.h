#pragma once

#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <functional>

namespace GTA5VR {

// RAGE Engine camera data structure (reverse-engineered)
struct RAGECameraData {
    float position[3];          // World position
    float rotation[3];          // Euler angles (pitch, yaw, roll)
    float fov;                  // Field of view in degrees
    float nearPlane;            // Near clip plane
    float farPlane;             // Far clip plane
    float aspectRatio;          // Aspect ratio
    uint8_t padding[32];        // Unknown/padding
    float viewMatrix[16];       // View matrix
    float projectionMatrix[16]; // Projection matrix
};

// RAGE Engine viewport data
struct RAGEViewport {
    float x, y;           // Position
    float width, height;  // Size
    float minDepth;       // Min depth
    float maxDepth;       // Max depth
};

// Camera mode for GTA V
enum class GTACameraMode {
    Unknown = 0,
    FirstPerson = 1,
    ThirdPerson = 2,
    Cinematic = 3,
    Vehicle = 4,
    Cutscene = 5,
    Aim = 6,
    Cover = 7
};

// Hook callbacks
using CameraUpdateCallback = std::function<void(RAGECameraData* camera)>;
using ViewportUpdateCallback = std::function<void(RAGEViewport* viewport)>;
using FOVChangeCallback = std::function<void(float newFOV)>;

class RAGEEngineHook {
public:
    RAGEEngineHook();
    ~RAGEEngineHook();

    // Prevent copying
    RAGEEngineHook(const RAGEEngineHook&) = delete;
    RAGEEngineHook& operator=(const RAGEEngineHook&) = delete;

    // Initialization
    bool Initialize();
    void Shutdown();

    // Hook installation
    bool HookCameraUpdate();
    bool HookViewportUpdate();
    bool HookFOVFunction();
    bool HookWorldToScreen();

    // Hook removal
    void UnhookAll();

    // Camera manipulation
    void SetCameraPosition(float x, float y, float z);
    void SetCameraRotation(float pitch, float yaw, float roll);
    void SetCameraFOV(float fov);
    void SetViewMatrix(const float* matrix);
    void SetProjectionMatrix(const float* matrix);

    // Camera queries
    void GetCameraPosition(float& x, float& y, float& z) const;
    void GetCameraRotation(float& pitch, float& yaw, float& roll) const;
    float GetCameraFOV() const;
    GTACameraMode GetCurrentCameraMode() const;
    const float* GetViewMatrix() const;
    const float* GetProjectionMatrix() const;

    // Callback registration
    void RegisterCameraCallback(CameraUpdateCallback callback);
    void RegisterViewportCallback(ViewportUpdateCallback callback);
    void RegisterFOVCallback(FOVChangeCallback callback);

    // VR overrides
    void EnableVRCameraOverride(bool enable);
    bool IsVRCameraOverrideEnabled() const { return m_vrOverrideEnabled; }

    // Force first person mode
    void ForceFirstPerson(bool force);
    bool IsFirstPersonForced() const { return m_forceFirstPerson; }

    // Disable in-game camera shake
    void DisableCameraShake(bool disable);

    // Lock camera to head tracking
    void LockToHeadTracking(bool lock);

    // Game state queries
    bool IsInCutscene() const;
    bool IsInVehicle() const;
    bool IsPlayerAiming() const;
    bool IsInCover() const;
    bool IsPaused() const;
    bool IsLoading() const;

    // Memory pattern scanning
    static uintptr_t FindPattern(const char* module, const char* pattern);
    static uintptr_t FindPattern(uintptr_t start, size_t size, const char* pattern);

    // Status
    bool IsInitialized() const { return m_initialized; }
    bool AreCameraHooksActive() const { return m_cameraHooked; }

private:
    bool FindCameraAddresses();
    bool FindGameStateAddresses();
    bool InstallHook(uintptr_t target, void* detour, void** original);
    void RemoveHook(uintptr_t target);

    // Hook trampolines (called by the hooked functions)
    static void CameraUpdateHook(void* cameraData);
    static void ViewportUpdateHook(void* viewportData);
    static float FOVGetHook();

    // Pattern scanning helpers
    static bool ParsePattern(const char* pattern, std::vector<uint8_t>& bytes,
                            std::vector<bool>& mask);

private:
    // Singleton instance for static hook callbacks
    static RAGEEngineHook* s_instance;

    // Addresses
    uintptr_t m_cameraUpdateAddr = 0;
    uintptr_t m_viewportUpdateAddr = 0;
    uintptr_t m_fovGetAddr = 0;
    uintptr_t m_worldToScreenAddr = 0;
    uintptr_t m_cameraDataAddr = 0;
    uintptr_t m_gameStateAddr = 0;

    // Original function pointers
    void* m_originalCameraUpdate = nullptr;
    void* m_originalViewportUpdate = nullptr;
    void* m_originalFOVGet = nullptr;

    // Current camera state
    RAGECameraData m_currentCamera = {};
    RAGEViewport m_currentViewport = {};

    // VR override matrices
    float m_vrViewMatrix[16] = {};
    float m_vrProjectionMatrix[16] = {};
    float m_vrPosition[3] = {};
    float m_vrRotation[3] = {};
    float m_vrFOV = 90.0f;

    // Callbacks
    std::vector<CameraUpdateCallback> m_cameraCallbacks;
    std::vector<ViewportUpdateCallback> m_viewportCallbacks;
    std::vector<FOVChangeCallback> m_fovCallbacks;

    // State flags
    bool m_initialized = false;
    bool m_cameraHooked = false;
    bool m_viewportHooked = false;
    bool m_fovHooked = false;
    bool m_vrOverrideEnabled = false;
    bool m_forceFirstPerson = false;
    bool m_disableCameraShake = false;
    bool m_lockToHeadTracking = false;
};

} // namespace GTA5VR
