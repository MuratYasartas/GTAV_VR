#pragma once

#include "XRCore.hpp"
#include "XRInstance.hpp"
#include "XRSession.hpp"
#include <array>
#include <string>

namespace OVRInject {
namespace XR {

// Hand enumeration
enum class Hand : uint32_t {
    Left = 0,
    Right = 1,
    Count = 2
};

/**
 * Controller button/input state - mirrors TrackedController::ButtonState
 */
struct ControllerState {
    // Thumbstick/touchpad
    float thumbstickX = 0.0f;
    float thumbstickY = 0.0f;

    bool thumbstickTouched = false;
    bool thumbstickJustTouched = false;
    bool thumbstickJustReleased = false;

    bool thumbstickPressed = false;
    bool thumbstickJustPressed = false;
    bool thumbstickJustPressReleased = false;

    // Grip
    float gripValue = 0.0f;
    bool gripPressed = false;
    bool gripJustPressed = false;
    bool gripJustReleased = false;

    // Trigger
    float triggerValue = 0.0f;
    bool triggerPressed = false;
    bool triggerJustPressed = false;
    bool triggerJustReleased = false;

    // Primary button (A/X)
    bool primaryPressed = false;
    bool primaryJustPressed = false;
    bool primaryJustReleased = false;
    bool primaryTouched = false;

    // Secondary button (B/Y)
    bool secondaryPressed = false;
    bool secondaryJustPressed = false;
    bool secondaryJustReleased = false;
    bool secondaryTouched = false;

    // Menu button
    bool menuPressed = false;
    bool menuJustPressed = false;
    bool menuJustReleased = false;

    // State validity
    bool valid = false;
    bool poseValid = false;

    // Controller pose
    XrPosef pose = {};
    XMMATRIX poseMatrix = {};
};

/**
 * XRActionManager - Handles controller input via OpenXR action system
 *
 * OpenXR uses an action-based input system:
 * 1. Create an action set
 * 2. Create actions for each input type
 * 3. Suggest bindings for different controller profiles
 * 4. Attach action sets to session
 * 5. Sync actions each frame
 * 6. Query action state
 */
class XRActionManager {
public:
    XRActionManager(XRInstance* instance, XRSession* session);
    ~XRActionManager();

    // Disable copy
    XRActionManager(const XRActionManager&) = delete;
    XRActionManager& operator=(const XRActionManager&) = delete;

    /**
     * Initialize the action system
     * Creates action set, actions, and suggests bindings
     * Must be called before session is in READY state
     *
     * @return true on success
     */
    bool Initialize();

    /**
     * Attach action sets to the session
     * Must be called after session transitions to READY state
     *
     * @return true on success
     */
    bool AttachToSession();

    /**
     * Shutdown and cleanup
     */
    void Shutdown();

    /**
     * Check if initialized
     */
    bool IsInitialized() const { return action_set_ != XR_NULL_HANDLE; }

    /**
     * Check if attached to session
     */
    bool IsAttached() const { return attached_; }

    //-------------------------------------------------------------------------
    // Per-Frame Updates
    //-------------------------------------------------------------------------

    /**
     * Sync action states (call once per frame)
     * @return true on success
     */
    bool SyncActions();

    /**
     * Update controller states from synced actions
     * @param display_time Time for pose location
     * @param reference_space Reference space for poses
     */
    void UpdateControllerStates(XrTime display_time, XrSpace reference_space);

    //-------------------------------------------------------------------------
    // State Queries
    //-------------------------------------------------------------------------

    /**
     * Get controller state for a hand
     */
    const ControllerState& GetControllerState(Hand hand) const;

    /**
     * Get left controller state
     */
    const ControllerState& GetLeftState() const { return GetControllerState(Hand::Left); }

    /**
     * Get right controller state
     */
    const ControllerState& GetRightState() const { return GetControllerState(Hand::Right); }

    /**
     * Check if controller is active (has valid tracking)
     */
    bool IsControllerActive(Hand hand) const;

    //-------------------------------------------------------------------------
    // Haptics
    //-------------------------------------------------------------------------

    /**
     * Trigger haptic vibration on a controller
     *
     * @param hand Which controller
     * @param duration_ns Duration in nanoseconds (XR_MIN_HAPTIC_DURATION for minimum)
     * @param frequency Frequency in Hz (XR_FREQUENCY_UNSPECIFIED for default)
     * @param amplitude Amplitude 0.0 to 1.0
     * @return true on success
     */
    bool TriggerHaptic(Hand hand, int64_t duration_ns, float frequency, float amplitude);

    /**
     * Stop haptic feedback on a controller
     */
    bool StopHaptic(Hand hand);

    //-------------------------------------------------------------------------
    // Space Access
    //-------------------------------------------------------------------------

    /**
     * Get action space for controller pose (grip pose)
     */
    XrSpace GetHandSpace(Hand hand) const;

    /**
     * Get action space for aim pose
     */
    XrSpace GetAimSpace(Hand hand) const;

private:
    /**
     * Create action set and all actions
     */
    bool CreateActions();

    /**
     * Suggest bindings for different controller profiles
     */
    bool SuggestBindings();

    /**
     * Create action spaces for pose actions
     */
    bool CreateActionSpaces();

    /**
     * Suggest bindings for a specific profile
     */
    bool SuggestBindingsForProfile(const char* profilePath,
                                    const std::vector<XrActionSuggestedBinding>& bindings);

    /**
     * Get boolean action state with edge detection
     */
    void UpdateBooleanAction(XrAction action, Hand hand,
                             bool& current, bool& justPressed, bool& justReleased);

    /**
     * Get float action state
     */
    float GetFloatAction(XrAction action, Hand hand);

    /**
     * Get vector2 action state
     */
    void GetVector2Action(XrAction action, Hand hand, float& x, float& y);

    /**
     * Get pose action state
     */
    bool GetPoseAction(XrAction action, XrSpace space, XrTime time,
                       XrSpace reference_space, XrPosef& pose);

    //-------------------------------------------------------------------------
    // Members
    //-------------------------------------------------------------------------

    XRInstance* instance_;
    XRSession* session_;

    // Action set
    XrActionSet action_set_ = XR_NULL_HANDLE;

    // Paths for left/right hands
    std::array<XrPath, 2> hand_paths_ = {};

    // Pose actions
    XrAction grip_pose_action_ = XR_NULL_HANDLE;
    XrAction aim_pose_action_ = XR_NULL_HANDLE;

    // Button actions
    XrAction trigger_action_ = XR_NULL_HANDLE;
    XrAction trigger_click_action_ = XR_NULL_HANDLE;
    XrAction grip_action_ = XR_NULL_HANDLE;
    XrAction grip_click_action_ = XR_NULL_HANDLE;
    XrAction primary_action_ = XR_NULL_HANDLE;      // A/X
    XrAction secondary_action_ = XR_NULL_HANDLE;    // B/Y
    XrAction menu_action_ = XR_NULL_HANDLE;
    XrAction thumbstick_action_ = XR_NULL_HANDLE;
    XrAction thumbstick_click_action_ = XR_NULL_HANDLE;
    XrAction thumbstick_touch_action_ = XR_NULL_HANDLE;

    // Haptic action
    XrAction haptic_action_ = XR_NULL_HANDLE;

    // Action spaces for pose actions
    std::array<XrSpace, 2> grip_spaces_ = {};
    std::array<XrSpace, 2> aim_spaces_ = {};

    // Controller states
    std::array<ControllerState, 2> controller_states_ = {};

    // State tracking
    bool attached_ = false;
};

} // namespace XR
} // namespace OVRInject
