#include "CutsceneHandler.h"
#include "../core/Logger.h"
#include <cmath>
#include <algorithm>

namespace GTA5VR {

constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;

CutsceneHandler::CutsceneHandler() {
    LOG_DEBUG("CutsceneHandler", "Constructor called");
}

CutsceneHandler::~CutsceneHandler() {
    Shutdown();
}

bool CutsceneHandler::Initialize() {
    if (m_initialized) {
        return true;
    }

    LOG_INFO("CutsceneHandler", "Initializing cutscene handler");

    // Initialize theater screen position (in front of player)
    m_theaterScreenPosition = Vector3(0.0f, 1.5f, -m_theaterSettings.distance);
    m_theaterScreenOrientation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);

    m_initialized = true;
    LOG_INFO("CutsceneHandler", "Cutscene handler initialized");
    return true;
}

void CutsceneHandler::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("CutsceneHandler", "Shutting down cutscene handler");

    m_startCallbacks.clear();
    m_endCallbacks.clear();
    m_cameraCallbacks.clear();

    m_initialized = false;
}

void CutsceneHandler::Update(float deltaTime) {
    if (!m_initialized || m_state == CutsceneState::None) {
        return;
    }

    // Update camera blend
    if (m_cameraBlendTime < m_cameraBlendDuration) {
        m_cameraBlendTime += deltaTime;
    }

    // Update based on mode
    switch (m_mode) {
        case CutsceneMode::VRAdapted:
            UpdateVRAdaptedMode(deltaTime);
            break;
        case CutsceneMode::Theater:
            UpdateTheaterMode(deltaTime);
            break;
        case CutsceneMode::FirstPerson:
            UpdateFirstPersonMode(deltaTime);
            break;
        case CutsceneMode::Skip:
            if (CanSkip()) {
                RequestSkip();
            }
            break;
        case CutsceneMode::Standard:
        default:
            // Standard mode - no special handling
            break;
    }
}

void CutsceneHandler::SetCutsceneMode(CutsceneMode mode) {
    m_mode = mode;
    LOG_INFO("CutsceneHandler", "Cutscene mode set to %d", static_cast<int>(mode));
}

CutsceneCameraInfo CutsceneHandler::GetCurrentCamera() const {
    // If blending, interpolate between cameras
    if (m_cameraBlendTime < m_cameraBlendDuration && m_cameraBlendDuration > 0.0f) {
        float t = m_cameraBlendTime / m_cameraBlendDuration;
        t = t * t * (3.0f - 2.0f * t);  // Smoothstep

        CutsceneCameraInfo blended;
        blended.position = LerpPosition(m_previousCamera.position, m_currentCamera.position, t);
        blended.lookAt = LerpPosition(m_previousCamera.lookAt, m_currentCamera.lookAt, t);
        blended.fov = m_previousCamera.fov + (m_currentCamera.fov - m_previousCamera.fov) * t;
        blended.nearPlane = m_currentCamera.nearPlane;
        blended.farPlane = m_currentCamera.farPlane;
        blended.isFirstPerson = m_currentCamera.isFirstPerson;
        blended.isInterior = m_currentCamera.isInterior;

        return blended;
    }

    return m_currentCamera;
}

void CutsceneHandler::SetVRHeadPose(const HeadPose& pose) {
    m_vrHeadPose = pose;
}

void CutsceneHandler::SetHeadRotationLimits(float pitchLimit, float yawLimit) {
    m_pitchLimit = std::clamp(pitchLimit, 0.0f, 90.0f);
    m_yawLimit = std::clamp(yawLimit, 0.0f, 180.0f);
}

void CutsceneHandler::SetHeadPositionLimits(float xLimit, float yLimit, float zLimit) {
    m_positionLimitX = std::max(0.0f, xLimit);
    m_positionLimitY = std::max(0.0f, yLimit);
    m_positionLimitZ = std::max(0.0f, zLimit);
}

void CutsceneHandler::SetTheaterSettings(const TheaterScreenSettings& settings) {
    m_theaterSettings = settings;

    // Update screen position based on new distance
    m_theaterScreenPosition.z = -m_theaterSettings.distance;
}

void CutsceneHandler::GetTheaterScreenTransform(Vector3& position, Quaternion& orientation,
                                                 float& width, float& height) const {
    position = m_theaterScreenPosition;
    orientation = m_theaterScreenOrientation;
    width = m_theaterSettings.width;
    height = m_theaterSettings.height;
}

void CutsceneHandler::RequestSkip() {
    if (!CanSkip()) {
        return;
    }

    LOG_INFO("CutsceneHandler", "Cutscene skip requested");
    m_state = CutsceneState::Skipping;

    // In a real implementation, this would trigger the game's skip mechanism
}

bool CutsceneHandler::CanSkip() const {
    // Most cutscenes can be skipped after a short delay
    return m_state == CutsceneState::Playing && m_progress > 0.1f;
}

void CutsceneHandler::SetAutoSkip(bool enable) {
    m_autoSkip = enable;
    LOG_INFO("CutsceneHandler", "Auto-skip %s", enable ? "enabled" : "disabled");
}

void CutsceneHandler::SetFadeOnCameraChange(bool enable) {
    m_fadeOnCameraChange = enable;
}

void CutsceneHandler::SetMaxCameraSpeed(float metersPerSecond) {
    m_maxCameraSpeed = std::max(0.1f, metersPerSecond);
}

void CutsceneHandler::SetSnapToCamera(bool enable) {
    m_snapToCamera = enable;
}

void CutsceneHandler::RegisterCutsceneStartCallback(CutsceneStartCallback callback) {
    if (callback) {
        m_startCallbacks.push_back(callback);
    }
}

void CutsceneHandler::RegisterCutsceneEndCallback(CutsceneEndCallback callback) {
    if (callback) {
        m_endCallbacks.push_back(callback);
    }
}

void CutsceneHandler::RegisterCameraChangeCallback(CameraChangeCallback callback) {
    if (callback) {
        m_cameraCallbacks.push_back(callback);
    }
}

void CutsceneHandler::SimulateCutsceneStart(const std::string& name) {
    OnCutsceneStart(name);
}

void CutsceneHandler::SimulateCutsceneEnd() {
    OnCutsceneEnd();
}

void CutsceneHandler::OnCutsceneStart(const std::string& name) {
    LOG_INFO("CutsceneHandler", "Cutscene started: %s", name.c_str());

    m_state = CutsceneState::Starting;
    m_currentCutsceneName = name;
    m_progress = 0.0f;

    // Notify callbacks
    for (auto& callback : m_startCallbacks) {
        if (callback) {
            callback(name);
        }
    }

    m_state = CutsceneState::Playing;
}

void CutsceneHandler::OnCutsceneEnd() {
    LOG_INFO("CutsceneHandler", "Cutscene ended: %s", m_currentCutsceneName.c_str());

    m_state = CutsceneState::Ending;

    // Notify callbacks
    for (auto& callback : m_endCallbacks) {
        if (callback) {
            callback();
        }
    }

    m_state = CutsceneState::None;
    m_currentCutsceneName.clear();
    m_progress = 0.0f;
}

void CutsceneHandler::OnCameraChange(const CutsceneCameraInfo& camera) {
    m_previousCamera = m_currentCamera;
    m_currentCamera = camera;
    m_cameraBlendTime = 0.0f;

    LOG_DEBUG("CutsceneHandler", "Camera changed: pos(%.2f, %.2f, %.2f) fov=%.1f",
              camera.position.x, camera.position.y, camera.position.z, camera.fov);

    // Notify callbacks
    for (auto& callback : m_cameraCallbacks) {
        if (callback) {
            callback(camera);
        }
    }
}

void CutsceneHandler::UpdateVRAdaptedMode(float deltaTime) {
    // In VR Adapted mode, we allow head tracking within limits
    // The camera position follows the cutscene but orientation is influenced by head tracking

    if (!m_vrHeadPose.isValid) {
        return;
    }

    // Apply head tracking with limits
    Vector3 adjustedPosition = m_vrHeadPose.position;
    Quaternion adjustedOrientation = m_vrHeadPose.orientation;

    ApplyHeadTrackingLimits(adjustedPosition, adjustedOrientation);

    // The modified pose would be used to offset the cutscene camera
    // This allows looking around slightly while maintaining the cutscene framing
}

void CutsceneHandler::UpdateTheaterMode(float deltaTime) {
    // In Theater mode, the cutscene plays on a virtual screen
    // The player can look around freely in their virtual theater environment

    // Update theater screen position relative to player's initial facing
    // (Screen stays fixed in world space)
}

void CutsceneHandler::UpdateFirstPersonMode(float deltaTime) {
    // In First Person mode, we try to force the camera to the character's eyes
    // This may not work for all cutscenes

    if (!m_currentCamera.isFirstPerson) {
        // Attempt to reposition camera to character's head position
        // This would require game-specific hooks to get character position
    }
}

void CutsceneHandler::ApplyHeadTrackingLimits(Vector3& position, Quaternion& orientation) {
    // Clamp position
    position.x = std::clamp(position.x, -m_positionLimitX, m_positionLimitX);
    position.y = std::clamp(position.y, -m_positionLimitY, m_positionLimitY);
    position.z = std::clamp(position.z, -m_positionLimitZ, m_positionLimitZ);

    // Extract euler angles and clamp rotation
    float pitch, yaw, roll;

    // Convert quaternion to euler (simplified)
    float sinp = 2.0f * (orientation.w * orientation.y - orientation.z * orientation.x);
    if (std::abs(sinp) >= 1.0f) {
        pitch = std::copysign(3.14159f / 2.0f, sinp);
    } else {
        pitch = std::asin(sinp);
    }

    float siny = 2.0f * (orientation.w * orientation.z + orientation.x * orientation.y);
    float cosy = 1.0f - 2.0f * (orientation.y * orientation.y + orientation.z * orientation.z);
    yaw = std::atan2(siny, cosy);

    float sinr = 2.0f * (orientation.w * orientation.x + orientation.y * orientation.z);
    float cosr = 1.0f - 2.0f * (orientation.x * orientation.x + orientation.y * orientation.y);
    roll = std::atan2(sinr, cosr);

    // Convert to degrees and clamp
    pitch *= RAD_TO_DEG;
    yaw *= RAD_TO_DEG;
    roll *= RAD_TO_DEG;

    pitch = std::clamp(pitch, -m_pitchLimit, m_pitchLimit);
    yaw = std::clamp(yaw, -m_yawLimit, m_yawLimit);
    // Roll is usually left unclamped

    // Convert back to quaternion
    pitch *= DEG_TO_RAD;
    yaw *= DEG_TO_RAD;
    roll *= DEG_TO_RAD;

    float cp = std::cos(pitch * 0.5f);
    float sp = std::sin(pitch * 0.5f);
    float cy = std::cos(yaw * 0.5f);
    float sy = std::sin(yaw * 0.5f);
    float cr = std::cos(roll * 0.5f);
    float sr = std::sin(roll * 0.5f);

    orientation.w = cp * cy * cr + sp * sy * sr;
    orientation.x = sp * cy * cr - cp * sy * sr;
    orientation.y = cp * sy * cr + sp * cy * sr;
    orientation.z = cp * cy * sr - sp * sy * cr;
}

Vector3 CutsceneHandler::LerpPosition(const Vector3& from, const Vector3& to, float t) {
    return Vector3(
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
        from.z + (to.z - from.z) * t
    );
}

Quaternion CutsceneHandler::SlerpOrientation(const Quaternion& from, const Quaternion& to, float t) {
    // Simplified slerp
    float dot = from.w * to.w + from.x * to.x + from.y * to.y + from.z * to.z;

    Quaternion to2 = to;
    if (dot < 0.0f) {
        dot = -dot;
        to2.w = -to2.w;
        to2.x = -to2.x;
        to2.y = -to2.y;
        to2.z = -to2.z;
    }

    if (dot > 0.9995f) {
        // Linear interpolation for very close quaternions
        Quaternion result;
        result.w = from.w + t * (to2.w - from.w);
        result.x = from.x + t * (to2.x - from.x);
        result.y = from.y + t * (to2.y - from.y);
        result.z = from.z + t * (to2.z - from.z);

        // Normalize
        float len = std::sqrt(result.w * result.w + result.x * result.x +
                              result.y * result.y + result.z * result.z);
        result.w /= len;
        result.x /= len;
        result.y /= len;
        result.z /= len;

        return result;
    }

    float theta = std::acos(dot);
    float sinTheta = std::sin(theta);
    float wa = std::sin((1.0f - t) * theta) / sinTheta;
    float wb = std::sin(t * theta) / sinTheta;

    Quaternion result;
    result.w = wa * from.w + wb * to2.w;
    result.x = wa * from.x + wb * to2.x;
    result.y = wa * from.y + wb * to2.y;
    result.z = wa * from.z + wb * to2.z;

    return result;
}

} // namespace GTA5VR
