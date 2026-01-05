#include "RAGEEngineHook.h"
#include "../core/Logger.h"
#include "../core/MemoryManager.h"
#include <Psapi.h>
#include <vector>
#include <sstream>

namespace GTA5VR {

// Static instance pointer for hook callbacks
RAGEEngineHook* RAGEEngineHook::s_instance = nullptr;

RAGEEngineHook::RAGEEngineHook() {
    LOG_DEBUG("RAGEEngineHook", "Constructor called");
    s_instance = this;

    // Initialize identity matrices
    for (int i = 0; i < 16; i++) {
        m_vrViewMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        m_vrProjectionMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
}

RAGEEngineHook::~RAGEEngineHook() {
    Shutdown();
    s_instance = nullptr;
}

bool RAGEEngineHook::Initialize() {
    if (m_initialized) {
        LOG_WARN("RAGEEngineHook", "Already initialized");
        return true;
    }

    LOG_INFO("RAGEEngineHook", "Initializing RAGE Engine hooks");

    // Find necessary addresses through pattern scanning
    if (!FindCameraAddresses()) {
        LOG_ERROR("RAGEEngineHook", "Failed to find camera addresses");
        return false;
    }

    if (!FindGameStateAddresses()) {
        LOG_WARN("RAGEEngineHook", "Failed to find game state addresses (non-critical)");
    }

    m_initialized = true;
    LOG_INFO("RAGEEngineHook", "RAGE Engine hook system initialized");
    return true;
}

void RAGEEngineHook::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("RAGEEngineHook", "Shutting down RAGE Engine hooks");

    UnhookAll();

    m_cameraCallbacks.clear();
    m_viewportCallbacks.clear();
    m_fovCallbacks.clear();

    m_initialized = false;
}

bool RAGEEngineHook::FindCameraAddresses() {
    LOG_INFO("RAGEEngineHook", "Searching for camera addresses...");

    // Get GTA5.exe module info
    HMODULE gameModule = GetModuleHandleA("GTA5.exe");
    if (!gameModule) {
        LOG_ERROR("RAGEEngineHook", "Failed to get GTA5.exe module handle");
        return false;
    }

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), gameModule, &moduleInfo, sizeof(moduleInfo))) {
        LOG_ERROR("RAGEEngineHook", "Failed to get module information");
        return false;
    }

    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(gameModule);
    size_t moduleSize = moduleInfo.SizeOfImage;

    LOG_DEBUG("RAGEEngineHook", "GTA5.exe base: 0x%llX, size: 0x%llX", baseAddr, moduleSize);

    // Pattern for camera update function (example pattern - needs verification)
    // This is a placeholder pattern that would need to be verified against actual game
    const char* cameraPattern = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 30";

    m_cameraUpdateAddr = FindPattern(baseAddr, moduleSize, cameraPattern);
    if (m_cameraUpdateAddr) {
        LOG_INFO("RAGEEngineHook", "Found camera update at 0x%llX", m_cameraUpdateAddr);
    } else {
        LOG_WARN("RAGEEngineHook", "Camera update pattern not found, trying alternative...");
    }

    // Pattern for camera data structure
    const char* cameraDataPattern = "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B D8";

    uintptr_t cameraDataRef = FindPattern(baseAddr, moduleSize, cameraDataPattern);
    if (cameraDataRef) {
        // Resolve RIP-relative address
        int32_t offset = *reinterpret_cast<int32_t*>(cameraDataRef + 3);
        m_cameraDataAddr = cameraDataRef + 7 + offset;
        LOG_INFO("RAGEEngineHook", "Found camera data at 0x%llX", m_cameraDataAddr);
    }

    // Pattern for FOV getter
    const char* fovPattern = "F3 0F 10 05 ?? ?? ?? ?? F3 0F 59 05 ?? ?? ?? ??";

    m_fovGetAddr = FindPattern(baseAddr, moduleSize, fovPattern);
    if (m_fovGetAddr) {
        LOG_INFO("RAGEEngineHook", "Found FOV getter at 0x%llX", m_fovGetAddr);
    }

    // Even if some patterns fail, we can still partially function
    return true;
}

bool RAGEEngineHook::FindGameStateAddresses() {
    HMODULE gameModule = GetModuleHandleA("GTA5.exe");
    if (!gameModule) {
        return false;
    }

    MODULEINFO moduleInfo;
    GetModuleInformation(GetCurrentProcess(), gameModule, &moduleInfo, sizeof(moduleInfo));

    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(gameModule);
    size_t moduleSize = moduleInfo.SizeOfImage;

    // Pattern for game state (is in cutscene, is paused, etc.)
    const char* gameStatePattern = "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 80 78";

    uintptr_t gameStateRef = FindPattern(baseAddr, moduleSize, gameStatePattern);
    if (gameStateRef) {
        int32_t offset = *reinterpret_cast<int32_t*>(gameStateRef + 3);
        m_gameStateAddr = gameStateRef + 7 + offset;
        LOG_INFO("RAGEEngineHook", "Found game state at 0x%llX", m_gameStateAddr);
        return true;
    }

    return false;
}

bool RAGEEngineHook::HookCameraUpdate() {
    if (!m_initialized || m_cameraHooked) {
        return m_cameraHooked;
    }

    if (m_cameraUpdateAddr == 0) {
        LOG_WARN("RAGEEngineHook", "Camera update address not found");
        return false;
    }

    if (!InstallHook(m_cameraUpdateAddr, &CameraUpdateHook, &m_originalCameraUpdate)) {
        LOG_ERROR("RAGEEngineHook", "Failed to install camera update hook");
        return false;
    }

    m_cameraHooked = true;
    LOG_INFO("RAGEEngineHook", "Camera update hooked successfully");
    return true;
}

bool RAGEEngineHook::HookViewportUpdate() {
    if (!m_initialized || m_viewportHooked) {
        return m_viewportHooked;
    }

    if (m_viewportUpdateAddr == 0) {
        LOG_WARN("RAGEEngineHook", "Viewport update address not found");
        return false;
    }

    if (!InstallHook(m_viewportUpdateAddr, &ViewportUpdateHook, &m_originalViewportUpdate)) {
        LOG_ERROR("RAGEEngineHook", "Failed to install viewport update hook");
        return false;
    }

    m_viewportHooked = true;
    LOG_INFO("RAGEEngineHook", "Viewport update hooked successfully");
    return true;
}

bool RAGEEngineHook::HookFOVFunction() {
    if (!m_initialized || m_fovHooked) {
        return m_fovHooked;
    }

    if (m_fovGetAddr == 0) {
        LOG_WARN("RAGEEngineHook", "FOV address not found");
        return false;
    }

    if (!InstallHook(m_fovGetAddr, &FOVGetHook, &m_originalFOVGet)) {
        LOG_ERROR("RAGEEngineHook", "Failed to install FOV hook");
        return false;
    }

    m_fovHooked = true;
    LOG_INFO("RAGEEngineHook", "FOV function hooked successfully");
    return true;
}

bool RAGEEngineHook::HookWorldToScreen() {
    // World to screen conversion hook for HUD projection
    // Implementation depends on specific game version
    LOG_DEBUG("RAGEEngineHook", "World to screen hook not yet implemented");
    return false;
}

void RAGEEngineHook::UnhookAll() {
    if (m_cameraHooked && m_cameraUpdateAddr) {
        RemoveHook(m_cameraUpdateAddr);
        m_cameraHooked = false;
    }

    if (m_viewportHooked && m_viewportUpdateAddr) {
        RemoveHook(m_viewportUpdateAddr);
        m_viewportHooked = false;
    }

    if (m_fovHooked && m_fovGetAddr) {
        RemoveHook(m_fovGetAddr);
        m_fovHooked = false;
    }

    LOG_INFO("RAGEEngineHook", "All hooks removed");
}

bool RAGEEngineHook::InstallHook(uintptr_t target, void* detour, void** original) {
    return MemoryManager::Instance().CreateHook(
        reinterpret_cast<void*>(target), detour, original);
}

void RAGEEngineHook::RemoveHook(uintptr_t target) {
    MemoryManager::Instance().RemoveHook(reinterpret_cast<void*>(target));
}

void RAGEEngineHook::CameraUpdateHook(void* cameraData) {
    if (!s_instance || !cameraData) {
        return;
    }

    RAGECameraData* camera = static_cast<RAGECameraData*>(cameraData);

    // Store original camera data
    memcpy(&s_instance->m_currentCamera, camera, sizeof(RAGECameraData));

    // Apply VR override if enabled
    if (s_instance->m_vrOverrideEnabled) {
        // Override with VR matrices
        memcpy(camera->viewMatrix, s_instance->m_vrViewMatrix, sizeof(float) * 16);
        memcpy(camera->projectionMatrix, s_instance->m_vrProjectionMatrix, sizeof(float) * 16);

        if (s_instance->m_lockToHeadTracking) {
            camera->position[0] = s_instance->m_vrPosition[0];
            camera->position[1] = s_instance->m_vrPosition[1];
            camera->position[2] = s_instance->m_vrPosition[2];
            camera->rotation[0] = s_instance->m_vrRotation[0];
            camera->rotation[1] = s_instance->m_vrRotation[1];
            camera->rotation[2] = s_instance->m_vrRotation[2];
        }

        camera->fov = s_instance->m_vrFOV;
    }

    // Disable camera shake if requested
    if (s_instance->m_disableCameraShake) {
        // Zero out any shake offsets
        // Implementation depends on camera structure layout
    }

    // Call registered callbacks
    for (auto& callback : s_instance->m_cameraCallbacks) {
        if (callback) {
            callback(camera);
        }
    }

    // Call original function if we have it
    if (s_instance->m_originalCameraUpdate) {
        using OriginalFunc = void(*)(void*);
        static_cast<OriginalFunc>(s_instance->m_originalCameraUpdate)(cameraData);
    }
}

void RAGEEngineHook::ViewportUpdateHook(void* viewportData) {
    if (!s_instance || !viewportData) {
        return;
    }

    RAGEViewport* viewport = static_cast<RAGEViewport*>(viewportData);

    // Store current viewport
    memcpy(&s_instance->m_currentViewport, viewport, sizeof(RAGEViewport));

    // Call registered callbacks
    for (auto& callback : s_instance->m_viewportCallbacks) {
        if (callback) {
            callback(viewport);
        }
    }

    // Call original
    if (s_instance->m_originalViewportUpdate) {
        using OriginalFunc = void(*)(void*);
        static_cast<OriginalFunc>(s_instance->m_originalViewportUpdate)(viewportData);
    }
}

float RAGEEngineHook::FOVGetHook() {
    if (!s_instance) {
        return 75.0f;  // Default FOV
    }

    float fov = 75.0f;

    // Call original to get base FOV
    if (s_instance->m_originalFOVGet) {
        using OriginalFunc = float(*)();
        fov = static_cast<OriginalFunc>(s_instance->m_originalFOVGet)();
    }

    // Override with VR FOV if enabled
    if (s_instance->m_vrOverrideEnabled) {
        fov = s_instance->m_vrFOV;
    }

    // Notify callbacks
    for (auto& callback : s_instance->m_fovCallbacks) {
        if (callback) {
            callback(fov);
        }
    }

    return fov;
}

void RAGEEngineHook::SetCameraPosition(float x, float y, float z) {
    m_vrPosition[0] = x;
    m_vrPosition[1] = y;
    m_vrPosition[2] = z;
}

void RAGEEngineHook::SetCameraRotation(float pitch, float yaw, float roll) {
    m_vrRotation[0] = pitch;
    m_vrRotation[1] = yaw;
    m_vrRotation[2] = roll;
}

void RAGEEngineHook::SetCameraFOV(float fov) {
    m_vrFOV = fov;
}

void RAGEEngineHook::SetViewMatrix(const float* matrix) {
    if (matrix) {
        memcpy(m_vrViewMatrix, matrix, sizeof(float) * 16);
    }
}

void RAGEEngineHook::SetProjectionMatrix(const float* matrix) {
    if (matrix) {
        memcpy(m_vrProjectionMatrix, matrix, sizeof(float) * 16);
    }
}

void RAGEEngineHook::GetCameraPosition(float& x, float& y, float& z) const {
    x = m_currentCamera.position[0];
    y = m_currentCamera.position[1];
    z = m_currentCamera.position[2];
}

void RAGEEngineHook::GetCameraRotation(float& pitch, float& yaw, float& roll) const {
    pitch = m_currentCamera.rotation[0];
    yaw = m_currentCamera.rotation[1];
    roll = m_currentCamera.rotation[2];
}

float RAGEEngineHook::GetCameraFOV() const {
    return m_currentCamera.fov;
}

GTACameraMode RAGEEngineHook::GetCurrentCameraMode() const {
    // This would query the game's camera mode
    // Implementation depends on reverse engineering
    return GTACameraMode::Unknown;
}

const float* RAGEEngineHook::GetViewMatrix() const {
    return m_currentCamera.viewMatrix;
}

const float* RAGEEngineHook::GetProjectionMatrix() const {
    return m_currentCamera.projectionMatrix;
}

void RAGEEngineHook::RegisterCameraCallback(CameraUpdateCallback callback) {
    if (callback) {
        m_cameraCallbacks.push_back(callback);
    }
}

void RAGEEngineHook::RegisterViewportCallback(ViewportUpdateCallback callback) {
    if (callback) {
        m_viewportCallbacks.push_back(callback);
    }
}

void RAGEEngineHook::RegisterFOVCallback(FOVChangeCallback callback) {
    if (callback) {
        m_fovCallbacks.push_back(callback);
    }
}

void RAGEEngineHook::EnableVRCameraOverride(bool enable) {
    m_vrOverrideEnabled = enable;
    LOG_INFO("RAGEEngineHook", "VR camera override %s", enable ? "enabled" : "disabled");
}

void RAGEEngineHook::ForceFirstPerson(bool force) {
    m_forceFirstPerson = force;
    // Would also need to write to game memory to force the camera mode
    LOG_INFO("RAGEEngineHook", "Force first person %s", force ? "enabled" : "disabled");
}

void RAGEEngineHook::DisableCameraShake(bool disable) {
    m_disableCameraShake = disable;
    LOG_DEBUG("RAGEEngineHook", "Camera shake %s", disable ? "disabled" : "enabled");
}

void RAGEEngineHook::LockToHeadTracking(bool lock) {
    m_lockToHeadTracking = lock;
    LOG_DEBUG("RAGEEngineHook", "Lock to head tracking: %s", lock ? "yes" : "no");
}

bool RAGEEngineHook::IsInCutscene() const {
    if (m_gameStateAddr == 0) {
        return false;
    }

    // Read cutscene flag from game memory
    // Implementation depends on game structure
    return false;
}

bool RAGEEngineHook::IsInVehicle() const {
    // Query game state
    return false;
}

bool RAGEEngineHook::IsPlayerAiming() const {
    return false;
}

bool RAGEEngineHook::IsInCover() const {
    return false;
}

bool RAGEEngineHook::IsPaused() const {
    return false;
}

bool RAGEEngineHook::IsLoading() const {
    return false;
}

uintptr_t RAGEEngineHook::FindPattern(const char* module, const char* pattern) {
    HMODULE hModule = GetModuleHandleA(module);
    if (!hModule) {
        return 0;
    }

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo))) {
        return 0;
    }

    return FindPattern(reinterpret_cast<uintptr_t>(hModule), moduleInfo.SizeOfImage, pattern);
}

uintptr_t RAGEEngineHook::FindPattern(uintptr_t start, size_t size, const char* pattern) {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;

    if (!ParsePattern(pattern, bytes, mask)) {
        return 0;
    }

    if (bytes.empty()) {
        return 0;
    }

    const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(start);
    const uint8_t* scanEnd = scanStart + size - bytes.size();

    for (const uint8_t* current = scanStart; current <= scanEnd; current++) {
        bool found = true;

        for (size_t i = 0; i < bytes.size(); i++) {
            if (mask[i] && current[i] != bytes[i]) {
                found = false;
                break;
            }
        }

        if (found) {
            return reinterpret_cast<uintptr_t>(current);
        }
    }

    return 0;
}

bool RAGEEngineHook::ParsePattern(const char* pattern, std::vector<uint8_t>& bytes,
                                   std::vector<bool>& mask) {
    bytes.clear();
    mask.clear();

    std::string patternStr(pattern);
    std::istringstream iss(patternStr);
    std::string token;

    while (iss >> token) {
        if (token == "?" || token == "??") {
            bytes.push_back(0);
            mask.push_back(false);  // Wildcard
        } else {
            try {
                uint8_t byte = static_cast<uint8_t>(std::stoul(token, nullptr, 16));
                bytes.push_back(byte);
                mask.push_back(true);  // Must match
            } catch (...) {
                return false;
            }
        }
    }

    return !bytes.empty();
}

} // namespace GTA5VR
