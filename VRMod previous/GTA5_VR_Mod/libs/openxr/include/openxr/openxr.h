/*
 * OpenXR Header Placeholder
 *
 * This is a minimal placeholder for compilation.
 * Replace with the official OpenXR SDK headers from:
 * https://github.com/KhronosGroup/OpenXR-SDK
 *
 * Download the official SDK:
 * https://github.com/KhronosGroup/OpenXR-SDK/releases
 */

#pragma once

#ifndef OPENXR_H_
#define OPENXR_H_ 1

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Basic types
typedef uint64_t XrVersion;
typedef uint64_t XrFlags64;
typedef int64_t XrTime;
typedef int64_t XrDuration;

// Handles
#define XR_DEFINE_HANDLE(name) typedef struct name##_T* name
#define XR_DEFINE_ATOM(name) typedef uint64_t name

XR_DEFINE_HANDLE(XrInstance);
XR_DEFINE_HANDLE(XrSession);
XR_DEFINE_HANDLE(XrSpace);
XR_DEFINE_HANDLE(XrSwapchain);
XR_DEFINE_HANDLE(XrAction);
XR_DEFINE_HANDLE(XrActionSet);

typedef uint64_t XrSystemId;
typedef uint64_t XrPath;

// Null handles
#define XR_NULL_HANDLE nullptr
#define XR_NULL_SYSTEM_ID 0
#define XR_NULL_PATH 0

// Result codes
typedef enum XrResult {
    XR_SUCCESS = 0,
    XR_TIMEOUT_EXPIRED = 1,
    XR_SESSION_LOSS_PENDING = 3,
    XR_EVENT_UNAVAILABLE = 4,
    XR_ERROR_VALIDATION_FAILURE = -1,
    XR_ERROR_RUNTIME_FAILURE = -2,
    XR_ERROR_OUT_OF_MEMORY = -3,
    XR_ERROR_API_VERSION_UNSUPPORTED = -4,
    XR_ERROR_INITIALIZATION_FAILED = -6,
    XR_ERROR_FUNCTION_UNSUPPORTED = -7,
    XR_ERROR_FEATURE_UNSUPPORTED = -8,
    XR_ERROR_HANDLE_INVALID = -12,
    XR_ERROR_INSTANCE_LOST = -13,
    XR_ERROR_SESSION_RUNNING = -14,
    XR_ERROR_SESSION_NOT_RUNNING = -16,
    XR_RESULT_MAX_ENUM = 0x7FFFFFFF
} XrResult;

#define XR_SUCCEEDED(result) ((result) >= 0)
#define XR_FAILED(result) ((result) < 0)

// Structure types
typedef enum XrStructureType {
    XR_TYPE_UNKNOWN = 0,
    XR_TYPE_API_LAYER_PROPERTIES = 1,
    XR_TYPE_EXTENSION_PROPERTIES = 2,
    XR_TYPE_INSTANCE_CREATE_INFO = 3,
    XR_TYPE_SYSTEM_GET_INFO = 4,
    XR_TYPE_SYSTEM_PROPERTIES = 5,
    XR_TYPE_VIEW_LOCATE_INFO = 6,
    XR_TYPE_VIEW = 7,
    XR_TYPE_SESSION_CREATE_INFO = 8,
    XR_TYPE_SWAPCHAIN_CREATE_INFO = 9,
    XR_TYPE_SESSION_BEGIN_INFO = 10,
    XR_TYPE_VIEW_STATE = 11,
    XR_TYPE_FRAME_END_INFO = 12,
    XR_TYPE_FRAME_WAIT_INFO = 14,
    XR_TYPE_FRAME_STATE = 16,
    XR_TYPE_FRAME_BEGIN_INFO = 17,
    XR_TYPE_COMPOSITION_LAYER_PROJECTION = 35,
    XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW = 48,
    XR_TYPE_EVENT_DATA_BUFFER = 60,
    XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED = 65,
    XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING = 66,
    XR_TYPE_ACTION_STATE_BOOLEAN = 23,
    XR_TYPE_ACTION_STATE_FLOAT = 24,
    XR_TYPE_ACTION_STATE_VECTOR2F = 25,
    XR_TYPE_ACTION_STATE_POSE = 27,
    XR_TYPE_ACTION_SET_CREATE_INFO = 28,
    XR_TYPE_ACTION_CREATE_INFO = 29,
    XR_TYPE_ACTIONS_SYNC_INFO = 31,
    XR_TYPE_ACTION_STATE_GET_INFO = 32,
    XR_TYPE_HAPTIC_VIBRATION = 34,
    XR_TYPE_HAPTIC_ACTION_INFO = 33,
    XR_TYPE_ACTIVE_ACTION_SET = 30,
    XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING = 51,
    XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO = 52,
    XR_TYPE_REFERENCE_SPACE_CREATE_INFO = 37,
    XR_TYPE_ACTION_SPACE_CREATE_INFO = 21,
    XR_TYPE_SPACE_LOCATION = 42,
    XR_TYPE_VIEW_CONFIGURATION_VIEW = 41,
    XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO = 55,
    XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO = 56,
    XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO = 57,
    XR_TYPE_INSTANCE_PROPERTIES = 11,
    XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR = 1000027000,
    XR_TYPE_GRAPHICS_BINDING_D3D11_KHR = 1000027001,
    XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR = 1000027002,
    XR_STRUCTURE_TYPE_MAX_ENUM = 0x7FFFFFFF
} XrStructureType;

// Form factors
typedef enum XrFormFactor {
    XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY = 1,
    XR_FORM_FACTOR_HANDHELD_DISPLAY = 2,
    XR_FORM_FACTOR_MAX_ENUM = 0x7FFFFFFF
} XrFormFactor;

// View configuration types
typedef enum XrViewConfigurationType {
    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO = 1,
    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO = 2,
    XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM = 0x7FFFFFFF
} XrViewConfigurationType;

// Environment blend modes
typedef enum XrEnvironmentBlendMode {
    XR_ENVIRONMENT_BLEND_MODE_OPAQUE = 1,
    XR_ENVIRONMENT_BLEND_MODE_ADDITIVE = 2,
    XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND = 3,
    XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM = 0x7FFFFFFF
} XrEnvironmentBlendMode;

// Reference space types
typedef enum XrReferenceSpaceType {
    XR_REFERENCE_SPACE_TYPE_VIEW = 1,
    XR_REFERENCE_SPACE_TYPE_LOCAL = 2,
    XR_REFERENCE_SPACE_TYPE_STAGE = 3,
    XR_REFERENCE_SPACE_TYPE_MAX_ENUM = 0x7FFFFFFF
} XrReferenceSpaceType;

// Action types
typedef enum XrActionType {
    XR_ACTION_TYPE_BOOLEAN_INPUT = 1,
    XR_ACTION_TYPE_FLOAT_INPUT = 2,
    XR_ACTION_TYPE_VECTOR2F_INPUT = 3,
    XR_ACTION_TYPE_POSE_INPUT = 4,
    XR_ACTION_TYPE_VIBRATION_OUTPUT = 100,
    XR_ACTION_TYPE_MAX_ENUM = 0x7FFFFFFF
} XrActionType;

// Session states
typedef enum XrSessionState {
    XR_SESSION_STATE_UNKNOWN = 0,
    XR_SESSION_STATE_IDLE = 1,
    XR_SESSION_STATE_READY = 2,
    XR_SESSION_STATE_SYNCHRONIZED = 3,
    XR_SESSION_STATE_VISIBLE = 4,
    XR_SESSION_STATE_FOCUSED = 5,
    XR_SESSION_STATE_STOPPING = 6,
    XR_SESSION_STATE_LOSS_PENDING = 7,
    XR_SESSION_STATE_EXITING = 8,
    XR_SESSION_STATE_MAX_ENUM = 0x7FFFFFFF
} XrSessionState;

// Space location flags
typedef XrFlags64 XrSpaceLocationFlags;
#define XR_SPACE_LOCATION_ORIENTATION_VALID_BIT 0x00000001
#define XR_SPACE_LOCATION_POSITION_VALID_BIT 0x00000002
#define XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT 0x00000004
#define XR_SPACE_LOCATION_POSITION_TRACKED_BIT 0x00000008

// Swapchain usage flags
typedef XrFlags64 XrSwapchainUsageFlags;
#define XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT 0x00000001
#define XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 0x00000002
#define XR_SWAPCHAIN_USAGE_SAMPLED_BIT 0x00000020
#define XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT 0x00000080
#define XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT 0x00000100

// Constants
#define XR_CURRENT_API_VERSION XR_MAKE_VERSION(1, 0, 27)
#define XR_MAKE_VERSION(major, minor, patch) \
    ((((uint64_t)(major)) << 48) | (((uint64_t)(minor)) << 32) | ((uint64_t)(patch)))
#define XR_VERSION_MAJOR(version) ((uint32_t)((version) >> 48))
#define XR_VERSION_MINOR(version) ((uint32_t)(((version) >> 32) & 0xFFFF))
#define XR_VERSION_PATCH(version) ((uint32_t)((version) & 0xFFFFFFFF))

#define XR_MAX_APPLICATION_NAME_SIZE 128
#define XR_MAX_ENGINE_NAME_SIZE 128
#define XR_MAX_EXTENSION_NAME_SIZE 128
#define XR_MAX_RESULT_STRING_SIZE 64
#define XR_MAX_SYSTEM_NAME_SIZE 256
#define XR_MAX_ACTION_NAME_SIZE 64
#define XR_MAX_ACTION_SET_NAME_SIZE 64
#define XR_MAX_LOCALIZED_ACTION_NAME_SIZE 128
#define XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE 128

#define XR_INFINITE_DURATION 0x7FFFFFFFFFFFFFFFLL
#define XR_FREQUENCY_UNSPECIFIED 0

// Math types
typedef struct XrVector2f {
    float x;
    float y;
} XrVector2f;

typedef struct XrVector3f {
    float x;
    float y;
    float z;
} XrVector3f;

typedef struct XrQuaternionf {
    float x;
    float y;
    float z;
    float w;
} XrQuaternionf;

typedef struct XrPosef {
    XrQuaternionf orientation;
    XrVector3f position;
} XrPosef;

typedef struct XrFovf {
    float angleLeft;
    float angleRight;
    float angleUp;
    float angleDown;
} XrFovf;

typedef struct XrOffset2Di {
    int32_t x;
    int32_t y;
} XrOffset2Di;

typedef struct XrExtent2Di {
    int32_t width;
    int32_t height;
} XrExtent2Di;

typedef struct XrRect2Di {
    XrOffset2Di offset;
    XrExtent2Di extent;
} XrRect2Di;

// Core structures
typedef struct XrApplicationInfo {
    char applicationName[XR_MAX_APPLICATION_NAME_SIZE];
    uint32_t applicationVersion;
    char engineName[XR_MAX_ENGINE_NAME_SIZE];
    uint32_t engineVersion;
    XrVersion apiVersion;
} XrApplicationInfo;

typedef struct XrInstanceCreateInfo {
    XrStructureType type;
    const void* next;
    uint64_t createFlags;
    XrApplicationInfo applicationInfo;
    uint32_t enabledApiLayerCount;
    const char* const* enabledApiLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* enabledExtensionNames;
} XrInstanceCreateInfo;

typedef struct XrInstanceProperties {
    XrStructureType type;
    void* next;
    XrVersion runtimeVersion;
    char runtimeName[XR_MAX_SYSTEM_NAME_SIZE];
} XrInstanceProperties;

typedef struct XrExtensionProperties {
    XrStructureType type;
    void* next;
    char extensionName[XR_MAX_EXTENSION_NAME_SIZE];
    uint32_t extensionVersion;
} XrExtensionProperties;

typedef struct XrSystemGetInfo {
    XrStructureType type;
    const void* next;
    XrFormFactor formFactor;
} XrSystemGetInfo;

typedef struct XrSystemProperties {
    XrStructureType type;
    void* next;
    XrSystemId systemId;
    uint32_t vendorId;
    char systemName[XR_MAX_SYSTEM_NAME_SIZE];
    // Additional fields omitted for brevity
} XrSystemProperties;

typedef struct XrSessionCreateInfo {
    XrStructureType type;
    const void* next;
    uint64_t createFlags;
    XrSystemId systemId;
} XrSessionCreateInfo;

typedef struct XrSessionBeginInfo {
    XrStructureType type;
    const void* next;
    XrViewConfigurationType primaryViewConfigurationType;
} XrSessionBeginInfo;

typedef struct XrSwapchainCreateInfo {
    XrStructureType type;
    const void* next;
    uint64_t createFlags;
    XrSwapchainUsageFlags usageFlags;
    int64_t format;
    uint32_t sampleCount;
    uint32_t width;
    uint32_t height;
    uint32_t faceCount;
    uint32_t arraySize;
    uint32_t mipCount;
} XrSwapchainCreateInfo;

typedef struct XrSwapchainImageBaseHeader {
    XrStructureType type;
    void* next;
} XrSwapchainImageBaseHeader;

typedef struct XrSwapchainImageAcquireInfo {
    XrStructureType type;
    const void* next;
} XrSwapchainImageAcquireInfo;

typedef struct XrSwapchainImageWaitInfo {
    XrStructureType type;
    const void* next;
    XrDuration timeout;
} XrSwapchainImageWaitInfo;

typedef struct XrSwapchainImageReleaseInfo {
    XrStructureType type;
    const void* next;
} XrSwapchainImageReleaseInfo;

typedef struct XrReferenceSpaceCreateInfo {
    XrStructureType type;
    const void* next;
    XrReferenceSpaceType referenceSpaceType;
    XrPosef poseInReferenceSpace;
} XrReferenceSpaceCreateInfo;

typedef struct XrView {
    XrStructureType type;
    void* next;
    XrPosef pose;
    XrFovf fov;
} XrView;

typedef struct XrViewLocateInfo {
    XrStructureType type;
    const void* next;
    XrViewConfigurationType viewConfigurationType;
    XrTime displayTime;
    XrSpace space;
} XrViewLocateInfo;

typedef struct XrViewState {
    XrStructureType type;
    void* next;
    XrFlags64 viewStateFlags;
} XrViewState;

typedef struct XrViewConfigurationView {
    XrStructureType type;
    void* next;
    uint32_t recommendedImageRectWidth;
    uint32_t recommendedImageRectHeight;
    uint32_t maxImageRectWidth;
    uint32_t maxImageRectHeight;
    uint32_t recommendedSwapchainSampleCount;
    uint32_t maxSwapchainSampleCount;
} XrViewConfigurationView;

typedef struct XrFrameWaitInfo {
    XrStructureType type;
    const void* next;
} XrFrameWaitInfo;

typedef struct XrFrameState {
    XrStructureType type;
    void* next;
    XrTime predictedDisplayTime;
    XrDuration predictedDisplayPeriod;
    uint32_t shouldRender;
} XrFrameState;

typedef struct XrFrameBeginInfo {
    XrStructureType type;
    const void* next;
} XrFrameBeginInfo;

typedef struct XrFrameEndInfo {
    XrStructureType type;
    const void* next;
    XrTime displayTime;
    XrEnvironmentBlendMode environmentBlendMode;
    uint32_t layerCount;
    const struct XrCompositionLayerBaseHeader* const* layers;
} XrFrameEndInfo;

typedef struct XrCompositionLayerBaseHeader {
    XrStructureType type;
    const void* next;
    XrFlags64 layerFlags;
    XrSpace space;
} XrCompositionLayerBaseHeader;

typedef struct XrSwapchainSubImage {
    XrSwapchain swapchain;
    XrRect2Di imageRect;
    uint32_t imageArrayIndex;
} XrSwapchainSubImage;

typedef struct XrCompositionLayerProjectionView {
    XrStructureType type;
    const void* next;
    XrPosef pose;
    XrFovf fov;
    XrSwapchainSubImage subImage;
} XrCompositionLayerProjectionView;

typedef struct XrCompositionLayerProjection {
    XrStructureType type;
    const void* next;
    XrFlags64 layerFlags;
    XrSpace space;
    uint32_t viewCount;
    const XrCompositionLayerProjectionView* views;
} XrCompositionLayerProjection;

typedef struct XrEventDataBuffer {
    XrStructureType type;
    const void* next;
    uint8_t varying[4000];
} XrEventDataBuffer;

typedef struct XrEventDataSessionStateChanged {
    XrStructureType type;
    const void* next;
    XrSession session;
    XrSessionState state;
    XrTime time;
} XrEventDataSessionStateChanged;

typedef struct XrSpaceLocation {
    XrStructureType type;
    void* next;
    XrSpaceLocationFlags locationFlags;
    XrPosef pose;
} XrSpaceLocation;

// Action structures
typedef struct XrActionSetCreateInfo {
    XrStructureType type;
    const void* next;
    char actionSetName[XR_MAX_ACTION_SET_NAME_SIZE];
    char localizedActionSetName[XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE];
    uint32_t priority;
} XrActionSetCreateInfo;

typedef struct XrActionCreateInfo {
    XrStructureType type;
    const void* next;
    char actionName[XR_MAX_ACTION_NAME_SIZE];
    XrActionType actionType;
    uint32_t countSubactionPaths;
    const XrPath* subactionPaths;
    char localizedActionName[XR_MAX_LOCALIZED_ACTION_NAME_SIZE];
} XrActionCreateInfo;

typedef struct XrActionSuggestedBinding {
    XrAction action;
    XrPath binding;
} XrActionSuggestedBinding;

typedef struct XrInteractionProfileSuggestedBinding {
    XrStructureType type;
    const void* next;
    XrPath interactionProfile;
    uint32_t countSuggestedBindings;
    const XrActionSuggestedBinding* suggestedBindings;
} XrInteractionProfileSuggestedBinding;

typedef struct XrSessionActionSetsAttachInfo {
    XrStructureType type;
    const void* next;
    uint32_t countActionSets;
    const XrActionSet* actionSets;
} XrSessionActionSetsAttachInfo;

typedef struct XrActiveActionSet {
    XrActionSet actionSet;
    XrPath subactionPath;
} XrActiveActionSet;

typedef struct XrActionsSyncInfo {
    XrStructureType type;
    const void* next;
    uint32_t countActiveActionSets;
    const XrActiveActionSet* activeActionSets;
} XrActionsSyncInfo;

typedef struct XrActionStateGetInfo {
    XrStructureType type;
    const void* next;
    XrAction action;
    XrPath subactionPath;
} XrActionStateGetInfo;

typedef struct XrActionStateBoolean {
    XrStructureType type;
    void* next;
    uint32_t currentState;
    uint32_t changedSinceLastSync;
    XrTime lastChangeTime;
    uint32_t isActive;
} XrActionStateBoolean;

typedef struct XrActionStateFloat {
    XrStructureType type;
    void* next;
    float currentState;
    uint32_t changedSinceLastSync;
    XrTime lastChangeTime;
    uint32_t isActive;
} XrActionStateFloat;

typedef struct XrActionStateVector2f {
    XrStructureType type;
    void* next;
    XrVector2f currentState;
    uint32_t changedSinceLastSync;
    XrTime lastChangeTime;
    uint32_t isActive;
} XrActionStateVector2f;

typedef struct XrActionSpaceCreateInfo {
    XrStructureType type;
    const void* next;
    XrAction action;
    XrPath subactionPath;
    XrPosef poseInActionSpace;
} XrActionSpaceCreateInfo;

typedef struct XrHapticVibration {
    XrStructureType type;
    const void* next;
    XrDuration duration;
    float frequency;
    float amplitude;
} XrHapticVibration;

typedef struct XrHapticBaseHeader {
    XrStructureType type;
    const void* next;
} XrHapticBaseHeader;

typedef struct XrHapticActionInfo {
    XrStructureType type;
    const void* next;
    XrAction action;
    XrPath subactionPath;
} XrHapticActionInfo;

// Function pointer types
typedef XrResult (XRAPI_PTR *PFN_xrVoidFunction)(void);

// Core functions (declarations only - implement stubs or link to actual loader)
XrResult xrEnumerateInstanceExtensionProperties(
    const char* layerName,
    uint32_t propertyCapacityInput,
    uint32_t* propertyCountOutput,
    XrExtensionProperties* properties);

XrResult xrCreateInstance(
    const XrInstanceCreateInfo* createInfo,
    XrInstance* instance);

XrResult xrDestroyInstance(XrInstance instance);

XrResult xrGetInstanceProperties(
    XrInstance instance,
    XrInstanceProperties* instanceProperties);

XrResult xrGetSystem(
    XrInstance instance,
    const XrSystemGetInfo* getInfo,
    XrSystemId* systemId);

XrResult xrGetSystemProperties(
    XrInstance instance,
    XrSystemId systemId,
    XrSystemProperties* properties);

XrResult xrCreateSession(
    XrInstance instance,
    const XrSessionCreateInfo* createInfo,
    XrSession* session);

XrResult xrDestroySession(XrSession session);

XrResult xrBeginSession(
    XrSession session,
    const XrSessionBeginInfo* beginInfo);

XrResult xrEndSession(XrSession session);

XrResult xrWaitFrame(
    XrSession session,
    const XrFrameWaitInfo* frameWaitInfo,
    XrFrameState* frameState);

XrResult xrBeginFrame(
    XrSession session,
    const XrFrameBeginInfo* frameBeginInfo);

XrResult xrEndFrame(
    XrSession session,
    const XrFrameEndInfo* frameEndInfo);

XrResult xrLocateViews(
    XrSession session,
    const XrViewLocateInfo* viewLocateInfo,
    XrViewState* viewState,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrView* views);

XrResult xrCreateSwapchain(
    XrSession session,
    const XrSwapchainCreateInfo* createInfo,
    XrSwapchain* swapchain);

XrResult xrDestroySwapchain(XrSwapchain swapchain);

XrResult xrEnumerateSwapchainImages(
    XrSwapchain swapchain,
    uint32_t imageCapacityInput,
    uint32_t* imageCountOutput,
    XrSwapchainImageBaseHeader* images);

XrResult xrAcquireSwapchainImage(
    XrSwapchain swapchain,
    const XrSwapchainImageAcquireInfo* acquireInfo,
    uint32_t* index);

XrResult xrWaitSwapchainImage(
    XrSwapchain swapchain,
    const XrSwapchainImageWaitInfo* waitInfo);

XrResult xrReleaseSwapchainImage(
    XrSwapchain swapchain,
    const XrSwapchainImageReleaseInfo* releaseInfo);

XrResult xrCreateReferenceSpace(
    XrSession session,
    const XrReferenceSpaceCreateInfo* createInfo,
    XrSpace* space);

XrResult xrDestroySpace(XrSpace space);

XrResult xrLocateSpace(
    XrSpace space,
    XrSpace baseSpace,
    XrTime time,
    XrSpaceLocation* location);

XrResult xrEnumerateViewConfigurationViews(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrViewConfigurationView* views);

XrResult xrEnumerateSwapchainFormats(
    XrSession session,
    uint32_t formatCapacityInput,
    uint32_t* formatCountOutput,
    int64_t* formats);

XrResult xrPollEvent(
    XrInstance instance,
    XrEventDataBuffer* eventData);

XrResult xrStringToPath(
    XrInstance instance,
    const char* pathString,
    XrPath* path);

XrResult xrCreateActionSet(
    XrInstance instance,
    const XrActionSetCreateInfo* createInfo,
    XrActionSet* actionSet);

XrResult xrDestroyActionSet(XrActionSet actionSet);

XrResult xrCreateAction(
    XrActionSet actionSet,
    const XrActionCreateInfo* createInfo,
    XrAction* action);

XrResult xrDestroyAction(XrAction action);

XrResult xrSuggestInteractionProfileBindings(
    XrInstance instance,
    const XrInteractionProfileSuggestedBinding* suggestedBindings);

XrResult xrAttachSessionActionSets(
    XrSession session,
    const XrSessionActionSetsAttachInfo* attachInfo);

XrResult xrSyncActions(
    XrSession session,
    const XrActionsSyncInfo* syncInfo);

XrResult xrGetActionStateBoolean(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateBoolean* state);

XrResult xrGetActionStateFloat(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateFloat* state);

XrResult xrGetActionStateVector2f(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateVector2f* state);

XrResult xrCreateActionSpace(
    XrSession session,
    const XrActionSpaceCreateInfo* createInfo,
    XrSpace* space);

XrResult xrApplyHapticFeedback(
    XrSession session,
    const XrHapticActionInfo* hapticActionInfo,
    const XrHapticBaseHeader* hapticFeedback);

XrResult xrGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function);

XrResult xrResultToString(
    XrInstance instance,
    XrResult value,
    char buffer[XR_MAX_RESULT_STRING_SIZE]);

// Extension name
#define XR_KHR_D3D11_ENABLE_EXTENSION_NAME "XR_KHR_D3D11_enable"
#define XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME "XR_KHR_composition_layer_depth"

#ifdef __cplusplus
}
#endif

#endif // OPENXR_H_
