#pragma once

#include <string>
#include <map>
#include <variant>

namespace GTA5VR {

// Rendering mode options
enum class RenderingMode {
    NativeStereo,
    SynchronizedSequential,
    AlternatingEye
};

// VR Runtime options
enum class VRRuntimeType {
    Auto,
    OpenXR,
    OpenVR
};

// HUD modes
enum class HUDMode {
    Floating,
    HeadLocked,
    Wrist,
    Hidden
};

// Vehicle camera modes
enum class VehicleCameraMode {
    Dashboard,
    Hood,
    ThirdPerson
};

// Cutscene modes
enum class CutsceneMode {
    VRAdapted,
    Theater,
    Original,
    Skip
};

// Input modes
enum class InputMode {
    Gamepad,
    MotionControllers,
    Hybrid
};

// Recenter methods
enum class RecenterMethod {
    HeadShake,
    Button,
    Both
};

// Configuration structure
struct VRConfigData {
    // General
    bool modEnabled = true;
    std::string gameVersion = "auto";
    bool firstRun = true;

    // VR Runtime
    VRRuntimeType preferredRuntime = VRRuntimeType::Auto;
    std::string openXRRuntime = "";
    int vrAPI = 0; // 0=auto, 1=oculus, 2=steamvr, 3=wmr

    // Rendering
    RenderingMode renderingMode = RenderingMode::SynchronizedSequential;
    int resolutionScale = 100;
    bool enableDepthBuffer = true;
    std::string depthFormat = "D32_FLOAT";
    int targetFramerate = 90;
    bool allowReprojection = true;

    // Display
    float worldScale = 1.0f;
    bool ipdOverride = false;
    float ipdValue = 63.0f;
    float nearClip = 0.1f;
    float farClip = 10000.0f;

    // Comfort
    bool enableSnapTurn = true;
    float snapTurnDegrees = 45.0f;
    bool enableComfortVignette = true;
    float vignetteIntensity = 0.5f;
    bool seatedMode = false;
    float standingHeight = 0.0f;

    // HUD
    HUDMode hudMode = HUDMode::Floating;
    float hudDistance = 2.0f;
    float hudScale = 1.0f;
    float hudOpacity = 1.0f;
    float hudCurvature = 0.0f;
    float radarScale = 1.0f;
    float subtitleSize = 1.0f;

    // Camera
    bool forceFirstPerson = true;
    VehicleCameraMode vehicleCamera = VehicleCameraMode::Dashboard;
    CutsceneMode cutsceneMode = CutsceneMode::VRAdapted;
    bool enablePositionalTracking = true;
    float positionalScale = 1.0f;
    RecenterMethod recenterMethod = RecenterMethod::HeadShake;
    int recenterButton = 0x7B; // F12 key

    // Input
    InputMode inputMode = InputMode::Gamepad;
    bool swapSticks = false;
    bool enableHaptics = true;
    float hapticIntensity = 0.7f;

    // Performance
    bool showPerformanceOverlay = false;
    bool showReprojectionIndicator = false;
    bool limitFramerate = false;
    int framerateLimit = 90;

    // Debug
    bool enableDebugMode = false;
    int logLevel = 1; // 0=minimal, 1=verbose, 2=debug
    bool logToFile = true;
    std::string logFilePath = "GTA5VR.log";
    bool showDebugOverlay = false;

    // Advanced
    bool disablePostProcessing = false;
    bool disableDOF = true;
    bool disableMotionBlur = true;
    bool disableBloom = false;
    bool useAsyncCompute = true;
    bool preferDedicatedGPU = true;
};

class VRConfig {
public:
    static VRConfig& GetInstance();

    // File operations
    bool Load(const std::string& configPath);
    bool Save(const std::string& configPath = "");
    bool SaveAs(const std::string& configPath);
    void ResetToDefaults();

    // Accessors
    VRConfigData& GetConfig();
    const VRConfigData& GetConfig() const;

    // Quick accessors for common settings
    RenderingMode GetRenderingMode() const;
    void SetRenderingMode(RenderingMode mode);

    VRRuntimeType GetRuntimeType() const;
    void SetRuntimeType(VRRuntimeType type);

    float GetWorldScale() const;
    void SetWorldScale(float scale);

    float GetIPD() const;
    void SetIPD(float ipd);

    bool IsSnapTurnEnabled() const;
    void SetSnapTurnEnabled(bool enabled);

    float GetSnapTurnDegrees() const;
    void SetSnapTurnDegrees(float degrees);

    HUDMode GetHUDMode() const;
    void SetHUDMode(HUDMode mode);

    // Profile management
    bool LoadProfile(const std::string& profileName);
    bool SaveProfile(const std::string& profileName);
    std::vector<std::string> GetAvailableProfiles() const;

    // Validation
    bool Validate();
    std::string GetLastError() const;

private:
    VRConfig() = default;
    ~VRConfig() = default;
    VRConfig(const VRConfig&) = delete;
    VRConfig& operator=(const VRConfig&) = delete;

    bool ParseINI(const std::string& content);
    std::string SerializeINI() const;

    template<typename T>
    T ParseValue(const std::string& value, T defaultValue) const;

    VRConfigData m_config;
    std::string m_currentConfigPath;
    std::string m_lastError;
    bool m_dirty = false;
};

} // namespace GTA5VR
