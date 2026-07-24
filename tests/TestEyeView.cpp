// Eye-view composition and shared-memory layout tests.
//
// These replicate, locally, the exact per-eye composition used by the mod:
//     V = inverse(P_eye * P_head)
// where P_eye is the eye pose relative to the head and P_head is the head
// pose in world space (OpenVRBackend.cpp:412-413 builds
// `poseMatrix = eyeMatrix * headMatrix; XMMatrixInverse(nullptr, poseMatrix)`;
// GtaCameraHook.cpp:1649-1661 inverts the head pose the same way).
// DirectXMath uses the row-vector convention: v_view = v_world * V.
//
// No production code is required for this file -- only <DirectXMath.h> --
// so these tests must always pass standalone.

#include "TestFramework.hpp"

#include <DirectXMath.h>
#include <cstring>

using namespace DirectX;

namespace {

constexpr float kEps = 1e-5f;

// Same formula as OpenVRBackend::GetViewMatrix / the GtaCameraHook pipeline.
XMMATRIX ComposeEyeView(const XMMATRIX& eyePose, const XMMATRIX& headPose) {
    return XMMatrixInverse(nullptr, eyePose * headPose);
}

XMFLOAT3 TransformPoint(const XMMATRIX& m, float x, float y, float z) {
    XMVECTOR v = XMVector3Transform(XMVectorSet(x, y, z, 1.0f), m);
    XMFLOAT3 out;
    XMStoreFloat3(&out, v);
    return out;
}

XMFLOAT3 TransformDirection(const XMMATRIX& m, float x, float y, float z) {
    XMVECTOR v = XMVector3TransformNormal(XMVectorSet(x, y, z, 0.0f), m);
    XMFLOAT3 out;
    XMStoreFloat3(&out, v);
    return out;
}

XMFLOAT3 TranslationOf(const XMMATRIX& pose) {
    XMFLOAT3 out;
    XMStoreFloat3(&out, pose.r[3]);
    return out;
}

} // namespace

// Identity head, right eye at +0.0315 on X: a world point exactly at the
// eye's X must have view-space x == 0.
TEST(EyeView_IdentityHeadRightEyeCentersPointAtEyeX) {
    const XMMATRIX eyePose = XMMatrixTranslation(0.0315f, 0.0f, 0.0f);
    const XMMATRIX headPose = XMMatrixIdentity();
    const XMMATRIX view = ComposeEyeView(eyePose, headPose);

    const XMFLOAT3 v = TransformPoint(view, 0.0315f, 0.0f, -5.0f);
    CHECK_NEAR(v.x, 0.0f, 1e-6f);
    CHECK_NEAR(v.y, 0.0f, 1e-6f);
    CHECK_NEAR(v.z, -5.0f, 1e-6f);
}

// Handedness: the mod uses a right-handed system with forward = -Z.
// A +90 deg yaw about +Y (right-hand rule) must rotate the forward vector
// (0,0,-1) to (-1,0,0); a left-handed convention would give (+1,0,0).
TEST(EyeView_HandednessRightHandedForwardMinusZ) {
    const XMMATRIX headPose = XMMatrixRotationY(XM_PIDIV2);

    const XMFLOAT3 forward = TransformDirection(headPose, 0.0f, 0.0f, -1.0f);
    CHECK_NEAR(forward.x, -1.0f, kEps);
    CHECK_NEAR(forward.y, 0.0f, kEps);
    CHECK_NEAR(forward.z, 0.0f, kEps);

    // A world point straight ahead of the rotated head must land on the -Z
    // axis in view space (x ~= 0, y ~= 0, z < 0).
    const XMMATRIX view = ComposeEyeView(XMMatrixIdentity(), headPose);
    const XMFLOAT3 ahead = TransformPoint(view, -5.0f, 0.0f, 0.0f);
    CHECK_NEAR(ahead.x, 0.0f, kEps);
    CHECK_NEAR(ahead.y, 0.0f, kEps);
    CHECK_NEAR(ahead.z, -5.0f, kEps);
    CHECK(ahead.z < 0.0f);
}

// 90 deg yaw WITH an eye translation: the eye offset is rotated with the
// head, and a world point straight ahead of the rotated eye still lands on
// the view-space -Z axis.
TEST(EyeView_Yaw90WithEyeOffsetAheadPointOnMinusZAxis) {
    const XMMATRIX eyePose = XMMatrixTranslation(0.0315f, 0.0f, 0.0f);
    const XMMATRIX headPose = XMMatrixRotationY(XM_PIDIV2);
    const XMMATRIX pose = eyePose * headPose;
    const XMMATRIX view = XMMatrixInverse(nullptr, pose);

    // Compute the world-space eye position and forward from the pose itself.
    const XMFLOAT3 eyePos = TranslationOf(pose);
    const XMFLOAT3 forward = TransformDirection(pose, 0.0f, 0.0f, -1.0f);
    const float wx = eyePos.x + 5.0f * forward.x;
    const float wy = eyePos.y + 5.0f * forward.y;
    const float wz = eyePos.z + 5.0f * forward.z;

    const XMFLOAT3 v = TransformPoint(view, wx, wy, wz);
    CHECK_NEAR(v.x, 0.0f, kEps);
    CHECK_NEAR(v.y, 0.0f, kEps);
    CHECK_NEAR(v.z, -5.0f, kEps);
    CHECK(v.z < 0.0f);
}

// IPD sanity: with an identity head, left eye (-ipd/2) and right eye
// (+ipd/2) views of the same world point differ in view-space x by exactly
// +/- ipd/2 relative to the point's world x.
TEST(EyeView_IpdOffsetsAreExactlyPlusMinusHalfIpd) {
    const float ipd = 0.063f;
    const float half = ipd * 0.5f;
    const XMMATRIX headPose = XMMatrixIdentity();
    const XMMATRIX leftView = ComposeEyeView(XMMatrixTranslation(-half, 0.0f, 0.0f), headPose);
    const XMMATRIX rightView = ComposeEyeView(XMMatrixTranslation(+half, 0.0f, 0.0f), headPose);

    // Centered point: offsets are exactly +/- ipd/2.
    const XMFLOAT3 vl = TransformPoint(leftView, 0.0f, 0.0f, -5.0f);
    const XMFLOAT3 vr = TransformPoint(rightView, 0.0f, 0.0f, -5.0f);
    CHECK(vl.x == half);   // exact: 0 + ipd/2 through a pure translation
    CHECK(vr.x == -half);  // exact
    CHECK(vl.x == -vr.x);  // exact mirror symmetry

    // Off-center point: same offsets relative to the point's own x.
    const XMFLOAT3 wl = TransformPoint(leftView, 0.4f, 0.2f, -7.0f);
    const XMFLOAT3 wr = TransformPoint(rightView, 0.4f, 0.2f, -7.0f);
    CHECK_NEAR(wl.x - 0.4f, +half, 1e-6f);
    CHECK_NEAR(wr.x - 0.4f, -half, 1e-6f);
    CHECK_NEAR(wl.x - wr.x, ipd, 1e-6f);
}

// Row-major float[16] layout contract: producers (e.g. GtaCameraHook's
// GtaCameraMatrix { right[4], forward[4], up[4], position[4] },
// GtaCameraHook.hpp:71-76) memcpy matrices into shared memory and naive
// consumers index m[row*4 + col]. This pins XMMATRIX's memory layout to the
// documented row-major convention used by the slice harness.
TEST(MatrixLayout_XmmatrixIsRowMajorFloat16) {
    CHECK(sizeof(XMMATRIX) == 16 * sizeof(float));

    XMMATRIX m(
        1.0f,  2.0f,  3.0f,  4.0f,
        5.0f,  6.0f,  7.0f,  8.0f,
        9.0f,  10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f);

    float flat[16];
    std::memcpy(flat, &m, sizeof(flat));

    // Naive consumer: flat[row*4 + col] must equal element (row, col).
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            CHECK(flat[r * 4 + c] == static_cast<float>(r * 4 + c + 1));
        }
    }
}

// The producer row convention for the GTA camera matrix shared with the
// slice harness: rows are right, forward, up, position (in that order).
TEST(MatrixLayout_ProducerRowsAreRightForwardUpPosition) {
    const XMMATRIX camera(
        1.0f,  0.0f,  0.0f, 0.0f,   // right
        0.0f,  0.0f, -1.0f, 0.0f,   // forward
        0.0f,  1.0f,  0.0f, 0.0f,   // up
        10.0f, 20.0f, 30.0f, 1.0f); // position

    float flat[16];
    std::memcpy(flat, &camera, sizeof(flat));

    // right row
    CHECK(flat[0] == 1.0f && flat[1] == 0.0f && flat[2] == 0.0f && flat[3] == 0.0f);
    // forward row
    CHECK(flat[4] == 0.0f && flat[5] == 0.0f && flat[6] == -1.0f && flat[7] == 0.0f);
    // up row
    CHECK(flat[8] == 0.0f && flat[9] == 1.0f && flat[10] == 0.0f && flat[11] == 0.0f);
    // position row
    CHECK(flat[12] == 10.0f && flat[13] == 20.0f && flat[14] == 30.0f && flat[15] == 1.0f);
}
