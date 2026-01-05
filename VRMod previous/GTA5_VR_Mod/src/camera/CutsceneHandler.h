#pragma once

#include "../input/HeadTracking.h"
#include <functional>
#include <string>

namespace GTA5VR {

// Cutscene VR mode options
enum class CutsceneMode {
    Standard,       // Normal game cutscene (may cause motion sickness)
    VRAdapted,      // Camera follows head but with limits
    Theater,        // Watch cutscene on virtual screen
    FirstPerson,    // Force first-person perspective
    Skip            // Automatically skip cutscenes
};

// Cutscene state
enum class CutsceneState {
    None,
    Starting,
    Playing,
    Ending,
    Skipping
};

// Cutscene camera info
struct CutsceneCameraInfo {
    Vector3 position;
    Vector3 lookAt;
    float fov;
    float nearPlane;
    float farPlane;
    bool isFirstPerson;
    bool isInterior;
};

// Theater screen settings
struct TheaterScreenSettings {
    float distance = 5.0f;      // Meters from player
    float width = 8.0f;         // Screen width in meters
    float height = 4.5f;        // Screen height (16:9 aspect)
    float curvature = 0.1f;     // 0.0 = flat, 1.0 = full curve
    float brightness = 1.0f;
    bool showFrame = true;
};

// Callbacks
using CutsceneStartCallback = std::function<void(const std::string& cutsceneName)>;
using CutsceneEndCallback = std::function<void()>;
using CameraChangeCallback = std::function<void(const CutsceneCameraInfo& camera)>;

class CutsceneHandler {
public:
    CutsceneHandler();
    ~CutsceneHandler();

    // Prevent copying
    CutsceneHandler(const CutsceneHandler&) = delete;
    CutsceneHandler& operator=(const CutsceneHandler&) = delete;

    // Initialization
    bool Initialize();
    void Shutdown();

    // Update (called every frame)
    void Update(float deltaTime);

    // Mode settings
    void SetCutsceneMode(CutsceneMode mode);
    CutsceneMode GetCutsceneMode() const { return m_mode; }

    // State queries
    bool IsInCutscene() const { return m_state != CutsceneState::None; }
    CutsceneState GetState() const { return m_state; }
    float GetProgress() const { return m_progress; }
    std::string GetCurrentCutsceneName() const { return m_currentCutsceneName; }

    // Camera management
    CutsceneCameraInfo GetCurrentCamera() const;
    void SetVRHeadPose(const HeadPose& pose);

    // Head tracking limits for VR Adapted mode
    void SetHeadRotationLimits(float pitchLimit, float yawLimit);
    void SetHeadPositionLimits(float xLimit, float yLimit, float zLimit);

    // Theater mode settings
    void SetTheaterSettings(const TheaterScreenSettings& settings);
    TheaterScreenSettings GetTheaterSettings() const { return m_theaterSettings; }

    // Get theater screen transform for rendering
    void GetTheaterScreenTransform(Vector3& position, Quaternion& orientation,
                                    float& width, float& height) const;

    // Skip control
    void RequestSkip();
    bool CanSkip() const;
    void SetAutoSkip(bool enable);

    // Comfort settings
    void SetFadeOnCameraChange(bool enable);
    void SetMaxCameraSpeed(float metersPerSecond);
    void SetSnapToCamera(bool enable);

    // Callbacks
    void RegisterCutsceneStartCallback(CutsceneStartCallback callback);
    void RegisterCutsceneEndCallback(CutsceneEndCallback callback);
    void RegisterCameraChangeCallback(CameraChangeCallback callback);

    // Manual state control (for testing)
    void SimulateCutsceneStart(const std::string& name);
    void SimulateCutsceneEnd();

private:
    void OnCutsceneStart(const std::string& name);
    void OnCutsceneEnd();
    void OnCameraChange(const CutsceneCameraInfo& camera);

    void UpdateVRAdaptedMode(float deltaTime);
    void UpdateTheaterMode(float deltaTime);
    void UpdateFirstPersonMode(float deltaTime);

    void ApplyHeadTrackingLimits(Vector3& position, Quaternion& orientation);

    Vector3 LerpPosition(const Vector3& from, const Vector3& to, float t);
    Quaternion SlerpOrientation(const Quaternion& from, const Quaternion& to, float t);

private:
    // Mode and state
    CutsceneMode m_mode = CutsceneMode::VRAdapted;
    CutsceneState m_state = CutsceneState::None;
    float m_progress = 0.0f;
    std::string m_currentCutsceneName;

    // Current camera
    CutsceneCameraInfo m_currentCamera;
    CutsceneCameraInfo m_previousCamera;
    float m_cameraBlendTime = 0.0f;
    float m_cameraBlendDuration = 0.3f;

    // VR head pose
    HeadPose m_vrHeadPose;

    // Head tracking limits
    float m_pitchLimit = 45.0f;     // Degrees
    float m_yawLimit = 60.0f;       // Degrees
    float m_positionLimitX = 0.3f;  // Meters
    float m_positionLimitY = 0.2f;
    float m_positionLimitZ = 0.3f;

    // Theater mode
    TheaterScreenSettings m_theaterSettings;
    Vector3 m_theaterScreenPosition;
    Quaternion m_theaterScreenOrientation;

    // Settings
    bool m_autoSkip = false;
    bool m_fadeOnCameraChange = true;
    float m_maxCameraSpeed = 5.0f;
    bool m_snapToCamera = false;

    // Callbacks
    std::vector<CutsceneStartCallback> m_startCallbacks;
    std::vector<CutsceneEndCallback> m_endCallbacks;
    std::vector<CameraChangeCallback> m_cameraCallbacks;

    bool m_initialized = false;
};

} // namespace GTA5VR
