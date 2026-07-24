#pragma once

// OpenXR platform configuration - must be defined before including openxr headers
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_PLATFORM_WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <unknwn.h>
#include <d3d11.h>
#include <dxgi.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <DirectXMath.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <stdexcept>
#include <functional>

#include "../Log.hpp"

namespace OVRInject {
namespace XR {

using namespace DirectX;
using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
// Error Handling
//-----------------------------------------------------------------------------

// Result wrapper for better error context
struct XRResult {
    XrResult result;
    const char* function;
    const char* file;
    int line;

    bool Success() const { return XR_SUCCEEDED(result); }
    bool Failed() const { return XR_FAILED(result); }
};

// Get human-readable error string
const char* GetResultString(XrResult result);

// Check result and log errors
bool CheckXrResult(XrInstance instance, const XRResult& xrResult);

// Macro for convenient error checking
#define XR_CHECK(instance, call) \
    XR::CheckXrResult(instance, {call, #call, __FILE__, __LINE__})

#define XR_CHECK_THROW(instance, call) \
    do { \
        XrResult _result = (call); \
        if (XR_FAILED(_result)) { \
            LOGSTRF("OpenXR Error: %s failed with %d at %s:%d\n", #call, _result, __FILE__, __LINE__); \
            throw std::runtime_error("OpenXR call failed: " #call); \
        } \
    } while(0)

//-----------------------------------------------------------------------------
// Enums
//-----------------------------------------------------------------------------

// Eye indices for stereo rendering
enum class Eye : uint32_t {
    Left = 0,
    Right = 1,
    Count = 2
};

// Session state (mirrors XrSessionState but with cleaner naming)
enum class SessionState {
    Unknown = 0,
    Idle,
    Ready,
    Synchronized,
    Visible,
    Focused,
    Stopping,
    LossPending,
    Exiting
};

SessionState FromXrSessionState(XrSessionState state);
const char* SessionStateToString(SessionState state);

//-----------------------------------------------------------------------------
// Type Conversions - OpenXR to DirectX
//-----------------------------------------------------------------------------

// Convert XrPosef to DirectX matrix
inline XMMATRIX XrPoseToMatrix(const XrPosef& pose) {
    XMVECTOR orientation = XMVectorSet(
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z,
        pose.orientation.w
    );

    XMVECTOR position = XMVectorSet(
        pose.position.x,
        pose.position.y,
        pose.position.z,
        1.0f
    );

    XMMATRIX rotationMatrix = XMMatrixRotationQuaternion(orientation);
    XMMATRIX translationMatrix = XMMatrixTranslationFromVector(position);

    return rotationMatrix * translationMatrix;
}

// Convert XrPosef to view matrix (inverted for camera)
inline XMMATRIX XrPoseToViewMatrix(const XrPosef& pose) {
    XMMATRIX poseMatrix = XrPoseToMatrix(pose);
    return XMMatrixInverse(nullptr, poseMatrix);
}

// Convert XrVector3f to XMFLOAT3
inline XMFLOAT3 XrVector3ToFloat3(const XrVector3f& v) {
    return XMFLOAT3(v.x, v.y, v.z);
}

// Convert XrQuaternionf to XMFLOAT4
inline XMFLOAT4 XrQuatToFloat4(const XrQuaternionf& q) {
    return XMFLOAT4(q.x, q.y, q.z, q.w);
}

// Convert XrFovf to a D3D-style projection matrix (z_ndc in [0,1]).
// OpenXR view space is right-handed: forward is -Z, +Y is up.
inline XMMATRIX XrFovToProjectionMatrixD3D(const XrFovf& fov, float nearZ, float farZ) {
    return XMMatrixPerspectiveOffCenterRH(
        tanf(fov.angleLeft) * nearZ,   // ViewLeft
        tanf(fov.angleRight) * nearZ,  // ViewRight
        tanf(fov.angleDown) * nearZ,   // ViewBottom
        tanf(fov.angleUp) * nearZ,     // ViewTop
        nearZ, farZ);
}

//-----------------------------------------------------------------------------
// Type Conversions - DirectX to OpenXR
//-----------------------------------------------------------------------------

inline XrVector3f Float3ToXrVector3(const XMFLOAT3& v) {
    return {v.x, v.y, v.z};
}

inline XrQuaternionf Float4ToXrQuat(const XMFLOAT4& q) {
    return {q.x, q.y, q.z, q.w};
}

//-----------------------------------------------------------------------------
// Pose Utilities
//-----------------------------------------------------------------------------

// Extract position from pose
inline XMFLOAT3 GetPosePosition(const XrPosef& pose) {
    return XMFLOAT3(pose.position.x, pose.position.y, pose.position.z);
}

// Extract forward vector from pose (negative Z in OpenXR)
inline XMFLOAT3 GetPoseForward(const XrPosef& pose) {
    XMVECTOR quat = XMVectorSet(
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z,
        pose.orientation.w
    );

    // Forward is negative Z axis rotated by quaternion
    XMVECTOR forward = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
    forward = XMVector3Rotate(forward, quat);

    XMFLOAT3 result;
    XMStoreFloat3(&result, forward);
    return result;
}

// Extract up vector from pose
inline XMFLOAT3 GetPoseUp(const XrPosef& pose) {
    XMVECTOR quat = XMVectorSet(
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z,
        pose.orientation.w
    );

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    up = XMVector3Rotate(up, quat);

    XMFLOAT3 result;
    XMStoreFloat3(&result, up);
    return result;
}

// Convert quaternion to euler angles (pitch, yaw, roll in degrees)
inline XMFLOAT3 QuatToEuler(const XrQuaternionf& q) {
    XMFLOAT3 euler;

    // Roll (x-axis rotation)
    float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
    float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    euler.z = atan2f(sinr_cosp, cosr_cosp);

    // Pitch (y-axis rotation)
    float sinp = 2.0f * (q.w * q.y - q.z * q.x);
    if (fabsf(sinp) >= 1.0f)
        euler.x = copysignf(XM_PI / 2.0f, sinp);
    else
        euler.x = asinf(sinp);

    // Yaw (z-axis rotation)
    float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
    float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    euler.y = atan2f(siny_cosp, cosy_cosp);

    // Convert to degrees
    euler.x = XMConvertToDegrees(euler.x);
    euler.y = XMConvertToDegrees(euler.y);
    euler.z = XMConvertToDegrees(euler.z);

    return euler;
}

//-----------------------------------------------------------------------------
// Identity Pose
//-----------------------------------------------------------------------------

inline XrPosef IdentityPose() {
    XrPosef pose{};
    pose.orientation.w = 1.0f;
    return pose;
}

//-----------------------------------------------------------------------------
// Extension Helpers
//-----------------------------------------------------------------------------

// Check if an extension is supported
bool IsExtensionSupported(const char* extensionName,
                          const std::vector<XrExtensionProperties>& extensions);

// Enumerate all available extensions
std::vector<XrExtensionProperties> EnumerateExtensions();

} // namespace XR
} // namespace OVRInject
