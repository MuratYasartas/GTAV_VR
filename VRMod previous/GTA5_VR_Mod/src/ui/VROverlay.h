#pragma once

#include <d3d11.h>
#include <string>

namespace GTA5VR {

class IRuntimeInterface;

class VROverlay {
public:
    VROverlay();
    ~VROverlay();

    bool Initialize(IRuntimeInterface* runtime, ID3D11Device* device);
    void Shutdown();
    void Render();

    // Overlay windows
    void ShowSettingsMenu();
    void HideSettingsMenu();
    bool IsSettingsMenuVisible() const { return m_settingsVisible; }

    void ShowDebugOverlay();
    void HideDebugOverlay();
    void ShowPerformanceStats();

    // HUD projection
    void ProjectGameHUD(ID3D11Texture2D* hudTexture);
    void SetHUDDistance(float distance);
    void SetHUDScale(float scale);
    void SetHUDCurvature(float curvature);
    void SetHUDOpacity(float opacity);

    // HUD position modes
    enum class HUDMode { Floating, HeadLocked, Wrist, Hidden };
    void SetHUDMode(HUDMode mode);

private:
    void InitImGui();
    void ShutdownImGui();
    void RenderImGuiFrame();
    void RenderSettingsMenu();
    void RenderDebugOverlay();
    void RenderHUD();

    bool CreateHUDResources();
    void DestroyHUDResources();

    IRuntimeInterface* m_runtime = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // HUD rendering
    ID3D11Buffer* m_hudQuadVB = nullptr;
    ID3D11VertexShader* m_hudVS = nullptr;
    ID3D11PixelShader* m_hudPS = nullptr;
    ID3D11InputLayout* m_hudInputLayout = nullptr;
    ID3D11SamplerState* m_hudSampler = nullptr;
    ID3D11Buffer* m_hudConstantBuffer = nullptr;

    // Settings
    float m_hudDistance = 2.0f;
    float m_hudScale = 1.0f;
    float m_hudCurvature = 0.0f;
    float m_hudOpacity = 1.0f;
    HUDMode m_hudMode = HUDMode::Floating;

    bool m_settingsVisible = false;
    bool m_debugVisible = false;
    bool m_initialized = false;
};

} // namespace GTA5VR
