#pragma once

#include <cstdint>
#include <array>
#include <mutex>

namespace GTA5VR {

// 3D vector
struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vector3() = default;
    Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

// Quaternion for rotation
struct Quaternion {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Quaternion() = default;
    Quaternion(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}
};

// Head pose data
struct HeadPose {
    Vector3 position;
    Quaternion orientation;
    Vector3 velocity;
    Vector3 angularVelocity;
    bool isValid = false;
    uint64_t timestamp = 0;
};

// Eye-specific pose (includes IPD offset)
struct EyePose {
    Vector3 position;
    Quaternion orientation;
    float projectionMatrix[16];
    float viewMatrix[16];
};

// Tracking space origin
enum class TrackingOrigin {
    Seated,         // Seated experience, origin at headset startup position
    Standing,       // Standing experience, origin at floor level
    RoomScale       // Room-scale, origin at center of play area
};

class HeadTracking {
public:
    HeadTracking();
    ~HeadTracking();

    // Prevent copying
    HeadTracking(const HeadTracking&) = delete;
    HeadTracking& operator=(const HeadTracking&) = delete;

    // Update tracking from VR runtime
    void UpdatePose(const HeadPose& pose);
    void UpdateEyePose(uint32_t eye, const EyePose& pose);

    // Get current tracking data
    HeadPose GetHeadPose() const;
    EyePose GetEyePose(uint32_t eye) const;
    Vector3 GetHeadPosition() const;
    Quaternion GetHeadOrientation() const;

    // Euler angles (in degrees)
    void GetHeadEulerAngles(float& pitch, float& yaw, float& roll) const;

    // View direction
    Vector3 GetForwardVector() const;
    Vector3 GetRightVector() const;
    Vector3 GetUpVector() const;

    // Tracking origin management
    void SetTrackingOrigin(TrackingOrigin origin);
    TrackingOrigin GetTrackingOrigin() const { return m_trackingOrigin; }

    // Recenter/reset
    void RecenterView();
    void SetWorldOffset(const Vector3& offset);
    void SetWorldRotationOffset(float yawDegrees);

    // Get world-adjusted pose
    Vector3 GetWorldPosition() const;
    Quaternion GetWorldOrientation() const;

    // IPD (Inter-Pupillary Distance)
    void SetIPD(float ipd);
    float GetIPD() const { return m_ipd; }

    // World scale
    void SetWorldScale(float scale);
    float GetWorldScale() const { return m_worldScale; }

    // Prediction
    HeadPose PredictPose(float secondsFromNow) const;

    // Smoothing (for comfort)
    void SetPositionSmoothing(float factor);
    void SetRotationSmoothing(float factor);

    // Validity
    bool IsTrackingValid() const;
    uint64_t GetLastUpdateTimestamp() const;

    // Matrix generation
    void GetViewMatrix(uint32_t eye, float* outMatrix) const;
    void GetProjectionMatrix(uint32_t eye, float nearPlane, float farPlane,
                            float* outMatrix) const;

private:
    // Quaternion math helpers
    static Quaternion QuaternionMultiply(const Quaternion& a, const Quaternion& b);
    static Quaternion QuaternionFromEuler(float pitch, float yaw, float roll);
    static void QuaternionToEuler(const Quaternion& q, float& pitch, float& yaw, float& roll);
    static Quaternion QuaternionSlerp(const Quaternion& a, const Quaternion& b, float t);
    static Vector3 RotateVectorByQuaternion(const Vector3& v, const Quaternion& q);

    // Matrix helpers
    static void CreateViewMatrix(const Vector3& position, const Quaternion& orientation,
                                 float* outMatrix);

private:
    mutable std::mutex m_mutex;

    // Current pose
    HeadPose m_currentPose;
    std::array<EyePose, 2> m_eyePoses;

    // Previous pose for smoothing
    HeadPose m_previousPose;

    // World offsets
    Vector3 m_worldOffset;
    float m_worldYawOffset = 0.0f;  // Degrees
    Vector3 m_recenterOffset;
    float m_recenterYaw = 0.0f;

    // Settings
    TrackingOrigin m_trackingOrigin = TrackingOrigin::Standing;
    float m_ipd = 0.063f;  // 63mm default
    float m_worldScale = 1.0f;
    float m_positionSmoothing = 0.0f;
    float m_rotationSmoothing = 0.0f;
};

} // namespace GTA5VR
