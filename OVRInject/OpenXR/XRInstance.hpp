#pragma once

#include "XRCore.hpp"
#include <memory>
#include <string>

namespace OVRInject {
namespace XR {

/**
 * XRInstance - Manages OpenXR instance lifecycle and system discovery
 *
 * This class handles:
 * - Creating and destroying the XrInstance
 * - Discovering the XR system (HMD)
 * - Querying system properties
 * - Loading extension functions
 * - Getting D3D11 graphics requirements
 */
class XRInstance {
public:
    XRInstance();
    ~XRInstance();

    // Disable copy
    XRInstance(const XRInstance&) = delete;
    XRInstance& operator=(const XRInstance&) = delete;

    /**
     * Initialize the OpenXR instance
     * @param appName Application name for OpenXR
     * @return true on success
     */
    bool Initialize(const char* appName = "GTA VR");

    /**
     * Shutdown and cleanup
     */
    void Shutdown();

    /**
     * Check if initialized
     */
    bool IsInitialized() const { return instance_ != XR_NULL_HANDLE; }

    //-------------------------------------------------------------------------
    // Accessors
    //-------------------------------------------------------------------------

    XrInstance GetHandle() const { return instance_; }
    XrSystemId GetSystemId() const { return system_id_; }

    //-------------------------------------------------------------------------
    // System Properties
    //-------------------------------------------------------------------------

    /**
     * Get the system (HMD) name
     */
    const char* GetSystemName() const;

    /**
     * Get vendor ID
     */
    uint32_t GetVendorId() const;

    /**
     * Get maximum swapchain dimensions
     */
    uint32_t GetMaxSwapchainWidth() const;
    uint32_t GetMaxSwapchainHeight() const;

    /**
     * Get maximum layer count
     */
    uint32_t GetMaxLayerCount() const;

    /**
     * Check if orientation tracking is supported
     */
    bool SupportsOrientationTracking() const;

    /**
     * Check if position tracking is supported
     */
    bool SupportsPositionTracking() const;

    //-------------------------------------------------------------------------
    // D3D11 Graphics Requirements
    //-------------------------------------------------------------------------

    /**
     * Get D3D11 graphics requirements from the runtime
     * Must be called before creating a session
     *
     * @param minFeatureLevel Output: minimum required D3D feature level
     * @param adapterLuid Output: LUID of the adapter to use
     * @return true on success
     */
    bool GetD3D11GraphicsRequirements(D3D_FEATURE_LEVEL& minFeatureLevel,
                                       LUID& adapterLuid);

    //-------------------------------------------------------------------------
    // Extension Support
    //-------------------------------------------------------------------------

    /**
     * Check if an extension is enabled
     */
    bool IsExtensionEnabled(const char* extensionName) const;

    /**
     * Get list of enabled extensions
     */
    const std::vector<std::string>& GetEnabledExtensions() const {
        return enabled_extensions_;
    }

private:
    /**
     * Create the XrInstance with required extensions
     */
    bool CreateInstance(const char* appName);

    /**
     * Get the XR system (HMD)
     */
    bool GetSystem();

    /**
     * Query system properties
     */
    bool QuerySystemProperties();

    /**
     * Load extension function pointers
     */
    bool LoadExtensionFunctions();

    //-------------------------------------------------------------------------
    // Members
    //-------------------------------------------------------------------------

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId system_id_ = XR_NULL_SYSTEM_ID;

    // System properties
    XrSystemProperties system_properties_ = {XR_TYPE_SYSTEM_PROPERTIES};

    // Graphics requirements
    XrGraphicsRequirementsD3D11KHR graphics_requirements_ = {
        XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR
    };
    bool graphics_requirements_queried_ = false;

    // Enabled extensions
    std::vector<std::string> enabled_extensions_;

    // Extension function pointers
    PFN_xrGetD3D11GraphicsRequirementsKHR xrGetD3D11GraphicsRequirementsKHR_ = nullptr;
};

} // namespace XR
} // namespace OVRInject
