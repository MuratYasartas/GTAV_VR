#include "TestFramework.hpp"

#include "../OVRInject/Game/ShvBridgeShared.hpp"

using namespace OVRInject::Game::ShvBridge;

TEST(ShvBridgeQueue_CapacityAndFifoAreExact) {
    BridgeState state = {};
    for (uint32_t i = 0; i < kQueueCapacity - 1; ++i) {
        const BridgeOp operation = {OpCamSetFov, static_cast<int32_t>(i),
                                    static_cast<float>(i), 0.0f, 0.0f};
        CHECK(TryPush(&state, operation));
    }
    const BridgeOp overflow = {OpCamCreate, 0, 0, 0, 0};
    CHECK(!TryPush(&state, overflow));

    for (uint32_t i = 0; i < kQueueCapacity - 1; ++i) {
        BridgeOp operation = {};
        CHECK(TryPop(&state, operation));
        CHECK(operation.type == OpCamSetFov);
        CHECK(operation.cam == static_cast<int32_t>(i));
        CHECK_NEAR(operation.a, static_cast<float>(i), 0.0f);
    }
    BridgeOp empty = {};
    CHECK(!TryPop(&state, empty));
}

TEST(ShvBridgeQueue_BatchPublishIsAllOrNothing) {
    BridgeState state = {};
    for (uint32_t i = 0; i < kQueueCapacity - 2; ++i) {
        const BridgeOp operation = {OpCamSetFov, 1, 60.0f, 0.0f, 0.0f};
        CHECK(TryPush(&state, operation));
    }

    const uint32_t tailBefore = LoadAcquire(&state.qTail);
    const BridgeOp transform[] = {
        {OpCamSetCoord, 7, 1.0f, 2.0f, 3.0f},
        {OpCamSetRot, 7, 4.0f, 5.0f, 6.0f},
    };
    CHECK(!TryPushBatch(&state, transform, 2));
    CHECK(LoadAcquire(&state.qTail) == tailBefore);

    BridgeOp removed = {};
    CHECK(TryPop(&state, removed));
    CHECK(TryPushBatch(&state, transform, 2));

    while (TryPop(&state, removed)) {
        if (removed.type == OpCamSetCoord) {
            CHECK(TryPop(&state, removed));
            CHECK(removed.type == OpCamSetRot);
            CHECK(removed.cam == 7);
            return;
        }
    }
    CHECK(false);
}
