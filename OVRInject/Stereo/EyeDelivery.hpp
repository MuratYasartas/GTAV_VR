#pragma once

// EyeDelivery - the AER (alternate-eye rendering, ADR-0002) eye-delivery
// state machine.
//
// This is pure decision logic with no D3D/XR/Windows dependencies so it can
// be unit-tested in isolation (tests/TestEyeDelivery.cpp);
// StereoEngine::OnPresent drives it and executes the plan with real
// textures. The model:
//
//   * The game renders ONE viewpoint per frame. In AER the viewpoint
//     alternates L,R,L,R... because the game camera is rewritten once per
//     frame with the NEXT eye's pose and the write is consumed by the next
//     game render (one-frame delay, see StereoEngine::OnPresent).
//   * Each eye owns a persistent eye texture. A completed frame blits the
//     backbuffer into the FRESH eye's texture only; the other (stale) eye
//     keeps its own previous own-rendered content. Submitted layer i is
//     ALWAYS backed by eye texture i - the stale eye is re-submitted with
//     its own texture every frame, so there is no cross-eye contamination.
//   * Parity advances exactly once per COMPLETED Present (Advance()). A
//     missed/pass-through frame (no blit, no camera write, no Advance)
//     therefore cannot desync the blit parity from the camera-write parity:
//     both derive from the same counter and only completed frames move it.
//   * Reset() pins the phase so the first AER frame after a mode switch or
//     a camera-ready transition captures the Left-rendered backbuffer (the
//     non-AER paths write the Left camera every frame).
//   * An eye that was never produced since Reset has no own content. Its
//     layer falls back to the fresh eye's texture for exactly that frame
//     (explicit mono warmup - the single, plan-visible exception to the
//     no-cross-eye rule); from the next frame on the eye serves its own.
//
// Eyes are plain ints here (0 = left, 1 = right) to keep this header free of
// the VR backend headers; the values are identical to VR::Eye and a
// static_assert at the StereoEngine call site pins that.

#include <cstdint>

namespace OVRInject {
namespace Stereo {

class EyeDelivery {
public:
    static constexpr int kLeft = 0;
    static constexpr int kRight = 1;

    // What one completed AER frame must do. Computed once per Present,
    // before the blit, and consumed by the blit, the submit and the camera
    // write of the same frame.
    struct FramePlan {
        int renderEye = kLeft;             // backbuffer is blitted into this eye's texture
        int cameraWriteEye = kRight;       // this eye's pose is written to the game camera
        int layerTexture[2] = {kLeft, kRight};  // eye-texture index backing submitted layer L/R
        bool layerFresh[2] = {false, false};    // layer content produced this frame
        bool layerValid[2] = {false, false};    // layer content produced at least once since reset
    };

    // Plan for the current frame (valid until Advance or Reset).
    FramePlan PlanAlternateEye() const {
        FramePlan plan = {};
        plan.renderEye = RenderEye();
        plan.cameraWriteEye = OtherEye(plan.renderEye);
        for (int layer = 0; layer < 2; ++layer) {
            const bool fresh = (layer == plan.renderEye);
            plan.layerFresh[layer] = fresh;
            plan.layerValid[layer] = fresh || produced_[layer];
            // Warmup fallback (see header comment): a never-produced eye has
            // no own content yet, so its layer borrows the fresh eye's
            // texture for this one frame instead of showing garbage/black.
            plan.layerTexture[layer] = plan.layerValid[layer] ? layer : plan.renderEye;
        }
        return plan;
    }

    // The eye whose render the current frame's backbuffer contains.
    int RenderEye() const { return static_cast<int>(frameIndex_ & 1); }  // even=L, odd=R

    static int OtherEye(int eye) { return eye == kLeft ? kRight : kLeft; }

    // Marks the current frame completed: the render eye's texture now holds
    // valid content and parity advances to the next eye. Call exactly once
    // per completed Present - never on pass-through frames.
    void Advance() {
        produced_[RenderEye()] = true;
        ++frameIndex_;
    }

    // Phase pin for leaving AER / camera-not-ready transitions (see header).
    void Reset() {
        frameIndex_ = 0;
        produced_[0] = produced_[1] = false;
    }

    uint64_t FrameIndex() const { return frameIndex_; }
    bool Produced(int eye) const { return produced_[eye & 1]; }

private:
    uint64_t frameIndex_ = 0;
    bool produced_[2] = {false, false};
};

} // namespace Stereo
} // namespace OVRInject
