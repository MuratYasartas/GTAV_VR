#pragma once

#include "../input/HeadTracking.h"
#include <functional>
#include <string>

namespace GTA5VR {

// Vehicle camera mode options
enum class VehicleCameraMode {
    Dashboard,          // Inside vehicle, at dashboard level
    Hood,               // Above hood, outside windshield
    Chase,              // Third person chase camera (may cause motion sickness)
    FirstPersonDriver,  // At driver's eye level
    FirstPersonPassenger, // For passengers in multiplayer
    Cinematic,          // Cinematic angles
    FreeCamera          // User-controlled camera position
};

// Vehicle type (affects camera behavior)
enum class VehicleType {
    Unknown,
    Car,
    Motorcycle,
    Bicycle,
    Boat,
    Helicopter,
    Plane,
    Train,
    Tank
};

// Vehicle camera state
struct VehicleCameraState {
    Vector3 position;
    Quaternion orientation;
    float fov;
    bool isInterior;
    bool hasRoof;
    float speed;            // Current vehicle speed (m/s)
    float acceleration;     // Current acceleration
    Vector3 vehicleUp;      // Vehicle's up vector
    Vector3 vehicleForward; // Vehicle's forward vector
};

// Dashboard camera settings
struct DashboardCameraSettings {
    Vector3 seatOffset;         // Offset from vehicle center
    float heightOffset = 0.0f;  // Additional height adjustment
    float forwardOffset = 0.0f; // Move forward/back from seat
    bool lockToVehicle = true;  // Lock rotation to vehicle
    float rotationInfluence = 0.3f; // How much head rotation affects view (0-1)
};

// Motion dampening settings
struct MotionDampeningSettings {
    float positionSmoothing = 0.5f;   // Smooth position changes
    float rotationSmoothing = 0.3f;   // Smooth rotation changes
    float accelerationDampening = 0.5f; // Reduce felt acceleration
    float rollReduction = 0.7f;       // Reduce vehicle roll effect
    float pitchReduction = 0.5f;      // Reduce vehicle pitch effect
    bool decouplePitch = true;        // Decouple head pitch from vehicle
    bool decoupleRoll = true;         // Decouple head roll from vehicle
};

// Callbacks
using VehicleEnteredCallback = std::function<void(VehicleType type)>;
using VehicleExitedCallback = std::function<void()>;
using CameraModeChangedCallback = std::function<void(VehicleCameraMode mode)>;

class VehicleCamera {
public:
    VehicleCamera();
    ~VehicleCamera();

    // Prevent copying
    VehicleCamera(const VehicleCamera&) = delete;
    VehicleCamera& operator=(const VehicleCamera&) = delete;

    // Initialization
    bool Initialize();
    void Shutdown();

    // Update (called every frame when in vehicle)
    void Update(float deltaTime);

    // Vehicle state
    void SetInVehicle(bool inVehicle, VehicleType type = VehicleType::Unknown);
    bool IsInVehicle() const { return m_inVehicle; }
    VehicleType GetVehicleType() const { return m_vehicleType; }

    // Camera mode
    void SetCameraMode(VehicleCameraMode mode);
    VehicleCameraMode GetCameraMode() const { return m_cameraMode; }
    VehicleCameraMode GetDefaultModeForVehicle(VehicleType type) const;

    // VR head pose
    void SetVRHeadPose(const HeadPose& pose);

    // Vehicle transform (from game)
    void SetVehicleTransform(const Vector3& position, const Quaternion& orientation);
    void SetVehicleVelocity(const Vector3& velocity, const Vector3& angularVelocity);

    // Dashboard settings
    void SetDashboardSettings(const DashboardCameraSettings& settings);
    DashboardCameraSettings GetDashboardSettings() const { return m_dashboardSettings; }

    // Motion dampening
    void SetMotionDampening(const MotionDampeningSettings& settings);
    MotionDampeningSettings GetMotionDampening() const { return m_dampeningSettings; }

    // Get computed camera
    VehicleCameraState GetCameraState() const;
    void GetViewMatrix(float* outMatrix) const;

    // Seat position presets
    void SetSeatPosition(int seatIndex);
    void AdjustSeatHeight(float delta);
    void AdjustSeatForward(float delta);
    void ResetSeatPosition();

    // Mirrors
    void SetMirrorsEnabled(bool enabled);
    bool AreMirrorsEnabled() const { return m_mirrorsEnabled; }
    void GetMirrorTransform(int mirrorIndex, Vector3& position, Quaternion& orientation) const;

    // Speed-based effects
    void SetSpeedBlurEnabled(bool enabled);
    void SetFOVScalingEnabled(bool enabled);
    float GetSpeedBlurIntensity() const;
    float GetSpeedAdjustedFOV(float baseFOV) const;

    // Comfort options
    void SetHorizonLock(bool enabled);
    void SetVignetteOnAcceleration(bool enabled);
    float GetAccelerationVignetteIntensity() const;

    // Callbacks
    void RegisterVehicleEnteredCallback(VehicleEnteredCallback callback);
    void RegisterVehicleExitedCallback(VehicleExitedCallback callback);
    void RegisterCameraModeChangedCallback(CameraModeChangedCallback callback);

private:
    void UpdateDashboardCamera(float deltaTime);
    void UpdateHoodCamera(float deltaTime);
    void UpdateChaseCamera(float deltaTime);
    void UpdateFirstPersonCamera(float deltaTime);

    void ApplyMotionDampening(float deltaTime);
    void ComputeViewMatrix();

    // Helper functions
    Vector3 TransformPointByVehicle(const Vector3& localPoint) const;
    Quaternion CombineOrientations(const Quaternion& vehicle, const Quaternion& head) const;

private:
    // State
    bool m_inVehicle = false;
    VehicleType m_vehicleType = VehicleType::Unknown;
    VehicleCameraMode m_cameraMode = VehicleCameraMode::Dashboard;

    // VR head pose
    HeadPose m_vrHeadPose;

    // Vehicle transform
    Vector3 m_vehiclePosition;
    Quaternion m_vehicleOrientation;
    Vector3 m_vehicleVelocity;
    Vector3 m_vehicleAngularVelocity;

    // Settings
    DashboardCameraSettings m_dashboardSettings;
    MotionDampeningSettings m_dampeningSettings;

    // Current seat adjustments
    float m_seatHeightAdjust = 0.0f;
    float m_seatForwardAdjust = 0.0f;
    int m_currentSeatIndex = 0;

    // Computed camera state
    VehicleCameraState m_cameraState;
    float m_viewMatrix[16];

    // Smoothed values for dampening
    Vector3 m_smoothedPosition;
    Quaternion m_smoothedOrientation;

    // Features
    bool m_mirrorsEnabled = true;
    bool m_speedBlurEnabled = false;
    bool m_fovScalingEnabled = true;
    bool m_horizonLock = false;
    bool m_vignetteOnAcceleration = true;

    // Callbacks
    std::vector<VehicleEnteredCallback> m_enteredCallbacks;
    std::vector<VehicleExitedCallback> m_exitedCallbacks;
    std::vector<CameraModeChangedCallback> m_modeCallbacks;

    bool m_initialized = false;
};

} // namespace GTA5VR
