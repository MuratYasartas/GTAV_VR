#include "TestFramework.hpp"

#include "../OVRInject/OpenXR/XRRecenter.hpp"

using namespace DirectX;
using namespace OVRInject::XR;

TEST(XRRecenter_YawOnlyKeepsHeadAsRotationPivot) {
    XrPosef head = IdentityPose();
    const XMVECTOR yaw = XMQuaternionRotationAxis(
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMConvertToRadians(70.0f));
    head.orientation = {
        XMVectorGetX(yaw), XMVectorGetY(yaw),
        XMVectorGetZ(yaw), XMVectorGetW(yaw)};
    head.position = {1.25f, 1.72f, -0.65f};

    const XrPosef recenter = ComputeYawOnlyRecenterPose(head);
    const XMVECTOR q = XMVectorSet(
        recenter.orientation.x, recenter.orientation.y,
        recenter.orientation.z, recenter.orientation.w);
    const XMVECTOR p = XMVectorSet(
        head.position.x, head.position.y, head.position.z, 0.0f);
    const XMVECTOR transformed = XMVectorAdd(
        XMVector3Rotate(p, q),
        XMVectorSet(recenter.position.x, recenter.position.y,
                    recenter.position.z, 0.0f));

    CHECK_NEAR(XMVectorGetX(transformed), head.position.x, 1e-5f);
    CHECK_NEAR(XMVectorGetY(transformed), head.position.y, 1e-5f);
    CHECK_NEAR(XMVectorGetZ(transformed), head.position.z, 1e-5f);

    XrPosef rebasedHead = head;
    const XMVECTOR headQ = XMVectorSet(
        head.orientation.x, head.orientation.y,
        head.orientation.z, head.orientation.w);
    // Production MultiplyPose(recenter, head) reverses DirectX's arguments
    // to obtain algebraic recenter*head.
    const XMVECTOR rebasedQ = XMQuaternionMultiply(headQ, q);
    rebasedHead.orientation = {
        XMVectorGetX(rebasedQ), XMVectorGetY(rebasedQ),
        XMVectorGetZ(rebasedQ), XMVectorGetW(rebasedQ)};
    const XMFLOAT3 forward = GetPoseForward(rebasedHead);
    CHECK_NEAR(forward.x, 0.0f, 1e-5f);
    CHECK_NEAR(forward.z, -1.0f, 1e-5f);
}

TEST(XRRecenter_IdentityHeadProducesIdentityTransform) {
    XrPosef head = IdentityPose();
    head.position = {0.4f, 1.6f, -0.2f};
    const XrPosef recenter = ComputeYawOnlyRecenterPose(head);
    CHECK_NEAR(recenter.orientation.x, 0.0f, 1e-6f);
    CHECK_NEAR(recenter.orientation.y, 0.0f, 1e-6f);
    CHECK_NEAR(recenter.orientation.z, 0.0f, 1e-6f);
    CHECK_NEAR(recenter.orientation.w, 1.0f, 1e-6f);
    CHECK_NEAR(recenter.position.x, 0.0f, 1e-6f);
    CHECK_NEAR(recenter.position.y, 0.0f, 1e-6f);
    CHECK_NEAR(recenter.position.z, 0.0f, 1e-6f);
}

TEST(XRRecenter_MixedPitchYawRemovesYawOnly) {
    XrPosef head = IdentityPose();
    const XMVECTOR headQ = XMQuaternionRotationRollPitchYaw(
        XMConvertToRadians(22.0f), XMConvertToRadians(-48.0f),
        XMConvertToRadians(7.0f));
    head.orientation = {
        XMVectorGetX(headQ), XMVectorGetY(headQ),
        XMVectorGetZ(headQ), XMVectorGetW(headQ)};

    const XrPosef recenter = ComputeYawOnlyRecenterPose(head);
    const XMVECTOR recenterQ = XMVectorSet(
        recenter.orientation.x, recenter.orientation.y,
        recenter.orientation.z, recenter.orientation.w);
    const XMVECTOR rebasedQ = XMQuaternionMultiply(headQ, recenterQ);
    XrPosef rebased = head;
    rebased.orientation = {
        XMVectorGetX(rebasedQ), XMVectorGetY(rebasedQ),
        XMVectorGetZ(rebasedQ), XMVectorGetW(rebasedQ)};
    const XMFLOAT3 forward = GetPoseForward(rebased);

    CHECK_NEAR(forward.x, 0.0f, 1e-5f);
    CHECK(forward.y != 0.0f);  // pitch remains
    CHECK(forward.z < 0.0f);
}
