#pragma once

// ShvNatives - client of the GTAVRBridge.asi shared-memory channel.
//
// The bridge (loaded by the ASI loader) owns a ScriptHookV script thread and
// publishes a live camera snapshot + a native-op queue in a named mapping.
// Direct scriptRegister from an injected DLL never schedules (ScriptHookV
// only honors its own loader chain), so this is THE native access path.

#include <atomic>
#include <cstdint>
#include <Windows.h>
#include "ShvBridgeShared.hpp"

namespace OVRInject {
namespace Game {

class ShvNatives {
public:
    struct CamSnapshot {
        float coordX = 0, coordY = 0, coordZ = 0;
        float rotX = 0, rotY = 0, rotZ = 0;
        float fov = 0;
        float relHeading = 0;
        float relPitch = 0;
        float pedHeadX = 0, pedHeadY = 0, pedHeadZ = 0;
        uint32_t sequence = 0;
    };

    static ShvNatives& Get();

    // Open the bridge mapping. Returns false cleanly when the bridge .asi is
    // not loaded in the game (ASI loader absent).
    bool Initialize();
    void Shutdown();
    bool IsAvailable() const;
    const CamSnapshot& Snapshot() {
        if (!IsAvailable()) {
            return snapshot_;
        }
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint32_t begin = ShvBridge::LoadAcquire(&state_->stateSeq);
            if (begin & 1u) {
                YieldProcessor();
                continue;
            }
            MemoryBarrier();
            CamSnapshot candidate;
            candidate.coordX = state_->coord[0];
            candidate.coordY = state_->coord[1];
            candidate.coordZ = state_->coord[2];
            candidate.rotX = state_->rot[0];
            candidate.rotY = state_->rot[1];
            candidate.rotZ = state_->rot[2];
            candidate.fov = state_->fov;
            candidate.relHeading = state_->relHeading;
            candidate.relPitch = state_->relPitch;
            candidate.pedHeadX = state_->pedHead[0];
            candidate.pedHeadY = state_->pedHead[1];
            candidate.pedHeadZ = state_->pedHead[2];
            MemoryBarrier();
            const uint32_t end = ShvBridge::LoadAcquire(&state_->stateSeq);
            if (begin == end && !(end & 1u)) {
                candidate.sequence = end;
                snapshot_ = candidate;
                break;
            }
        }
        return snapshot_;
    }

    // Control ops (written into the bridge's queue; executed on its script
    // thread, fire-and-forget).
    void SetRawYawPitch(float yawDeg, float pitchDeg);
    void SetRelativeHeadingPitch(float headingDeg, float pitchDeg, float clampValue);
    bool CamCreate();
    int LastCreatedCam() const;
    bool CamSetTransform(
        int cam,
        float x, float y, float z,
        float pitch, float roll, float yaw);
    bool CamSetFov(int cam, float fov);
    bool CamSetEnabled(int cam, bool enabled);
    bool CamDestroy(int cam);

private:
    ShvNatives() = default;
    bool PushOp(uint32_t type, int cam, float a, float b, float c);
    bool PushOps(const ShvBridge::BridgeOp* operations, uint32_t count);

    HANDLE mapping_ = nullptr;
    ShvBridge::BridgeState* state_ = nullptr;
    CamSnapshot snapshot_{};
    std::atomic<uint32_t> lastLoggedSeq_{0};
};

} // namespace Game
} // namespace OVRInject
