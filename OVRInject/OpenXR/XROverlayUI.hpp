#pragma once

#include "../Overlay/OverlaySurface.hpp"

#include <d3d11.h>
#include <functional>
#include <string>
#include <vector>

namespace OVRInject {
namespace XR {

struct OverlayInputState {
    float thumbstickX = 0.0f;
    float thumbstickY = 0.0f;

    bool thumbstickTouched = false;
    bool thumbstickPressed = false;
    bool thumbstickJustPressed = false;
    bool thumbstickJustReleased = false;

    bool triggerPressed = false;
    bool gripPressed = false;

    bool menuPressed = false;
    bool menuJustPressed = false;

    bool primaryPressed = false;
    bool secondaryPressed = false;
};

/**
 * VR Settings structure - persisted to INI file
 */
struct VRSettings {
    // World Scale
    float worldScale = 1.0f;
    float playerHeight = 1.7f;

    // Camera
    float cameraOffsetX = 0.0f;
    float cameraOffsetY = 0.34f;
    float cameraOffsetZ = 0.0f;
    float imageOffsetX = 0.0f;
    float imageOffsetY = 0.0f;
    float imageScale = 1.0f;

    // Camera FOV override
    bool fovOverride = false;
    bool fovPerType = false;
    float fovGlobal = 55.0f;
    float fovFpPed = 55.0f;
    float fovTpPed = 55.0f;
    float fovTpAim = 55.0f;
    float fovFpVehicle = 55.0f;
    float fovTpVehicle = 55.0f;
    bool fovOffsetOverride = false;
    int fovManualOffset = 48;

    // Stereo
    int stereoMode = 1; // 0 = Reprojection, 1 = Alternate-Eye
    float stereoIPD = 0.0617f;
    bool ipdAuto = true;   // eye offset from the runtime (physical HMD IPD)
    float headPredictMs = 0.0f;   // head-motion prediction (0 = off)
    bool headTracking = true;
    bool positionTracking = true;

    // Comfort (defaults per docs/user/comfort.md: snap turn ON, vignette ON,
    // vehicle horizon lock ON - comfort is a safety requirement)
    bool snapTurning = true;
    float snapTurnAngle = 45.0f;
    bool vignetteEnabled = true;
    float vignetteIntensity = 0.5f;
    bool vehicleHorizonLock = true;   // suppress vehicle pitch/roll in VR view
    float smoothTurnSpeed = 120.0f;   // deg/s, used when snapTurning == false

    // Performance
    bool asyncReprojection = true;
    float renderScale = 1.0f;
    float gameResolutionScale = 1.0f;
    bool desktopMirrorSyncOverride = true;

    // Depth reprojection (fake stereo)
    bool reprojectionEnabled = true;
    float reprojectionIPD = 0.022f;
    float reprojectionDepthScale = 0.033f;
    float reprojectionDepthBias = -0.2f;
    bool reprojectionInvertDepth = false;

    // Controls
    bool swapHands = false;
    float triggerThreshold = 0.5f;
    float gripThreshold = 0.5f;

    // Head-look (right stick emulation)
    bool headLookEnabled = false;
    float headLookMaxAngle = 30.0f;
    float headLookDeadzone = 2.0f;
    float headLookSensitivity = 400.0f;
    bool headLookInvertY = true;

    // Decoupling (VR head independent from game camera)
    bool decouplingEnabled = true;
    int decouplingMode = 0;        // 0=Always, 1=OnlyAiming, 2=Never
    float decouplingMaxPitch = 85.0f;
    float decouplingMaxYaw = 180.0f;
    float decouplingAimCone = 30.0f;  // Cone limit when aiming

    // Cutscene handling
    int cutsceneMode = 1;          // 0=Normal, 1=VirtualScreen, 2=Unlock
    float cutsceneScreenDistance = 4.0f;
    float cutsceneScreenScale = 3.0f;
    float cutsceneScreenCurve = 0.0f;

    // Overlay
    float overlayDistance = 1.1f;
    float overlayScale = 0.7f;
    float overlayOpacity = 1.0f;

    // Debug
    bool showDebugInfo = false;
    bool showControllerModels = true;
};

/**
 * XROverlayUI - ImGui-based settings interface for VR
 *
 * Provides a UEVR/LukeRoss-style overlay menu with:
 * - World scale adjustment
 * - Camera/head offset
 * - Comfort options (vignette, snap turn)
 * - Performance settings
 * - Input configuration
 * - Debug visualization
 *
 * Requires ImGui to be present (define HAS_IMGUI)
 */
class XROverlayUI {
public:
    XROverlayUI();
    ~XROverlayUI();

    /**
     * Initialize the overlay UI
     * @param device D3D11 device for ImGui initialization
     * @param overlay The overlay to render to
     * @return true on success
     */
    bool Initialize(ID3D11Device* device, IOverlaySurface* overlay);

    /**
     * Shutdown and cleanup
     */
    void Shutdown();

    /**
     * Check if initialized
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * Render the UI to the overlay
     * Call each frame when overlay is visible
     */
    void Render();

    /**
     * Handle input for UI navigation. Controllers (thumbstick cursor +
     * trigger click) are primary; the desktop mouse and keyboard arrows
     * are automatic fallbacks when controllers are dead or unmapped.
     * @param leftState Left controller state
     * @param rightState Right controller state
     */
    void HandleInput(const OverlayInputState& leftState, const OverlayInputState& rightState);

    //-------------------------------------------------------------------------
    // Settings Access
    //-------------------------------------------------------------------------

    /**
     * Get current settings
     */
    const VRSettings& GetSettings() const { return settings_; }

    /**
     * Get mutable settings reference
     */
    VRSettings& GetSettings() { return settings_; }

    /**
     * Set settings changed callback
     */
    void SetOnSettingsChanged(std::function<void(const VRSettings&)> callback) {
        on_settings_changed_ = callback;
    }

    //-------------------------------------------------------------------------
    // Persistence
    //-------------------------------------------------------------------------

    /**
     * Load settings from INI file
     */
    bool LoadSettings(const char* filename = "gtavr_settings.ini");

    /**
     * Save settings to INI file
     */
    bool SaveSettings(const char* filename = "gtavr_settings.ini");

    //-------------------------------------------------------------------------
    // UI State
    //-------------------------------------------------------------------------

    /**
     * Check if UI wants to capture input
     */
    bool WantsCaptureInput() const { return visible_ && wants_capture_; }

    /**
     * Consume recenter request flag
     */
    bool ConsumeRecenterRequest();

    /**
     * Set visibility
     */
    void SetVisible(bool visible);
    bool IsVisible() const { return visible_; }

    /**
     * Toggle visibility
     */
    void Toggle();

private:
    /**
     * Render main menu bar
     */
    void RenderMenuBar();

    /**
     * Render world settings tab
     */
    void RenderWorldSettings();

    /**
     * Render comfort settings tab
     */
    void RenderComfortSettings();

    /**
     * Render performance settings tab
     */
    void RenderPerformanceSettings();

    /**
     * Render controls settings tab
     */
    void RenderControlsSettings();

    /**
     * Render debug tab
     */
    void RenderDebugInfo();

    /**
     * Notify settings changed
     */
    void NotifySettingsChanged();

    //-------------------------------------------------------------------------
    // Members
    //-------------------------------------------------------------------------

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IOverlaySurface* overlay_ = nullptr;

    VRSettings settings_;
    std::function<void(const VRSettings&)> on_settings_changed_;

    bool initialized_ = false;
    bool visible_ = false;
    bool wants_capture_ = false;

    // UI state
    int current_tab_ = 0;
    float cursor_x_ = 0.0f;
    float cursor_y_ = 0.0f;
    bool recenter_requested_ = false;
};

} // namespace XR
} // namespace OVRInject
