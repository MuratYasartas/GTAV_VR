#include "ShvNatives.hpp"
#include "../Log.hpp"

namespace OVRInject {
namespace Game {

static constexpr uint32_t kBridgeMagic = 0x32564847u; // 'GHV2'
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
    state_ = static_cast<BridgeState*>(
        MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BridgeState)));
    if (!state_) {
        LOGSTR("ShvNatives: failed to map GTAVRBridge channel\n");
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }
    if (state_->magic != kBridgeMagic) {
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

void ShvNatives::PushOp(uint32_t type, int cam, float a, float b, float c) {
    if (!state_) return;
    uint32_t tail = state_->qTail;
    uint32_t next = (tail + 1) % 64;
    if (next == state_->qHead) return; // full - drop (re-issued every frame)
    BridgeOp& op = state_->queue[tail];
    op.type = type;
    op.cam = cam;
    op.a = a;
    op.b = b;
    op.c = c;
    std::atomic_thread_fence(std::memory_order_release);
    state_->qTail = next;
}

void ShvNatives::SetRawYawPitch(float yawDeg, float pitchDeg) {
    PushOp(OpSetRawYawPitch, 0, yawDeg, pitchDeg, 0.0f);
}
void ShvNatives::SetRelativeHeadingPitch(float headingDeg, float pitchDeg, float clampValue) {
    PushOp(OpSetRelativeHeadingPitch, 0, headingDeg, pitchDeg, clampValue);
}
void ShvNatives::CamCreate() { PushOp(OpCamCreate, 0, 0, 0, 0); }
void ShvNatives::CamSetCoord(int cam, float x, float y, float z) { PushOp(OpCamSetCoord, cam, x, y, z); }
void ShvNatives::CamSetRot(int cam, float x, float y, float z) { PushOp(OpCamSetRot, cam, x, y, z); }
void ShvNatives::CamSetFov(int cam, float fov) { PushOp(OpCamSetFov, cam, fov, 0, 0); }
void ShvNatives::CamSetActive(int cam, bool active) { PushOp(OpCamSetActive, cam, active ? 1.0f : 0.0f, 0, 0); }
void ShvNatives::CamRender(int cam, bool render) { PushOp(OpCamRender, cam, render ? 1.0f : 0.0f, 0, 0); }
void ShvNatives::CamDestroy(int cam) { PushOp(OpCamDestroy, cam, 1.0f, 0, 0); }

} // namespace Game
} // namespace OVRInject
