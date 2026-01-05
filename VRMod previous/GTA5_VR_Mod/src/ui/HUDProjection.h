#pragma once

#include "../input/HeadTracking.h"
#include <d3d11.h>
#include <vector>

namespace GTA5VR {

// HUD display mode
enum class HUDMode {
    Disabled,       // HUD not shown
    Floating,       // HUD floats in front of player
    Helmet,         // HUD follows head movement
    WorldLocked     // HUD fixed in world space
};

// HUD element type
enum class HUDElementType {
    Minimap,
    Health,
    Armor,
    Weapon,
    Ammo,
    WantedLevel,
    Money,
    Subtitles,
    MissionInfo,
    RadioInfo,
    Other
};

// Individual HUD element
struct HUDElement {
    HUDElementType type;
    float x, y;             // Position on original HUD (0-1)
    float width, height;    // Size (0-1)
    float opacity;
    bool visible;
    bool important;         // Keep visible even in minimal mode
};

// HUD texture capture
struct HUDCapture {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    bool valid = false;
};

// HUD projection settings
struct HUDSettings {
    HUDMode mode = HUDMode::Floating;

    // Floating mode settings
    float distance = 2.0f;          // Meters from player
    float scale = 1.0f;
    float opacity = 0.9f;
    float width = 1.5f;             // Meters
    float height = 0.84375f;        // 16:9 aspect

    // Position offset (for all modes)
    float offsetX = 0.0f;           // Left/right offset
    float offsetY = -0.1f;          // Up/down offset (default: slightly below center)

    // Helmet mode settings
    float helmetFollowSpeed = 5.0f; // How fast HUD follows head
    float helmetDeadzone = 10.0f;   // Degrees before HUD starts following

    // Curvature (0 = flat, 1 = full curve)
    float curvature = 0.2f;

    // Visibility
    bool showMinimap = true;
    bool showHealth = true;
    bool showWeapon = true;
    bool showAmmo = true;
    bool showMoney = true;
    bool showWantedLevel = true;
    bool showSubtitles = true;
    bool minimalMode = false;       // Only show important elements
};

class HUDProjection {
public:
    HUDProjection();
    ~HUDProjection();

    // Prevent copying
    HUDProjection(const HUDProjection&) = delete;
    HUDProjection& operator=(const HUDProjection&) = delete;

    // Initialization
    bool Initialize(ID3D11Device* device);
    void Shutdown();

    // Update (called every frame)
    void Update(float deltaTime, const HeadPose& headPose);

    // HUD mode
    void SetMode(HUDMode mode);
    HUDMode GetMode() const { return m_settings.mode; }

    // Settings
    void SetSettings(const HUDSettings& settings);
    HUDSettings GetSettings() const { return m_settings; }

    // Individual setting accessors
    void SetDistance(float distance);
    void SetScale(float scale);
    void SetOpacity(float opacity);
    void SetOffset(float x, float y);
    void SetCurvature(float curvature);

    // Element visibility
    void SetElementVisible(HUDElementType type, bool visible);
    bool IsElementVisible(HUDElementType type) const;
    void SetMinimalMode(bool minimal);

    // HUD capture (from game)
    void CaptureHUD(ID3D11DeviceContext* context, ID3D11Texture2D* gameHUD);
    HUDCapture* GetHUDCapture() { return &m_capture; }

    // Get transform for rendering
    void GetHUDTransform(Vector3& position, Quaternion& orientation,
                         float& width, float& height) const;

    // Get vertices for curved HUD mesh
    void GetHUDMeshVertices(std::vector<float>& vertices,
                            std::vector<uint32_t>& indices) const;

    // Render helpers
    ID3D11ShaderResourceView* GetHUDTexture() const { return m_capture.srv; }
    float GetOpacity() const { return m_settings.opacity; }
    float GetCurvature() const { return m_settings.curvature; }

    // World position (for world-locked mode)
    void SetWorldPosition(const Vector3& position, const Quaternion& orientation);
    void AttachToHead(const HeadPose& pose);

    // Subtitle handling
    void SetSubtitleText(const std::string& text);
    std::string GetSubtitleText() const { return m_subtitleText; }
    bool HasActiveSubtitle() const { return !m_subtitleText.empty(); }

private:
    void UpdateFloatingMode(float deltaTime, const HeadPose& headPose);
    void UpdateHelmetMode(float deltaTime, const HeadPose& headPose);
    void UpdateWorldLockedMode(float deltaTime);

    void CreateCaptureTexture(uint32_t width, uint32_t height);
    void GenerateCurvedMesh();

private:
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Settings
    HUDSettings m_settings;

    // Captured HUD texture
    HUDCapture m_capture;
    ID3D11Texture2D* m_stagingTexture = nullptr;

    // Current HUD transform
    Vector3 m_hudPosition;
    Quaternion m_hudOrientation;

    // World-locked position
    Vector3 m_worldPosition;
    Quaternion m_worldOrientation;

    // Helmet mode smooth follow
    Vector3 m_targetPosition;
    Quaternion m_targetOrientation;

    // HUD element tracking
    std::vector<HUDElement> m_elements;

    // Subtitle text
    std::string m_subtitleText;
    float m_subtitleTimer = 0.0f;

    // Curved mesh data
    std::vector<float> m_meshVertices;
    std::vector<uint32_t> m_meshIndices;
    bool m_meshDirty = true;

    // State
    bool m_initialized = false;
    HeadPose m_lastHeadPose;
};

} // namespace GTA5VR
