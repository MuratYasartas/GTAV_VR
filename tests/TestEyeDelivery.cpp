// Eye-delivery state machine tests (OVRInject/Stereo/EyeDelivery.hpp).
//
// These pin the AER delivery contract that StereoEngine::OnPresent executes
// with real textures:
//   * which eye texture backs each submitted layer on frames F, F+1, F+2;
//   * the no-cross-eye-contamination invariant (layer i <- eye texture i),
//     with the single documented warmup exception right after a reset;
//   * missed/pass-through frames never advance parity, so the blit parity
//     and the camera-write parity cannot desync;
//   * the camera-write eye is always the eye the game will render NEXT.
//
// The state machine is dependency-free, so these tests must always pass
// standalone.

#include "TestFramework.hpp"

#include "../OVRInject/Stereo/EyeDelivery.hpp"

using OVRInject::Stereo::EyeDelivery;

namespace {

constexpr int L = EyeDelivery::kLeft;
constexpr int R = EyeDelivery::kRight;

// Simulates one completed engine frame: returns the plan that governed it,
// then advances the machine exactly as StereoEngine does at the end of a
// completed Present.
EyeDelivery::FramePlan CompletedFrame(EyeDelivery& delivery) {
    const EyeDelivery::FramePlan plan = delivery.PlanAlternateEye();
    delivery.Advance();
    return plan;
}

} // namespace

// Frames F, F+1, F+2 alternate the fresh (blitted) eye L, R, L, and the
// camera write always targets the eye the game renders NEXT.
TEST(EyeDelivery_AlternatesFreshEyeAndCameraWriteEye) {
    EyeDelivery delivery;

    const EyeDelivery::FramePlan f0 = delivery.PlanAlternateEye();
    CHECK(f0.renderEye == L);
    CHECK(f0.cameraWriteEye == R);
    delivery.Advance();

    const EyeDelivery::FramePlan f1 = delivery.PlanAlternateEye();
    CHECK(f1.renderEye == R);
    CHECK(f1.cameraWriteEye == L);
    delivery.Advance();

    const EyeDelivery::FramePlan f2 = delivery.PlanAlternateEye();
    CHECK(f2.renderEye == L);
    CHECK(f2.cameraWriteEye == R);
}

// Steady state: submitted layer i is ALWAYS backed by eye texture i, the
// fresh flag marks exactly the render eye, and both eyes serve their own
// content. This is the "real stereo" invariant - a violation here means two
// layers carrying the same texture, i.e. fake stereo.
TEST(EyeDelivery_SteadyStateLayersNeverCrossEyes) {
    EyeDelivery delivery;

    // Warm up both eyes (frame 0 and 1 produce L and R respectively).
    CompletedFrame(delivery);
    CompletedFrame(delivery);

    for (int frame = 0; frame < 16; ++frame) {
        const EyeDelivery::FramePlan plan = CompletedFrame(delivery);

        // Layer -> texture mapping: strict identity, never the other eye.
        CHECK(plan.layerTexture[L] == L);
        CHECK(plan.layerTexture[R] == R);

        // Exactly one layer is fresh: the render eye's.
        CHECK(plan.layerFresh[plan.renderEye]);
        CHECK(!plan.layerFresh[EyeDelivery::OtherEye(plan.renderEye)]);

        // Both layers carry valid own-rendered content.
        CHECK(plan.layerValid[L]);
        CHECK(plan.layerValid[R]);

        // The two layers never share a texture while both eyes are valid.
        CHECK(plan.layerTexture[L] != plan.layerTexture[R]);
    }
}

// A missed frame (pass-through Present: no blit, no camera write, no
// Advance) must leave the plan completely unchanged: the eye refreshed on
// the next completed frame is the SAME eye, because the game camera still
// holds the write targeting it. Advancing on a missed frame would blit the
// backbuffer into the wrong eye's texture (cross-eye contamination).
TEST(EyeDelivery_MissedFrameKeepsParityAndContent) {
    EyeDelivery delivery;

    // Complete one frame (L produced), then plan the next (R).
    const EyeDelivery::FramePlan first = CompletedFrame(delivery);
    CHECK(first.renderEye == L);

    const EyeDelivery::FramePlan beforeMiss = delivery.PlanAlternateEye();
    CHECK(beforeMiss.renderEye == R);
    CHECK(beforeMiss.cameraWriteEye == L);

    // Missed frame: engine takes a pass-through early return - no Advance.
    const EyeDelivery::FramePlan afterMiss = delivery.PlanAlternateEye();
    CHECK(afterMiss.renderEye == beforeMiss.renderEye);
    CHECK(afterMiss.cameraWriteEye == beforeMiss.cameraWriteEye);
    CHECK(afterMiss.layerTexture[L] == beforeMiss.layerTexture[L]);
    CHECK(afterMiss.layerTexture[R] == beforeMiss.layerTexture[R]);
    CHECK(afterMiss.layerValid[L] == beforeMiss.layerValid[L]);
    CHECK(afterMiss.layerValid[R] == beforeMiss.layerValid[R]);
    CHECK(afterMiss.layerFresh[R]);  // still the eye this frame would refresh

    // Two missed frames in a row: still identical.
    const EyeDelivery::FramePlan afterSecondMiss = delivery.PlanAlternateEye();
    CHECK(afterSecondMiss.renderEye == R);

    // The next completed frame refreshes R and parity resumes alternating.
    const EyeDelivery::FramePlan resumed = CompletedFrame(delivery);
    CHECK(resumed.renderEye == R);
    CHECK(delivery.PlanAlternateEye().renderEye == L);
}

// The very first frame after a reset: the stale eye has no own content yet.
// Its layer explicitly borrows the fresh eye's texture (mono warmup) instead
// of showing garbage - the single permitted cross-eye frame. From the next
// frame on, the mapping is strict.
TEST(EyeDelivery_FirstFrameAfterResetWarmsUpStaleEye) {
    EyeDelivery delivery;

    const EyeDelivery::FramePlan warmup = delivery.PlanAlternateEye();
    CHECK(warmup.renderEye == L);
    CHECK(warmup.layerValid[L]);          // fresh this frame
    CHECK(!warmup.layerValid[R]);         // never produced: no own content
    CHECK(warmup.layerTexture[L] == L);
    CHECK(warmup.layerTexture[R] == L);   // documented warmup borrow
    CHECK(warmup.layerFresh[L]);
    CHECK(!warmup.layerFresh[R]);

    delivery.Advance();

    const EyeDelivery::FramePlan second = delivery.PlanAlternateEye();
    CHECK(second.renderEye == R);
    CHECK(second.layerValid[L]);          // produced on the warmup frame
    CHECK(second.layerValid[R]);          // fresh this frame
    CHECK(second.layerTexture[L] == L);   // strict from here on
    CHECK(second.layerTexture[R] == R);
}

// Reset (mode switch / camera-not-ready transition) re-pins the phase to
// Left and clears per-eye validity, so the first AER frame afterwards
// captures the Left-rendered backbuffer (the non-AER paths write the Left
// camera every frame) and the warmup logic applies again.
TEST(EyeDelivery_ResetPinsLeftPhaseAndClearsProduced) {
    EyeDelivery delivery;

    CompletedFrame(delivery);
    CompletedFrame(delivery);
    CompletedFrame(delivery);
    CHECK(delivery.FrameIndex() == 3);
    CHECK(delivery.Produced(L));
    CHECK(delivery.Produced(R));

    delivery.Reset();
    CHECK(delivery.FrameIndex() == 0);
    CHECK(!delivery.Produced(L));
    CHECK(!delivery.Produced(R));

    const EyeDelivery::FramePlan plan = delivery.PlanAlternateEye();
    CHECK(plan.renderEye == L);
    CHECK(plan.cameraWriteEye == R);
    CHECK(!plan.layerValid[R]);           // warmup required again
    CHECK(plan.layerTexture[R] == L);
}
