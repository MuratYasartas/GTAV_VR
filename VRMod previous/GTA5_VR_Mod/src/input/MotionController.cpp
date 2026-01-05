#include "MotionController.h"
#include "../core/Logger.h"
#include <cmath>
#include <algorithm>

namespace GTA5VR {

constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

MotionController::MotionController() {
    LOG_DEBUG("MotionController", "Constructor called");
}

MotionController::~MotionController() {
    LOG_DEBUG("MotionController", "Destructor called");
}

void MotionController::UpdateControllerState(ControllerHand hand, const ControllerState& state) {
    if (static_cast<size_t>(hand) >= static_cast<size_t>(ControllerHand::Count)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    size_t index = static_cast<size_t>(hand);
    m_previousStates[index] = m_states[index];
    m_states[index] = state;

    // Process button changes
    uint32_t buttonChanges = m_states[index].buttonsPressed ^ m_previousStates[index].buttonsPressed;
    for (uint32_t i = 0; i < static_cast<uint32_t>(ControllerButton::Count); i++) {
        uint32_t mask = 1u << i;
        if (buttonChanges & mask) {
            bool pressed = (m_states[index].buttonsPressed & mask) != 0;
            ProcessButtonChange(hand, static_cast<ControllerButton>(i), pressed);
        }
    }

    // Process axis changes
    for (size_t i = 0; i < static_cast<size_t>(ControllerAxis::Count); i++) {
        float oldValue = m_previousStates[index].axes[i];
        float newValue = m_states[index].axes[i];
        if (std::abs(newValue - oldValue) > 0.01f) {
            ProcessAxisChange(hand, static_cast<ControllerAxis>(i), oldValue, newValue);
        }
    }

    // Notify pose callbacks
    for (auto& callback : m_poseCallbacks) {
        if (callback) {
            callback(hand, m_states[index]);
        }
    }
}

ControllerState MotionController::GetControllerState(ControllerHand hand) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    if (index < static_cast<size_t>(ControllerHand::Count)) {
        return m_states[index];
    }
    return ControllerState();
}

bool MotionController::IsButtonPressed(ControllerHand hand, ControllerButton button) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    if (index >= static_cast<size_t>(ControllerHand::Count)) {
        return false;
    }
    uint32_t mask = 1u << static_cast<uint32_t>(button);
    return (m_states[index].buttonsPressed & mask) != 0;
}

bool MotionController::IsButtonTouched(ControllerHand hand, ControllerButton button) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    if (index >= static_cast<size_t>(ControllerHand::Count)) {
        return false;
    }
    uint32_t mask = 1u << static_cast<uint32_t>(button);
    return (m_states[index].buttonsTouched & mask) != 0;
}

bool MotionController::IsButtonJustPressed(ControllerHand hand, ControllerButton button) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    if (index >= static_cast<size_t>(ControllerHand::Count)) {
        return false;
    }
    uint32_t mask = 1u << static_cast<uint32_t>(button);
    bool pressedNow = (m_states[index].buttonsPressed & mask) != 0;
    bool pressedBefore = (m_previousStates[index].buttonsPressed & mask) != 0;
    return pressedNow && !pressedBefore;
}

bool MotionController::IsButtonJustReleased(ControllerHand hand, ControllerButton button) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    if (index >= static_cast<size_t>(ControllerHand::Count)) {
        return false;
    }
    uint32_t mask = 1u << static_cast<uint32_t>(button);
    bool pressedNow = (m_states[index].buttonsPressed & mask) != 0;
    bool pressedBefore = (m_previousStates[index].buttonsPressed & mask) != 0;
    return !pressedNow && pressedBefore;
}

float MotionController::GetAxisValue(ControllerHand hand, ControllerAxis axis) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    size_t axisIndex = static_cast<size_t>(axis);

    if (index >= static_cast<size_t>(ControllerHand::Count) ||
        axisIndex >= static_cast<size_t>(ControllerAxis::Count)) {
        return 0.0f;
    }

    float value = m_states[index].axes[axisIndex];

    // Apply deadzone for thumbstick
    if (axis == ControllerAxis::ThumbstickX || axis == ControllerAxis::ThumbstickY) {
        value = ApplyDeadzone(value, m_thumbstickDeadzone);
    }

    return value;
}

Vector3 MotionController::GetControllerPosition(ControllerHand hand) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    if (index < static_cast<size_t>(ControllerHand::Count)) {
        return m_states[index].position;
    }
    return Vector3();
}

Quaternion MotionController::GetControllerOrientation(ControllerHand hand) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    if (index < static_cast<size_t>(ControllerHand::Count)) {
        return m_states[index].orientation;
    }
    return Quaternion();
}

Vector3 MotionController::GetControllerForward(ControllerHand hand) const {
    Quaternion orientation = GetControllerOrientation(hand);

    // Rotate forward vector by controller orientation
    // Forward is -Z in most VR coordinate systems
    float xx = orientation.x * orientation.x;
    float yy = orientation.y * orientation.y;
    float zz = orientation.z * orientation.z;
    float xz = orientation.x * orientation.z;
    float yz = orientation.y * orientation.z;
    float wx = orientation.w * orientation.x;
    float wy = orientation.w * orientation.y;

    Vector3 forward;
    forward.x = -2.0f * (xz - wy);
    forward.y = -2.0f * (yz + wx);
    forward.z = -(1.0f - 2.0f * (xx + yy));

    return forward;
}

Vector3 MotionController::GetAimDirection(ControllerHand hand) const {
    // Aim direction might have a slight offset from forward
    // For now, same as forward
    return GetControllerForward(hand);
}

void MotionController::TriggerHaptic(ControllerHand hand, const HapticParams& params) {
    size_t index = static_cast<size_t>(hand);
    if (index >= static_cast<size_t>(ControllerHand::Count)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingHaptics[index].hand = hand;
    m_pendingHaptics[index].params = params;
    m_pendingHaptics[index].pending = true;

    LOG_DEBUG("MotionController", "Haptic triggered: hand=%d amp=%.2f dur=%.3f",
              index, params.amplitude, params.duration);
}

void MotionController::TriggerHaptic(ControllerHand hand, float amplitude, float duration) {
    HapticParams params;
    params.amplitude = amplitude;
    params.duration = duration;
    TriggerHaptic(hand, params);
}

void MotionController::StopHaptic(ControllerHand hand) {
    size_t index = static_cast<size_t>(hand);
    if (index >= static_cast<size_t>(ControllerHand::Count)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingHaptics[index].pending = false;
}

void MotionController::AddBinding(const InputBinding& binding) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Remove existing binding with same name
    m_bindings.erase(
        std::remove_if(m_bindings.begin(), m_bindings.end(),
                       [&](const InputBinding& b) { return b.actionName == binding.actionName; }),
        m_bindings.end());

    m_bindings.push_back(binding);
    LOG_DEBUG("MotionController", "Added binding: %s", binding.actionName.c_str());
}

void MotionController::RemoveBinding(const std::string& actionName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bindings.erase(
        std::remove_if(m_bindings.begin(), m_bindings.end(),
                       [&](const InputBinding& b) { return b.actionName == actionName; }),
        m_bindings.end());
}

void MotionController::ClearBindings() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bindings.clear();
}

bool MotionController::IsActionActive(const std::string& actionName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& binding : m_bindings) {
        if (binding.actionName != actionName) {
            continue;
        }

        size_t index = static_cast<size_t>(binding.hand);
        if (index >= static_cast<size_t>(ControllerHand::Count)) {
            continue;
        }

        // Check if button is pressed
        uint32_t mask = 1u << static_cast<uint32_t>(binding.button);
        if (m_states[index].buttonsPressed & mask) {
            return true;
        }

        // Check analog threshold (for trigger/grip buttons)
        if (binding.button == ControllerButton::Trigger) {
            if (m_states[index].axes[static_cast<size_t>(ControllerAxis::Trigger)] >= binding.threshold) {
                return true;
            }
        } else if (binding.button == ControllerButton::Grip) {
            if (m_states[index].axes[static_cast<size_t>(ControllerAxis::Grip)] >= binding.threshold) {
                return true;
            }
        }
    }

    return false;
}

void MotionController::RegisterButtonCallback(ButtonCallback callback) {
    if (callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buttonCallbacks.push_back(callback);
    }
}

void MotionController::RegisterAxisCallback(AxisCallback callback) {
    if (callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_axisCallbacks.push_back(callback);
    }
}

void MotionController::RegisterPoseCallback(PoseCallback callback) {
    if (callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_poseCallbacks.push_back(callback);
    }
}

bool MotionController::IsGripping(ControllerHand hand) const {
    return GetAxisValue(hand, ControllerAxis::Grip) > m_gripThreshold;
}

bool MotionController::IsPointing(ControllerHand hand) const {
    // Pointing: index finger extended (trigger not pressed), others curled
    float trigger = GetAxisValue(hand, ControllerAxis::Trigger);
    float grip = GetAxisValue(hand, ControllerAxis::Grip);
    return trigger < 0.3f && grip > 0.7f;
}

bool MotionController::IsFist(ControllerHand hand) const {
    float trigger = GetAxisValue(hand, ControllerAxis::Trigger);
    float grip = GetAxisValue(hand, ControllerAxis::Grip);
    return trigger > 0.8f && grip > 0.8f;
}

bool MotionController::IsThumbUp(ControllerHand hand) const {
    // Thumb up: thumbstick not touched, fingers curled
    bool thumbTouched = IsButtonTouched(hand, ControllerButton::ThumbstickTouch);
    float grip = GetAxisValue(hand, ControllerAxis::Grip);
    return !thumbTouched && grip > 0.7f;
}

void MotionController::SetThumbstickDeadzone(float deadzone) {
    m_thumbstickDeadzone = std::clamp(deadzone, 0.0f, 0.5f);
}

void MotionController::SetTriggerThreshold(float threshold) {
    m_triggerThreshold = std::clamp(threshold, 0.0f, 1.0f);
}

void MotionController::SetGripThreshold(float threshold) {
    m_gripThreshold = std::clamp(threshold, 0.0f, 1.0f);
}

bool MotionController::IsConnected(ControllerHand hand) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    if (index < static_cast<size_t>(ControllerHand::Count)) {
        return m_states[index].isConnected;
    }
    return false;
}

bool MotionController::IsTracking(ControllerHand hand) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(hand);
    if (index < static_cast<size_t>(ControllerHand::Count)) {
        return m_states[index].isTracking;
    }
    return false;
}

void MotionController::SetWorldTransform(const Vector3& offset, float yawDegrees) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_worldOffset = offset;
    m_worldYaw = yawDegrees;
}

Vector3 MotionController::GetWorldPosition(ControllerHand hand) const {
    Vector3 pos = GetControllerPosition(hand);

    std::lock_guard<std::mutex> lock(m_mutex);

    // Apply yaw rotation
    float yawRad = m_worldYaw * DEG_TO_RAD;
    float cosYaw = std::cos(yawRad);
    float sinYaw = std::sin(yawRad);

    Vector3 worldPos;
    worldPos.x = pos.x * cosYaw - pos.z * sinYaw + m_worldOffset.x;
    worldPos.y = pos.y + m_worldOffset.y;
    worldPos.z = pos.x * sinYaw + pos.z * cosYaw + m_worldOffset.z;

    return worldPos;
}

Quaternion MotionController::GetWorldOrientation(ControllerHand hand) const {
    Quaternion orientation = GetControllerOrientation(hand);

    std::lock_guard<std::mutex> lock(m_mutex);

    // Apply yaw rotation
    float yawRad = m_worldYaw * DEG_TO_RAD * 0.5f;
    Quaternion yawQuat;
    yawQuat.w = std::cos(yawRad);
    yawQuat.x = 0.0f;
    yawQuat.y = std::sin(yawRad);
    yawQuat.z = 0.0f;

    // yawQuat * orientation
    Quaternion result;
    result.w = yawQuat.w * orientation.w - yawQuat.y * orientation.y;
    result.x = yawQuat.w * orientation.x + yawQuat.y * orientation.z;
    result.y = yawQuat.w * orientation.y + yawQuat.y * orientation.w;
    result.z = yawQuat.w * orientation.z - yawQuat.y * orientation.x;

    return result;
}

void MotionController::ProcessButtonChange(ControllerHand hand, ControllerButton button, bool pressed) {
    LOG_DEBUG("MotionController", "Button %d on hand %d: %s",
              static_cast<int>(button), static_cast<int>(hand),
              pressed ? "pressed" : "released");

    // Notify callbacks (mutex already held)
    for (auto& callback : m_buttonCallbacks) {
        if (callback) {
            callback(hand, button, pressed);
        }
    }
}

void MotionController::ProcessAxisChange(ControllerHand hand, ControllerAxis axis,
                                          float oldValue, float newValue) {
    // Notify callbacks (mutex already held)
    for (auto& callback : m_axisCallbacks) {
        if (callback) {
            callback(hand, axis, newValue);
        }
    }
}

float MotionController::ApplyDeadzone(float value, float deadzone) const {
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }

    // Remap value from [deadzone, 1] to [0, 1]
    float sign = value > 0.0f ? 1.0f : -1.0f;
    float absValue = std::abs(value);
    return sign * (absValue - deadzone) / (1.0f - deadzone);
}

} // namespace GTA5VR
