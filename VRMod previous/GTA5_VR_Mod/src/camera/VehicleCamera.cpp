#include "VehicleCamera.h"
#include "../core/Logger.h"
#include <cmath>
#include <algorithm>

namespace GTA5VR {

VehicleCamera::VehicleCamera() {
    LOG_DEBUG("VehicleCamera", "Constructor called");

    // Initialize identity matrices
    for (int i = 0; i < 16; i++) {
        m_viewMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    // Default dashboard settings for cars
    m_dashboardSettings.seatOffset = Vector3(-0.4f, 0.5f, 0.2f);  // Left seat, up, forward
    m_dashboardSettings.heightOffset = 0.0f;
    m_dashboardSettings.forwardOffset = 0.0f;
    m_dashboardSettings.lockToVehicle = true;
    m_dashboardSettings.rotationInfluence = 0.3f;
}

VehicleCamera::~VehicleCamera() {
    Shutdown();
}

bool VehicleCamera::Initialize() {
    if (m_initialized) {
        return true;
    }

    LOG_INFO("VehicleCamera", "Initializing vehicle camera");

    m_initialized = true;
    return true;
}

void VehicleCamera::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("VehicleCamera", "Shutting down vehicle camera");

    m_enteredCallbacks.clear();
    m_exitedCallbacks.clear();
    m_modeCallbacks.clear();

    m_initialized = false;
}

void VehicleCamera::Update(float deltaTime) {
    if (!m_initialized || !m_inVehicle) {
        return;
    }

    // Apply motion dampening to smooth out vehicle movement
    ApplyMotionDampening(deltaTime);

    // Update camera based on mode
    switch (m_cameraMode) {
        case VehicleCameraMode::Dashboard:
            UpdateDashboardCamera(deltaTime);
            break;
        case VehicleCameraMode::Hood:
            UpdateHoodCamera(deltaTime);
            break;
        case VehicleCameraMode::Chase:
            UpdateChaseCamera(deltaTime);
            break;
        case VehicleCameraMode::FirstPersonDriver:
        case VehicleCameraMode::FirstPersonPassenger:
            UpdateFirstPersonCamera(deltaTime);
            break;
        default:
            UpdateDashboardCamera(deltaTime);
            break;
    }

    // Compute final view matrix
    ComputeViewMatrix();
}

void VehicleCamera::SetInVehicle(bool inVehicle, VehicleType type) {
    if (m_inVehicle == inVehicle && m_vehicleType == type) {
        return;
    }

    bool wasInVehicle = m_inVehicle;
    m_inVehicle = inVehicle;
    m_vehicleType = inVehicle ? type : VehicleType::Unknown;

    if (inVehicle && !wasInVehicle) {
        LOG_INFO("VehicleCamera", "Entered vehicle type %d", static_cast<int>(type));

        // Set default camera mode for vehicle type
        m_cameraMode = GetDefaultModeForVehicle(type);

        // Notify callbacks
        for (auto& callback : m_enteredCallbacks) {
            if (callback) {
                callback(type);
            }
        }
    } else if (!inVehicle && wasInVehicle) {
        LOG_INFO("VehicleCamera", "Exited vehicle");

        // Notify callbacks
        for (auto& callback : m_exitedCallbacks) {
            if (callback) {
                callback();
            }
        }
    }
}

void VehicleCamera::SetCameraMode(VehicleCameraMode mode) {
    if (m_cameraMode == mode) {
        return;
    }

    m_cameraMode = mode;
    LOG_INFO("VehicleCamera", "Camera mode changed to %d", static_cast<int>(mode));

    for (auto& callback : m_modeCallbacks) {
        if (callback) {
            callback(mode);
        }
    }
}

VehicleCameraMode VehicleCamera::GetDefaultModeForVehicle(VehicleType type) const {
    switch (type) {
        case VehicleType::Car:
        case VehicleType::Tank:
            return VehicleCameraMode::Dashboard;
        case VehicleType::Motorcycle:
        case VehicleType::Bicycle:
            return VehicleCameraMode::FirstPersonDriver;
        case VehicleType::Boat:
            return VehicleCameraMode::FirstPersonDriver;
        case VehicleType::Helicopter:
        case VehicleType::Plane:
            return VehicleCameraMode::Dashboard;
        case VehicleType::Train:
            return VehicleCameraMode::Dashboard;
        default:
            return VehicleCameraMode::Dashboard;
    }
}

void VehicleCamera::SetVRHeadPose(const HeadPose& pose) {
    m_vrHeadPose = pose;
}

void VehicleCamera::SetVehicleTransform(const Vector3& position, const Quaternion& orientation) {
    m_vehiclePosition = position;
    m_vehicleOrientation = orientation;
}

void VehicleCamera::SetVehicleVelocity(const Vector3& velocity, const Vector3& angularVelocity) {
    m_vehicleVelocity = velocity;
    m_vehicleAngularVelocity = angularVelocity;

    // Calculate speed
    m_cameraState.speed = std::sqrt(velocity.x * velocity.x +
                                     velocity.y * velocity.y +
                                     velocity.z * velocity.z);
}

void VehicleCamera::SetDashboardSettings(const DashboardCameraSettings& settings) {
    m_dashboardSettings = settings;
}

void VehicleCamera::SetMotionDampening(const MotionDampeningSettings& settings) {
    m_dampeningSettings = settings;
}

VehicleCameraState VehicleCamera::GetCameraState() const {
    return m_cameraState;
}

void VehicleCamera::GetViewMatrix(float* outMatrix) const {
    if (outMatrix) {
        memcpy(outMatrix, m_viewMatrix, sizeof(float) * 16);
    }
}

void VehicleCamera::SetSeatPosition(int seatIndex) {
    m_currentSeatIndex = seatIndex;

    // Adjust seat offset based on seat index
    // 0 = driver, 1 = front passenger, 2+ = rear seats
    switch (seatIndex) {
        case 0:
            m_dashboardSettings.seatOffset = Vector3(-0.4f, 0.5f, 0.2f);
            break;
        case 1:
            m_dashboardSettings.seatOffset = Vector3(0.4f, 0.5f, 0.2f);
            break;
        case 2:
            m_dashboardSettings.seatOffset = Vector3(-0.4f, 0.5f, -0.5f);
            break;
        case 3:
            m_dashboardSettings.seatOffset = Vector3(0.4f, 0.5f, -0.5f);
            break;
        default:
            break;
    }

    LOG_DEBUG("VehicleCamera", "Seat position set to %d", seatIndex);
}

void VehicleCamera::AdjustSeatHeight(float delta) {
    m_seatHeightAdjust += delta;
    m_seatHeightAdjust = std::clamp(m_seatHeightAdjust, -0.3f, 0.3f);
}

void VehicleCamera::AdjustSeatForward(float delta) {
    m_seatForwardAdjust += delta;
    m_seatForwardAdjust = std::clamp(m_seatForwardAdjust, -0.3f, 0.3f);
}

void VehicleCamera::ResetSeatPosition() {
    m_seatHeightAdjust = 0.0f;
    m_seatForwardAdjust = 0.0f;
}

void VehicleCamera::SetMirrorsEnabled(bool enabled) {
    m_mirrorsEnabled = enabled;
}

void VehicleCamera::GetMirrorTransform(int mirrorIndex, Vector3& position,
                                        Quaternion& orientation) const {
    // Mirror positions relative to driver seat
    // 0 = left mirror, 1 = right mirror, 2 = rear-view mirror
    switch (mirrorIndex) {
        case 0: // Left
            position = Vector3(-1.0f, 0.7f, 0.8f);
            orientation = Quaternion(0.98f, 0.0f, 0.17f, 0.0f);  // Slight angle inward
            break;
        case 1: // Right
            position = Vector3(1.0f, 0.7f, 0.8f);
            orientation = Quaternion(0.98f, 0.0f, -0.17f, 0.0f);  // Slight angle inward
            break;
        case 2: // Rear-view
            position = Vector3(0.0f, 0.8f, 0.5f);
            orientation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
            break;
        default:
            position = Vector3();
            orientation = Quaternion();
            break;
    }
}

void VehicleCamera::SetSpeedBlurEnabled(bool enabled) {
    m_speedBlurEnabled = enabled;
}

void VehicleCamera::SetFOVScalingEnabled(bool enabled) {
    m_fovScalingEnabled = enabled;
}

float VehicleCamera::GetSpeedBlurIntensity() const {
    if (!m_speedBlurEnabled) {
        return 0.0f;
    }

    // Speed blur starts at ~100 km/h and increases
    float speedKmh = m_cameraState.speed * 3.6f;
    float intensity = (speedKmh - 100.0f) / 200.0f;
    return std::clamp(intensity, 0.0f, 1.0f);
}

float VehicleCamera::GetSpeedAdjustedFOV(float baseFOV) const {
    if (!m_fovScalingEnabled) {
        return baseFOV;
    }

    // Slightly increase FOV at high speeds for sense of motion
    float speedKmh = m_cameraState.speed * 3.6f;
    float fovIncrease = (speedKmh / 200.0f) * 10.0f;  // Max 10 degree increase
    return baseFOV + std::min(fovIncrease, 10.0f);
}

void VehicleCamera::SetHorizonLock(bool enabled) {
    m_horizonLock = enabled;
}

void VehicleCamera::SetVignetteOnAcceleration(bool enabled) {
    m_vignetteOnAcceleration = enabled;
}

float VehicleCamera::GetAccelerationVignetteIntensity() const {
    if (!m_vignetteOnAcceleration) {
        return 0.0f;
    }

    // Calculate acceleration magnitude
    float accelMag = std::abs(m_cameraState.acceleration);
    float intensity = accelMag / 10.0f;  // Normalize by ~1g
    return std::clamp(intensity, 0.0f, 0.5f);
}

void VehicleCamera::RegisterVehicleEnteredCallback(VehicleEnteredCallback callback) {
    if (callback) {
        m_enteredCallbacks.push_back(callback);
    }
}

void VehicleCamera::RegisterVehicleExitedCallback(VehicleExitedCallback callback) {
    if (callback) {
        m_exitedCallbacks.push_back(callback);
    }
}

void VehicleCamera::RegisterCameraModeChangedCallback(CameraModeChangedCallback callback) {
    if (callback) {
        m_modeCallbacks.push_back(callback);
    }
}

void VehicleCamera::UpdateDashboardCamera(float deltaTime) {
    // Calculate camera position in vehicle space
    Vector3 localPos = m_dashboardSettings.seatOffset;
    localPos.y += m_dashboardSettings.heightOffset + m_seatHeightAdjust;
    localPos.z += m_dashboardSettings.forwardOffset + m_seatForwardAdjust;

    // Apply VR head position offset
    if (m_vrHeadPose.isValid) {
        localPos.x += m_vrHeadPose.position.x;
        localPos.y += m_vrHeadPose.position.y;
        localPos.z += m_vrHeadPose.position.z;
    }

    // Transform to world space
    m_cameraState.position = TransformPointByVehicle(localPos);

    // Combine vehicle and head orientation
    if (m_dashboardSettings.lockToVehicle) {
        m_cameraState.orientation = CombineOrientations(
            m_smoothedOrientation,
            m_vrHeadPose.orientation);
    } else {
        m_cameraState.orientation = m_vrHeadPose.orientation;
    }

    m_cameraState.isInterior = true;
    m_cameraState.hasRoof = (m_vehicleType != VehicleType::Motorcycle &&
                             m_vehicleType != VehicleType::Bicycle);
}

void VehicleCamera::UpdateHoodCamera(float deltaTime) {
    // Hood camera is above the hood, outside the windshield
    Vector3 localPos(0.0f, 1.0f, 1.5f);  // Up and forward

    // Apply head tracking
    if (m_vrHeadPose.isValid) {
        localPos.x += m_vrHeadPose.position.x;
        localPos.y += m_vrHeadPose.position.y;
        localPos.z += m_vrHeadPose.position.z;
    }

    m_cameraState.position = TransformPointByVehicle(localPos);
    m_cameraState.orientation = CombineOrientations(
        m_smoothedOrientation,
        m_vrHeadPose.orientation);

    m_cameraState.isInterior = false;
    m_cameraState.hasRoof = false;
}

void VehicleCamera::UpdateChaseCamera(float deltaTime) {
    // Chase camera is behind and above the vehicle
    // This mode can cause motion sickness in VR
    Vector3 localPos(0.0f, 2.0f, -5.0f);  // Up and back

    m_cameraState.position = TransformPointByVehicle(localPos);

    // Look at vehicle
    // For now, just use vehicle forward
    m_cameraState.orientation = m_smoothedOrientation;

    m_cameraState.isInterior = false;
    m_cameraState.hasRoof = false;
}

void VehicleCamera::UpdateFirstPersonCamera(float deltaTime) {
    // First person camera at eye level
    Vector3 localPos = m_dashboardSettings.seatOffset;
    localPos.y += 0.1f + m_seatHeightAdjust;  // Eye level

    if (m_vrHeadPose.isValid) {
        localPos.x += m_vrHeadPose.position.x;
        localPos.y += m_vrHeadPose.position.y;
        localPos.z += m_vrHeadPose.position.z;
    }

    m_cameraState.position = TransformPointByVehicle(localPos);
    m_cameraState.orientation = CombineOrientations(
        m_smoothedOrientation,
        m_vrHeadPose.orientation);

    m_cameraState.isInterior = true;
    m_cameraState.hasRoof = (m_vehicleType != VehicleType::Motorcycle &&
                             m_vehicleType != VehicleType::Bicycle);
}

void VehicleCamera::ApplyMotionDampening(float deltaTime) {
    // Smooth position
    float posLerp = 1.0f - std::pow(m_dampeningSettings.positionSmoothing, deltaTime * 60.0f);
    m_smoothedPosition.x += (m_vehiclePosition.x - m_smoothedPosition.x) * posLerp;
    m_smoothedPosition.y += (m_vehiclePosition.y - m_smoothedPosition.y) * posLerp;
    m_smoothedPosition.z += (m_vehiclePosition.z - m_smoothedPosition.z) * posLerp;

    // Smooth orientation (simplified)
    float rotLerp = 1.0f - std::pow(m_dampeningSettings.rotationSmoothing, deltaTime * 60.0f);

    // Apply horizon lock if enabled
    if (m_horizonLock) {
        // Keep the horizon level by zeroing pitch and roll
        m_smoothedOrientation = m_vehicleOrientation;
        // Would need to extract and zero pitch/roll here
    } else {
        // Apply roll and pitch reduction
        m_smoothedOrientation = m_vehicleOrientation;
        // Would modify pitch/roll based on reduction settings
    }
}

void VehicleCamera::ComputeViewMatrix() {
    // Build view matrix from camera state
    Vector3 pos = m_cameraState.position;
    Quaternion rot = m_cameraState.orientation;

    // Convert quaternion to rotation matrix (transposed for view matrix)
    float xx = rot.x * rot.x;
    float yy = rot.y * rot.y;
    float zz = rot.z * rot.z;
    float xy = rot.x * rot.y;
    float xz = rot.x * rot.z;
    float yz = rot.y * rot.z;
    float wx = rot.w * rot.x;
    float wy = rot.w * rot.y;
    float wz = rot.w * rot.z;

    m_viewMatrix[0] = 1.0f - 2.0f * (yy + zz);
    m_viewMatrix[1] = 2.0f * (xy + wz);
    m_viewMatrix[2] = 2.0f * (xz - wy);
    m_viewMatrix[3] = 0.0f;

    m_viewMatrix[4] = 2.0f * (xy - wz);
    m_viewMatrix[5] = 1.0f - 2.0f * (xx + zz);
    m_viewMatrix[6] = 2.0f * (yz + wx);
    m_viewMatrix[7] = 0.0f;

    m_viewMatrix[8] = 2.0f * (xz + wy);
    m_viewMatrix[9] = 2.0f * (yz - wx);
    m_viewMatrix[10] = 1.0f - 2.0f * (xx + yy);
    m_viewMatrix[11] = 0.0f;

    // Translation (negative, rotated by inverse rotation)
    m_viewMatrix[12] = -(m_viewMatrix[0] * pos.x + m_viewMatrix[4] * pos.y + m_viewMatrix[8] * pos.z);
    m_viewMatrix[13] = -(m_viewMatrix[1] * pos.x + m_viewMatrix[5] * pos.y + m_viewMatrix[9] * pos.z);
    m_viewMatrix[14] = -(m_viewMatrix[2] * pos.x + m_viewMatrix[6] * pos.y + m_viewMatrix[10] * pos.z);
    m_viewMatrix[15] = 1.0f;
}

Vector3 VehicleCamera::TransformPointByVehicle(const Vector3& localPoint) const {
    // Rotate local point by vehicle orientation
    Quaternion& q = const_cast<Quaternion&>(m_smoothedOrientation);

    // q * v * q^-1
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    Vector3 rotated;
    rotated.x = (1.0f - 2.0f * (yy + zz)) * localPoint.x +
                (2.0f * (xy - wz)) * localPoint.y +
                (2.0f * (xz + wy)) * localPoint.z;
    rotated.y = (2.0f * (xy + wz)) * localPoint.x +
                (1.0f - 2.0f * (xx + zz)) * localPoint.y +
                (2.0f * (yz - wx)) * localPoint.z;
    rotated.z = (2.0f * (xz - wy)) * localPoint.x +
                (2.0f * (yz + wx)) * localPoint.y +
                (1.0f - 2.0f * (xx + yy)) * localPoint.z;

    // Add vehicle position
    rotated.x += m_smoothedPosition.x;
    rotated.y += m_smoothedPosition.y;
    rotated.z += m_smoothedPosition.z;

    return rotated;
}

Quaternion VehicleCamera::CombineOrientations(const Quaternion& vehicle,
                                               const Quaternion& head) const {
    // Apply rotation influence factor
    float influence = m_dashboardSettings.rotationInfluence;

    // Interpolate head rotation towards identity based on influence
    Quaternion identity(1.0f, 0.0f, 0.0f, 0.0f);
    Quaternion modifiedHead;

    float t = 1.0f - influence;
    float dot = head.w * identity.w + head.x * identity.x +
                head.y * identity.y + head.z * identity.z;

    if (dot > 0.9995f || t < 0.001f) {
        modifiedHead = head;
    } else {
        float theta = std::acos(std::abs(dot));
        float sinTheta = std::sin(theta);
        float wa = std::sin(t * theta) / sinTheta;
        float wb = std::sin((1.0f - t) * theta) / sinTheta;

        modifiedHead.w = wa * identity.w + wb * head.w;
        modifiedHead.x = wa * identity.x + wb * head.x;
        modifiedHead.y = wa * identity.y + wb * head.y;
        modifiedHead.z = wa * identity.z + wb * head.z;
    }

    // Combine: vehicle * modifiedHead
    Quaternion result;
    result.w = vehicle.w * modifiedHead.w - vehicle.x * modifiedHead.x -
               vehicle.y * modifiedHead.y - vehicle.z * modifiedHead.z;
    result.x = vehicle.w * modifiedHead.x + vehicle.x * modifiedHead.w +
               vehicle.y * modifiedHead.z - vehicle.z * modifiedHead.y;
    result.y = vehicle.w * modifiedHead.y - vehicle.x * modifiedHead.z +
               vehicle.y * modifiedHead.w + vehicle.z * modifiedHead.x;
    result.z = vehicle.w * modifiedHead.z + vehicle.x * modifiedHead.y -
               vehicle.y * modifiedHead.x + vehicle.z * modifiedHead.w;

    return result;
}

} // namespace GTA5VR
