#pragma once

#include "OverlaySurface.hpp"
#include <d3d11.h>
#include <openvr.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;

namespace OVRInject {

class OpenVROverlaySurface : public IOverlaySurface {
public:
    OpenVROverlaySurface();
    ~OpenVROverlaySurface();

    bool Init(vr::IVRSystem* session);
    void Shutdown();

    bool IsInitialized() const override;
    ID3D11RenderTargetView* BeginRender() override;
    void EndRender() override;

    uint32_t GetTextureWidth() const override;
    uint32_t GetTextureHeight() const override;

    void SetVisible(bool visible);
    bool IsVisible() const;

    void ApplySettings(float distanceMeters, float scale, float opacity);

    vr::HmdVector3_t GetPosition();
    vr::HmdMatrix34_t GetTransform();

private:
    vr::IVRSystem* session_ = nullptr;
    vr::IVROverlay* overlay_ = nullptr;
    vr::VROverlayHandle_t overlay_handle_ = vr::k_ulOverlayHandleInvalid;

    ID3D11Device* device_ = nullptr;
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11RenderTargetView* render_target_view_ = nullptr;

    uint32_t width_ = 1536;
    uint32_t height_ = 1024;
    float width_meters_ = 1.2f;
    float distance_meters_ = 1.1f;
    float vertical_offset_ = -0.5f;
    float opacity_ = 1.0f;
    bool initialized_ = false;
    bool visible_ = true;
    vr::HmdMatrix34_t transform_;
};

} // namespace OVRInject
