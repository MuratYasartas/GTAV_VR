#pragma once

// VRCamera - scripted VR camera driven through the GTAVRBridge channel.
//
// Replaces the (proven-dead on b3889) direct memory-matrix write path: the
// game renders a scripted camera that we parent to the gameplay camera each
// frame, applying the VR head delta and the per-eye stereo offset. All
// native calls execute on the bridge's ScriptHookV script thread; this
// class only reads the shared snapshot and pushes ops.
//
// Rotation conventions (GTA V, rotation order 2, degrees):
//   rx = pitch about cam-right (positive = look up)
//   ry = roll  about cam-forward
//   rz = yaw   about world-up  (0 = north/+Y, positive = counterclockwise)
// The internal composition frame mirrors GtaCameraHook: matrix rows are
// (right, up, forward) with GTA-world values. Euler<->matrix conversion is
// GTA-native and self-consistent, so a convention mistake can only mirror
// the head delta (one-line sign fix), never corrupt the base pose.

#include "../VR/IVRBackend.hpp"
#include <DirectXMath.h>
#include <cstdint>

namespace OVRInject {
namespace Game {

class GtaGameState;

class VRCamera {
public:
    explicit VRCamera(VR::IVRBackend* backend);
    ~VRCamera();

    // Bridge channel usable (mapping open). Cheap.
    bool IsAvailable() const;
    // Scripted cam created AND currently rendering.
    bool IsEngaged() const { return engaged_; }

    // One update per frame from the stereo engine's camera-write section.
    // eye = the eye the NEXT game render should carry (AER parity).
    void Update(VR::Eye eye, GtaGameState* gameState);

    // Re-reference the head pose (recenter hotkey).
    void Recenter();

    // Best-effort release of the scripted cam (mod unload).
    void Shutdown();

private:
    void Engage();
    void Disengage();

    VR::IVRBackend* backend_;
    int cam_ = 0;
    bool createRequested_ = false;
    bool engaged_ = false;

    // Reference (recenter) pose
    bool hasRefRot_ = false;
    bool hasRefPos_ = false;
    DirectX::XMMATRIX refRot_ = DirectX::XMMatrixIdentity();
    DirectX::XMVECTOR refPos_ = DirectX::XMVectorZero();

    float lastFov_ = -1.0f;
    uint32_t updateCount_ = 0;
    uint32_t lastSnapSeq_ = 0;
    uint64_t lastBridgeRetryMs_ = 0;
    bool loggedFirstPose_ = false;
    float lastHeadYawDeg_ = 0.0f;
    bool haveLastHeadYaw_ = false;
};

} // namespace Game
} // namespace OVRInject
