#include "TestFramework.hpp"

#include "../OVRInject/Game/GtaCameraMath.hpp"

#include <cmath>

using namespace DirectX;
using namespace OVRInject::Game;

namespace {

XMMATRIX ExplicitOrder2Oracle(float pitchDeg, float rollDeg, float yawDeg) {
    const float x = XMConvertToRadians(pitchDeg);
    const float y = XMConvertToRadians(rollDeg);
    const float z = XMConvertToRadians(yawDeg);
    const float cx = cosf(x), sx = sinf(x);
    const float cy = cosf(y), sy = sinf(y);
    const float cz = cosf(z), sz = sinf(z);

    // Explicit expansion of row-vector Rz * Rx * Ry. This deliberately does
    // not call the production composition helper.
    return XMMATRIX(
        cz * cy + sz * sx * sy,  sz * cx, -cz * sy + sz * sx * cy, 0.0f,
       -sz * cy + cz * sx * sy,  cz * cx,  sz * sy + cz * sx * cy, 0.0f,
        cx * sy,                 -sx,       cx * cy,                0.0f,
        0.0f,                     0.0f,      0.0f,                  1.0f);
}

void CheckMatrixNear(const XMMATRIX& actual, const XMMATRIX& expected, float epsilon) {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            CHECK_NEAR(actual.r[row].m128_f32[column],
                       expected.r[row].m128_f32[column], epsilon);
        }
    }
}

} // namespace

TEST(GtaOrder2_MixedAnglesMatchIndependentZxyOracle) {
    const XMMATRIX actual = GtaOrder2EulerToBasis(25.0f, 15.0f, 40.0f);
    const XMMATRIX expected = ExplicitOrder2Oracle(25.0f, 15.0f, 40.0f);
    CheckMatrixNear(actual, expected, 1.0e-5f);

    // Adversarial guard: the old Z-Y-X formula must be detectably different.
    const XMMATRIX oldWrong =
        XMMatrixRotationZ(XMConvertToRadians(40.0f)) *
        XMMatrixRotationY(XMConvertToRadians(15.0f)) *
        XMMatrixRotationX(XMConvertToRadians(25.0f));
    CHECK(fabsf(actual.r[0].m128_f32[1] - oldWrong.r[0].m128_f32[1]) > 0.01f);
}

TEST(GtaOrder2_ExtractionRecoversIndependentOracleAngles) {
    const XMMATRIX oracle = ExplicitOrder2Oracle(-31.0f, 18.0f, 123.0f);
    float pitch = 0.0f, roll = 0.0f, yaw = 0.0f;
    GtaOrder2BasisToEuler(oracle, pitch, roll, yaw);
    CHECK_NEAR(pitch, -31.0f, 1.0e-4f);
    CHECK_NEAR(roll, 18.0f, 1.0e-4f);
    CHECK_NEAR(yaw, 123.0f, 1.0e-4f);
}

TEST(GtaOrder2_RoundTripMixedAngles) {
    const float cases[][3] = {
        {0.0f, 0.0f, 0.0f},
        {25.0f, 15.0f, 40.0f},
        {-45.0f, 22.0f, -170.0f},
        {70.0f, -35.0f, 179.0f},
    };
    for (const auto& angles : cases) {
        const XMMATRIX basis =
            GtaOrder2EulerToBasis(angles[0], angles[1], angles[2]);
        float pitch = 0.0f, roll = 0.0f, yaw = 0.0f;
        GtaOrder2BasisToEuler(basis, pitch, roll, yaw);
        CHECK_NEAR(pitch, angles[0], 1.0e-3f);
        CHECK_NEAR(roll, angles[1], 1.0e-3f);
        CHECK_NEAR(yaw, angles[2], 1.0e-3f);
    }
}

TEST(GtaOrder2_GimbalLockPreservesTheObservableBasis) {
    for (float pitch : {-90.0f, 90.0f}) {
        const XMMATRIX oracle = ExplicitOrder2Oracle(pitch, 37.0f, -122.0f);
        float extractedPitch = 0.0f;
        float extractedRoll = 0.0f;
        float extractedYaw = 0.0f;
        GtaOrder2BasisToEuler(
            oracle, extractedPitch, extractedRoll, extractedYaw);
        const XMMATRIX reconstructed = GtaOrder2EulerToBasis(
            extractedPitch, extractedRoll, extractedYaw);
        CheckMatrixNear(reconstructed, oracle, 1.0e-4f);
    }
}
