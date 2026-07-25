#pragma once

// ImageFit - pure decision math for mapping the game backbuffer into the
// per-eye VR render targets. No D3D/XR/Windows dependencies, so it is
// unit-testable in isolation (tests/TestImageFit.cpp), same pattern as
// EyeDelivery.hpp. The D3D mechanics that consume this math live in
// D3DHook/D3DHooks_VRManager.hpp (RenderBlit / GetImageTransform).
//
// Two contracts live here:
//
// 1) ComputeContainFit - the mono-fallback blit mapping. While the camera is
//    unresolved both eyes show the SAME backbuffer (zero disparity). The old
//    mapping stretched the source across the whole eye texture (e.g. a
//    3840x1600 backbuffer into a 4096x4045 eye texture = ~2.4x more vertical
//    stretch than horizontal), destroying the image's aspect: the world lost
//    its size constancy and read as "far away / wrong". Contain-fit instead
//    maps the FULL source, aspect-preserved, into the largest centered rect
//    of the target; the caller clears the remaining bars to black. The
//    result is a theater-screen feel at a sane virtual distance. The
//    unchanged blit shader then applies imageScale (zoom around the fitted
//    image's center = the user "virtual distance" knob) and the image
//    offsets on top.
//
// 2) ComputeUserImageOffsets - the user image-alignment contract applied by
//    GetImageTransform. imageOffsetX is a STEREO CONVERGENCE adjustment: the
//    two eyes shift in opposite directions (scaled by eyeSign). imageOffsetY
//    is a COMMON-MODE vertical position: both eyes must shift identically -
//    multiplying it by eyeSign (the old behavior) created vertical
//    disparity, which the visual system cannot fuse and which users report
//    as "the eyes are not aligned".

#include <cstdint>

namespace OVRInject {
namespace Stereo {

// Scale from user imageOffsetX/Y setting units to UV fractions (kept
// identical to the historical D3DHook constant).
constexpr float kImageOffsetScale = 0.01f;

struct ContainFitRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    // True when the fitted rect does not cover the whole target, i.e. the
    // caller must clear the target (letterbox/pillarbox bars) before drawing.
    bool letterboxed = false;
};

// Largest rect centered in (dstW x dstH) that preserves the source aspect
// (srcW x srcH). Pure integer math with round-to-nearest, so the result is
// deterministic: any two targets of the same size get the identical rect
// (both eyes stay pixel-aligned). Degenerate inputs (any zero dimension)
// fall back to the full target - the historical full-stretch behavior.
inline ContainFitRect ComputeContainFit(uint32_t srcW, uint32_t srcH,
                                        uint32_t dstW, uint32_t dstH) {
    ContainFitRect rect = {};
    rect.width = dstW;
    rect.height = dstH;
    if (srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) {
        return rect;
    }

    const uint64_t srcW64 = srcW;
    const uint64_t srcH64 = srcH;
    const uint64_t dstW64 = dstW;
    const uint64_t dstH64 = dstH;

    if (dstW64 * srcH64 <= dstH64 * srcW64) {
        // Target is wider (relative) than the source: width-limited fit,
        // letterbox bars top/bottom.
        rect.width = dstW;
        rect.height = static_cast<uint32_t>((dstW64 * srcH64 + srcW64 / 2) / srcW64);
    } else {
        // Target is taller (relative) than the source: height-limited fit,
        // pillarbox bars left/right.
        rect.height = dstH;
        rect.width = static_cast<uint32_t>((dstH64 * srcW64 + srcH64 / 2) / srcH64);
    }
    if (rect.width == 0) {
        rect.width = 1;  // never emit a zero-sized viewport
    }
    if (rect.height == 0) {
        rect.height = 1;
    }
    if (rect.width > dstW) {
        rect.width = dstW;  // round-to-nearest can overshoot by <1px
    }
    if (rect.height > dstH) {
        rect.height = dstH;
    }
    rect.x = (dstW - rect.width) / 2;
    rect.y = (dstH - rect.height) / 2;
    rect.letterboxed = (rect.width != dstW) || (rect.height != dstH);
    return rect;
}

// User image-alignment settings -> UV offsets for one eye. eyeSign is -1 for
// the left eye, +1 for the right, 0 for a mono blit shared by both eyes.
//
//   * outOffsetX (convergence): signed per eye - the eyes shift in opposite
//     directions, changing where the two images converge. In a mono blit
//     (eyeSign == 0) this is intentionally zero: a convergence adjustment is
//     meaningless for a single shared image and must not shift it sideways.
//   * outOffsetY (vertical position): common-mode - identical for both eyes.
//     A vertical per-eye difference is vertical disparity, which the visual
//     system cannot fuse ("eyes not aligned"). It also applies to a mono
//     blit, where it simply moves the shared image vertically.
inline void ComputeUserImageOffsets(float screenOffsetX, float screenOffsetY,
                                    float eyeSign,
                                    float& outOffsetX, float& outOffsetY) {
    outOffsetX = screenOffsetX * kImageOffsetScale * eyeSign;
    outOffsetY = screenOffsetY * kImageOffsetScale;
}

} // namespace Stereo
} // namespace OVRInject
