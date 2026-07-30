#include "ShvNatives.hpp"
#include "../Log.hpp"

namespace OVRInject {
namespace Game {

static constexpr const char* kBridgeMapping = "GTAVR_SHV_BRIDGE";

ShvNatives& ShvNatives::Get() {
    static ShvNatives instance;
    return instance;
}

bool ShvNatives::Initialize() {
    if (state_) return true;

    mapping_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, kBridgeMapping);
    if (!mapping_) {
        LOGSTR("ShvNatives: GTAVRBridge channel not found (bridge .asi not loaded)\n");
        return false;
    }
    state_ = static_cast<ShvBridge::BridgeState*>(
        MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0,
                      sizeof(ShvBridge::BridgeState)));
    if (!state_) {
        LOGSTR("ShvNatives: failed to map GTAVRBridge channel\n");
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }
    if (state_->magic != ShvBridge::kMagic) {
        LOGSTRF("ShvNatives: bad bridge magic 0x%08X - version mismatch\n", state_->magic);
        UnmapViewOfFile(state_);
        CloseHandle(mapping_);
        state_ = nullptr;
        mapping_ = nullptr;
        return false;
    }
    LOGSTRF("ShvNatives: bridge connected (alive=%u, seq=%u)\n", state_->bridgeAlive, state_->stateSeq);
    return true;
}

void ShvNatives::Shutdown() {
    if (state_) {
        UnmapViewOfFile(state_);
        state_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
}

bool ShvNatives::IsAvailable() const {
    return state_ && ShvBridge::LoadAcquire(&state_->bridgeAlive) != 0;
}

int ShvNatives::LastCreatedCam() const {
    return state_ ? static_cast<int32_t>(
        ShvBridge::LoadAcquire(
            reinterpret_cast<const uint32_t*>(&state_->lastCreateResult))) : 0;
}

bool ShvNatives::PushOps(
    const ShvBridge::BridgeOp* operations, uint32_t count) {
    if (!IsAvailable()) return false;
    const bool pushed = ShvBridge::TryPushBatch(state_, operations, count);
    if (!pushed) {
        const uint32_t seq = ShvBridge::LoadAcquire(&state_->stateSeq);
        uint32_t previous = lastLoggedSeq_.load();
        if (seq - previous >= 120 &&
            lastLoggedSeq_.compare_exchange_strong(previous, seq)) {
            LOGSTR("ShvNatives: bridge queue full - operation deferred\n");
        }
    }
    return pushed;
}

bool ShvNatives::PushOp(
    uint32_t type, int cam, float a, float b, float c) {
    const ShvBridge::BridgeOp operation = {type, cam, a, b, c};
    return PushOps(&operation, 1);
}

void ShvNatives::SetRawYawPitch(float yawDeg, float pitchDeg) {
    PushOp(ShvBridge::OpSetRawYawPitch, 0, yawDeg, pitchDeg, 0.0f);
}
void ShvNatives::SetRelativeHeadingPitch(float headingDeg, float pitchDeg, float clampValue) {
    PushOp(ShvBridge::OpSetRelativeHeadingPitch, 0, headingDeg, pitchDeg, clampValue);
}
bool ShvNatives::CamCreate() {
    if (!IsAvailable()) {
        return false;
    }
    // A previous scripted camera may have been destroyed while the bridge
    // retained its last result. Clear it before publishing a new request so
    // VRCamera cannot adopt a stale handle during the request/response gap.
    InterlockedExchange(
        reinterpret_cast<volatile LONG*>(&state_->lastCreateResult), 0);
    return PushOp(ShvBridge::OpCamCreate, 0, 0, 0, 0);
}

bool ShvNatives::CamSetTransform(
    int cam,
    float x, float y, float z,
    float pitch, float roll, float yaw) {
    const ShvBridge::BridgeOp operations[] = {
        {ShvBridge::OpCamSetCoord, cam, x, y, z},
        {ShvBridge::OpCamSetRot, cam, pitch, roll, yaw},
    };
    return PushOps(operations, 2);
}

bool ShvNatives::CamSetFov(int cam, float fov) {
    return PushOp(ShvBridge::OpCamSetFov, cam, fov, 0, 0);
}

bool ShvNatives::CamSetEnabled(int cam, bool enabled) {
    const float value = enabled ? 1.0f : 0.0f;
    const ShvBridge::BridgeOp operations[] = {
        enabled
            ? ShvBridge::BridgeOp{ShvBridge::OpCamSetActive, cam, value, 0, 0}
            : ShvBridge::BridgeOp{ShvBridge::OpCamRender, cam, value, 0, 0},
        enabled
            ? ShvBridge::BridgeOp{ShvBridge::OpCamRender, cam, value, 0, 0}
            : ShvBridge::BridgeOp{ShvBridge::OpCamSetActive, cam, value, 0, 0},
    };
    return PushOps(operations, 2);
}

bool ShvNatives::CamDestroy(int cam) {
    return PushOp(ShvBridge::OpCamDestroy, cam, 1.0f, 0, 0);
}

} // namespace Game
} // namespace OVRInject
