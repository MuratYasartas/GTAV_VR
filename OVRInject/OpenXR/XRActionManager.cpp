#include "XRActionManager.hpp"
#include "../VR/SharedSettings.hpp"

#include <cstring>
#include <algorithm>

namespace OVRInject {
namespace XR {

//-----------------------------------------------------------------------------
// Constructor / Destructor
//-----------------------------------------------------------------------------

XRActionManager::XRActionManager(XRInstance* instance, XRSession* session)
    : instance_(instance)
    , session_(session)
{
}

XRActionManager::~XRActionManager() {
    Shutdown();
}

//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------

bool XRActionManager::Initialize() {
    if (!instance_ || !instance_->IsInitialized()) {
        LOGSTR("XRActionManager: Instance not initialized\n");
        return false;
    }

    if (!CreateActions()) {
        LOGSTR("XRActionManager: Failed to create actions\n");
        return false;
    }

    if (!SuggestBindings()) {
        LOGSTR("XRActionManager: Failed to suggest bindings\n");
        return false;
    }

    LOGSTR("XRActionManager: Initialized successfully\n");
    return true;
}

bool XRActionManager::AttachToSession() {
    if (!session_ || !session_->IsCreated()) {
        LOGSTR("XRActionManager: Session not created\n");
        return false;
    }

    if (attached_) {
        LOGSTR("XRActionManager: Already attached to session\n");
        return true;
    }

    XrSession session = session_->GetHandle();

    // Attach action sets to the session
    XrSessionActionSetsAttachInfo attachInfo = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &action_set_;

    XrResult result = xrAttachSessionActionSets(session, &attachInfo);
    if (result == XR_ERROR_ACTIONSETS_ALREADY_ATTACHED) {
        // A previous AttachToSession call attached the sets but failed during
        // action-space creation (or the session re-entered READY after
        // STOPPING). Action sets stay attached for the session lifetime;
        // continue to (re)create the action spaces instead of failing forever.
        LOGSTR("XRActionManager: Action sets already attached; continuing with action spaces\n");
    } else if (XR_FAILED(result)) {
        LOGSTRF("XRActionManager: Failed to attach action sets (result: %d)\n", result);
        return false;
    }

    // Create action spaces after attachment
    if (!CreateActionSpaces()) {
        LOGSTR("XRActionManager: Failed to create action spaces\n");
        return false;
    }

    attached_ = true;
    LOGSTR("XRActionManager: Attached to session\n");
    return true;
}

void XRActionManager::Shutdown() {
    // Destroy action spaces
    for (auto& space : grip_spaces_) {
        if (space != XR_NULL_HANDLE) {
            xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }

    for (auto& space : aim_spaces_) {
        if (space != XR_NULL_HANDLE) {
            xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }

    // Destroy action set (this also destroys all actions in the set)
    if (action_set_ != XR_NULL_HANDLE) {
        xrDestroyActionSet(action_set_);
        action_set_ = XR_NULL_HANDLE;
    }

    // Reset actions handles
    grip_pose_action_ = XR_NULL_HANDLE;
    aim_pose_action_ = XR_NULL_HANDLE;
    trigger_action_ = XR_NULL_HANDLE;
    trigger_click_action_ = XR_NULL_HANDLE;
    grip_action_ = XR_NULL_HANDLE;
    grip_click_action_ = XR_NULL_HANDLE;
    primary_action_ = XR_NULL_HANDLE;
    secondary_action_ = XR_NULL_HANDLE;
    menu_action_ = XR_NULL_HANDLE;
    thumbstick_action_ = XR_NULL_HANDLE;
    thumbstick_click_action_ = XR_NULL_HANDLE;
    thumbstick_touch_action_ = XR_NULL_HANDLE;
    haptic_action_ = XR_NULL_HANDLE;

    attached_ = false;

    LOGSTR("XRActionManager: Shutdown complete\n");
}

//-----------------------------------------------------------------------------
// Action Creation
//-----------------------------------------------------------------------------

bool XRActionManager::CreateActions() {
    XrInstance instance = instance_->GetHandle();

    // Create paths for hands
    XrResult result = xrStringToPath(instance, "/user/hand/left", &hand_paths_[0]);
    if (XR_FAILED(result)) return false;

    result = xrStringToPath(instance, "/user/hand/right", &hand_paths_[1]);
    if (XR_FAILED(result)) return false;

    // Create action set
    XrActionSetCreateInfo actionSetInfo = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy(actionSetInfo.actionSetName, "gtavr_gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    actionSetInfo.actionSetName[XR_MAX_ACTION_SET_NAME_SIZE - 1] = '\0';
    strncpy(actionSetInfo.localizedActionSetName, "GTA VR Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    actionSetInfo.localizedActionSetName[XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1] = '\0';
    actionSetInfo.priority = 0;

    result = xrCreateActionSet(instance, &actionSetInfo, &action_set_);
    if (XR_FAILED(result)) {
        LOGSTRF("XRActionManager: Failed to create action set (result: %d)\n", result);
        return false;
    }

    // Helper to create an action
    auto createAction = [&](XrAction& action, XrActionType type,
                           const char* name, const char* localizedName,
                           bool subactionPaths = true) -> bool {
        XrActionCreateInfo actionInfo = {XR_TYPE_ACTION_CREATE_INFO};
        actionInfo.actionType = type;
        strncpy(actionInfo.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
        actionInfo.actionName[XR_MAX_ACTION_NAME_SIZE - 1] = '\0';
        strncpy(actionInfo.localizedActionName, localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        actionInfo.localizedActionName[XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1] = '\0';

        if (subactionPaths) {
            actionInfo.countSubactionPaths = 2;
            actionInfo.subactionPaths = hand_paths_.data();
        }

        XrResult res = xrCreateAction(action_set_, &actionInfo, &action);
        if (XR_FAILED(res)) {
            LOGSTRF("XRActionManager: Failed to create action '%s' (result: %d)\n", name, res);
            return false;
        }
        return true;
    };

    // Pose actions
    if (!createAction(grip_pose_action_, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose"))
        return false;
    if (!createAction(aim_pose_action_, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose"))
        return false;

    // Trigger
    if (!createAction(trigger_action_, XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger"))
        return false;
    if (!createAction(trigger_click_action_, XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_click", "Trigger Click"))
        return false;

    // Grip
    if (!createAction(grip_action_, XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Grip"))
        return false;
    if (!createAction(grip_click_action_, XR_ACTION_TYPE_BOOLEAN_INPUT, "grip_click", "Grip Click"))
        return false;

    // Face buttons
    if (!createAction(primary_action_, XR_ACTION_TYPE_BOOLEAN_INPUT, "primary_button", "Primary Button (A/X)"))
        return false;
    if (!createAction(secondary_action_, XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary_button", "Secondary Button (B/Y)"))
        return false;

    // Menu
    if (!createAction(menu_action_, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu"))
        return false;

    // Thumbstick
    if (!createAction(thumbstick_action_, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick"))
        return false;
    if (!createAction(thumbstick_click_action_, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick Click"))
        return false;
    if (!createAction(thumbstick_touch_action_, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_touch", "Thumbstick Touch"))
        return false;

    // Haptic
    if (!createAction(haptic_action_, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic"))
        return false;

    LOGSTR("XRActionManager: Created all actions\n");
    return true;
}

//-----------------------------------------------------------------------------
// Bindings
//-----------------------------------------------------------------------------

bool XRActionManager::SuggestBindings() {
    XrInstance instance = instance_->GetHandle();

    // Helper to create path
    auto getPath = [&](const char* pathString) -> XrPath {
        XrPath path;
        xrStringToPath(instance, pathString, &path);
        return path;
    };

    //-------------------------------------------------------------------------
    // Oculus Touch Controllers
    //-------------------------------------------------------------------------
    {
        std::vector<XrActionSuggestedBinding> bindings = {
            // Pose
            {grip_pose_action_, getPath("/user/hand/left/input/grip/pose")},
            {grip_pose_action_, getPath("/user/hand/right/input/grip/pose")},
            {aim_pose_action_, getPath("/user/hand/left/input/aim/pose")},
            {aim_pose_action_, getPath("/user/hand/right/input/aim/pose")},
            // Trigger
            {trigger_action_, getPath("/user/hand/left/input/trigger/value")},
            {trigger_action_, getPath("/user/hand/right/input/trigger/value")},
            {trigger_click_action_, getPath("/user/hand/left/input/trigger/value")},
            {trigger_click_action_, getPath("/user/hand/right/input/trigger/value")},
            // Grip (squeeze)
            {grip_action_, getPath("/user/hand/left/input/squeeze/value")},
            {grip_action_, getPath("/user/hand/right/input/squeeze/value")},
            {grip_click_action_, getPath("/user/hand/left/input/squeeze/value")},
            {grip_click_action_, getPath("/user/hand/right/input/squeeze/value")},
            // Face buttons
            {primary_action_, getPath("/user/hand/left/input/x/click")},
            {primary_action_, getPath("/user/hand/right/input/a/click")},
            {secondary_action_, getPath("/user/hand/left/input/y/click")},
            {secondary_action_, getPath("/user/hand/right/input/b/click")},
            // Menu
            {menu_action_, getPath("/user/hand/left/input/menu/click")},
            // Thumbstick
            {thumbstick_action_, getPath("/user/hand/left/input/thumbstick")},
            {thumbstick_action_, getPath("/user/hand/right/input/thumbstick")},
            {thumbstick_click_action_, getPath("/user/hand/left/input/thumbstick/click")},
            {thumbstick_click_action_, getPath("/user/hand/right/input/thumbstick/click")},
            {thumbstick_touch_action_, getPath("/user/hand/left/input/thumbstick/touch")},
            {thumbstick_touch_action_, getPath("/user/hand/right/input/thumbstick/touch")},
            // Haptic
            {haptic_action_, getPath("/user/hand/left/output/haptic")},
            {haptic_action_, getPath("/user/hand/right/output/haptic")},
        };

        if (!SuggestBindingsForProfile("/interaction_profiles/oculus/touch_controller", bindings)) {
            LOGSTR("XRActionManager: Failed to suggest Oculus Touch bindings (may not be supported)\n");
        }
    }

    //-------------------------------------------------------------------------
    // Valve Index Controllers
    //-------------------------------------------------------------------------
    {
        std::vector<XrActionSuggestedBinding> bindings = {
            // Pose
            {grip_pose_action_, getPath("/user/hand/left/input/grip/pose")},
            {grip_pose_action_, getPath("/user/hand/right/input/grip/pose")},
            {aim_pose_action_, getPath("/user/hand/left/input/aim/pose")},
            {aim_pose_action_, getPath("/user/hand/right/input/aim/pose")},
            // Trigger
            {trigger_action_, getPath("/user/hand/left/input/trigger/value")},
            {trigger_action_, getPath("/user/hand/right/input/trigger/value")},
            {trigger_click_action_, getPath("/user/hand/left/input/trigger/click")},
            {trigger_click_action_, getPath("/user/hand/right/input/trigger/click")},
            // Grip (squeeze)
            {grip_action_, getPath("/user/hand/left/input/squeeze/value")},
            {grip_action_, getPath("/user/hand/right/input/squeeze/value")},
            {grip_click_action_, getPath("/user/hand/left/input/squeeze/force")},
            {grip_click_action_, getPath("/user/hand/right/input/squeeze/force")},
            // Face buttons
            {primary_action_, getPath("/user/hand/left/input/a/click")},
            {primary_action_, getPath("/user/hand/right/input/a/click")},
            {secondary_action_, getPath("/user/hand/left/input/b/click")},
            {secondary_action_, getPath("/user/hand/right/input/b/click")},
            // Thumbstick
            {thumbstick_action_, getPath("/user/hand/left/input/thumbstick")},
            {thumbstick_action_, getPath("/user/hand/right/input/thumbstick")},
            {thumbstick_click_action_, getPath("/user/hand/left/input/thumbstick/click")},
            {thumbstick_click_action_, getPath("/user/hand/right/input/thumbstick/click")},
            {thumbstick_touch_action_, getPath("/user/hand/left/input/thumbstick/touch")},
            {thumbstick_touch_action_, getPath("/user/hand/right/input/thumbstick/touch")},
            // Haptic
            {haptic_action_, getPath("/user/hand/left/output/haptic")},
            {haptic_action_, getPath("/user/hand/right/output/haptic")},
        };

        if (!SuggestBindingsForProfile("/interaction_profiles/valve/index_controller", bindings)) {
            LOGSTR("XRActionManager: Failed to suggest Index controller bindings (may not be supported)\n");
        }
    }

    //-------------------------------------------------------------------------
    // HTC Vive Controllers
    //-------------------------------------------------------------------------
    {
        std::vector<XrActionSuggestedBinding> bindings = {
            // Pose
            {grip_pose_action_, getPath("/user/hand/left/input/grip/pose")},
            {grip_pose_action_, getPath("/user/hand/right/input/grip/pose")},
            {aim_pose_action_, getPath("/user/hand/left/input/aim/pose")},
            {aim_pose_action_, getPath("/user/hand/right/input/aim/pose")},
            // Trigger
            {trigger_action_, getPath("/user/hand/left/input/trigger/value")},
            {trigger_action_, getPath("/user/hand/right/input/trigger/value")},
            {trigger_click_action_, getPath("/user/hand/left/input/trigger/click")},
            {trigger_click_action_, getPath("/user/hand/right/input/trigger/click")},
            // Grip
            {grip_click_action_, getPath("/user/hand/left/input/squeeze/click")},
            {grip_click_action_, getPath("/user/hand/right/input/squeeze/click")},
            // Menu
            {menu_action_, getPath("/user/hand/left/input/menu/click")},
            {menu_action_, getPath("/user/hand/right/input/menu/click")},
            // Trackpad (as thumbstick)
            {thumbstick_action_, getPath("/user/hand/left/input/trackpad")},
            {thumbstick_action_, getPath("/user/hand/right/input/trackpad")},
            {thumbstick_click_action_, getPath("/user/hand/left/input/trackpad/click")},
            {thumbstick_click_action_, getPath("/user/hand/right/input/trackpad/click")},
            {thumbstick_touch_action_, getPath("/user/hand/left/input/trackpad/touch")},
            {thumbstick_touch_action_, getPath("/user/hand/right/input/trackpad/touch")},
            // Haptic
            {haptic_action_, getPath("/user/hand/left/output/haptic")},
            {haptic_action_, getPath("/user/hand/right/output/haptic")},
        };

        if (!SuggestBindingsForProfile("/interaction_profiles/htc/vive_controller", bindings)) {
            LOGSTR("XRActionManager: Failed to suggest Vive controller bindings (may not be supported)\n");
        }
    }

    //-------------------------------------------------------------------------
    // Windows Mixed Reality Controllers
    //-------------------------------------------------------------------------
    {
        std::vector<XrActionSuggestedBinding> bindings = {
            // Pose
            {grip_pose_action_, getPath("/user/hand/left/input/grip/pose")},
            {grip_pose_action_, getPath("/user/hand/right/input/grip/pose")},
            {aim_pose_action_, getPath("/user/hand/left/input/aim/pose")},
            {aim_pose_action_, getPath("/user/hand/right/input/aim/pose")},
            // Trigger
            {trigger_action_, getPath("/user/hand/left/input/trigger/value")},
            {trigger_action_, getPath("/user/hand/right/input/trigger/value")},
            {trigger_click_action_, getPath("/user/hand/left/input/trigger/value")},
            {trigger_click_action_, getPath("/user/hand/right/input/trigger/value")},
            // Grip
            {grip_click_action_, getPath("/user/hand/left/input/squeeze/click")},
            {grip_click_action_, getPath("/user/hand/right/input/squeeze/click")},
            // Menu
            {menu_action_, getPath("/user/hand/left/input/menu/click")},
            {menu_action_, getPath("/user/hand/right/input/menu/click")},
            // Thumbstick
            {thumbstick_action_, getPath("/user/hand/left/input/thumbstick")},
            {thumbstick_action_, getPath("/user/hand/right/input/thumbstick")},
            {thumbstick_click_action_, getPath("/user/hand/left/input/thumbstick/click")},
            {thumbstick_click_action_, getPath("/user/hand/right/input/thumbstick/click")},
            // Trackpad
            {thumbstick_touch_action_, getPath("/user/hand/left/input/trackpad/touch")},
            {thumbstick_touch_action_, getPath("/user/hand/right/input/trackpad/touch")},
            // Haptic
            {haptic_action_, getPath("/user/hand/left/output/haptic")},
            {haptic_action_, getPath("/user/hand/right/output/haptic")},
        };

        if (!SuggestBindingsForProfile("/interaction_profiles/microsoft/motion_controller", bindings)) {
            LOGSTR("XRActionManager: Failed to suggest WMR controller bindings (may not be supported)\n");
        }
    }

    return true;
}

bool XRActionManager::SuggestBindingsForProfile(const char* profilePath,
                                                  const std::vector<XrActionSuggestedBinding>& bindings) {
    XrInstance instance = instance_->GetHandle();

    XrPath interactionProfilePath;
    XrResult result = xrStringToPath(instance, profilePath, &interactionProfilePath);
    if (XR_FAILED(result)) return false;

    XrInteractionProfileSuggestedBinding suggestedBindings = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggestedBindings.interactionProfile = interactionProfilePath;
    suggestedBindings.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggestedBindings.suggestedBindings = bindings.data();

    result = xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
    if (XR_FAILED(result)) {
        // This is not fatal - the profile may not be supported by the current runtime
        return false;
    }

    LOGSTRF("XRActionManager: Suggested bindings for %s\n", profilePath);
    return true;
}

bool XRActionManager::CreateActionSpaces() {
    if (!session_ || !session_->IsCreated()) return false;

    XrSession session = session_->GetHandle();

    // Create grip spaces for both hands (skip any already created by a
    // previous partial attach, so a retry never leaks or duplicates)
    for (size_t i = 0; i < 2; ++i) {
        if (grip_spaces_[i] != XR_NULL_HANDLE) continue;
        XrActionSpaceCreateInfo spaceInfo = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = grip_pose_action_;
        spaceInfo.subactionPath = hand_paths_[i];
        spaceInfo.poseInActionSpace = IdentityPose();

        XrResult result = xrCreateActionSpace(session, &spaceInfo, &grip_spaces_[i]);
        if (XR_FAILED(result)) {
            LOGSTRF("XRActionManager: Failed to create grip space for hand %zu\n", i);
            return false;
        }
    }

    // Create aim spaces for both hands
    for (size_t i = 0; i < 2; ++i) {
        if (aim_spaces_[i] != XR_NULL_HANDLE) continue;
        XrActionSpaceCreateInfo spaceInfo = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = aim_pose_action_;
        spaceInfo.subactionPath = hand_paths_[i];
        spaceInfo.poseInActionSpace = IdentityPose();

        XrResult result = xrCreateActionSpace(session, &spaceInfo, &aim_spaces_[i]);
        if (XR_FAILED(result)) {
            LOGSTRF("XRActionManager: Failed to create aim space for hand %zu\n", i);
            return false;
        }
    }

    LOGSTR("XRActionManager: Created action spaces\n");
    return true;
}

//-----------------------------------------------------------------------------
// Per-Frame Updates
//-----------------------------------------------------------------------------

bool XRActionManager::SyncActions() {
    if (!session_ || !session_->IsCreated()) return false;
    if (!attached_) return false;

    XrSession session = session_->GetHandle();

    XrActiveActionSet activeActionSet = {};
    activeActionSet.actionSet = action_set_;
    activeActionSet.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo syncInfo = {XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;

    XrResult result = xrSyncActions(session, &syncInfo);
    if (XR_FAILED(result)) {
        // SESSION_NOT_FOCUSED is expected when the app doesn't have focus
        if (result != XR_SESSION_NOT_FOCUSED) {
            LOGSTRF("XRActionManager: xrSyncActions failed (result: %d)\n", result);
        }
        return false;
    }

    return true;
}

void XRActionManager::UpdateControllerStates(XrTime display_time, XrSpace reference_space) {
    for (size_t i = 0; i < 2; ++i) {
        Hand hand = static_cast<Hand>(i);
        ControllerState& state = controller_states_[i];

        // Update pose
        state.poseValid = GetPoseAction(grip_pose_action_, grip_spaces_[i],
                                        display_time, reference_space, state.pose);
        if (state.poseValid) {
            state.poseMatrix = XrPoseToMatrix(state.pose);
        }

        // Update trigger
        bool prevTriggerPressed = state.triggerPressed;
        state.triggerValue = GetFloatAction(trigger_action_, hand);
        UpdateBooleanAction(trigger_click_action_, hand,
                            state.triggerPressed, state.triggerJustPressed, state.triggerJustReleased);

        auto& input = VR::GetInputSettings();
        float triggerPress = input.triggerThreshold.load();
        if (triggerPress < 0.05f) triggerPress = 0.05f;
        if (triggerPress > 0.95f) triggerPress = 0.95f;
        float triggerRelease = (std::max)(0.05f, triggerPress - 0.2f);
        bool triggerPressed = prevTriggerPressed ? (state.triggerValue >= triggerRelease)
                                                 : (state.triggerValue >= triggerPress);
        state.triggerJustPressed = triggerPressed && !prevTriggerPressed;
        state.triggerJustReleased = !triggerPressed && prevTriggerPressed;
        state.triggerPressed = triggerPressed;

        // Update grip
        bool prevGripPressed = state.gripPressed;
        state.gripValue = GetFloatAction(grip_action_, hand);
        UpdateBooleanAction(grip_click_action_, hand,
                            state.gripPressed, state.gripJustPressed, state.gripJustReleased);
        float gripPress = input.gripThreshold.load();
        if (gripPress < 0.05f) gripPress = 0.05f;
        if (gripPress > 0.95f) gripPress = 0.95f;
        float gripRelease = (std::max)(0.05f, gripPress - 0.2f);
        bool gripPressed = prevGripPressed ? (state.gripValue >= gripRelease)
                                           : (state.gripValue >= gripPress);
        state.gripJustPressed = gripPressed && !prevGripPressed;
        state.gripJustReleased = !gripPressed && prevGripPressed;
        state.gripPressed = gripPressed;

        // Update face buttons
        UpdateBooleanAction(primary_action_, hand,
                           state.primaryPressed, state.primaryJustPressed, state.primaryJustReleased);
        UpdateBooleanAction(secondary_action_, hand,
                           state.secondaryPressed, state.secondaryJustPressed, state.secondaryJustReleased);

        // Update menu
        UpdateBooleanAction(menu_action_, hand,
                           state.menuPressed, state.menuJustPressed, state.menuJustReleased);

        // Update thumbstick
        GetVector2Action(thumbstick_action_, hand, state.thumbstickX, state.thumbstickY);
        UpdateBooleanAction(thumbstick_click_action_, hand,
                           state.thumbstickPressed, state.thumbstickJustPressed, state.thumbstickJustPressReleased);
        UpdateBooleanAction(thumbstick_touch_action_, hand,
                           state.thumbstickTouched, state.thumbstickJustTouched, state.thumbstickJustReleased);

        state.valid = true;
    }
}

//-----------------------------------------------------------------------------
// State Queries
//-----------------------------------------------------------------------------

const ControllerState& XRActionManager::GetControllerState(Hand hand) const {
    return controller_states_[static_cast<size_t>(hand)];
}

bool XRActionManager::IsControllerActive(Hand hand) const {
    const auto& state = GetControllerState(hand);
    return state.valid && state.poseValid;
}

//-----------------------------------------------------------------------------
// Haptics
//-----------------------------------------------------------------------------

bool XRActionManager::TriggerHaptic(Hand hand, int64_t duration_ns, float frequency, float amplitude) {
    if (!session_ || !session_->IsCreated()) return false;
    if (!attached_) return false;

    XrSession session = session_->GetHandle();

    XrHapticVibration vibration = {XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration = duration_ns;
    vibration.frequency = frequency;
    vibration.amplitude = amplitude;

    XrHapticActionInfo hapticInfo = {XR_TYPE_HAPTIC_ACTION_INFO};
    hapticInfo.action = haptic_action_;
    hapticInfo.subactionPath = hand_paths_[static_cast<size_t>(hand)];

    XrResult result = xrApplyHapticFeedback(session, &hapticInfo,
                                            reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
    return XR_SUCCEEDED(result);
}

bool XRActionManager::StopHaptic(Hand hand) {
    if (!session_ || !session_->IsCreated()) return false;
    if (!attached_) return false;

    XrSession session = session_->GetHandle();

    XrHapticActionInfo hapticInfo = {XR_TYPE_HAPTIC_ACTION_INFO};
    hapticInfo.action = haptic_action_;
    hapticInfo.subactionPath = hand_paths_[static_cast<size_t>(hand)];

    XrResult result = xrStopHapticFeedback(session, &hapticInfo);
    return XR_SUCCEEDED(result);
}

//-----------------------------------------------------------------------------
// Space Access
//-----------------------------------------------------------------------------

XrSpace XRActionManager::GetHandSpace(Hand hand) const {
    return grip_spaces_[static_cast<size_t>(hand)];
}

XrSpace XRActionManager::GetAimSpace(Hand hand) const {
    return aim_spaces_[static_cast<size_t>(hand)];
}

//-----------------------------------------------------------------------------
// Action State Helpers
//-----------------------------------------------------------------------------

void XRActionManager::UpdateBooleanAction(XrAction action, Hand hand,
                                           bool& current, bool& justPressed, bool& justReleased) {
    if (!session_ || !session_->IsCreated() || !attached_) {
        justPressed = false;
        justReleased = false;
        return;
    }

    XrSession session = session_->GetHandle();

    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = action;
    getInfo.subactionPath = hand_paths_[static_cast<size_t>(hand)];

    XrActionStateBoolean state = {XR_TYPE_ACTION_STATE_BOOLEAN};
    XrResult result = xrGetActionStateBoolean(session, &getInfo, &state);

    if (XR_SUCCEEDED(result) && state.isActive) {
        bool newValue = state.currentState == XR_TRUE;
        justPressed = newValue && !current;
        justReleased = !newValue && current;
        current = newValue;
    } else {
        justPressed = false;
        justReleased = false;
    }
}

float XRActionManager::GetFloatAction(XrAction action, Hand hand) {
    if (!session_ || !session_->IsCreated() || !attached_) return 0.0f;

    XrSession session = session_->GetHandle();

    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = action;
    getInfo.subactionPath = hand_paths_[static_cast<size_t>(hand)];

    XrActionStateFloat state = {XR_TYPE_ACTION_STATE_FLOAT};
    XrResult result = xrGetActionStateFloat(session, &getInfo, &state);

    if (XR_SUCCEEDED(result) && state.isActive) {
        return state.currentState;
    }

    return 0.0f;
}

void XRActionManager::GetVector2Action(XrAction action, Hand hand, float& x, float& y) {
    x = 0.0f;
    y = 0.0f;

    if (!session_ || !session_->IsCreated() || !attached_) return;

    XrSession session = session_->GetHandle();

    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = action;
    getInfo.subactionPath = hand_paths_[static_cast<size_t>(hand)];

    XrActionStateVector2f state = {XR_TYPE_ACTION_STATE_VECTOR2F};
    XrResult result = xrGetActionStateVector2f(session, &getInfo, &state);

    if (XR_SUCCEEDED(result) && state.isActive) {
        x = state.currentState.x;
        y = state.currentState.y;
    }
}

bool XRActionManager::GetPoseAction(XrAction action, XrSpace space, XrTime time,
                                     XrSpace reference_space, XrPosef& pose) {
    if (space == XR_NULL_HANDLE || reference_space == XR_NULL_HANDLE) return false;

    XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
    XrResult result = xrLocateSpace(space, reference_space, time, &location);

    if (XR_FAILED(result)) return false;

    bool positionValid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    bool orientationValid = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

    if (positionValid && orientationValid) {
        pose = location.pose;
        return true;
    }

    return false;
}

} // namespace XR
} // namespace OVRInject
