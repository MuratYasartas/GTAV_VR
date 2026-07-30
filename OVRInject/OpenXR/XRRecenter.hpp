#pragma once

#include "XRCore.hpp"

namespace OVRInject {
namespace XR {

inline XrQuaternionf ComposeQuaternions(
    const XrQuaternionf& a, const XrQuaternionf& b) {
    const XMVECTOR qa = XMVectorSet(a.x, a.y, a.z, a.w);
    const XMVECTOR qb = XMVectorSet(b.x, b.y, b.z, b.w);
    // XMQuaternionMultiply(Q1,Q2) concatenates Q1 followed by Q2
    // (algebraically Q2*Q1). Reverse its arguments for algebraic a*b.
    const XMVECTOR result = XMQuaternionMultiply(qb, qa);
    return {
        XMVectorGetX(result), XMVectorGetY(result),
        XMVectorGetZ(result), XMVectorGetW(result)};
}

inline XrPosef ComposePoses(const XrPosef& a, const XrPosef& b) {
    XrPosef out = IdentityPose();
    out.orientation = ComposeQuaternions(a.orientation, b.orientation);

    const XMVECTOR qa = XMVectorSet(
        a.orientation.x, a.orientation.y, a.orientation.z, a.orientation.w);
    const XMVECTOR bp = XMVectorSet(
        b.position.x, b.position.y, b.position.z, 0.0f);
    const XMVECTOR rotated = XMVector3Rotate(bp, qa);
    out.position.x = a.position.x + XMVectorGetX(rotated);
    out.position.y = a.position.y + XMVectorGetY(rotated);
    out.position.z = a.position.z + XMVectorGetZ(rotated);
    return out;
}

inline XrPosef InvertPose(const XrPosef& pose) {
    XrPosef out = IdentityPose();
    const XMVECTOR q = XMVectorSet(
        pose.orientation.x, pose.orientation.y,
        pose.orientation.z, pose.orientation.w);
    const XMVECTOR inverse = XMQuaternionInverse(q);
    out.orientation = {
        XMVectorGetX(inverse), XMVectorGetY(inverse),
        XMVectorGetZ(inverse), XMVectorGetW(inverse)};

    const XMVECTOR negativePosition = XMVectorSet(
        -pose.position.x, -pose.position.y, -pose.position.z, 0.0f);
    const XMVECTOR rotated = XMVector3Rotate(negativePosition, inverse);
    out.position = {
        XMVectorGetX(rotated), XMVectorGetY(rotated), XMVectorGetZ(rotated)};
    return out;
}

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
