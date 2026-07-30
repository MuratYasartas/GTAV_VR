#pragma once

#include <Windows.h>
#include <cstdint>

namespace OVRInject {
namespace Game {
namespace ShvBridge {

constexpr uint32_t kMagic = 0x32564847u; // 'GHV2'
constexpr uint32_t kQueueCapacity = 64;

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
    BridgeOp queue[kQueueCapacity];
};
static_assert(sizeof(BridgeState) == 1352,
              "GTAVR bridge ABI changed; bump magic and bridge layout");

inline uint32_t LoadAcquire(const uint32_t* value) {
    return static_cast<uint32_t>(InterlockedCompareExchange(
        reinterpret_cast<volatile LONG*>(const_cast<uint32_t*>(value)), 0, 0));
}

inline void StoreRelease(uint32_t* target, uint32_t value) {
    InterlockedExchange(
        reinterpret_cast<volatile LONG*>(target), static_cast<LONG>(value));
}

inline uint32_t QueueUsed(uint32_t head, uint32_t tail) {
    return (tail + kQueueCapacity - head) % kQueueCapacity;
}

// Single-producer batch publish. The consumer cannot observe a partial batch
// because qTail advances only after every operation payload is written.
inline bool TryPushBatch(
    BridgeState* state, const BridgeOp* operations, uint32_t count) {
    if (!state || !operations || count == 0 || count >= kQueueCapacity) {
        return false;
    }

    const uint32_t tail = LoadAcquire(&state->qTail);
    const uint32_t head = LoadAcquire(&state->qHead);
    const uint32_t freeSlots =
        (kQueueCapacity - 1u) - QueueUsed(head, tail);
    if (count > freeSlots) {
        return false;
    }

    uint32_t next = tail;
    for (uint32_t i = 0; i < count; ++i) {
        state->queue[next] = operations[i];
        next = (next + 1u) % kQueueCapacity;
    }
    MemoryBarrier();
    StoreRelease(&state->qTail, next);
    return true;
}

inline bool TryPush(BridgeState* state, const BridgeOp& operation) {
    return TryPushBatch(state, &operation, 1);
}

// Single-consumer pop. Interlocked index loads/stores provide the
// cross-process acquire/release contract missing from plain uint32 accesses.
inline bool TryPop(BridgeState* state, BridgeOp& operation) {
    if (!state) {
        return false;
    }
    const uint32_t head = LoadAcquire(&state->qHead);
    const uint32_t tail = LoadAcquire(&state->qTail);
    if (head == tail) {
        return false;
    }
    MemoryBarrier();
    operation = state->queue[head];
    MemoryBarrier();
    StoreRelease(&state->qHead, (head + 1u) % kQueueCapacity);
    return true;
}

} // namespace ShvBridge
} // namespace Game
} // namespace OVRInject
