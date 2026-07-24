#pragma once

#include "../VR/IVRBackend.hpp"
#include "../VR/SharedSettings.hpp"
#include <d3d11.h>
#include <DirectXMath.h>
#include <memory>

namespace OVRInject {

/**
 * VirtualScreen - Renders game content on a floating 2D screen in VR
 *
 * Used for cutscenes: instead of injecting VR rotation into the game camera,
 * the cutscene is displayed on a large floating screen in front of the player.
 * The player can look around freely in VR space while watching the cutscene.
 */
class VirtualScreen {
public:
    VirtualScreen();
    ~VirtualScreen();

    // Initialize with D3D device
    bool Initialize(ID3D11Device* device, VR::IVRBackend* backend);

    // Shutdown and release resources
    void Shutdown();

    // Check if initialized
    bool IsInitialized() const { return initialized_; }

    // Update screen position based on head pose (call each frame when active)
    void Update();

    // Render the game texture to the virtual screen
    // sourceTexture: the game's rendered frame
    // Returns the texture to submit to VR (with screen rendered in 3D space)
    void Render(ID3D11Texture2D* sourceTexture);

    // Get the rendered output for each eye
    ID3D11Texture2D* GetEyeTexture(VR::Eye eye) const;

    // Enable/disable the virtual screen
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    // Lock/unlock screen position to head
    // When locked, screen follows head movement
    // When unlocked, screen stays in world space
    void SetLockedToHead(bool locked) { locked_to_head_ = locked; }
    bool IsLockedToHead() const { return locked_to_head_; }

    // Recenter the screen in front of the player
    void Recenter();

    // Settings
    void SetDistance(float distance) { distance_ = distance; }
    void SetScale(float scale) { scale_ = scale; }
    void SetCurvature(float curvature) { curvature_ = curvature; }

private:
    bool CreateResources();
    void ReleaseResources();
    bool CreateShaders();
    bool CreateGeometry();
    bool CreateRenderTargets();

    // Render helpers
    void RenderToEye(VR::Eye eye, ID3D11Texture2D* sourceTexture);
    DirectX::XMMATRIX CalculateScreenTransform() const;

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    VR::IVRBackend* backend_ = nullptr;

    // Shaders
    ID3D11VertexShader* vertex_shader_ = nullptr;
    ID3D11PixelShader* pixel_shader_ = nullptr;
    ID3D11InputLayout* input_layout_ = nullptr;

    // Geometry
    ID3D11Buffer* vertex_buffer_ = nullptr;
    ID3D11Buffer* index_buffer_ = nullptr;
    ID3D11Buffer* constant_buffer_ = nullptr;
    uint32_t index_count_ = 0;

    // Render targets (one per eye)
    ID3D11Texture2D* eye_textures_[2] = { nullptr, nullptr };
    ID3D11RenderTargetView* eye_rtvs_[2] = { nullptr, nullptr };
    ID3D11ShaderResourceView* source_srv_ = nullptr;
    ID3D11Texture2D* source_copy_ = nullptr;
    ID3D11ShaderResourceView* source_copy_srv_ = nullptr;
    DXGI_FORMAT source_copy_format_ = DXGI_FORMAT_UNKNOWN;
    uint32_t source_copy_width_ = 0;
    uint32_t source_copy_height_ = 0;
    ID3D11SamplerState* sampler_ = nullptr;

    // State
    bool initialized_ = false;
    bool enabled_ = false;
    bool locked_to_head_ = false;
    bool needs_initial_position_ = true;  // True until first positioned

    // Screen transform
    DirectX::XMMATRIX screen_world_matrix_ = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT3 screen_position_ = { 0, 0, -4.0f };  // Default 4m in front

    // Settings
    float distance_ = 4.0f;   // Distance from player in meters
    float scale_ = 3.0f;      // Screen size multiplier
    float curvature_ = 0.0f;  // 0 = flat, 1 = fully curved

    // Constant buffer structure
    struct ScreenConstants {
        DirectX::XMMATRIX worldViewProj;
        float opacity;
        float padding[3];
    };
};

} // namespace OVRInject
