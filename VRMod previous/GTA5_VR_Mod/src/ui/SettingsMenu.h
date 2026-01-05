#pragma once

#include <string>
#include <vector>
#include <functional>
#include <map>

namespace GTA5VR {

// Menu categories
enum class SettingsCategory {
    Display,
    Comfort,
    HUD,
    Camera,
    Controls,
    Performance,
    Advanced,
    About
};

// Setting types
enum class SettingType {
    Toggle,
    Slider,
    Dropdown,
    Action,
    Separator,
    Header
};

// Forward declarations
class VRConfig;

// Setting definition
struct SettingDefinition {
    std::string id;
    std::string label;
    std::string description;
    SettingType type;
    SettingsCategory category;

    // For sliders
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float step = 0.1f;
    std::string format = "%.1f";

    // For dropdowns
    std::vector<std::string> options;

    // Current value
    union {
        bool boolValue;
        float floatValue;
        int intValue;
    };

    // Callbacks
    std::function<void(const SettingDefinition&)> onChange;
};

// Menu state
struct MenuState {
    bool isVisible = false;
    SettingsCategory currentCategory = SettingsCategory::Display;
    int selectedIndex = 0;
    float scrollOffset = 0.0f;
    bool isEditing = false;
    float editValue = 0.0f;
};

class SettingsMenu {
public:
    SettingsMenu();
    ~SettingsMenu();

    // Prevent copying
    SettingsMenu(const SettingsMenu&) = delete;
    SettingsMenu& operator=(const SettingsMenu&) = delete;

    // Initialization
    bool Initialize(VRConfig* config);
    void Shutdown();

    // Visibility
    void Show();
    void Hide();
    void Toggle();
    bool IsVisible() const { return m_state.isVisible; }

    // Update (call every frame when visible)
    void Update(float deltaTime);

    // Input handling
    void OnThumbstickMove(float x, float y);
    void OnTriggerPressed();
    void OnGripPressed();
    void OnMenuButtonPressed();
    void OnBackPressed();

    // Navigation
    void NavigateUp();
    void NavigateDown();
    void NavigateLeft();
    void NavigateRight();
    void SelectCurrent();
    void GoBack();

    // Category navigation
    void SelectCategory(SettingsCategory category);
    SettingsCategory GetCurrentCategory() const { return m_state.currentCategory; }

    // Get menu data for rendering
    const MenuState& GetMenuState() const { return m_state; }
    std::vector<SettingDefinition>& GetCurrentSettings();
    const std::vector<SettingsCategory>& GetCategories() const { return m_categories; }
    std::string GetCategoryName(SettingsCategory category) const;

    // Value changes (for external modification)
    void SetSettingValue(const std::string& id, bool value);
    void SetSettingValue(const std::string& id, float value);
    void SetSettingValue(const std::string& id, int value);

    // Get current selection info
    SettingDefinition* GetSelectedSetting();
    int GetSelectedIndex() const { return m_state.selectedIndex; }

    // Presets
    void ApplyPreset(const std::string& presetName);
    std::vector<std::string> GetPresetNames() const;

    // Save/Load
    void SaveCurrentSettings();
    void LoadSettings();
    void ResetToDefaults();

private:
    void InitializeSettings();
    void RegisterDisplaySettings();
    void RegisterComfortSettings();
    void RegisterHUDSettings();
    void RegisterCameraSettings();
    void RegisterControlSettings();
    void RegisterPerformanceSettings();
    void RegisterAdvancedSettings();
    void RegisterAboutSection();

    void UpdateFromConfig();
    void ApplyToConfig();

    SettingDefinition& AddSetting(const SettingDefinition& setting);
    void AddSeparator(SettingsCategory category);
    void AddHeader(SettingsCategory category, const std::string& text);

private:
    VRConfig* m_config = nullptr;
    MenuState m_state;

    // Settings organized by category
    std::map<SettingsCategory, std::vector<SettingDefinition>> m_settings;
    std::vector<SettingsCategory> m_categories;

    // Input timing
    float m_inputRepeatDelay = 0.3f;
    float m_inputRepeatRate = 0.1f;
    float m_timeSinceLastInput = 0.0f;
    bool m_isRepeating = false;

    // Animation
    float m_showAnimation = 0.0f;
    float m_categoryAnimation = 0.0f;
    SettingsCategory m_previousCategory;

    bool m_initialized = false;
};

} // namespace GTA5VR
