#pragma once

#include "IVRBackend.hpp"
#include <memory>
#include <string>

#ifdef OVRINJECT_EXPORTS
#define OVR_API __declspec(dllexport)
#else
#define OVR_API __declspec(dllimport)
#endif

namespace OVRInject {
namespace VR {

/**
 * VR runtime selection
 */
enum class Runtime {
    Auto,       // Auto-detect best available runtime
    OpenVR,     // Force OpenVR (SteamVR)
    OpenXR      // Force OpenXR
};

/**
 * VRManager - Manages VR runtime selection and backend lifecycle
 *
 * Provides runtime selection between OpenVR and OpenXR backends with:
 * - Automatic detection of available runtimes
 * - Manual override via config or environment variable
 * - Unified access to active backend
 *
 * Usage:
 * ```cpp
 * auto& mgr = VRManager::Get();
 * if (mgr.Initialize(device, Runtime::Auto)) {
 *     auto* backend = mgr.GetBackend();
 *     // Use backend...
 * }
 * ```
 */
class VRManager {
public:
    /**
     * Get singleton instance
     */
    OVR_API static VRManager& Get();

    /**
     * Initialize VR with preferred runtime
     * @param device D3D11 device from game
     * @param preferred Preferred runtime (Auto for best available)
     * @return true if any runtime initialized successfully
     */
    OVR_API bool Initialize(ID3D11Device* device, Runtime preferred = Runtime::Auto);

    /**
     * Shutdown VR
     */
    OVR_API void Shutdown();

    /**
     * Check if initialized
     */
    OVR_API bool IsInitialized() const;

    /**
     * Get active backend
     * @return Pointer to active backend, or nullptr if not initialized
     */
    OVR_API IVRBackend* GetBackend() const;

    /**
     * Get active runtime type
     */
    OVR_API Runtime GetActiveRuntime() const;

    /**
     * Get active runtime name as string
     */
    OVR_API const char* GetActiveRuntimeName() const;

    /**
     * Check if OpenVR is available on this system
     */
    OVR_API static bool IsOpenVRAvailable();

    /**
     * Check if OpenXR is available on this system
     */
    OVR_API static bool IsOpenXRAvailable();

private:
    VRManager() = default;
    ~VRManager();

    // Disable copy
    VRManager(const VRManager&) = delete;
    VRManager& operator=(const VRManager&) = delete;

    /**
     * Try to initialize OpenVR backend
     */
    bool TryInitializeOpenVR(ID3D11Device* device);

    /**
     * Try to initialize OpenXR backend
     */
    bool TryInitializeOpenXR(ID3D11Device* device);

    /**
     * Get preferred runtime from environment/config
     */
    Runtime GetPreferredRuntimeFromConfig() const;

    std::unique_ptr<IVRBackend> backend_;
    Runtime active_runtime_ = Runtime::Auto;
    ID3D11Device* device_ = nullptr;
};

} // namespace VR
} // namespace OVRInject
