// Image-fit / image-offset contract tests (OVRInject/Stereo/ImageFit.hpp).
//
// These pin the two mono/alignment contracts consumed by the D3DHook blit:
//   * ComputeContainFit: the mono-fallback backbuffer->eye-texture mapping.
//     Aspect must be preserved (contain-fit), the fitted rect centered, the
//     result deterministic (both eyes get the identical mapping), and
//     degenerate inputs must fall back to the historical full-target fill.
//   * ComputeUserImageOffsets: imageOffsetX is a per-eye convergence shift
//     (opposite signs per eye, zero for a shared mono blit); imageOffsetY is
//     common-mode (IDENTICAL for both eyes - a per-eye vertical difference
//     is unfusable vertical disparity, the "eyes not aligned" bug).
//
// The header is dependency-free, so these tests must always pass standalone.

#include "TestFramework.hpp"

#include "../OVRInject/Stereo/ImageFit.hpp"

using OVRInject::Stereo::ComputeContainFit;
using OVRInject::Stereo::ComputeUserImageOffsets;
using OVRInject::Stereo::ContainFitRect;

// The live case: a 3840x1600 ultrawide backbuffer into a 4096x4045
// (near-square) eye texture. Width-limited fit: full width, height
// 4096*1600/3840 = 1706.67 -> 1707 (round-to-nearest), centered vertically
// at y = (4045-1707)/2 = 1169, letterboxed.
TEST(ImageFit_UltrawideIntoNearSquareEyeTexture) {
    const ContainFitRect fit = ComputeContainFit(3840, 1600, 4096, 4045);
    CHECK(fit.width == 4096);
    CHECK(fit.height == 1707);
    CHECK(fit.x == 0);
    CHECK(fit.y == 1169);
    CHECK(fit.letterboxed);
}

// Source and target aspects match: the fit covers the whole target and no
// clear/letterbox is needed (historical full-fill behavior preserved).
TEST(ImageFit_SameAspectFillsTargetExactly) {
    const ContainFitRect fit = ComputeContainFit(1920, 1080, 1920, 1080);
    CHECK(fit.x == 0);
    CHECK(fit.y == 0);
    CHECK(fit.width == 1920);
    CHECK(fit.height == 1080);
    CHECK(!fit.letterboxed);
}

// Tall source into a wide target: height-limited (pillarbox), centered in X.
TEST(ImageFit_TallSourcePillarboxes) {
    const ContainFitRect fit = ComputeContainFit(1080, 1920, 1920, 1080);
    CHECK(fit.height == 1080);
    CHECK(fit.width == 608);  // 1080*1080/1920 = 607.5 -> 608
    CHECK(fit.x == (1920 - 608) / 2);
    CHECK(fit.y == 0);
    CHECK(fit.letterboxed);
}

// Rounding is round-to-nearest, not floor: 100*3/7 = 42.857 -> 43.
TEST(ImageFit_RoundsToNearest) {
    const ContainFitRect fit = ComputeContainFit(7, 3, 100, 100);
    CHECK(fit.width == 100);
    CHECK(fit.height == 43);
    CHECK(fit.letterboxed);
}

// Degenerate inputs (zero source or target dimension) must not divide by
// zero and must fall back to the full-target historical behavior.
TEST(ImageFit_DegenerateInputFallsBackToFullTarget) {
    const ContainFitRect zeroSrc = ComputeContainFit(0, 0, 4096, 4045);
    CHECK(zeroSrc.width == 4096);
    CHECK(zeroSrc.height == 4045);
    CHECK(!zeroSrc.letterboxed);

    const ContainFitRect zeroDst = ComputeContainFit(3840, 1600, 0, 0);
    CHECK(zeroDst.width == 0);
    CHECK(zeroDst.height == 0);
    CHECK(!zeroDst.letterboxed);
}

// Determinism + symmetric centering: identical inputs give identical rects
// (both eyes MUST get the same mapping), and the bars split evenly (the
// leftover pixel from an odd difference goes to the far side, never
// drifting between eyes or frames).
TEST(ImageFit_DeterministicAndSymmetricallyCentered) {
    const ContainFitRect a = ComputeContainFit(3840, 1600, 4096, 4045);
    const ContainFitRect b = ComputeContainFit(3840, 1600, 4096, 4045);
    CHECK(a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height);

    // Symmetric split: leftover after centering is at most one pixel.
    CHECK((4096u - a.width) - 2u * a.x <= 1u);
    CHECK((4045u - a.height) - 2u * a.y <= 1u);
}

// The alignment contract: imageOffsetY is common-mode. Left eye (eyeSign
// -1) and right eye (+1) must receive the IDENTICAL Y offset - the old
// eyeSign-multiplied behavior produced vertical disparity ("eyes not
// aligned"). X stays signed per eye (convergence adjustment).
TEST(ImageOffsets_YIsCommonModeXIsPerEye) {
    float leftX = 0.0f, leftY = 0.0f, rightX = 0.0f, rightY = 0.0f;
    ComputeUserImageOffsets(10.0f, 20.0f, -1.0f, leftX, leftY);
    ComputeUserImageOffsets(10.0f, 20.0f, 1.0f, rightX, rightY);

    CHECK_NEAR(leftY, 0.2, 1e-6);
    CHECK_NEAR(rightY, 0.2, 1e-6);   // same direction both eyes
    CHECK_NEAR(leftX, -0.1, 1e-6);   // convergence: opposite signs
    CHECK_NEAR(rightX, 0.1, 1e-6);
}

// Mono blit (eyeSign 0): the shared image gets NO horizontal convergence
// shift (meaningless for one image), while the common-mode vertical offset
// still applies. Zero settings produce zero offsets.
TEST(ImageOffsets_MonoKeepsXZeroAndAppliesY) {
    float x = 1.0f, y = 1.0f;
    ComputeUserImageOffsets(10.0f, 20.0f, 0.0f, x, y);
    CHECK_NEAR(x, 0.0, 1e-6);
    CHECK_NEAR(y, 0.2, 1e-6);

    ComputeUserImageOffsets(0.0f, 0.0f, 1.0f, x, y);
    CHECK_NEAR(x, 0.0, 1e-6);
    CHECK_NEAR(y, 0.0, 1e-6);
}
