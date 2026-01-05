#pragma once

#include "HeadTracking.h"
#include <functional>
#include <vector>
#include <string>

namespace GTA5VR {

// Controller hand
enum class ControllerHand {
    Left = 0,
    Right = 1,
    Count = 2
};

// Button IDs (unified across platforms)
enum class ControllerButton {
    Trigger,
    Grip,
    Menu,
    System,
    Primary,        // A/X button
    Secondary,      // B/Y button
    Thumbstick,     // Thumbstick press
    ThumbstickTouch,
    TriggerTouch,
    GripTouch,
    Count
};

// Axis IDs
enum class ControllerAxis {
    Trigger,        // 0.0 - 1.0
    Grip,           // 0.0 - 1.0
    ThumbstickX,    // -1.0 to 1.0
    ThumbstickY,    // -1.0 to 1.0
    Count
};

// Controller state
struct ControllerState {
    // Pose
    Vector3 position;
    Quaternion orientation;
    Vector3 velocity;
    Vector3 angularVelocity;

    // Buttons (bitmask)
    uint32_t buttonsPressed = 0;
    uint32_t buttonsTouched = 0;

    // Axes
    float axes[static_cast<size_t>(ControllerAxis::Count)] = {};

    // Tracking state
    bool isTracking = false;
    bool isConnected = false;
};

// Haptic feedback parameters
struct HapticParams {
    float amplitude = 1.0f;     // 0.0 - 1.0
    float duration = 0.1f;      // Seconds
    float frequency = 160.0f;   // Hz
};

// Input binding for game actions
struct InputBinding {
    std::string actionName;
    ControllerHand hand;
    ControllerButton button;
    float threshold = 0.5f;     // For analog buttons
    bool continuous = false;    // Fire every frame while held
};

// Callbacks
using ButtonCallback = std::function<void(ControllerHand, ControllerButton, bool pressed)>;
using AxisCallback = std::function<void(ControllerHand, ControllerAxis, float value)>;
using PoseCallback = std::function<void(ControllerHand, const ControllerState&)>;

class MotionController {
public:
    MotionController();
    ~MotionController();

    // Prevent copying
    MotionController(const MotionController&) = delete;
    MotionController& operator=(const MotionController&) = delete;

    // Update from VR runtime
    void UpdateControllerState(ControllerHand hand, const ControllerState& state);

    // State queries
    ControllerState GetControllerState(ControllerHand hand) const;
    bool IsButtonPressed(ControllerHand hand, ControllerButton button) const;
    bool IsButtonTouched(ControllerHand hand, ControllerButton button) const;
    bool IsButtonJustPressed(ControllerHand hand, ControllerButton button) const;
    bool IsButtonJustReleased(ControllerHand hand, ControllerButton button) const;
    float GetAxisValue(ControllerHand hand, ControllerAxis axis) const;

    // Pose queries
    Vector3 GetControllerPosition(ControllerHand hand) const;
    Quaternion GetControllerOrientation(ControllerHand hand) const;
    Vector3 GetControllerForward(ControllerHand hand) const;
    Vector3 GetAimDirection(ControllerHand hand) const;

    // Haptic feedback
    void TriggerHaptic(ControllerHand hand, const HapticParams& params);
    void TriggerHaptic(ControllerHand hand, float amplitude, float duration);
    void StopHaptic(ControllerHand hand);

    // Input bindings
    void AddBinding(const InputBinding& binding);
    void RemoveBinding(const std::string& actionName);
    void ClearBindings();
    bool IsActionActive(const std::string& actionName) const;

    // Callbacks
    void RegisterButtonCallback(ButtonCallback callback);
    void RegisterAxisCallback(AxisCallback callback);
    void RegisterPoseCallback(PoseCallback callback);

    // Gesture detection
    bool IsGripping(ControllerHand hand) const;
    bool IsPointing(ControllerHand hand) const;
    bool IsFist(ControllerHand hand) const;
    bool IsThumbUp(ControllerHand hand) const;

    // Dead zones and sensitivity
    void SetThumbstickDeadzone(float deadzone);
    void SetTriggerThreshold(float threshold);
    void SetGripThreshold(float threshold);

    // Controller visualization
    void SetShowControllers(bool show) { m_showControllers = show; }
    bool ShouldShowControllers() const { return m_showControllers; }

    // Connection status
    bool IsConnected(ControllerHand hand) const;
    bool IsTracking(ControllerHand hand) const;

    // Dominant hand
    void SetDominantHand(ControllerHand hand) { m_dominantHand = hand; }
    ControllerHand GetDominantHand() const { return m_dominantHand; }

    // World-adjusted positions
    void SetWorldTransform(const Vector3& offset, float yawDegrees);
    Vector3 GetWorldPosition(ControllerHand hand) const;
    Quaternion GetWorldOrientation(ControllerHand hand) const;

private:
    void ProcessButtonChange(ControllerHand hand, ControllerButton button, bool pressed);
    void ProcessAxisChange(ControllerHand hand, ControllerAxis axis, float oldValue, float newValue);
    float ApplyDeadzone(float value, float deadzone) const;

private:
    mutable std::mutex m_mutex;

    // Controller states
    ControllerState m_states[static_cast<size_t>(ControllerHand::Count)];
    ControllerState m_previousStates[static_cast<size_t>(ControllerHand::Count)];

    // Callbacks
    std::vector<ButtonCallback> m_buttonCallbacks;
    std::vector<AxisCallback> m_axisCallbacks;
    std::vector<PoseCallback> m_poseCallbacks;

    // Input bindings
    std::vector<InputBinding> m_bindings;

    // Settings
    float m_thumbstickDeadzone = 0.15f;
    float m_triggerThreshold = 0.1f;
    float m_gripThreshold = 0.5f;
    bool m_showControllers = true;
    ControllerHand m_dominantHand = ControllerHand::Right;

    // World transform
    Vector3 m_worldOffset;
    float m_worldYaw = 0.0f;

    // Pending haptics (for runtime to process)
    struct PendingHaptic {
        ControllerHand hand;
        HapticParams params;
        bool pending = false;
    };
    PendingHaptic m_pendingHaptics[static_cast<size_t>(ControllerHand::Count)];
};

} // namespace GTA5VR
