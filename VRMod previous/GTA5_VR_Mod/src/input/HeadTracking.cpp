#include "HeadTracking.h"
#include "../core/Logger.h"
#include <cmath>
#include <algorithm>

namespace GTA5VR {

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;

HeadTracking::HeadTracking() {
    LOG_DEBUG("HeadTracking", "Constructor called");

    // Initialize identity matrices in eye poses
    for (auto& eye : m_eyePoses) {
        for (int i = 0; i < 16; i++) {
            eye.projectionMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            eye.viewMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
    }
}

HeadTracking::~HeadTracking() {
    LOG_DEBUG("HeadTracking", "Destructor called");
}

void HeadTracking::UpdatePose(const HeadPose& pose) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_previousPose = m_currentPose;

    // Apply smoothing if configured
    if (m_positionSmoothing > 0.0f && m_previousPose.isValid) {
        m_currentPose.position.x = m_previousPose.position.x +
            (pose.position.x - m_previousPose.position.x) * (1.0f - m_positionSmoothing);
        m_currentPose.position.y = m_previousPose.position.y +
            (pose.position.y - m_previousPose.position.y) * (1.0f - m_positionSmoothing);
        m_currentPose.position.z = m_previousPose.position.z +
            (pose.position.z - m_previousPose.position.z) * (1.0f - m_positionSmoothing);
    } else {
        m_currentPose.position = pose.position;
    }

    if (m_rotationSmoothing > 0.0f && m_previousPose.isValid) {
        m_currentPose.orientation = QuaternionSlerp(
            m_previousPose.orientation, pose.orientation, 1.0f - m_rotationSmoothing);
    } else {
        m_currentPose.orientation = pose.orientation;
    }

    m_currentPose.velocity = pose.velocity;
    m_currentPose.angularVelocity = pose.angularVelocity;
    m_currentPose.isValid = pose.isValid;
    m_currentPose.timestamp = pose.timestamp;

    LOG_DEBUG("HeadTracking", "Pose updated: pos(%.3f, %.3f, %.3f) valid=%d",
              m_currentPose.position.x, m_currentPose.position.y, m_currentPose.position.z,
              m_currentPose.isValid);
}

void HeadTracking::UpdateEyePose(uint32_t eye, const EyePose& pose) {
    if (eye >= 2) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_eyePoses[eye] = pose;
}

HeadPose HeadTracking::GetHeadPose() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentPose;
}

EyePose HeadTracking::GetEyePose(uint32_t eye) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (eye >= 2) {
        return EyePose();
    }
    return m_eyePoses[eye];
}

Vector3 HeadTracking::GetHeadPosition() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentPose.position;
}

Quaternion HeadTracking::GetHeadOrientation() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentPose.orientation;
}

void HeadTracking::GetHeadEulerAngles(float& pitch, float& yaw, float& roll) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    QuaternionToEuler(m_currentPose.orientation, pitch, yaw, roll);

    // Convert to degrees
    pitch *= RAD_TO_DEG;
    yaw *= RAD_TO_DEG;
    roll *= RAD_TO_DEG;
}

Vector3 HeadTracking::GetForwardVector() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Vector3 forward(0.0f, 0.0f, -1.0f);  // -Z is forward in most VR systems
    return RotateVectorByQuaternion(forward, m_currentPose.orientation);
}

Vector3 HeadTracking::GetRightVector() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Vector3 right(1.0f, 0.0f, 0.0f);
    return RotateVectorByQuaternion(right, m_currentPose.orientation);
}

Vector3 HeadTracking::GetUpVector() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Vector3 up(0.0f, 1.0f, 0.0f);
    return RotateVectorByQuaternion(up, m_currentPose.orientation);
}

void HeadTracking::SetTrackingOrigin(TrackingOrigin origin) {
    m_trackingOrigin = origin;
    LOG_INFO("HeadTracking", "Tracking origin set to %d", static_cast<int>(origin));
}

void HeadTracking::RecenterView() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Store current position and yaw as offset
    m_recenterOffset = m_currentPose.position;

    float pitch, yaw, roll;
    QuaternionToEuler(m_currentPose.orientation, pitch, yaw, roll);
    m_recenterYaw = yaw * RAD_TO_DEG;

    LOG_INFO("HeadTracking", "View recentered at pos(%.3f, %.3f, %.3f) yaw=%.1f",
             m_recenterOffset.x, m_recenterOffset.y, m_recenterOffset.z, m_recenterYaw);
}

void HeadTracking::SetWorldOffset(const Vector3& offset) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_worldOffset = offset;
}

void HeadTracking::SetWorldRotationOffset(float yawDegrees) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_worldYawOffset = yawDegrees;
}

Vector3 HeadTracking::GetWorldPosition() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Apply recenter offset and world offset
    Vector3 pos;
    pos.x = (m_currentPose.position.x - m_recenterOffset.x) * m_worldScale + m_worldOffset.x;
    pos.y = (m_currentPose.position.y - m_recenterOffset.y) * m_worldScale + m_worldOffset.y;
    pos.z = (m_currentPose.position.z - m_recenterOffset.z) * m_worldScale + m_worldOffset.z;

    // Apply yaw rotation
    if (m_worldYawOffset != 0.0f || m_recenterYaw != 0.0f) {
        float totalYaw = (m_worldYawOffset - m_recenterYaw) * DEG_TO_RAD;
        float cosYaw = std::cos(totalYaw);
        float sinYaw = std::sin(totalYaw);

        float newX = pos.x * cosYaw - pos.z * sinYaw;
        float newZ = pos.x * sinYaw + pos.z * cosYaw;
        pos.x = newX;
        pos.z = newZ;
    }

    return pos;
}

Quaternion HeadTracking::GetWorldOrientation() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Apply yaw offset
    float totalYaw = (m_worldYawOffset - m_recenterYaw) * DEG_TO_RAD;
    Quaternion yawOffset = QuaternionFromEuler(0.0f, totalYaw, 0.0f);

    return QuaternionMultiply(yawOffset, m_currentPose.orientation);
}

void HeadTracking::SetIPD(float ipd) {
    m_ipd = std::clamp(ipd, 0.050f, 0.080f);  // 50mm to 80mm
    LOG_INFO("HeadTracking", "IPD set to %.1f mm", m_ipd * 1000.0f);
}

void HeadTracking::SetWorldScale(float scale) {
    m_worldScale = std::clamp(scale, 0.1f, 10.0f);
    LOG_INFO("HeadTracking", "World scale set to %.2f", m_worldScale);
}

HeadPose HeadTracking::PredictPose(float secondsFromNow) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    HeadPose predicted = m_currentPose;

    // Simple linear prediction for position
    predicted.position.x += m_currentPose.velocity.x * secondsFromNow;
    predicted.position.y += m_currentPose.velocity.y * secondsFromNow;
    predicted.position.z += m_currentPose.velocity.z * secondsFromNow;

    // Angular velocity prediction (simplified)
    float angVelMag = std::sqrt(
        m_currentPose.angularVelocity.x * m_currentPose.angularVelocity.x +
        m_currentPose.angularVelocity.y * m_currentPose.angularVelocity.y +
        m_currentPose.angularVelocity.z * m_currentPose.angularVelocity.z);

    if (angVelMag > 0.0001f) {
        float angle = angVelMag * secondsFromNow;
        Vector3 axis;
        axis.x = m_currentPose.angularVelocity.x / angVelMag;
        axis.y = m_currentPose.angularVelocity.y / angVelMag;
        axis.z = m_currentPose.angularVelocity.z / angVelMag;

        // Create rotation quaternion
        float halfAngle = angle * 0.5f;
        float sinHalf = std::sin(halfAngle);
        Quaternion deltaRot;
        deltaRot.w = std::cos(halfAngle);
        deltaRot.x = axis.x * sinHalf;
        deltaRot.y = axis.y * sinHalf;
        deltaRot.z = axis.z * sinHalf;

        predicted.orientation = QuaternionMultiply(deltaRot, m_currentPose.orientation);
    }

    return predicted;
}

void HeadTracking::SetPositionSmoothing(float factor) {
    m_positionSmoothing = std::clamp(factor, 0.0f, 0.99f);
}

void HeadTracking::SetRotationSmoothing(float factor) {
    m_rotationSmoothing = std::clamp(factor, 0.0f, 0.99f);
}

bool HeadTracking::IsTrackingValid() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentPose.isValid;
}

uint64_t HeadTracking::GetLastUpdateTimestamp() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentPose.timestamp;
}

void HeadTracking::GetViewMatrix(uint32_t eye, float* outMatrix) const {
    if (!outMatrix || eye >= 2) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Get eye position (head position + IPD offset)
    Vector3 eyePos = GetWorldPosition();
    float halfIPD = m_ipd * 0.5f * m_worldScale;

    // Offset based on eye
    Vector3 right = GetRightVector();
    if (eye == 0) {  // Left eye
        eyePos.x -= right.x * halfIPD;
        eyePos.y -= right.y * halfIPD;
        eyePos.z -= right.z * halfIPD;
    } else {  // Right eye
        eyePos.x += right.x * halfIPD;
        eyePos.y += right.y * halfIPD;
        eyePos.z += right.z * halfIPD;
    }

    CreateViewMatrix(eyePos, GetWorldOrientation(), outMatrix);
}

void HeadTracking::GetProjectionMatrix(uint32_t eye, float nearPlane, float farPlane,
                                        float* outMatrix) const {
    if (!outMatrix || eye >= 2) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Use pre-computed projection matrix if available
    if (m_eyePoses[eye].projectionMatrix[0] != 0.0f) {
        memcpy(outMatrix, m_eyePoses[eye].projectionMatrix, sizeof(float) * 16);
        return;
    }

    // Otherwise create a standard perspective projection
    float aspectRatio = 1.0f;  // Should come from VR runtime
    float fov = 90.0f * DEG_TO_RAD;  // Should come from VR runtime

    float tanHalfFov = std::tan(fov * 0.5f);

    memset(outMatrix, 0, sizeof(float) * 16);
    outMatrix[0] = 1.0f / (aspectRatio * tanHalfFov);
    outMatrix[5] = 1.0f / tanHalfFov;
    outMatrix[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    outMatrix[11] = -1.0f;
    outMatrix[14] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
}

// Static helper implementations

Quaternion HeadTracking::QuaternionMultiply(const Quaternion& a, const Quaternion& b) {
    Quaternion result;
    result.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    result.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    result.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    result.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return result;
}

Quaternion HeadTracking::QuaternionFromEuler(float pitch, float yaw, float roll) {
    float cp = std::cos(pitch * 0.5f);
    float sp = std::sin(pitch * 0.5f);
    float cy = std::cos(yaw * 0.5f);
    float sy = std::sin(yaw * 0.5f);
    float cr = std::cos(roll * 0.5f);
    float sr = std::sin(roll * 0.5f);

    Quaternion q;
    q.w = cp * cy * cr + sp * sy * sr;
    q.x = sp * cy * cr - cp * sy * sr;
    q.y = cp * sy * cr + sp * cy * sr;
    q.z = cp * cy * sr - sp * sy * cr;
    return q;
}

void HeadTracking::QuaternionToEuler(const Quaternion& q, float& pitch, float& yaw, float& roll) {
    // Roll (x-axis rotation)
    float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
    float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    roll = std::atan2(sinr_cosp, cosr_cosp);

    // Pitch (y-axis rotation)
    float sinp = 2.0f * (q.w * q.y - q.z * q.x);
    if (std::abs(sinp) >= 1.0f) {
        pitch = std::copysign(PI / 2.0f, sinp);  // Clamp to 90 degrees
    } else {
        pitch = std::asin(sinp);
    }

    // Yaw (z-axis rotation)
    float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
    float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    yaw = std::atan2(siny_cosp, cosy_cosp);
}

Quaternion HeadTracking::QuaternionSlerp(const Quaternion& a, const Quaternion& b, float t) {
    // Compute dot product
    float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;

    Quaternion b2 = b;
    if (dot < 0.0f) {
        dot = -dot;
        b2.w = -b2.w;
        b2.x = -b2.x;
        b2.y = -b2.y;
        b2.z = -b2.z;
    }

    Quaternion result;
    if (dot > 0.9995f) {
        // Linear interpolation for very close quaternions
        result.w = a.w + t * (b2.w - a.w);
        result.x = a.x + t * (b2.x - a.x);
        result.y = a.y + t * (b2.y - a.y);
        result.z = a.z + t * (b2.z - a.z);
    } else {
        float theta = std::acos(dot);
        float sinTheta = std::sin(theta);
        float wa = std::sin((1.0f - t) * theta) / sinTheta;
        float wb = std::sin(t * theta) / sinTheta;

        result.w = wa * a.w + wb * b2.w;
        result.x = wa * a.x + wb * b2.x;
        result.y = wa * a.y + wb * b2.y;
        result.z = wa * a.z + wb * b2.z;
    }

    // Normalize
    float len = std::sqrt(result.w * result.w + result.x * result.x +
                          result.y * result.y + result.z * result.z);
    result.w /= len;
    result.x /= len;
    result.y /= len;
    result.z /= len;

    return result;
}

Vector3 HeadTracking::RotateVectorByQuaternion(const Vector3& v, const Quaternion& q) {
    // q * v * q^-1
    Quaternion vq = {0.0f, v.x, v.y, v.z};
    Quaternion qConj = {q.w, -q.x, -q.y, -q.z};

    Quaternion result = QuaternionMultiply(QuaternionMultiply(q, vq), qConj);

    return Vector3(result.x, result.y, result.z);
}

void HeadTracking::CreateViewMatrix(const Vector3& position, const Quaternion& orientation,
                                     float* outMatrix) {
    // Convert quaternion to rotation matrix and combine with translation

    float xx = orientation.x * orientation.x;
    float yy = orientation.y * orientation.y;
    float zz = orientation.z * orientation.z;
    float xy = orientation.x * orientation.y;
    float xz = orientation.x * orientation.z;
    float yz = orientation.y * orientation.z;
    float wx = orientation.w * orientation.x;
    float wy = orientation.w * orientation.y;
    float wz = orientation.w * orientation.z;

    // Rotation part (transposed for view matrix)
    outMatrix[0] = 1.0f - 2.0f * (yy + zz);
    outMatrix[1] = 2.0f * (xy + wz);
    outMatrix[2] = 2.0f * (xz - wy);
    outMatrix[3] = 0.0f;

    outMatrix[4] = 2.0f * (xy - wz);
    outMatrix[5] = 1.0f - 2.0f * (xx + zz);
    outMatrix[6] = 2.0f * (yz + wx);
    outMatrix[7] = 0.0f;

    outMatrix[8] = 2.0f * (xz + wy);
    outMatrix[9] = 2.0f * (yz - wx);
    outMatrix[10] = 1.0f - 2.0f * (xx + yy);
    outMatrix[11] = 0.0f;

    // Translation part (rotated and negated)
    outMatrix[12] = -(outMatrix[0] * position.x + outMatrix[4] * position.y + outMatrix[8] * position.z);
    outMatrix[13] = -(outMatrix[1] * position.x + outMatrix[5] * position.y + outMatrix[9] * position.z);
    outMatrix[14] = -(outMatrix[2] * position.x + outMatrix[6] * position.y + outMatrix[10] * position.z);
    outMatrix[15] = 1.0f;
}

} // namespace GTA5VR
