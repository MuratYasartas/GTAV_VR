#include "VRInputManager.h"
#include "../core/Logger.h"

namespace GTA5VR {

VRInputManager::VRInputManager() {
    memset(m_controllerStates, 0, sizeof(m_controllerStates));
    memset(m_controllerPoses, 0, sizeof(m_controllerPoses));
}

VRInputManager::~VRInputManager() {
    Shutdown();
}

bool VRInputManager::Initialize(IRuntimeInterface* runtime) {
    if (m_initialized) return true;
    m_runtime = runtime;
    m_initialized = true;
    LOG_INFO("VRInputManager initialized");
    return true;
}

void VRInputManager::Shutdown() {
    m_initialized = false;
}

void VRInputManager::Update() {
    if (!m_initialized || !m_runtime) return;

    m_runtime->SyncActions();

    for (int hand = 0; hand < 2; ++hand) {
        m_runtime->GetControllerPose(static_cast<VRHand>(hand), m_controllerPoses[hand]);
        m_runtime->GetControllerState(static_cast<VRHand>(hand), m_controllerStates[hand]);
    }
}

bool VRInputManager::GetControllerPose(VRHand hand, float* position, float* rotation) {
    int idx = static_cast<int>(hand);
    if (!m_controllerPoses[idx].isValid) return false;

    if (position) {
        position[0] = m_controllerPoses[idx].position[0];
        position[1] = m_controllerPoses[idx].position[1];
        position[2] = m_controllerPoses[idx].position[2];
    }
    if (rotation) {
        rotation[0] = m_controllerPoses[idx].orientation[0];
        rotation[1] = m_controllerPoses[idx].orientation[1];
        rotation[2] = m_controllerPoses[idx].orientation[2];
        rotation[3] = m_controllerPoses[idx].orientation[3];
    }
    return true;
}

float VRInputManager::GetTrigger(VRHand hand) {
    return m_controllerStates[static_cast<int>(hand)].trigger;
}

float VRInputManager::GetGrip(VRHand hand) {
    return m_controllerStates[static_cast<int>(hand)].grip;
}

void VRInputManager::GetThumbstick(VRHand hand, float* x, float* y) {
    auto& state = m_controllerStates[static_cast<int>(hand)];
    if (x) *x = state.thumbstickX;
    if (y) *y = state.thumbstickY;
}

bool VRInputManager::GetButton(VRHand hand, int button) {
    auto& state = m_controllerStates[static_cast<int>(hand)];
    switch (button) {
        case 0: return state.primaryButton;
        case 1: return state.secondaryButton;
        case 2: return state.menuButton;
        case 3: return state.thumbstickClick;
        default: return false;
    }
}

void VRInputManager::TriggerHaptic(VRHand hand, float amplitude, float duration) {
    if (m_runtime) {
        m_runtime->TriggerHaptic(hand, amplitude, duration);
    }
}

bool VRInputManager::DetectHeadShakeRecenter() {
    // Would track head rotation velocity and detect shake pattern
    return false;
}

bool VRInputManager::DetectLookDown() {
    // Would check head pitch angle
    return false;
}

void VRInputManager::MapToGamepad() {
    // Would use XInput or ViGEm to emulate gamepad
}

void VRInputManager::MapToKeyboard() {
    // Would use SendInput to emulate keyboard
}

} // namespace GTA5VR
