#include "OpenVROverlaySurface.hpp"
#include "../Log.hpp"
#include "../VR/VRManager.hpp"

#include <d3d11.h>
#include <openvr.h>

namespace OVRInject {

OpenVROverlaySurface::OpenVROverlaySurface() {}

OpenVROverlaySurface::~OpenVROverlaySurface() {
    Shutdown();
}

bool OpenVROverlaySurface::Init(vr::IVRSystem* session) {
    session_ = session;
    if (!session_) return false;
    overlay_ = vr::VROverlay();
    if (!overlay_) {
        LOGSTR("OpenVROverlaySurface: Could not get IVROverlay interface.\n");
        return false;
    }

    vr::EVROverlayError error = overlay_->CreateOverlay("gta_v_vr_overlay", "GTA V VR", &overlay_handle_);
    if (error != vr::VROverlayError_None) {
        LOGSTRF("OpenVROverlaySurface: Could not create overlay. Error: %d\n", error);
        return false;
    }

    overlay_->SetOverlayWidthInMeters(overlay_handle_, width_meters_);
    overlay_->SetOverlayInputMethod(overlay_handle_, vr::VROverlayInputMethod_Mouse);
    overlay_->SetOverlayAlpha(overlay_handle_, opacity_);

    // Create the texture for the overlay
    device_ = VR::VRManager::Get().GetBackend()->GetDevice();
    if (!device_) {
        LOGSTR("OpenVROverlaySurface: Missing D3D11 device.\n");
        return false;
    }
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width_;
    desc.Height = height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;

    if (FAILED(device_->CreateTexture2D(&desc, NULL, &texture_))) {
        LOGSTR("OpenVROverlaySurface: Could not create overlay texture.\n");
        return false;
    }

    if (FAILED(device_->CreateRenderTargetView(texture_, NULL, &render_target_view_))) {
        LOGSTR("OpenVROverlaySurface: Could not create render target view.\n");
        return false;
    }

    ApplySettings(distance_meters_, 1.0f, opacity_);
    overlay_->ShowOverlay(overlay_handle_);
    visible_ = true;
    initialized_ = true;
    return true;
}

bool OpenVROverlaySurface::IsInitialized() const {
    return initialized_;
}

vr::HmdVector3_t OpenVROverlaySurface::GetPosition() {
    return {transform_.m[0][3], transform_.m[1][3], transform_.m[2][3]};
}

vr::HmdMatrix34_t OpenVROverlaySurface::GetTransform() {
    return transform_;
}

void OpenVROverlaySurface::Shutdown() {
    initialized_ = false;
    if (overlay_) {
        overlay_->DestroyOverlay(overlay_handle_);
    }
    if (render_target_view_) {
        render_target_view_->Release();
        render_target_view_ = nullptr;
    }
    if (texture_) {
        texture_->Release();
        texture_ = nullptr;
    }
    device_ = nullptr;
    overlay_ = nullptr;
    session_ = nullptr;
}

ID3D11RenderTargetView* OpenVROverlaySurface::BeginRender() {
    if (!render_target_view_) {
        return nullptr;
    }

    ID3D11DeviceContext* context;
    if (!device_) {
        return nullptr;
    }
    device_->GetImmediateContext(&context);

    // Clear the render target
    float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // Transparent
    context->ClearRenderTargetView(render_target_view_, clear_color);

    // Set the render target
    context->OMSetRenderTargets(1, &render_target_view_, NULL);

    // Set the viewport
    D3D11_VIEWPORT viewport;
    viewport.Width = (float)width_;
    viewport.Height = (float)height_;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    context->RSSetViewports(1, &viewport);

    context->Release();
    return render_target_view_;
}

void OpenVROverlaySurface::EndRender() {
    if (overlay_ && texture_) {
        vr::Texture_t vr_texture = { texture_, vr::TextureType_DirectX, vr::ColorSpace_Auto };
        vr::EVROverlayError error = overlay_->SetOverlayTexture(overlay_handle_, &vr_texture);
        if (error != vr::VROverlayError_None) {
            LOGSTRF("OpenVROverlaySurface: Could not set overlay texture. Error: %d\n", error);
        }
    }
}

uint32_t OpenVROverlaySurface::GetTextureWidth() const {
    return width_;
}

uint32_t OpenVROverlaySurface::GetTextureHeight() const {
    return height_;
}

void OpenVROverlaySurface::SetVisible(bool visible) {
    if (!overlay_) {
        visible_ = visible;
        return;
    }
    if (visible) {
        overlay_->ShowOverlay(overlay_handle_);
    } else {
        overlay_->HideOverlay(overlay_handle_);
    }
    visible_ = visible;
}

bool OpenVROverlaySurface::IsVisible() const {
    return visible_;
}

void OpenVROverlaySurface::ApplySettings(float distanceMeters, float scale, float opacity) {
    if (!overlay_) {
        return;
    }

    if (scale < 0.1f) scale = 0.1f;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    width_meters_ = 1.2f * scale;
    distance_meters_ = distanceMeters;
    opacity_ = opacity;

    overlay_->SetOverlayWidthInMeters(overlay_handle_, width_meters_);
    overlay_->SetOverlayAlpha(overlay_handle_, opacity_);

    transform_ = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, vertical_offset_,
        0.0f, 0.0f, 1.0f, -distance_meters_
    };
    overlay_->SetOverlayTransformTrackedDeviceRelative(overlay_handle_, vr::k_unTrackedDeviceIndex_Hmd, &transform_);
}

} // namespace OVRInject
