#include "VRConfig.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace GTA5VR {

VRConfig& VRConfig::GetInstance() {
    static VRConfig instance;
    return instance;
}

bool VRConfig::Load(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        m_lastError = "Failed to open config file: " + configPath;
        LOG_ERROR(m_lastError);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    m_currentConfigPath = configPath;

    if (!ParseINI(buffer.str())) {
        return false;
    }

    if (!Validate()) {
        LOG_WARNING("Config validation failed, some values reset to defaults");
    }

    LOG_INFO("Configuration loaded from: " + configPath);
    m_dirty = false;
    return true;
}

bool VRConfig::Save(const std::string& configPath) {
    std::string path = configPath.empty() ? m_currentConfigPath : configPath;
    if (path.empty()) {
        m_lastError = "No config path specified";
        return false;
    }
    return SaveAs(path);
}

bool VRConfig::SaveAs(const std::string& configPath) {
    std::ofstream file(configPath);
    if (!file.is_open()) {
        m_lastError = "Failed to create config file: " + configPath;
        LOG_ERROR(m_lastError);
        return false;
    }

    file << SerializeINI();
    file.close();

    m_currentConfigPath = configPath;
    m_dirty = false;
    LOG_INFO("Configuration saved to: " + configPath);
    return true;
}

void VRConfig::ResetToDefaults() {
    m_config = VRConfigData();
    m_dirty = true;
    LOG_INFO("Configuration reset to defaults");
}

VRConfigData& VRConfig::GetConfig() {
    return m_config;
}

const VRConfigData& VRConfig::GetConfig() const {
    return m_config;
}

RenderingMode VRConfig::GetRenderingMode() const {
    return m_config.renderingMode;
}

void VRConfig::SetRenderingMode(RenderingMode mode) {
    m_config.renderingMode = mode;
    m_dirty = true;
}

VRRuntimeType VRConfig::GetRuntimeType() const {
    return m_config.preferredRuntime;
}

void VRConfig::SetRuntimeType(VRRuntimeType type) {
    m_config.preferredRuntime = type;
    m_dirty = true;
}

float VRConfig::GetWorldScale() const {
    return m_config.worldScale;
}

void VRConfig::SetWorldScale(float scale) {
    m_config.worldScale = std::clamp(scale, 0.5f, 2.0f);
    m_dirty = true;
}

float VRConfig::GetIPD() const {
    return m_config.ipdValue;
}

void VRConfig::SetIPD(float ipd) {
    m_config.ipdValue = std::clamp(ipd, 50.0f, 80.0f);
    m_dirty = true;
}

bool VRConfig::IsSnapTurnEnabled() const {
    return m_config.enableSnapTurn;
}

void VRConfig::SetSnapTurnEnabled(bool enabled) {
    m_config.enableSnapTurn = enabled;
    m_dirty = true;
}

float VRConfig::GetSnapTurnDegrees() const {
    return m_config.snapTurnDegrees;
}

void VRConfig::SetSnapTurnDegrees(float degrees) {
    m_config.snapTurnDegrees = std::clamp(degrees, 15.0f, 90.0f);
    m_dirty = true;
}

HUDMode VRConfig::GetHUDMode() const {
    return m_config.hudMode;
}

void VRConfig::SetHUDMode(HUDMode mode) {
    m_config.hudMode = mode;
    m_dirty = true;
}

bool VRConfig::Validate() {
    bool valid = true;

    // Clamp values to valid ranges
    m_config.resolutionScale = std::clamp(m_config.resolutionScale, 50, 200);
    m_config.worldScale = std::clamp(m_config.worldScale, 0.5f, 2.0f);
    m_config.ipdValue = std::clamp(m_config.ipdValue, 50.0f, 80.0f);
    m_config.snapTurnDegrees = std::clamp(m_config.snapTurnDegrees, 15.0f, 90.0f);
    m_config.vignetteIntensity = std::clamp(m_config.vignetteIntensity, 0.0f, 1.0f);
    m_config.hudDistance = std::clamp(m_config.hudDistance, 0.5f, 10.0f);
    m_config.hudScale = std::clamp(m_config.hudScale, 0.5f, 3.0f);
    m_config.hudOpacity = std::clamp(m_config.hudOpacity, 0.0f, 1.0f);
    m_config.hapticIntensity = std::clamp(m_config.hapticIntensity, 0.0f, 1.0f);
    m_config.targetFramerate = std::clamp(m_config.targetFramerate, 30, 144);

    return valid;
}

std::string VRConfig::GetLastError() const {
    return m_lastError;
}

bool VRConfig::ParseINI(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    std::string currentSection;

    while (std::getline(stream, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines and comments
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // Check for section header
        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }

        // Parse key=value pairs
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        // Trim key and value
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        // Parse based on section and key
        if (currentSection == "General") {
            if (key == "ModEnabled") m_config.modEnabled = (value == "true" || value == "1");
            else if (key == "GameVersion") m_config.gameVersion = value;
            else if (key == "FirstRun") m_config.firstRun = (value == "true" || value == "1");
        }
        else if (currentSection == "VRRuntime") {
            if (key == "PreferredRuntime") {
                if (value == "openxr") m_config.preferredRuntime = VRRuntimeType::OpenXR;
                else if (value == "openvr") m_config.preferredRuntime = VRRuntimeType::OpenVR;
                else m_config.preferredRuntime = VRRuntimeType::Auto;
            }
            else if (key == "OpenXRRuntime") m_config.openXRRuntime = value;
            else if (key == "VRAPI") m_config.vrAPI = std::stoi(value);
        }
        else if (currentSection == "Rendering") {
            if (key == "RenderingMode") {
                if (value == "native_stereo") m_config.renderingMode = RenderingMode::NativeStereo;
                else if (value == "alternating_eye") m_config.renderingMode = RenderingMode::AlternatingEye;
                else m_config.renderingMode = RenderingMode::SynchronizedSequential;
            }
            else if (key == "ResolutionScale") m_config.resolutionScale = std::stoi(value);
            else if (key == "EnableDepthBuffer") m_config.enableDepthBuffer = (value == "true" || value == "1");
            else if (key == "DepthFormat") m_config.depthFormat = value;
            else if (key == "TargetFramerate") m_config.targetFramerate = std::stoi(value);
            else if (key == "AllowReprojection") m_config.allowReprojection = (value == "true" || value == "1");
        }
        else if (currentSection == "Display") {
            if (key == "WorldScale") m_config.worldScale = std::stof(value);
            else if (key == "IPDOverride") m_config.ipdOverride = (value == "true" || value == "1");
            else if (key == "IPDValue") m_config.ipdValue = std::stof(value);
            else if (key == "NearClip") m_config.nearClip = std::stof(value);
            else if (key == "FarClip") m_config.farClip = std::stof(value);
        }
        else if (currentSection == "Comfort") {
            if (key == "EnableSnapTurn") m_config.enableSnapTurn = (value == "true" || value == "1");
            else if (key == "SnapTurnDegrees") m_config.snapTurnDegrees = std::stof(value);
            else if (key == "EnableComfortVignette") m_config.enableComfortVignette = (value == "true" || value == "1");
            else if (key == "VignetteIntensity") m_config.vignetteIntensity = std::stof(value);
            else if (key == "SeatedMode") m_config.seatedMode = (value == "true" || value == "1");
            else if (key == "StandingHeight") m_config.standingHeight = std::stof(value);
        }
        else if (currentSection == "HUD") {
            if (key == "HUDMode") {
                if (value == "head_locked") m_config.hudMode = HUDMode::HeadLocked;
                else if (value == "wrist") m_config.hudMode = HUDMode::Wrist;
                else if (value == "hidden") m_config.hudMode = HUDMode::Hidden;
                else m_config.hudMode = HUDMode::Floating;
            }
            else if (key == "HUDDistance") m_config.hudDistance = std::stof(value);
            else if (key == "HUDScale") m_config.hudScale = std::stof(value);
            else if (key == "HUDOpacity") m_config.hudOpacity = std::stof(value);
            else if (key == "HUDCurvature") m_config.hudCurvature = std::stof(value);
            else if (key == "RadarScale") m_config.radarScale = std::stof(value);
            else if (key == "SubtitleSize") m_config.subtitleSize = std::stof(value);
        }
        else if (currentSection == "Camera") {
            if (key == "ForceFirstPerson") m_config.forceFirstPerson = (value == "true" || value == "1");
            else if (key == "VehicleCamera") {
                if (value == "hood") m_config.vehicleCamera = VehicleCameraMode::Hood;
                else if (value == "third_person") m_config.vehicleCamera = VehicleCameraMode::ThirdPerson;
                else m_config.vehicleCamera = VehicleCameraMode::Dashboard;
            }
            else if (key == "CutsceneMode") {
                if (value == "theater") m_config.cutsceneMode = CutsceneMode::Theater;
                else if (value == "original") m_config.cutsceneMode = CutsceneMode::Original;
                else if (value == "skip") m_config.cutsceneMode = CutsceneMode::Skip;
                else m_config.cutsceneMode = CutsceneMode::VRAdapted;
            }
            else if (key == "EnablePositionalTracking") m_config.enablePositionalTracking = (value == "true" || value == "1");
            else if (key == "PositionalScale") m_config.positionalScale = std::stof(value);
            else if (key == "RecenterMethod") {
                if (value == "button") m_config.recenterMethod = RecenterMethod::Button;
                else if (value == "both") m_config.recenterMethod = RecenterMethod::Both;
                else m_config.recenterMethod = RecenterMethod::HeadShake;
            }
            else if (key == "RecenterButton") m_config.recenterButton = std::stoi(value, nullptr, 16);
        }
        else if (currentSection == "Input") {
            if (key == "InputMode") {
                if (value == "motion_controllers") m_config.inputMode = InputMode::MotionControllers;
                else if (value == "hybrid") m_config.inputMode = InputMode::Hybrid;
                else m_config.inputMode = InputMode::Gamepad;
            }
            else if (key == "SwapSticks") m_config.swapSticks = (value == "true" || value == "1");
            else if (key == "EnableHaptics") m_config.enableHaptics = (value == "true" || value == "1");
            else if (key == "HapticIntensity") m_config.hapticIntensity = std::stof(value);
        }
        else if (currentSection == "Performance") {
            if (key == "ShowPerformanceOverlay") m_config.showPerformanceOverlay = (value == "true" || value == "1");
            else if (key == "ShowReprojectionIndicator") m_config.showReprojectionIndicator = (value == "true" || value == "1");
            else if (key == "LimitFramerate") m_config.limitFramerate = (value == "true" || value == "1");
            else if (key == "FramerateLimit") m_config.framerateLimit = std::stoi(value);
        }
        else if (currentSection == "Debug") {
            if (key == "EnableDebugMode") m_config.enableDebugMode = (value == "true" || value == "1");
            else if (key == "LogLevel") {
                if (value == "minimal") m_config.logLevel = 0;
                else if (value == "debug") m_config.logLevel = 2;
                else m_config.logLevel = 1;
            }
            else if (key == "LogToFile") m_config.logToFile = (value == "true" || value == "1");
            else if (key == "LogFilePath") m_config.logFilePath = value;
            else if (key == "ShowDebugOverlay") m_config.showDebugOverlay = (value == "true" || value == "1");
        }
        else if (currentSection == "Advanced") {
            if (key == "DisablePostProcessing") m_config.disablePostProcessing = (value == "true" || value == "1");
            else if (key == "DisableDOF") m_config.disableDOF = (value == "true" || value == "1");
            else if (key == "DisableMotionBlur") m_config.disableMotionBlur = (value == "true" || value == "1");
            else if (key == "DisableBloom") m_config.disableBloom = (value == "true" || value == "1");
            else if (key == "UseAsyncCompute") m_config.useAsyncCompute = (value == "true" || value == "1");
            else if (key == "PreferDedicatedGPU") m_config.preferDedicatedGPU = (value == "true" || value == "1");
        }
    }

    return true;
}

std::string VRConfig::SerializeINI() const {
    std::ostringstream ss;

    ss << "; GTA5 VR Mod Configuration\n";
    ss << "; Generated by GTA5VR\n\n";

    // General
    ss << "[General]\n";
    ss << "ModEnabled=" << (m_config.modEnabled ? "true" : "false") << "\n";
    ss << "GameVersion=" << m_config.gameVersion << "\n";
    ss << "FirstRun=" << (m_config.firstRun ? "true" : "false") << "\n\n";

    // VRRuntime
    ss << "[VRRuntime]\n";
    ss << "; Options: auto, openxr, openvr\n";
    ss << "PreferredRuntime=";
    switch (m_config.preferredRuntime) {
        case VRRuntimeType::OpenXR: ss << "openxr"; break;
        case VRRuntimeType::OpenVR: ss << "openvr"; break;
        default: ss << "auto"; break;
    }
    ss << "\n";
    ss << "OpenXRRuntime=" << m_config.openXRRuntime << "\n";
    ss << "; 0=auto, 1=oculus, 2=steamvr, 3=wmr\n";
    ss << "VRAPI=" << m_config.vrAPI << "\n\n";

    // Rendering
    ss << "[Rendering]\n";
    ss << "; Options: native_stereo, synchronized_sequential, alternating_eye\n";
    ss << "RenderingMode=";
    switch (m_config.renderingMode) {
        case RenderingMode::NativeStereo: ss << "native_stereo"; break;
        case RenderingMode::AlternatingEye: ss << "alternating_eye"; break;
        default: ss << "synchronized_sequential"; break;
    }
    ss << "\n";
    ss << "ResolutionScale=" << m_config.resolutionScale << "\n";
    ss << "EnableDepthBuffer=" << (m_config.enableDepthBuffer ? "true" : "false") << "\n";
    ss << "DepthFormat=" << m_config.depthFormat << "\n";
    ss << "TargetFramerate=" << m_config.targetFramerate << "\n";
    ss << "AllowReprojection=" << (m_config.allowReprojection ? "true" : "false") << "\n\n";

    // Display
    ss << "[Display]\n";
    ss << "WorldScale=" << m_config.worldScale << "\n";
    ss << "IPDOverride=" << (m_config.ipdOverride ? "1" : "0") << "\n";
    ss << "IPDValue=" << m_config.ipdValue << "\n";
    ss << "NearClip=" << m_config.nearClip << "\n";
    ss << "FarClip=" << m_config.farClip << "\n\n";

    // Comfort
    ss << "[Comfort]\n";
    ss << "EnableSnapTurn=" << (m_config.enableSnapTurn ? "true" : "false") << "\n";
    ss << "SnapTurnDegrees=" << m_config.snapTurnDegrees << "\n";
    ss << "EnableComfortVignette=" << (m_config.enableComfortVignette ? "true" : "false") << "\n";
    ss << "VignetteIntensity=" << m_config.vignetteIntensity << "\n";
    ss << "SeatedMode=" << (m_config.seatedMode ? "true" : "false") << "\n";
    ss << "StandingHeight=" << m_config.standingHeight << "\n\n";

    // HUD
    ss << "[HUD]\n";
    ss << "; Options: floating, head_locked, wrist, hidden\n";
    ss << "HUDMode=";
    switch (m_config.hudMode) {
        case HUDMode::HeadLocked: ss << "head_locked"; break;
        case HUDMode::Wrist: ss << "wrist"; break;
        case HUDMode::Hidden: ss << "hidden"; break;
        default: ss << "floating"; break;
    }
    ss << "\n";
    ss << "HUDDistance=" << m_config.hudDistance << "\n";
    ss << "HUDScale=" << m_config.hudScale << "\n";
    ss << "HUDOpacity=" << m_config.hudOpacity << "\n";
    ss << "HUDCurvature=" << m_config.hudCurvature << "\n";
    ss << "RadarScale=" << m_config.radarScale << "\n";
    ss << "SubtitleSize=" << m_config.subtitleSize << "\n\n";

    // Camera
    ss << "[Camera]\n";
    ss << "ForceFirstPerson=" << (m_config.forceFirstPerson ? "true" : "false") << "\n";
    ss << "; Options: dashboard, hood, third_person\n";
    ss << "VehicleCamera=";
    switch (m_config.vehicleCamera) {
        case VehicleCameraMode::Hood: ss << "hood"; break;
        case VehicleCameraMode::ThirdPerson: ss << "third_person"; break;
        default: ss << "dashboard"; break;
    }
    ss << "\n";
    ss << "; Options: vr_adapted, theater, original, skip\n";
    ss << "CutsceneMode=";
    switch (m_config.cutsceneMode) {
        case CutsceneMode::Theater: ss << "theater"; break;
        case CutsceneMode::Original: ss << "original"; break;
        case CutsceneMode::Skip: ss << "skip"; break;
        default: ss << "vr_adapted"; break;
    }
    ss << "\n";
    ss << "EnablePositionalTracking=" << (m_config.enablePositionalTracking ? "true" : "false") << "\n";
    ss << "PositionalScale=" << m_config.positionalScale << "\n";
    ss << "; Options: head_shake, button, both\n";
    ss << "RecenterMethod=";
    switch (m_config.recenterMethod) {
        case RecenterMethod::Button: ss << "button"; break;
        case RecenterMethod::Both: ss << "both"; break;
        default: ss << "head_shake"; break;
    }
    ss << "\n";
    ss << "RecenterButton=" << std::hex << m_config.recenterButton << std::dec << "\n\n";

    // Input
    ss << "[Input]\n";
    ss << "; Options: gamepad, motion_controllers, hybrid\n";
    ss << "InputMode=";
    switch (m_config.inputMode) {
        case InputMode::MotionControllers: ss << "motion_controllers"; break;
        case InputMode::Hybrid: ss << "hybrid"; break;
        default: ss << "gamepad"; break;
    }
    ss << "\n";
    ss << "SwapSticks=" << (m_config.swapSticks ? "true" : "false") << "\n";
    ss << "EnableHaptics=" << (m_config.enableHaptics ? "true" : "false") << "\n";
    ss << "HapticIntensity=" << m_config.hapticIntensity << "\n\n";

    // Performance
    ss << "[Performance]\n";
    ss << "ShowPerformanceOverlay=" << (m_config.showPerformanceOverlay ? "true" : "false") << "\n";
    ss << "ShowReprojectionIndicator=" << (m_config.showReprojectionIndicator ? "true" : "false") << "\n";
    ss << "LimitFramerate=" << (m_config.limitFramerate ? "true" : "false") << "\n";
    ss << "FramerateLimit=" << m_config.framerateLimit << "\n\n";

    // Debug
    ss << "[Debug]\n";
    ss << "EnableDebugMode=" << (m_config.enableDebugMode ? "true" : "false") << "\n";
    ss << "; Options: minimal, verbose, debug\n";
    ss << "LogLevel=";
    switch (m_config.logLevel) {
        case 0: ss << "minimal"; break;
        case 2: ss << "debug"; break;
        default: ss << "verbose"; break;
    }
    ss << "\n";
    ss << "LogToFile=" << (m_config.logToFile ? "true" : "false") << "\n";
    ss << "LogFilePath=" << m_config.logFilePath << "\n";
    ss << "ShowDebugOverlay=" << (m_config.showDebugOverlay ? "true" : "false") << "\n\n";

    // Advanced
    ss << "[Advanced]\n";
    ss << "; These settings are for advanced users\n";
    ss << "DisablePostProcessing=" << (m_config.disablePostProcessing ? "true" : "false") << "\n";
    ss << "DisableDOF=" << (m_config.disableDOF ? "true" : "false") << "\n";
    ss << "DisableMotionBlur=" << (m_config.disableMotionBlur ? "true" : "false") << "\n";
    ss << "DisableBloom=" << (m_config.disableBloom ? "true" : "false") << "\n";
    ss << "UseAsyncCompute=" << (m_config.useAsyncCompute ? "true" : "false") << "\n";
    ss << "PreferDedicatedGPU=" << (m_config.preferDedicatedGPU ? "true" : "false") << "\n";

    return ss.str();
}

bool VRConfig::LoadProfile(const std::string& profileName) {
    std::string profilePath = "config/profiles/" + profileName + ".ini";
    return Load(profilePath);
}

bool VRConfig::SaveProfile(const std::string& profileName) {
    std::string profilePath = "config/profiles/" + profileName + ".ini";
    return SaveAs(profilePath);
}

std::vector<std::string> VRConfig::GetAvailableProfiles() const {
    std::vector<std::string> profiles;
    std::string profileDir = "config/profiles/";

    if (std::filesystem::exists(profileDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(profileDir)) {
            if (entry.path().extension() == ".ini") {
                profiles.push_back(entry.path().stem().string());
            }
        }
    }

    return profiles;
}

} // namespace GTA5VR
