#pragma once

#include "XRCore.hpp"

namespace OVRInject {
namespace XR {

// Build a yaw-only recenter transform that keeps the current head position
// fixed. Applying a pure yaw about tracking origin makes an off-origin head
// travel on an arc; the compensating translation makes the head itself the
// pivot.
inline XrPosef ComputeYawOnlyRecenterPose(const XrPosef& pose) {
    const XMFLOAT3 forward = GetPoseForward(pose);
    const float yaw = atan2f(forward.x, -forward.z);
    const XMVECTOR axis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    // GetPoseForward reports +Y rotation as a negative atan2 yaw in this
    // right-handed -Z-forward frame, so `yaw` itself is the inverse. Using
    // -yaw doubles the heading (70 degrees became 140 on recenter).
    const XMVECTOR q = XMQuaternionRotationAxis(axis, yaw);

    XrPosef out = IdentityPose();
    out.orientation.x = XMVectorGetX(q);
    out.orientation.y = XMVectorGetY(q);
    out.orientation.z = XMVectorGetZ(q);
    out.orientation.w = XMVectorGetW(q);

    const XMVECTOR position = XMVectorSet(
        pose.position.x, pose.position.y, pose.position.z, 0.0f);
    const XMVECTOR rotated = XMVector3Rotate(position, q);
    out.position.x = pose.position.x - XMVectorGetX(rotated);
    out.position.y = pose.position.y - XMVectorGetY(rotated);
    out.position.z = pose.position.z - XMVectorGetZ(rotated);
    return out;
}

} // namespace XR
} // namespace OVRInject
