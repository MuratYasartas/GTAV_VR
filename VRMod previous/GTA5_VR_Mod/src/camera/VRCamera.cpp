#include "VRCamera.h"
#include "../core/Logger.h"
#include "../runtime/RuntimeInterface.h"
#include <openxr/openxr.h>
#include <cmath>
#include <cstring>

namespace GTA5VR {

VRCamera::VRCamera() {
    // Initialize matrices to identity
    for (int eye = 0; eye < 2; ++eye) {
        memset(m_viewMatrix[eye].data(), 0, sizeof(float) * 16);
        memset(m_projectionMatrix[eye].data(), 0, sizeof(float) * 16);
        m_viewMatrix[eye][0] = m_viewMatrix[eye][5] = m_viewMatrix[eye][10] = m_viewMatrix[eye][15] = 1.0f;
        m_projectionMatrix[eye][0] = m_projectionMatrix[eye][5] = m_projectionMatrix[eye][10] = m_projectionMatrix[eye][15] = 1.0f;
    }
}

VRCamera::~VRCamera() {
    Shutdown();
}

bool VRCamera::Initialize(IRuntimeInterface* runtime) {
    if (m_initialized) return true;
    m_runtime = runtime;
    m_initialized = true;
    LOG_INFO("VRCamera initialized");
    return true;
}

void VRCamera::Shutdown() {
    m_initialized = false;
}

void VRCamera::Update(float deltaTime) {
    if (!m_initialized || !m_runtime) return;
    UpdateViewMatrices();
}

void VRCamera::ApplyHeadTracking(const XrPosef& headPose) {
    if (!m_initialized) return;

    // Apply position with offset
    m_position[0] = headPose.position.x * m_worldScale - m_positionOffset[0];
    m_position[1] = headPose.position.y * m_worldScale - m_positionOffset[1];
    m_position[2] = headPose.position.z * m_worldScale - m_positionOffset[2];

    // Apply rotation (would need quaternion multiplication for offset)
    m_rotation[0] = headPose.orientation.x;
    m_rotation[1] = headPose.orientation.y;
    m_rotation[2] = headPose.orientation.z;
    m_rotation[3] = headPose.orientation.w;

    UpdateViewMatrices();
}

void VRCamera::ApplyPositionalTracking(const XrVector3f& position) {
    if (!m_initialized) return;
    m_position[0] = position.x * m_worldScale - m_positionOffset[0];
    m_position[1] = position.y * m_worldScale - m_positionOffset[1];
    m_position[2] = position.z * m_worldScale - m_positionOffset[2];
}

void VRCamera::GetViewMatrix(uint32_t eyeIndex, float* outMatrix) const {
    if (eyeIndex >= 2 || !outMatrix) return;
    memcpy(outMatrix, m_viewMatrix[eyeIndex].data(), sizeof(float) * 16);
}

void VRCamera::GetProjectionMatrix(uint32_t eyeIndex, float* outMatrix) const {
    if (eyeIndex >= 2 || !outMatrix) return;
    memcpy(outMatrix, m_projectionMatrix[eyeIndex].data(), sizeof(float) * 16);
}

void VRCamera::SetIPD(float ipd) {
    m_ipd = ipd;
}

void VRCamera::SetWorldScale(float scale) {
    m_worldScale = std::max(0.1f, std::min(scale, 10.0f));
}

void VRCamera::SetSnapTurning(bool enabled, float degrees) {
    m_snapTurnEnabled = enabled;
    m_snapTurnDegrees = degrees;
}

void VRCamera::SetVignetteOnTurn(bool enabled) {
    m_vignetteOnTurn = enabled;
}

void VRCamera::Recenter() {
    // Store current position as offset
    m_positionOffset[0] = m_position[0] + m_positionOffset[0];
    m_positionOffset[1] = 0; // Don't offset height
    m_positionOffset[2] = m_position[2] + m_positionOffset[2];

    LOG_INFO("View recentered");
}

void VRCamera::RecenterOnHeadShake() {
    // This would detect head shake gesture and call Recenter()
    // Implementation would track head rotation velocity
}

void VRCamera::GetPosition(float* outX, float* outY, float* outZ) const {
    if (outX) *outX = m_position[0];
    if (outY) *outY = m_position[1];
    if (outZ) *outZ = m_position[2];
}

void VRCamera::GetRotation(float* outX, float* outY, float* outZ, float* outW) const {
    if (outX) *outX = m_rotation[0];
    if (outY) *outY = m_rotation[1];
    if (outZ) *outZ = m_rotation[2];
    if (outW) *outW = m_rotation[3];
}

void VRCamera::CreateAsymmetricProjection(const XrFovf& fov, float nearPlane, float farPlane, float* outMatrix) {
    if (!outMatrix) return;

    float tanLeft = tanf(fov.angleLeft);
    float tanRight = tanf(fov.angleRight);
    float tanUp = tanf(fov.angleUp);
    float tanDown = tanf(fov.angleDown);

    float tanWidth = tanRight - tanLeft;
    float tanHeight = tanUp - tanDown;

    memset(outMatrix, 0, sizeof(float) * 16);

    outMatrix[0] = 2.0f / tanWidth;
    outMatrix[5] = 2.0f / tanHeight;
    outMatrix[8] = (tanRight + tanLeft) / tanWidth;
    outMatrix[9] = (tanUp + tanDown) / tanHeight;
    outMatrix[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    outMatrix[11] = -1.0f;
    outMatrix[14] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
}

void VRCamera::UpdateViewMatrices() {
    // Build view matrices from position and rotation
    // Using quaternion to rotation matrix conversion

    float qx = m_rotation[0], qy = m_rotation[1], qz = m_rotation[2], qw = m_rotation[3];

    // Rotation matrix from quaternion
    float rotMatrix[9] = {
        1.0f - 2.0f * (qy*qy + qz*qz), 2.0f * (qx*qy - qz*qw), 2.0f * (qx*qz + qy*qw),
        2.0f * (qx*qy + qz*qw), 1.0f - 2.0f * (qx*qx + qz*qz), 2.0f * (qy*qz - qx*qw),
        2.0f * (qx*qz - qy*qw), 2.0f * (qy*qz + qx*qw), 1.0f - 2.0f * (qx*qx + qy*qy)
    };

    for (int eye = 0; eye < 2; ++eye) {
        float eyeOffset = (eye == 0 ? -1.0f : 1.0f) * (m_ipd / 2.0f);

        // View matrix = inverse of camera transform
        // For VR, we need to account for eye offset
        auto& mat = m_viewMatrix[eye];

        // Rotation (transposed for inverse)
        mat[0] = rotMatrix[0]; mat[4] = rotMatrix[1]; mat[8] = rotMatrix[2];
        mat[1] = rotMatrix[3]; mat[5] = rotMatrix[4]; mat[9] = rotMatrix[5];
        mat[2] = rotMatrix[6]; mat[6] = rotMatrix[7]; mat[10] = rotMatrix[8];

        // Translation (negated and rotated for inverse)
        float px = m_position[0] + eyeOffset;
        float py = m_position[1];
        float pz = m_position[2];

        mat[12] = -(mat[0] * px + mat[4] * py + mat[8] * pz);
        mat[13] = -(mat[1] * px + mat[5] * py + mat[9] * pz);
        mat[14] = -(mat[2] * px + mat[6] * py + mat[10] * pz);

        mat[3] = mat[7] = mat[11] = 0.0f;
        mat[15] = 1.0f;
    }
}

} // namespace GTA5VR
