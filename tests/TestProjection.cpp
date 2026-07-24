// Projection-matrix oracle tests for OVRInject::XR::XrFovToProjectionMatrixD3D.
//
// OpenXR view space is right-handed: +X right, +Y up, forward is -Z.
// D3D clip/NDC space has z in [0, 1] (unlike OpenGL's [-1, 1], which is what
// the older XrFovToProjectionMatrix in XRCore.hpp wrongly produces).
//
// The function under test is specified as:
//   XMMatrixPerspectiveOffCenterRH(tan(angleLeft)  * n,   // ViewLeft
//                                  tan(angleRight) * n,   // ViewRight
//                                  tan(angleDown)  * n,   // ViewBottom
//                                  tan(angleUp)    * n,   // ViewTop
//                                  n, f)
// and is being added to OVRInject/OpenXR/XRCore.hpp by a concurrent change.
// While it is absent this suite is compiled with GTAVR_XR_FOV_D3D_PENDING and
// every test reports [SKIP] (EXPECTED-PENDING) instead of failing the build.
// Remove that define from GTAVRTests.vcxproj once the function lands.

#include "TestFramework.hpp"
#include "../OVRInject/OpenXR/XRCore.hpp"

#include <cmath>

using namespace DirectX;

#if !defined(GTAVR_XR_FOV_D3D_PENDING)
using OVRInject::XR::XrFovToProjectionMatrixD3D;
#endif

namespace {

// The FOV from the mission brief (radians), plus near/far planes.
XrFovf MissionFov() {
    XrFovf fov{};
    fov.angleLeft  = -0.93f;
    fov.angleRight =  0.85f;
    fov.angleUp    =  0.97f;
    fov.angleDown  = -0.96f;
    return fov;
}

constexpr float kNear = 0.1f;
constexpr float kFar  = 1500.0f;
constexpr float kEps  = 1e-4f;

// Projects a view-space point with XMVector3Transform and divides by w,
// returning the NDC coordinates.
XMFLOAT3 ProjectToNdc(const XMMATRIX& projection, float x, float y, float z) {
    XMVECTOR point = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR clip = XMVector3Transform(point, projection);
    XMVECTOR ndc = XMVectorDivide(clip, XMVectorSplatW(clip));
    XMFLOAT3 result;
    XMStoreFloat3(&result, ndc);
    return result;
}

#if defined(GTAVR_XR_FOV_D3D_PENDING)

#define GUARD_PROJECTION_PENDING()                                          \
    SKIP("EXPECTED-PENDING: XrFovToProjectionMatrixD3D is not in "          \
         "XRCore.hpp yet (concurrent change); see tests/README.md")

#else

#define GUARD_PROJECTION_PENDING() do {} while (0)

#endif

} // namespace

// (a) View-space points on the frustum edges at z = -1 must land exactly on
// the matching NDC edges. Note: with an asymmetric FOV the cross-axis NDC
// coordinate is NOT zero -- a point on a horizontal edge (view y = 0) sits at
// the vertical principal-point offset -(tanU+tanD)/(tanU-tanD), and a point
// on a vertical edge sits at -(tanL+tanR)/(tanR-tanL).
TEST(ProjD3D_RightFrustumEdgeMapsToNdcPlusOne) {
    GUARD_PROJECTION_PENDING();
    const XrFovf fov = MissionFov();
    const XMMATRIX p = XrFovToProjectionMatrixD3D(fov, kNear, kFar);
    const XMFLOAT3 ndc = ProjectToNdc(p, tanf(fov.angleRight), 0.0f, -1.0f);
    CHECK_NEAR(ndc.x, 1.0f, kEps);
    CHECK_NEAR(ndc.y, -(tanf(fov.angleUp) + tanf(fov.angleDown)) /
                      (tanf(fov.angleUp) - tanf(fov.angleDown)), kEps);
}

TEST(ProjD3D_LeftFrustumEdgeMapsToNdcMinusOne) {
    GUARD_PROJECTION_PENDING();
    const XrFovf fov = MissionFov();
    const XMMATRIX p = XrFovToProjectionMatrixD3D(fov, kNear, kFar);
    const XMFLOAT3 ndc = ProjectToNdc(p, tanf(fov.angleLeft), 0.0f, -1.0f);
    CHECK_NEAR(ndc.x, -1.0f, kEps);
    CHECK_NEAR(ndc.y, -(tanf(fov.angleUp) + tanf(fov.angleDown)) /
                      (tanf(fov.angleUp) - tanf(fov.angleDown)), kEps);
}

TEST(ProjD3D_TopFrustumEdgeMapsToNdcPlusOne) {
    GUARD_PROJECTION_PENDING();
    const XrFovf fov = MissionFov();
    const XMMATRIX p = XrFovToProjectionMatrixD3D(fov, kNear, kFar);
    const XMFLOAT3 ndc = ProjectToNdc(p, 0.0f, tanf(fov.angleUp), -1.0f);
    CHECK_NEAR(ndc.y, 1.0f, kEps);
    CHECK_NEAR(ndc.x, -(tanf(fov.angleLeft) + tanf(fov.angleRight)) /
                      (tanf(fov.angleRight) - tanf(fov.angleLeft)), kEps);
}

TEST(ProjD3D_BottomFrustumEdgeMapsToNdcMinusOne) {
    GUARD_PROJECTION_PENDING();
    const XrFovf fov = MissionFov();
    const XMMATRIX p = XrFovToProjectionMatrixD3D(fov, kNear, kFar);
    const XMFLOAT3 ndc = ProjectToNdc(p, 0.0f, tanf(fov.angleDown), -1.0f);
    CHECK_NEAR(ndc.y, -1.0f, kEps);
    CHECK_NEAR(ndc.x, -(tanf(fov.angleLeft) + tanf(fov.angleRight)) /
                      (tanf(fov.angleRight) - tanf(fov.angleLeft)), kEps);
}

// (b) D3D depth range: the near plane maps to z = 0 and the far plane to
// z = 1. (An OpenGL-style matrix would give -1 and +1 instead; this is the
// check that catches the bug in the old XrFovToProjectionMatrix.)
TEST(ProjD3D_NearPlaneMapsToNdcZero) {
    GUARD_PROJECTION_PENDING();
    const XMMATRIX p = XrFovToProjectionMatrixD3D(MissionFov(), kNear, kFar);
    const XMFLOAT3 ndc = ProjectToNdc(p, 0.0f, 0.0f, -kNear);
    CHECK_NEAR(ndc.z, 0.0f, 1e-6f);
}

TEST(ProjD3D_FarPlaneMapsToNdcOne) {
    GUARD_PROJECTION_PENDING();
    const XMMATRIX p = XrFovToProjectionMatrixD3D(MissionFov(), kNear, kFar);
    const XMFLOAT3 ndc = ProjectToNdc(p, 0.0f, 0.0f, -kFar);
    CHECK_NEAR(ndc.z, 1.0f, 1e-5f);
}

// (c) A point between the planes lands strictly inside (0, 1) and depth is
// monotonic: points farther along -Z get larger NDC z.
TEST(ProjD3D_DepthIsInsideRangeAndMonotonic) {
    GUARD_PROJECTION_PENDING();
    const XMMATRIX p = XrFovToProjectionMatrixD3D(MissionFov(), kNear, kFar);
    const float zHalf = ProjectToNdc(p, 0.0f, 0.0f, -0.5f).z;
    const float zMid  = ProjectToNdc(p, 0.0f, 0.0f, -1.0f).z;
    const float zFive = ProjectToNdc(p, 0.0f, 0.0f, -5.0f).z;
    CHECK(zMid > 0.0f);
    CHECK(zMid < 1.0f);
    CHECK(zHalf < zMid);
    CHECK(zMid < zFive);
}

// (d) Symmetric FOV: the forward axis (-Z) must project to NDC (0, 0).
TEST(ProjD3D_SymmetricFovPrincipalPointAtOrigin) {
    GUARD_PROJECTION_PENDING();
    XrFovf fov{};
    fov.angleLeft  = -0.9f;
    fov.angleRight =  0.9f;
    fov.angleUp    =  1.0f;
    fov.angleDown  = -1.0f;
    const XMMATRIX p = XrFovToProjectionMatrixD3D(fov, kNear, kFar);
    const XMFLOAT3 ndc = ProjectToNdc(p, 0.0f, 0.0f, -1.0f);
    CHECK_NEAR(ndc.x, 0.0f, kEps);
    CHECK_NEAR(ndc.y, 0.0f, kEps);
}

// (d) Asymmetric FOV: the principal point shifts away from the wider side.
// For v_clip = v_view * P (DirectXMath row-vector convention) the forward
// axis (0,0,-1) maps to NDC x = -(tanL + tanR) / (tanR - tanL) -- note the
// negation relative to the raw matrix element m[2][0] = (l+r)/(r-l). With
// the mission FOV |tanL| > tanR, so the principal point must sit slightly to
// the RIGHT of center (positive x).
TEST(ProjD3D_AsymmetricFovPrincipalPointSign) {
    GUARD_PROJECTION_PENDING();
    const XrFovf fov = MissionFov();
    const XMMATRIX p = XrFovToProjectionMatrixD3D(fov, kNear, kFar);
    const XMFLOAT3 ndc = ProjectToNdc(p, 0.0f, 0.0f, -1.0f);

    const float tanL = tanf(fov.angleLeft);
    const float tanR = tanf(fov.angleRight);
    const float tanU = tanf(fov.angleUp);
    const float tanD = tanf(fov.angleDown);
    const float expectedX = -(tanL + tanR) / (tanR - tanL);
    const float expectedY = -(tanU + tanD) / (tanU - tanD);

    CHECK_NEAR(ndc.x, expectedX, kEps);
    CHECK_NEAR(ndc.y, expectedY, kEps);
    CHECK(ndc.x > 0.0f); // wider FOV on the left pushes the center right
}

// Cross-check against the specified oracle: the result must equal
// XMMatrixPerspectiveOffCenterRH with bottom = tan(down)*n and
// top = tan(up)*n (argument-order mistakes are the classic bug here).
TEST(ProjD3D_MatchesPerspectiveOffCenterRHOracle) {
    GUARD_PROJECTION_PENDING();
    const XrFovf fov = MissionFov();
    const XMMATRIX actual = XrFovToProjectionMatrixD3D(fov, kNear, kFar);
    const XMMATRIX oracle = XMMatrixPerspectiveOffCenterRH(
        tanf(fov.angleLeft)  * kNear,
        tanf(fov.angleRight) * kNear,
        tanf(fov.angleDown)  * kNear,
        tanf(fov.angleUp)    * kNear,
        kNear, kFar);
    // XMMATRIX::operator()/.m only exist with _XM_NO_INTRINSICS_; go through
    // XMFLOAT4X4 for portable element access.
    XMFLOAT4X4 a;
    XMFLOAT4X4 o;
    XMStoreFloat4x4(&a, actual);
    XMStoreFloat4x4(&o, oracle);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            CHECK_NEAR(a.m[r][c], o.m[r][c], 1e-5f);
        }
    }
}
