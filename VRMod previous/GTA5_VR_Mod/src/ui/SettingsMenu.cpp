#include "SettingsMenu.h"
#include "../core/VRConfig.h"
#include "../core/Logger.h"
#include <algorithm>

namespace GTA5VR {

SettingsMenu::SettingsMenu() {
    LOG_DEBUG("SettingsMenu", "Constructor called");

    // Initialize category list
    m_categories = {
        SettingsCategory::Display,
        SettingsCategory::Comfort,
        SettingsCategory::HUD,
        SettingsCategory::Camera,
        SettingsCategory::Controls,
        SettingsCategory::Performance,
        SettingsCategory::Advanced,
        SettingsCategory::About
    };
}

SettingsMenu::~SettingsMenu() {
    Shutdown();
}

bool SettingsMenu::Initialize(VRConfig* config) {
    if (m_initialized) {
        return true;
    }

    m_config = config;

    LOG_INFO("SettingsMenu", "Initializing settings menu");

    InitializeSettings();
    UpdateFromConfig();

    m_initialized = true;
    return true;
}

void SettingsMenu::Shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("SettingsMenu", "Shutting down settings menu");

    m_settings.clear();
    m_initialized = false;
}

void SettingsMenu::Show() {
    m_state.isVisible = true;
    m_showAnimation = 0.0f;
    LOG_DEBUG("SettingsMenu", "Menu shown");
}

void SettingsMenu::Hide() {
    m_state.isVisible = false;
    m_state.isEditing = false;
    SaveCurrentSettings();
    LOG_DEBUG("SettingsMenu", "Menu hidden");
}

void SettingsMenu::Toggle() {
    if (m_state.isVisible) {
        Hide();
    } else {
        Show();
    }
}

void SettingsMenu::Update(float deltaTime) {
    if (!m_state.isVisible) {
        return;
    }

    // Update animations
    m_showAnimation = std::min(1.0f, m_showAnimation + deltaTime * 4.0f);
    m_categoryAnimation = std::min(1.0f, m_categoryAnimation + deltaTime * 6.0f);

    // Update input repeat timing
    m_timeSinceLastInput += deltaTime;
}

void SettingsMenu::OnThumbstickMove(float x, float y) {
    // Deadzone
    if (std::abs(x) < 0.3f) x = 0.0f;
    if (std::abs(y) < 0.3f) y = 0.0f;

    if (x == 0.0f && y == 0.0f) {
        m_isRepeating = false;
        return;
    }

    // Check if we should process input
    float delay = m_isRepeating ? m_inputRepeatRate : m_inputRepeatDelay;
    if (m_timeSinceLastInput < delay) {
        return;
    }

    m_timeSinceLastInput = 0.0f;
    m_isRepeating = true;

    // Navigate
    if (y > 0.5f) {
        NavigateUp();
    } else if (y < -0.5f) {
        NavigateDown();
    }

    if (x > 0.5f) {
        NavigateRight();
    } else if (x < -0.5f) {
        NavigateLeft();
    }
}

void SettingsMenu::OnTriggerPressed() {
    SelectCurrent();
}

void SettingsMenu::OnGripPressed() {
    GoBack();
}

void SettingsMenu::OnMenuButtonPressed() {
    Toggle();
}

void SettingsMenu::OnBackPressed() {
    if (m_state.isEditing) {
        m_state.isEditing = false;
    } else {
        Hide();
    }
}

void SettingsMenu::NavigateUp() {
    auto& settings = GetCurrentSettings();
    if (settings.empty()) return;

    m_state.selectedIndex--;
    if (m_state.selectedIndex < 0) {
        m_state.selectedIndex = static_cast<int>(settings.size()) - 1;
    }

    // Skip separators and headers
    while (m_state.selectedIndex >= 0 &&
           (settings[m_state.selectedIndex].type == SettingType::Separator ||
            settings[m_state.selectedIndex].type == SettingType::Header)) {
        m_state.selectedIndex--;
        if (m_state.selectedIndex < 0) {
            m_state.selectedIndex = static_cast<int>(settings.size()) - 1;
        }
    }
}

void SettingsMenu::NavigateDown() {
    auto& settings = GetCurrentSettings();
    if (settings.empty()) return;

    m_state.selectedIndex++;
    if (m_state.selectedIndex >= static_cast<int>(settings.size())) {
        m_state.selectedIndex = 0;
    }

    // Skip separators and headers
    while (m_state.selectedIndex < static_cast<int>(settings.size()) &&
           (settings[m_state.selectedIndex].type == SettingType::Separator ||
            settings[m_state.selectedIndex].type == SettingType::Header)) {
        m_state.selectedIndex++;
        if (m_state.selectedIndex >= static_cast<int>(settings.size())) {
            m_state.selectedIndex = 0;
        }
    }
}

void SettingsMenu::NavigateLeft() {
    SettingDefinition* setting = GetSelectedSetting();
    if (!setting) return;

    if (setting->type == SettingType::Slider) {
        setting->floatValue = std::max(setting->minValue,
                                        setting->floatValue - setting->step);
        if (setting->onChange) {
            setting->onChange(*setting);
        }
        ApplyToConfig();
    } else if (setting->type == SettingType::Dropdown) {
        setting->intValue--;
        if (setting->intValue < 0) {
            setting->intValue = static_cast<int>(setting->options.size()) - 1;
        }
        if (setting->onChange) {
            setting->onChange(*setting);
        }
        ApplyToConfig();
    } else {
        // Navigate to previous category
        auto it = std::find(m_categories.begin(), m_categories.end(), m_state.currentCategory);
        if (it != m_categories.begin()) {
            SelectCategory(*(it - 1));
        } else {
            SelectCategory(m_categories.back());
        }
    }
}

void SettingsMenu::NavigateRight() {
    SettingDefinition* setting = GetSelectedSetting();
    if (!setting) return;

    if (setting->type == SettingType::Slider) {
        setting->floatValue = std::min(setting->maxValue,
                                        setting->floatValue + setting->step);
        if (setting->onChange) {
            setting->onChange(*setting);
        }
        ApplyToConfig();
    } else if (setting->type == SettingType::Dropdown) {
        setting->intValue++;
        if (setting->intValue >= static_cast<int>(setting->options.size())) {
            setting->intValue = 0;
        }
        if (setting->onChange) {
            setting->onChange(*setting);
        }
        ApplyToConfig();
    } else {
        // Navigate to next category
        auto it = std::find(m_categories.begin(), m_categories.end(), m_state.currentCategory);
        if (it != m_categories.end() && (it + 1) != m_categories.end()) {
            SelectCategory(*(it + 1));
        } else {
            SelectCategory(m_categories.front());
        }
    }
}

void SettingsMenu::SelectCurrent() {
    SettingDefinition* setting = GetSelectedSetting();
    if (!setting) return;

    if (setting->type == SettingType::Toggle) {
        setting->boolValue = !setting->boolValue;
        if (setting->onChange) {
            setting->onChange(*setting);
        }
        ApplyToConfig();
    } else if (setting->type == SettingType::Action) {
        if (setting->onChange) {
            setting->onChange(*setting);
        }
    } else if (setting->type == SettingType::Slider ||
               setting->type == SettingType::Dropdown) {
        m_state.isEditing = !m_state.isEditing;
    }
}

void SettingsMenu::GoBack() {
    if (m_state.isEditing) {
        m_state.isEditing = false;
    } else {
        Hide();
    }
}

void SettingsMenu::SelectCategory(SettingsCategory category) {
    if (m_state.currentCategory == category) return;

    m_previousCategory = m_state.currentCategory;
    m_state.currentCategory = category;
    m_state.selectedIndex = 0;
    m_state.scrollOffset = 0.0f;
    m_categoryAnimation = 0.0f;

    // Find first non-separator/header item
    auto& settings = GetCurrentSettings();
    while (m_state.selectedIndex < static_cast<int>(settings.size()) &&
           (settings[m_state.selectedIndex].type == SettingType::Separator ||
            settings[m_state.selectedIndex].type == SettingType::Header)) {
        m_state.selectedIndex++;
    }
}

std::string SettingsMenu::GetCategoryName(SettingsCategory category) const {
    switch (category) {
        case SettingsCategory::Display: return "Display";
        case SettingsCategory::Comfort: return "Comfort";
        case SettingsCategory::HUD: return "HUD";
        case SettingsCategory::Camera: return "Camera";
        case SettingsCategory::Controls: return "Controls";
        case SettingsCategory::Performance: return "Performance";
        case SettingsCategory::Advanced: return "Advanced";
        case SettingsCategory::About: return "About";
        default: return "Unknown";
    }
}

std::vector<SettingDefinition>& SettingsMenu::GetCurrentSettings() {
    return m_settings[m_state.currentCategory];
}

SettingDefinition* SettingsMenu::GetSelectedSetting() {
    auto& settings = GetCurrentSettings();
    if (m_state.selectedIndex >= 0 &&
        m_state.selectedIndex < static_cast<int>(settings.size())) {
        return &settings[m_state.selectedIndex];
    }
    return nullptr;
}

void SettingsMenu::SetSettingValue(const std::string& id, bool value) {
    for (auto& categoryPair : m_settings) {
        for (auto& setting : categoryPair.second) {
            if (setting.id == id && setting.type == SettingType::Toggle) {
                setting.boolValue = value;
                return;
            }
        }
    }
}

void SettingsMenu::SetSettingValue(const std::string& id, float value) {
    for (auto& categoryPair : m_settings) {
        for (auto& setting : categoryPair.second) {
            if (setting.id == id && setting.type == SettingType::Slider) {
                setting.floatValue = std::clamp(value, setting.minValue, setting.maxValue);
                return;
            }
        }
    }
}

void SettingsMenu::SetSettingValue(const std::string& id, int value) {
    for (auto& categoryPair : m_settings) {
        for (auto& setting : categoryPair.second) {
            if (setting.id == id && setting.type == SettingType::Dropdown) {
                setting.intValue = std::clamp(value, 0, static_cast<int>(setting.options.size()) - 1);
                return;
            }
        }
    }
}

void SettingsMenu::ApplyPreset(const std::string& presetName) {
    LOG_INFO("SettingsMenu", "Applying preset: %s", presetName.c_str());

    if (presetName == "Performance") {
        SetSettingValue("resolution_scale", 0.8f);
        SetSettingValue("render_mode", 2);  // AER
        SetSettingValue("debug_overlay", false);
    } else if (presetName == "Quality") {
        SetSettingValue("resolution_scale", 1.0f);
        SetSettingValue("render_mode", 0);  // Native Stereo
        SetSettingValue("depth_buffer", true);
    } else if (presetName == "Comfort") {
        SetSettingValue("snap_turn", true);
        SetSettingValue("vignette", true);
        SetSettingValue("world_scale", 1.0f);
    }

    ApplyToConfig();
}

std::vector<std::string> SettingsMenu::GetPresetNames() const {
    return {"Performance", "Quality", "Comfort", "Default"};
}

void SettingsMenu::SaveCurrentSettings() {
    if (m_config) {
        ApplyToConfig();
        m_config->Save();
        LOG_INFO("SettingsMenu", "Settings saved");
    }
}

void SettingsMenu::LoadSettings() {
    if (m_config) {
        m_config->Load();
        UpdateFromConfig();
        LOG_INFO("SettingsMenu", "Settings loaded");
    }
}

void SettingsMenu::ResetToDefaults() {
    if (m_config) {
        m_config->ResetToDefaults();
        UpdateFromConfig();
        LOG_INFO("SettingsMenu", "Settings reset to defaults");
    }
}

void SettingsMenu::InitializeSettings() {
    RegisterDisplaySettings();
    RegisterComfortSettings();
    RegisterHUDSettings();
    RegisterCameraSettings();
    RegisterControlSettings();
    RegisterPerformanceSettings();
    RegisterAdvancedSettings();
    RegisterAboutSection();
}

SettingDefinition& SettingsMenu::AddSetting(const SettingDefinition& setting) {
    m_settings[setting.category].push_back(setting);
    return m_settings[setting.category].back();
}

void SettingsMenu::AddSeparator(SettingsCategory category) {
    SettingDefinition sep;
    sep.type = SettingType::Separator;
    sep.category = category;
    m_settings[category].push_back(sep);
}

void SettingsMenu::AddHeader(SettingsCategory category, const std::string& text) {
    SettingDefinition header;
    header.type = SettingType::Header;
    header.category = category;
    header.label = text;
    m_settings[category].push_back(header);
}

void SettingsMenu::RegisterDisplaySettings() {
    SettingsCategory cat = SettingsCategory::Display;

    AddHeader(cat, "VR Runtime");

    SettingDefinition runtime;
    runtime.id = "runtime";
    runtime.label = "VR Runtime";
    runtime.description = "Select VR runtime (restart required)";
    runtime.type = SettingType::Dropdown;
    runtime.category = cat;
    runtime.options = {"Auto", "OpenXR", "OpenVR/SteamVR"};
    runtime.intValue = 0;
    AddSetting(runtime);

    SettingDefinition renderMode;
    renderMode.id = "render_mode";
    renderMode.label = "Rendering Mode";
    renderMode.description = "Stereo rendering method";
    renderMode.type = SettingType::Dropdown;
    renderMode.category = cat;
    renderMode.options = {"Native Stereo", "Synchronized Sequential", "Alternating Eye"};
    renderMode.intValue = 1;
    AddSetting(renderMode);

    AddSeparator(cat);
    AddHeader(cat, "Resolution");

    SettingDefinition resScale;
    resScale.id = "resolution_scale";
    resScale.label = "Resolution Scale";
    resScale.description = "VR render resolution multiplier";
    resScale.type = SettingType::Slider;
    resScale.category = cat;
    resScale.minValue = 0.5f;
    resScale.maxValue = 2.0f;
    resScale.step = 0.1f;
    resScale.format = "%.0f%%";
    resScale.floatValue = 1.0f;
    AddSetting(resScale);

    SettingDefinition depthBuffer;
    depthBuffer.id = "depth_buffer";
    depthBuffer.label = "Depth Buffer";
    depthBuffer.description = "Enable depth buffer for ASW 2.0";
    depthBuffer.type = SettingType::Toggle;
    depthBuffer.category = cat;
    depthBuffer.boolValue = true;
    AddSetting(depthBuffer);
}

void SettingsMenu::RegisterComfortSettings() {
    SettingsCategory cat = SettingsCategory::Comfort;

    AddHeader(cat, "World");

    SettingDefinition worldScale;
    worldScale.id = "world_scale";
    worldScale.label = "World Scale";
    worldScale.description = "Adjust perceived world size";
    worldScale.type = SettingType::Slider;
    worldScale.category = cat;
    worldScale.minValue = 0.5f;
    worldScale.maxValue = 2.0f;
    worldScale.step = 0.05f;
    worldScale.format = "%.2f";
    worldScale.floatValue = 1.0f;
    AddSetting(worldScale);

    SettingDefinition ipd;
    ipd.id = "ipd_override";
    ipd.label = "IPD Override";
    ipd.description = "Override headset IPD (0 = use headset)";
    ipd.type = SettingType::Slider;
    ipd.category = cat;
    ipd.minValue = 0.0f;
    ipd.maxValue = 80.0f;
    ipd.step = 0.5f;
    ipd.format = "%.1f mm";
    ipd.floatValue = 0.0f;
    AddSetting(ipd);

    AddSeparator(cat);
    AddHeader(cat, "Movement");

    SettingDefinition snapTurn;
    snapTurn.id = "snap_turn";
    snapTurn.label = "Snap Turn";
    snapTurn.description = "Use snap turning instead of smooth";
    snapTurn.type = SettingType::Toggle;
    snapTurn.category = cat;
    snapTurn.boolValue = true;
    AddSetting(snapTurn);

    SettingDefinition snapAngle;
    snapAngle.id = "snap_angle";
    snapAngle.label = "Snap Angle";
    snapAngle.description = "Degrees per snap turn";
    snapAngle.type = SettingType::Slider;
    snapAngle.category = cat;
    snapAngle.minValue = 15.0f;
    snapAngle.maxValue = 90.0f;
    snapAngle.step = 15.0f;
    snapAngle.format = "%.0f";
    snapAngle.floatValue = 45.0f;
    AddSetting(snapAngle);

    SettingDefinition vignette;
    vignette.id = "vignette";
    vignette.label = "Comfort Vignette";
    vignette.description = "Reduce FOV during movement";
    vignette.type = SettingType::Toggle;
    vignette.category = cat;
    vignette.boolValue = true;
    AddSetting(vignette);
}

void SettingsMenu::RegisterHUDSettings() {
    SettingsCategory cat = SettingsCategory::HUD;

    AddHeader(cat, "HUD Display");

    SettingDefinition hudMode;
    hudMode.id = "hud_mode";
    hudMode.label = "HUD Mode";
    hudMode.description = "How the HUD is displayed in VR";
    hudMode.type = SettingType::Dropdown;
    hudMode.category = cat;
    hudMode.options = {"Disabled", "Floating", "Helmet", "World-Locked"};
    hudMode.intValue = 1;
    AddSetting(hudMode);

    SettingDefinition hudDistance;
    hudDistance.id = "hud_distance";
    hudDistance.label = "HUD Distance";
    hudDistance.description = "Distance of floating HUD";
    hudDistance.type = SettingType::Slider;
    hudDistance.category = cat;
    hudDistance.minValue = 0.5f;
    hudDistance.maxValue = 5.0f;
    hudDistance.step = 0.1f;
    hudDistance.format = "%.1f m";
    hudDistance.floatValue = 2.0f;
    AddSetting(hudDistance);

    SettingDefinition hudScale;
    hudScale.id = "hud_scale";
    hudScale.label = "HUD Scale";
    hudScale.description = "Size of the HUD";
    hudScale.type = SettingType::Slider;
    hudScale.category = cat;
    hudScale.minValue = 0.5f;
    hudScale.maxValue = 2.0f;
    hudScale.step = 0.1f;
    hudScale.format = "%.1f";
    hudScale.floatValue = 1.0f;
    AddSetting(hudScale);

    SettingDefinition hudOpacity;
    hudOpacity.id = "hud_opacity";
    hudOpacity.label = "HUD Opacity";
    hudOpacity.description = "Transparency of the HUD";
    hudOpacity.type = SettingType::Slider;
    hudOpacity.category = cat;
    hudOpacity.minValue = 0.1f;
    hudOpacity.maxValue = 1.0f;
    hudOpacity.step = 0.1f;
    hudOpacity.format = "%.0f%%";
    hudOpacity.floatValue = 0.9f;
    AddSetting(hudOpacity);
}

void SettingsMenu::RegisterCameraSettings() {
    SettingsCategory cat = SettingsCategory::Camera;

    AddHeader(cat, "On Foot");

    SettingDefinition forceFirstPerson;
    forceFirstPerson.id = "force_first_person";
    forceFirstPerson.label = "Force First Person";
    forceFirstPerson.description = "Always use first person view";
    forceFirstPerson.type = SettingType::Toggle;
    forceFirstPerson.category = cat;
    forceFirstPerson.boolValue = true;
    AddSetting(forceFirstPerson);

    AddSeparator(cat);
    AddHeader(cat, "Vehicles");

    SettingDefinition vehicleCamera;
    vehicleCamera.id = "vehicle_camera";
    vehicleCamera.label = "Vehicle Camera";
    vehicleCamera.description = "Default camera in vehicles";
    vehicleCamera.type = SettingType::Dropdown;
    vehicleCamera.category = cat;
    vehicleCamera.options = {"Dashboard", "Hood", "First Person", "Chase"};
    vehicleCamera.intValue = 0;
    AddSetting(vehicleCamera);

    AddSeparator(cat);
    AddHeader(cat, "Cutscenes");

    SettingDefinition cutsceneMode;
    cutsceneMode.id = "cutscene_mode";
    cutsceneMode.label = "Cutscene Mode";
    cutsceneMode.description = "How cutscenes are displayed";
    cutsceneMode.type = SettingType::Dropdown;
    cutsceneMode.category = cat;
    cutsceneMode.options = {"Standard", "VR Adapted", "Theater", "First Person", "Skip"};
    cutsceneMode.intValue = 1;
    AddSetting(cutsceneMode);
}

void SettingsMenu::RegisterControlSettings() {
    SettingsCategory cat = SettingsCategory::Controls;

    AddHeader(cat, "Input");

    SettingDefinition inputMode;
    inputMode.id = "input_mode";
    inputMode.label = "Input Mode";
    inputMode.description = "Controller input handling";
    inputMode.type = SettingType::Dropdown;
    inputMode.category = cat;
    inputMode.options = {"Motion Controllers", "Gamepad", "Mixed"};
    inputMode.intValue = 0;
    AddSetting(inputMode);

    SettingDefinition dominantHand;
    dominantHand.id = "dominant_hand";
    dominantHand.label = "Dominant Hand";
    dominantHand.description = "Primary hand for aiming";
    dominantHand.type = SettingType::Dropdown;
    dominantHand.category = cat;
    dominantHand.options = {"Right", "Left"};
    dominantHand.intValue = 0;
    AddSetting(dominantHand);

    AddSeparator(cat);
    AddHeader(cat, "Haptics");

    SettingDefinition haptics;
    haptics.id = "haptics_enabled";
    haptics.label = "Haptic Feedback";
    haptics.description = "Enable controller vibration";
    haptics.type = SettingType::Toggle;
    haptics.category = cat;
    haptics.boolValue = true;
    AddSetting(haptics);

    SettingDefinition hapticsStrength;
    hapticsStrength.id = "haptics_strength";
    hapticsStrength.label = "Haptics Strength";
    hapticsStrength.description = "Vibration intensity";
    hapticsStrength.type = SettingType::Slider;
    hapticsStrength.category = cat;
    hapticsStrength.minValue = 0.0f;
    hapticsStrength.maxValue = 1.0f;
    hapticsStrength.step = 0.1f;
    hapticsStrength.format = "%.0f%%";
    hapticsStrength.floatValue = 0.8f;
    AddSetting(hapticsStrength);
}

void SettingsMenu::RegisterPerformanceSettings() {
    SettingsCategory cat = SettingsCategory::Performance;

    AddHeader(cat, "Overlays");

    SettingDefinition fpsOverlay;
    fpsOverlay.id = "fps_overlay";
    fpsOverlay.label = "FPS Overlay";
    fpsOverlay.description = "Show framerate counter";
    fpsOverlay.type = SettingType::Toggle;
    fpsOverlay.category = cat;
    fpsOverlay.boolValue = false;
    AddSetting(fpsOverlay);

    SettingDefinition debugOverlay;
    debugOverlay.id = "debug_overlay";
    debugOverlay.label = "Debug Overlay";
    debugOverlay.description = "Show debug information";
    debugOverlay.type = SettingType::Toggle;
    debugOverlay.category = cat;
    debugOverlay.boolValue = false;
    AddSetting(debugOverlay);

    AddSeparator(cat);

    SettingDefinition frameTimingOverlay;
    frameTimingOverlay.id = "frame_timing";
    frameTimingOverlay.label = "Frame Timing";
    frameTimingOverlay.description = "Show detailed frame timing";
    frameTimingOverlay.type = SettingType::Toggle;
    frameTimingOverlay.category = cat;
    frameTimingOverlay.boolValue = false;
    AddSetting(frameTimingOverlay);
}

void SettingsMenu::RegisterAdvancedSettings() {
    SettingsCategory cat = SettingsCategory::Advanced;

    AddHeader(cat, "Logging");

    SettingDefinition logLevel;
    logLevel.id = "log_level";
    logLevel.label = "Log Level";
    logLevel.description = "Verbosity of log output";
    logLevel.type = SettingType::Dropdown;
    logLevel.category = cat;
    logLevel.options = {"Error", "Warning", "Info", "Debug"};
    logLevel.intValue = 2;
    AddSetting(logLevel);

    AddSeparator(cat);
    AddHeader(cat, "Actions");

    SettingDefinition resetSettings;
    resetSettings.id = "reset_settings";
    resetSettings.label = "Reset to Defaults";
    resetSettings.description = "Reset all settings to defaults";
    resetSettings.type = SettingType::Action;
    resetSettings.category = cat;
    resetSettings.onChange = [this](const SettingDefinition&) {
        ResetToDefaults();
    };
    AddSetting(resetSettings);

    SettingDefinition recenter;
    recenter.id = "recenter_view";
    recenter.label = "Recenter View";
    recenter.description = "Reset VR view position";
    recenter.type = SettingType::Action;
    recenter.category = cat;
    AddSetting(recenter);
}

void SettingsMenu::RegisterAboutSection() {
    SettingsCategory cat = SettingsCategory::About;

    AddHeader(cat, "GTA 5 VR Mod");

    SettingDefinition version;
    version.id = "version_info";
    version.label = "Version 1.0.0";
    version.description = "Built with OpenXR support";
    version.type = SettingType::Header;
    version.category = cat;
    AddSetting(version);

    AddSeparator(cat);

    SettingDefinition credits;
    credits.id = "credits";
    credits.label = "Press F12 to recenter view";
    credits.type = SettingType::Header;
    credits.category = cat;
    AddSetting(credits);
}

void SettingsMenu::UpdateFromConfig() {
    if (!m_config) return;

    // Update settings from config values
    SetSettingValue("runtime", m_config->GetInt("VRRuntime", "VRAPI", 0));
    SetSettingValue("render_mode", m_config->GetInt("Rendering", "RenderingMode", 1));
    SetSettingValue("resolution_scale", m_config->GetFloat("Rendering", "ResolutionScale", 100.0f) / 100.0f);
    SetSettingValue("depth_buffer", m_config->GetBool("Rendering", "EnableDepthBuffer", true));
    SetSettingValue("world_scale", m_config->GetFloat("Display", "WorldScale", 1.0f));
    SetSettingValue("snap_turn", m_config->GetBool("Comfort", "EnableSnapTurn", true));
    SetSettingValue("snap_angle", m_config->GetFloat("Comfort", "SnapTurnDegrees", 45.0f));
    SetSettingValue("vignette", m_config->GetBool("Comfort", "EnableComfortVignette", true));
    SetSettingValue("force_first_person", m_config->GetBool("Camera", "ForceFirstPerson", true));
}

void SettingsMenu::ApplyToConfig() {
    if (!m_config) return;

    // Apply settings to config
    for (auto& categoryPair : m_settings) {
        for (auto& setting : categoryPair.second) {
            if (setting.id == "runtime") {
                m_config->SetInt("VRRuntime", "VRAPI", setting.intValue);
            } else if (setting.id == "render_mode") {
                m_config->SetInt("Rendering", "RenderingMode", setting.intValue);
            } else if (setting.id == "resolution_scale") {
                m_config->SetFloat("Rendering", "ResolutionScale", setting.floatValue * 100.0f);
            } else if (setting.id == "depth_buffer") {
                m_config->SetBool("Rendering", "EnableDepthBuffer", setting.boolValue);
            } else if (setting.id == "world_scale") {
                m_config->SetFloat("Display", "WorldScale", setting.floatValue);
            } else if (setting.id == "snap_turn") {
                m_config->SetBool("Comfort", "EnableSnapTurn", setting.boolValue);
            } else if (setting.id == "snap_angle") {
                m_config->SetFloat("Comfort", "SnapTurnDegrees", setting.floatValue);
            } else if (setting.id == "vignette") {
                m_config->SetBool("Comfort", "EnableComfortVignette", setting.boolValue);
            } else if (setting.id == "force_first_person") {
                m_config->SetBool("Camera", "ForceFirstPerson", setting.boolValue);
            }
        }
    }
}

} // namespace GTA5VR
