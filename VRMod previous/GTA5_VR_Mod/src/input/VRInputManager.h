#pragma once

#include <cstdint>
#include "../runtime/RuntimeInterface.h"

namespace GTA5VR {

class VRInputManager {
public:
    VRInputManager();
    ~VRInputManager();

    bool Initialize(IRuntimeInterface* runtime);
    void Shutdown();
    void Update();

    // Controller state
    bool GetControllerPose(VRHand hand, float* position, float* rotation);
    float GetTrigger(VRHand hand);
    float GetGrip(VRHand hand);
    void GetThumbstick(VRHand hand, float* x, float* y);
    bool GetButton(VRHand hand, int button);

    // Haptic feedback
    void TriggerHaptic(VRHand hand, float amplitude, float duration);

    // Gesture detection
    bool DetectHeadShakeRecenter();
    bool DetectLookDown();

    // Input mapping
    void MapToGamepad();
    void MapToKeyboard();

private:
    IRuntimeInterface* m_runtime = nullptr;
    ControllerState m_controllerStates[2];
    ControllerPose m_controllerPoses[2];
    bool m_initialized = false;
};

} // namespace GTA5VR
