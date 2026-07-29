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
    bool IsAvailable() const { return state_ != nullptr; }
    const CamSnapshot& Snapshot() {
        if (state_) {
            snapshot_.coordX = state_->coord[0];
            snapshot_.coordY = state_->coord[1];
            snapshot_.coordZ = state_->coord[2];
            snapshot_.rotX = state_->rot[0];
            snapshot_.rotY = state_->rot[1];
            snapshot_.rotZ = state_->rot[2];
            snapshot_.fov = state_->fov;
            snapshot_.relHeading = state_->relHeading;
            snapshot_.relPitch = state_->relPitch;
            snapshot_.pedHeadX = state_->pedHead[0];
            snapshot_.pedHeadY = state_->pedHead[1];
            snapshot_.pedHeadZ = state_->pedHead[2];
            snapshot_.sequence = state_->stateSeq;
        }
        return snapshot_;
    }

    // Control ops (written into the bridge's queue; executed on its script
    // thread, fire-and-forget).
    void SetRawYawPitch(float yawDeg, float pitchDeg);
    void SetRelativeHeadingPitch(float headingDeg, float pitchDeg, float clampValue);
    void CamCreate();
    int  LastCreatedCam() const { return state_ ? state_->lastCreateResult : 0; }
    void CamSetCoord(int cam, float x, float y, float z);
    void CamSetRot(int cam, float x, float y, float z);
    void CamSetFov(int cam, float fov);
    void CamSetActive(int cam, bool active);
    void CamRender(int cam, bool render);
    void CamDestroy(int cam);

private:
    ShvNatives() = default;
    void PushOp(uint32_t type, int cam, float a, float b, float c);

    enum OpType : uint32_t {
        OpSetRawYawPitch = 1,
        OpSetRelativeHeadingPitch,
        OpCamCreate,
        OpCamSetCoord,
        OpCamSetRot,
        OpCamSetFov,
        OpCamSetActive,
        OpCamRender,
        OpCamDestroy,
    };

    struct BridgeOp {
        uint32_t type;
        int32_t cam;
        float a, b, c;
    };
    struct BridgeState {
        uint32_t magic;
        uint32_t stateSeq;
        float coord[3];
        float rot[3];
        float fov;
        float relHeading;
        float relPitch;
        float pedHead[3];
        uint32_t qHead;
        uint32_t qTail;
        int32_t lastCreateResult;
        uint32_t bridgeAlive;
        BridgeOp queue[64];
    };

    HANDLE mapping_ = nullptr;
    BridgeState* state_ = nullptr;
    CamSnapshot snapshot_{};
    std::atomic<uint32_t> lastLoggedSeq_{0};
};

} // namespace Game
} // namespace OVRInject
