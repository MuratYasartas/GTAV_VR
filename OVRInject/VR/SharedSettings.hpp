#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace OVRInject {
namespace VR {

constexpr float kDefaultRenderScale = 1.0f;
constexpr float kMinRequestedRenderScale = 0.5f;
constexpr float kMaxRequestedRenderScale = 2.0f;
// Eye-texture caps: high enough to never clamp below the HMD's own
// recommended resolution (Pimax Crystal Super: 5424x5356) on capable GPUs -
// the renderScale slider must reach full PPD when the user asks for it.
constexpr uint32_t kMaxEyeTextureDimension = 8192u;
constexpr uint64_t kMaxEyeTexturePixels = 48ull * 1024ull * 1024ull;

inline float ClampRequestedRenderScale(float scale) {
    if (!std::isfinite(scale)) {
        return kDefaultRenderScale;
    }
    if (scale < kMinRequestedRenderScale) {
        return kMinRequestedRenderScale;
    }
    if (scale > kMaxRequestedRenderScale) {
        return kMaxRequestedRenderScale;
    }
    return scale;
}

inline float ComputeSafeRenderScale(float requestedScale, uint32_t baseWidth, uint32_t baseHeight) {
    float scale = ClampRequestedRenderScale(requestedScale);
    if (baseWidth == 0 || baseHeight == 0) {
        return scale;
    }

    float maxDimensionScale = scale;
    if (baseWidth > kMaxEyeTextureDimension || baseHeight > kMaxEyeTextureDimension) {
        float widthScale = static_cast<float>(kMaxEyeTextureDimension) /
                           static_cast<float>(baseWidth);
        float heightScale = static_cast<float>(kMaxEyeTextureDimension) /
                            static_cast<float>(baseHeight);
        maxDimensionScale = (std::min)(widthScale, heightScale);
        scale = (std::min)(scale, maxDimensionScale);
    }

    double basePixels = static_cast<double>(baseWidth) * static_cast<double>(baseHeight);
    if (basePixels > static_cast<double>(kMaxEyeTexturePixels)) {
        double pixelScale = std::sqrt(static_cast<double>(kMaxEyeTexturePixels) / basePixels);
        scale = (std::min)(scale, static_cast<float>(pixelScale));
    }

    return scale;
}

inline void ComputeSafeRenderSize(uint32_t baseWidth,
                                  uint32_t baseHeight,
                                  float requestedScale,
                                  float& outScale,
                                  uint32_t& outWidth,
                                  uint32_t& outHeight) {
    outScale = ComputeSafeRenderScale(requestedScale, baseWidth, baseHeight);

    double scaledWidth = std::round(static_cast<double>(baseWidth) * outScale);
    double scaledHeight = std::round(static_cast<double>(baseHeight) * outScale);
    outWidth = static_cast<uint32_t>((std::max)(scaledWidth, 256.0));
    outHeight = static_cast<uint32_t>((std::max)(scaledHeight, 256.0));
}

enum class StereoMode {
    Reprojection = 0,
    AlternateEye = 1,
    // EXPERIMENTAL stub: config-gated, not functional - StereoEngine logs once
    // and falls back to AlternateEye. Do not select expecting a real dual-pass
    // render.
    DualPass = 2
};

struct ReprojectionSettings {
    std::atomic<bool> enabled{true};
    std::atomic<float> ipd{0.022f};
    std::atomic<float> depthScale{0.033f};
    std::atomic<float> depthBias{-0.2f};
    std::atomic<bool> invertDepth{false};
    std::atomic<float> renderScale{kDefaultRenderScale};
    std::atomic<float> screenOffsetX{0.0f};
    std::atomic<float> screenOffsetY{0.0f};
    std::atomic<float> imageScale{1.0f};
};

struct StereoSettings {
    std::atomic<int> mode{static_cast<int>(StereoMode::AlternateEye)};
    std::atomic<float> stereoIPD{0.0617f};
    // When true (default), the per-eye offset comes from the runtime's own
    // eye poses (xrLocateViews) and the stereoIPD slider is ignored - the
    // physically correct IPD for the HMD. When false, the slider rules.
    std::atomic<bool> ipdAuto{true};
    std::atomic<bool> headTracking{true};
    std::atomic<bool> positionTracking{true};
    std::atomic<bool> recenterRequested{false};
};

struct HeadLookSettings {
    std::atomic<bool> enabled{false};
    std::atomic<float> maxAngleDeg{30.0f};
    std::atomic<float> deadzoneDeg{2.0f};
    std::atomic<float> sensitivity{400.0f};
    std::atomic<bool> invertY{true};
    std::atomic<bool> recenterRequested{false};
};

struct CameraSettings {
    std::atomic<float> worldScale{1.0f};
    std::atomic<float> playerHeight{1.7f};
    std::atomic<float> cameraOffsetX{0.0f};
    std::atomic<float> cameraOffsetY{0.0f};
    std::atomic<float> cameraOffsetZ{0.0f};
};

struct ComfortSettings {
    // Defaults ON per docs/user/comfort.md (comfort is a safety requirement).
    // These must match XR::VRSettings (XROverlayUI.hpp): if overlay UI init
    // fails, no settings-apply ever runs and these atomic initializers are the
    // only defaults the pipeline sees.
    std::atomic<bool> snapTurning{true};
    std::atomic<float> snapTurnAngle{45.0f};
    std::atomic<bool> vignetteEnabled{true};
    std::atomic<float> vignetteIntensity{0.5f};
};

struct InputSettings {
    std::atomic<bool> swapHands{false};
    std::atomic<float> triggerThreshold{0.5f};
    std::atomic<float> gripThreshold{0.5f};
};

struct DebugSettings {
    std::atomic<bool> showDebugInfo{false};
    std::atomic<bool> showControllerModels{true};
};

struct PerformanceSettings {
    std::atomic<bool> asyncReprojection{true};
    std::atomic<float> gameResolutionScale{1.0f};
};

struct ViewSettings {
    std::atomic<float> snapYawOffsetDeg{0.0f};
};

struct FovSettings {
    std::atomic<bool> enabled{false};
    std::atomic<bool> perType{false};
    std::atomic<float> globalFov{55.0f};
    std::atomic<float> fpPedFov{55.0f};
    std::atomic<float> tpPedFov{55.0f};
    std::atomic<float> tpAimFov{55.0f};
    std::atomic<float> fpVehicleFov{55.0f};
    std::atomic<float> tpVehicleFov{55.0f};
    std::atomic<bool> overrideOffset{false};
    std::atomic<int> manualOffset{48};
};

enum class DecouplingMode {
    Always = 0,      // Decoupling always active
    OnlyAiming = 1,  // Decoupling only when aiming
    Never = 2        // Decoupling disabled
};

struct DecouplingSettings {
    std::atomic<bool> enabled{true};
    std::atomic<int> mode{static_cast<int>(DecouplingMode::Always)};
    std::atomic<float> maxPitchDeg{85.0f};   // Max pitch deviation
    std::atomic<float> maxYawDeg{180.0f};    // Max yaw deviation
    std::atomic<float> aimConeDeg{30.0f};    // Cone limit when aiming (semi-libre)
};

enum class CutsceneMode {
    Normal = 0,        // Keep VR head tracking during cutscene
    VirtualScreen = 1, // Show cutscene on floating 2D screen
    Unlock = 2         // Cutscene plays but head can look freely
};

struct CutsceneSettings {
    std::atomic<int> mode{static_cast<int>(CutsceneMode::VirtualScreen)};
    std::atomic<float> screenDistance{4.0f};  // Distance in meters
    std::atomic<float> screenScale{3.0f};     // Screen size multiplier
    std::atomic<float> screenCurve{0.0f};     // 0 = flat, 1 = curved
};

enum class GameState {
    Unknown = 0,
    Playing = 1,
    Cutscene = 2,
    Aiming = 3,
    InVehicle = 4,
    Loading = 5,
    Menu = 6
};

struct GameStateInfo {
    std::atomic<int> currentState{static_cast<int>(GameState::Unknown)};
    std::atomic<bool> isCutsceneActive{false};
    std::atomic<bool> isAiming{false};
    std::atomic<bool> isInVehicle{false};
    std::atomic<bool> isFirstPerson{false};
    std::atomic<uint32_t> currentCameraHash{0};
};

struct RuntimeStats {
    std::atomic<float> fps{0.0f};
    // PerfStats mirrors (written on the render thread once per completed
    // second; read by the overlay / any other thread).
    std::atomic<float> frametimeP50Ms{0.0f};
    std::atomic<float> frametimeP99Ms{0.0f};
    std::atomic<float> frametimeP999Ms{0.0f};
    std::atomic<bool> cameraHookReady{false};
    std::atomic<bool> cameraConfigLoaded{false};
    std::atomic<uint64_t> cameraMatrixAddress{0};
    std::atomic<bool> cameraMatrixWritable{false};
    std::atomic<uint32_t> activeCameraHash{0};
    std::atomic<uint32_t> activeCameraHashName{0};
    std::atomic<int> activeFovOffset{0};
    std::atomic<float> activeFov{0.0f};
    std::atomic<int> activeRuntime{0};
    // True while the settings overlay is visible: the WndProc hook swallows
    // game-bound input (WM_INPUT, keys, mouse) so menu interactions don't
    // leak into the game (and vice versa).
    std::atomic<bool> overlayVisible{false};
};

inline ReprojectionSettings& GetReprojectionSettings() {
    static ReprojectionSettings settings;
    return settings;
}

inline StereoSettings& GetStereoSettings() {
    static StereoSettings settings;
    return settings;
}

inline HeadLookSettings& GetHeadLookSettings() {
    static HeadLookSettings settings;
    return settings;
}

inline CameraSettings& GetCameraSettings() {
    static CameraSettings settings;
    return settings;
}

inline ComfortSettings& GetComfortSettings() {
    static ComfortSettings settings;
    return settings;
}

inline InputSettings& GetInputSettings() {
    static InputSettings settings;
    return settings;
}

inline DebugSettings& GetDebugSettings() {
    static DebugSettings settings;
    return settings;
}

inline PerformanceSettings& GetPerformanceSettings() {
    static PerformanceSettings settings;
    return settings;
}

inline ViewSettings& GetViewSettings() {
    static ViewSettings settings;
    return settings;
}

inline FovSettings& GetFovSettings() {
    static FovSettings settings;
    return settings;
}

inline RuntimeStats& GetRuntimeStats() {
    static RuntimeStats stats;
    return stats;
}

inline DecouplingSettings& GetDecouplingSettings() {
    static DecouplingSettings settings;
    return settings;
}

inline CutsceneSettings& GetCutsceneSettings() {
    static CutsceneSettings settings;
    return settings;
}

inline GameStateInfo& GetGameStateInfo() {
    static GameStateInfo info;
    return info;
}

} // namespace VR
} // namespace OVRInject
