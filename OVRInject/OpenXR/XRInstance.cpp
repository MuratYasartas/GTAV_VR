#include "XRInstance.hpp"

#include <algorithm>
#include <cstring>

namespace OVRInject {
namespace XR {

XRInstance::XRInstance() {
    memset(&system_properties_, 0, sizeof(system_properties_));
    system_properties_.type = XR_TYPE_SYSTEM_PROPERTIES;
}

XRInstance::~XRInstance() {
    Shutdown();
}

bool XRInstance::Initialize(const char* appName) {
    LOGSTRF("Initializing OpenXR instance...\n");

    if (instance_ != XR_NULL_HANDLE) {
        LOGSTRF("OpenXR instance already initialized\n");
        return true;
    }

    // Step 1: Create instance
    if (!CreateInstance(appName)) {
        LOGSTRF("Failed to create OpenXR instance\n");
        return false;
    }

    // Step 2: Load extension functions
    if (!LoadExtensionFunctions()) {
        LOGSTRF("Failed to load OpenXR extension functions\n");
        Shutdown();
        return false;
    }

    // Step 3: Get system (HMD)
    if (!GetSystem()) {
        LOGSTRF("Failed to get OpenXR system\n");
        Shutdown();
        return false;
    }

    // Step 4: Query system properties
    if (!QuerySystemProperties()) {
        LOGSTRF("Failed to query system properties\n");
        Shutdown();
        return false;
    }

    LOGSTRF("OpenXR instance initialized successfully\n");
    LOGSTRF("  System: %s\n", GetSystemName());
    LOGSTRF("  Position Tracking: %s\n",
            SupportsPositionTracking() ? "Yes" : "No");
    LOGSTRF("  Orientation Tracking: %s\n",
            SupportsOrientationTracking() ? "Yes" : "No");

    return true;
}

void XRInstance::Shutdown() {
    if (instance_ != XR_NULL_HANDLE) {
        LOGSTRF("Destroying OpenXR instance...\n");
        xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
    }

    system_id_ = XR_NULL_SYSTEM_ID;
    enabled_extensions_.clear();
    graphics_requirements_queried_ = false;
}

bool XRInstance::CreateInstance(const char* appName) {
    // Enumerate available extensions
    auto availableExtensions = EnumerateExtensions();

    LOGSTRF("Available OpenXR extensions:\n");
    for (const auto& ext : availableExtensions) {
        LOGSTRF("  %s (v%u)\n", ext.extensionName, ext.extensionVersion);
    }

    // Required extensions
    std::vector<const char*> requiredExtensions = {
        XR_KHR_D3D11_ENABLE_EXTENSION_NAME
    };

    // Optional extensions we'd like to enable
    std::vector<const char*> optionalExtensions = {
        // Add optional extensions here as needed
        // XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME,
        // XR_EXT_HAND_TRACKING_EXTENSION_NAME,
    };

    // Build list of extensions to enable
    std::vector<const char*> extensionsToEnable;

    // Add required extensions (fail if not available)
    for (const char* ext : requiredExtensions) {
        if (!IsExtensionSupported(ext, availableExtensions)) {
            LOGSTRF("Required extension not available: %s\n", ext);
            return false;
        }
        extensionsToEnable.push_back(ext);
        enabled_extensions_.push_back(ext);
    }

    // Add optional extensions if available
    for (const char* ext : optionalExtensions) {
        if (IsExtensionSupported(ext, availableExtensions)) {
            extensionsToEnable.push_back(ext);
            enabled_extensions_.push_back(ext);
            LOGSTRF("Enabling optional extension: %s\n", ext);
        }
    }

    // Create instance
    XrInstanceCreateInfo createInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
    strncpy(createInfo.applicationInfo.applicationName, appName,
            XR_MAX_APPLICATION_NAME_SIZE - 1);
    createInfo.applicationInfo.applicationVersion = 1;
    strncpy(createInfo.applicationInfo.engineName, "GTA VR Engine",
            XR_MAX_ENGINE_NAME_SIZE - 1);
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensionsToEnable.size());
    createInfo.enabledExtensionNames = extensionsToEnable.data();

    XrResult result = xrCreateInstance(&createInfo, &instance_);
    if (XR_FAILED(result)) {
        LOGSTRF("xrCreateInstance failed: %s\n", GetResultString(result));
        return false;
    }

    // Log instance info
    XrInstanceProperties instanceProps = {XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance_, &instanceProps);
    LOGSTRF("OpenXR Runtime: %s (v%u.%u.%u)\n",
            instanceProps.runtimeName,
            XR_VERSION_MAJOR(instanceProps.runtimeVersion),
            XR_VERSION_MINOR(instanceProps.runtimeVersion),
            XR_VERSION_PATCH(instanceProps.runtimeVersion));

    return true;
}

bool XRInstance::GetSystem() {
    XrSystemGetInfo systemInfo = {XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = xrGetSystem(instance_, &systemInfo, &system_id_);
    if (XR_FAILED(result)) {
        LOGSTRF("xrGetSystem failed: %s\n", GetResultString(result));
        if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            LOGSTRF("No HMD connected or VR runtime not running\n");
        }
        return false;
    }

    return true;
}

bool XRInstance::QuerySystemProperties() {
    XrResult result = xrGetSystemProperties(instance_, system_id_, &system_properties_);
    if (XR_FAILED(result)) {
        LOGSTRF("xrGetSystemProperties failed: %s\n", GetResultString(result));
        return false;
    }
    return true;
}

bool XRInstance::LoadExtensionFunctions() {
    // Load D3D11 graphics requirements function
    XrResult result = xrGetInstanceProcAddr(
        instance_,
        "xrGetD3D11GraphicsRequirementsKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&xrGetD3D11GraphicsRequirementsKHR_));

    if (XR_FAILED(result) || xrGetD3D11GraphicsRequirementsKHR_ == nullptr) {
        LOGSTRF("Failed to load xrGetD3D11GraphicsRequirementsKHR\n");
        return false;
    }

    return true;
}

const char* XRInstance::GetSystemName() const {
    return system_properties_.systemName;
}

uint32_t XRInstance::GetVendorId() const {
    return system_properties_.vendorId;
}

uint32_t XRInstance::GetMaxSwapchainWidth() const {
    return system_properties_.graphicsProperties.maxSwapchainImageWidth;
}

uint32_t XRInstance::GetMaxSwapchainHeight() const {
    return system_properties_.graphicsProperties.maxSwapchainImageHeight;
}

uint32_t XRInstance::GetMaxLayerCount() const {
    return system_properties_.graphicsProperties.maxLayerCount;
}

bool XRInstance::SupportsOrientationTracking() const {
    return system_properties_.trackingProperties.orientationTracking == XR_TRUE;
}

bool XRInstance::SupportsPositionTracking() const {
    return system_properties_.trackingProperties.positionTracking == XR_TRUE;
}

bool XRInstance::GetD3D11GraphicsRequirements(D3D_FEATURE_LEVEL& minFeatureLevel,
                                               LUID& adapterLuid) {
    if (xrGetD3D11GraphicsRequirementsKHR_ == nullptr) {
        LOGSTRF("xrGetD3D11GraphicsRequirementsKHR not loaded\n");
        return false;
    }

    if (!graphics_requirements_queried_) {
        XrResult result = xrGetD3D11GraphicsRequirementsKHR_(
            instance_, system_id_, &graphics_requirements_);

        if (XR_FAILED(result)) {
            LOGSTRF("xrGetD3D11GraphicsRequirementsKHR failed: %s\n",
                    GetResultString(result));
            return false;
        }

        graphics_requirements_queried_ = true;

        LOGSTRF("D3D11 Graphics Requirements:\n");
        LOGSTRF("  Adapter LUID: %08lx-%08lx\n",
                graphics_requirements_.adapterLuid.HighPart,
                graphics_requirements_.adapterLuid.LowPart);
        LOGSTRF("  Min Feature Level: 0x%x\n",
                graphics_requirements_.minFeatureLevel);
    }

    minFeatureLevel = graphics_requirements_.minFeatureLevel;
    adapterLuid = graphics_requirements_.adapterLuid;

    return true;
}

bool XRInstance::IsExtensionEnabled(const char* extensionName) const {
    return std::find(enabled_extensions_.begin(),
                     enabled_extensions_.end(),
                     extensionName) != enabled_extensions_.end();
}

} // namespace XR
} // namespace OVRInject
