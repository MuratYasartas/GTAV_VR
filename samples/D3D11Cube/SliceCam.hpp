// SliceCam.hpp
//
// Cooperative-engine camera harness ABI for the GTAVR vertical slice.
//
// The D3D11Cube sample creates a named shared-memory mapping
// ("GTAVR_SLICE_CAM") holding one SliceCamState. A pose producer (e.g. the
// injected OVRInject DLL, or a test harness) can open the same mapping and
// publish per-frame stereo camera data. The cube app reads it every frame:
//
//   - magic == kSliceCamMagic and (flags & kSliceCamFlagVrControl):
//       even app frames render with viewL, odd frames with viewR
//       (alternate-eye simulation), using proj as the projection matrix.
//   - otherwise the app renders with its built-in default camera.
//
// All matrices are row-major float[16], column-vector convention:
//   clipPos = proj * view * worldPos.
//
// The layout is versioned through the magic value: bump kSliceCamMagic
// whenever the struct changes so stale producers/consumers fail closed
// (the app simply falls back to its default camera).
//
// The mapping is session-local (no "Global\" prefix). Reads are not
// synchronized with the producer; a torn frame at worst shows one stale
// pose, which is acceptable for this slice harness.

#pragma once

#include <cstdint>

namespace gtavr {

constexpr char kSliceCamMapName[] = "GTAVR_SLICE_CAM";

// 'GTVC' as a little-endian fourcc; struct layout version 1.
constexpr std::uint32_t kSliceCamMagic = 0x43565447u;

// flags bits
constexpr std::uint32_t kSliceCamFlagVrControl = 0x1u;

struct SliceCamState {
    std::uint32_t magic;       // kSliceCamMagic when the contents are valid
    std::uint32_t frameIndex;  // producer-side frame counter
    std::uint32_t flags;       // bit 0: kSliceCamFlagVrControl
    float viewL[16];           // row-major world->view, left eye
    float viewR[16];           // row-major world->view, right eye
    float proj[16];            // row-major projection
    float ipd;                 // interpupillary distance in metres (informational)
};

static_assert(sizeof(SliceCamState) == 208, "SliceCamState ABI drift");

}  // namespace gtavr
