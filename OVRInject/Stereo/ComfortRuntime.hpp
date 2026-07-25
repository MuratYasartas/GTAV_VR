#pragma once

/**
 * ComfortRuntime - runtime state for the Phase 5/6 comfort features that have
 * no home in VR::SharedSettings (which is owned by the VR layer and holds only
 * the original settings structs).
 *
 * Sources of truth, in order of application:
 *   1. These atomic initializers (match docs/user/comfort.md defaults).
 *   2. gtavr_settings.ini [Comfort] at startup (D3DHooks_VRManager loads
 *      vehicleHorizonLock / smoothTurnSpeed directly so they apply on BOTH
 *      runtimes; the OpenXR settings-apply path cannot be extended from here).
 *   3. The overlay UI (XROverlayUI) via the settings-changed callback
 *      (live-apply on the OpenVR path; OpenXR applies them next launch).
 *
 * snapTurning / snapTurnAngle / vignetteEnabled / vignetteIntensity remain in
 * VR::ComfortSettings (SharedSettings.hpp); their effective defaults come from
 * XR::VRSettings + gtavr_settings.ini, both of which default them ON per
 * docs/user/comfort.md.
 */

#include <atomic>

namespace OVRInject {
namespace Stereo {

struct ComfortRuntimeSettings {
    // [Comfort] vehicleHorizonLock (default 1): while GtaGameState reports
    // in-vehicle, suppress vehicle pitch/roll from rotating the VR view; yaw
    // passes through. Implemented as a post-write correction of the composed
    // camera rotation in StereoEngine (requires decoupling, default on).
    std::atomic<bool> vehicleHorizonLock{true};

    // [Comfort] smoothTurnSpeed (default 120 deg/s): continuous turn speed
    // used when VR::ComfortSettings::snapTurning is false. When snapTurning is
    // true (the default) this is unused.
    std::atomic<float> smoothTurnSpeedDeg{120.0f};

    // Introspection only: the smoothed 0..1 locomotion activity that currently
    // drives the vignette (written by the D3D hook layer once per frame).
    std::atomic<float> vignetteActivity{0.0f};
};

inline ComfortRuntimeSettings& GetComfortRuntime() {
    static ComfortRuntimeSettings settings;
    return settings;
}

} // namespace Stereo
} // namespace OVRInject
