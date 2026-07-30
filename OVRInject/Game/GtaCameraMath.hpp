#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace OVRInject {
namespace Game {

constexpr float kGtaDegreesToRadians = 0.01745329251994329577f;
constexpr float kGtaRadiansToDegrees = 57.295779513082320876f;

// GTA rotation order 2 is ROT_ZXY: apply Z (yaw), then X (pitch), then Y
// (roll). DirectXMath uses row vectors, therefore the composition is
// M = Rz * Rx * Ry. Rows are GTA camera right, forward, and up.
inline DirectX::XMMATRIX GtaOrder2EulerToBasis(
    float pitchDeg, float rollDeg, float yawDeg) {
    using namespace DirectX;
    return XMMatrixRotationZ(yawDeg * kGtaDegreesToRadians) *
           XMMatrixRotationX(pitchDeg * kGtaDegreesToRadians) *
           XMMatrixRotationY(rollDeg * kGtaDegreesToRadians);
}

// Inverse of M = Rz * Rx * Ry. At pitch gimbal lock, roll and yaw are not
// independently observable; choose roll=0 and preserve their combined yaw.
inline void GtaOrder2BasisToEuler(
    const DirectX::XMMATRIX& basis,
    float& pitchDeg,
    float& rollDeg,
    float& yawDeg) {
    const float rightX = basis.r[0].m128_f32[0];
    const float rightY = basis.r[0].m128_f32[1];
    const float forwardX = basis.r[1].m128_f32[0];
    const float forwardY = basis.r[1].m128_f32[1];
    const float upX = basis.r[2].m128_f32[0];
    const float upY = basis.r[2].m128_f32[1];
    const float upZ = basis.r[2].m128_f32[2];

    const float sinPitch = (std::max)(-1.0f, (std::min)(1.0f, -upY));
    const float pitch = asinf(sinPitch);
    const float cosPitch = cosf(pitch);

    float roll = 0.0f;
    float yaw = 0.0f;
    if (fabsf(cosPitch) > 1.0e-5f) {
        roll = atan2f(upX, upZ);
        yaw = atan2f(rightY, forwardY);
    } else {
        // With roll fixed to zero, this preserves the observable yaw/roll
        // combination for both +90 and -90 degree pitch.
        yaw = atan2f(-forwardX, rightX);
    }

    pitchDeg = pitch * kGtaRadiansToDegrees;
    rollDeg = roll * kGtaRadiansToDegrees;
    yawDeg = yaw * kGtaRadiansToDegrees;
}

} // namespace Game
} // namespace OVRInject
