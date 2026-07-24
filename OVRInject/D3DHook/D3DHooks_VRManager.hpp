#pragma once
/**
 * D3DHooks_VRManager.hpp
 *
 * Updated D3D hooks using the unified VRManager system.
 * This replaces the direct HMDSupport usage with the abstracted VR backend.
 *
 * To use:
 * 1. Replace #include "Vive/HMDSupport.hpp" with this file
 * 2. Or define USE_VR_MANAGER before including D3DHooks.hpp
 */

#include "targetver.h"

#include <dxgi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>

#include "MinHook.h"
#include "../VR/VRManager.hpp"
#include "../VR/IVRBackend.hpp"
#include "../VR/SharedSettings.hpp"
#include "../Log.hpp"
#include "../Vive/HMDRenderer.hpp"
#include "../Game/GtaCameraHook.hpp"
#include "../Game/GtaCameraFov.hpp"
#include "../Game/GtaGameState.hpp"
#include "../Overlay/OpenVROverlaySurface.hpp"
#include "../Overlay/VirtualScreen.hpp"
#include "../OpenXR/XROverlayUI.hpp"
#include <vector>

// Assume these are compiled shaders
#include "../Shaders/StereoShader.h"
#include "../Shaders/VertexShader.h"


extern HMODULE dxgiModule;

namespace OVRInject {
namespace VRMgr {

    // VR Manager instance
    VR::VRManager* vrManager = nullptr;
	HMDRenderer* hmdRenderer = nullptr;
	Game::GtaCameraHook* cameraHook = nullptr;
    std::unique_ptr<Game::GtaCameraFov> cameraFov;
    std::unique_ptr<Game::GtaGameState> gameState;
    std::unique_ptr<VirtualScreen> virtualScreen;
    std::unique_ptr<OpenVROverlaySurface> overlay;
    std::unique_ptr<XR::XROverlayUI> overlay_ui;

    // D3D11 objects
    ID3D11PixelShader* stereo_pixel_shader_ = nullptr;
    ID3D11VertexShader* stereo_vertex_shader_ = nullptr;
    ID3D11Buffer* stereo_constant_buffer_ = nullptr;
    ID3D11SamplerState* sampler_state_ = nullptr;
    ID3D11Buffer* vertex_buffer_ = nullptr;
    ID3D11InputLayout* input_layout_ = nullptr;
    ID3D11PixelShader* reprojection_pixel_shader_ = nullptr;
    ID3D11Buffer* reprojection_constant_buffer_ = nullptr;
    ID3D11PixelShader* blit_pixel_shader_ = nullptr;
    ID3D11Buffer* blit_constant_buffer_ = nullptr;
    ID3D11Buffer* vignette_constant_buffer_ = nullptr;
    ID3D11BlendState* blit_blend_state_ = nullptr;
    ID3D11DepthStencilState* blit_depth_state_ = nullptr;
    ID3D11RasterizerState* blit_raster_state_ = nullptr;
    ID3D11Texture2D* blit_source_copy_ = nullptr;
    ID3D11ShaderResourceView* blit_source_srv_ = nullptr;
    DXGI_FORMAT blit_source_format_ = DXGI_FORMAT_UNKNOWN;
    uint32_t blit_source_width_ = 0;
    uint32_t blit_source_height_ = 0;
    ID3D11ShaderResourceView* depth_srv_ = nullptr;
    ID3D11Texture2D* depth_copy_ = nullptr;
    DXGI_FORMAT depth_format_ = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT depth_srv_format_ = DXGI_FORMAT_UNKNOWN;
    uint32_t depth_width_ = 0;
    uint32_t depth_height_ = 0;


    // State tracking
    bool first_present = true;
    bool using_openvr = false;
    bool overlay_visible = false;
    VR::Eye current_eye = VR::Eye::Left;
    VR::Eye next_eye = VR::Eye::Right;
    int current_stereo_mode = static_cast<int>(VR::StereoMode::Reprojection);
    uint32_t base_swap_width = 0;
    uint32_t base_swap_height = 0;
    float current_game_scale = 1.0f;
    DWORD last_swap_resize_ms = 0;
    bool present_hook_installed = false;
    bool create_hook_disabled = false;
    void* create_device_and_swapchain_target = nullptr;

    struct StereoParams {
        float u_ipd_offset;
        float padding[3];
    };
    struct ReprojectionParams {
        float u_ipd_offset;
        float u_depth_scale;
        float u_depth_bias;
        float u_image_scale;
        float u_offset_x;
        float u_offset_y;
        float u_invert_depth;
        float padding;
    };

    struct BlitParams {
        float u_image_scale;
        float u_offset_x;
        float u_offset_y;
        float padding;
    };

    struct VignetteParams {
        float u_intensity;
        float u_enabled;
        float padding[2];
    };

    struct Vertex {
        float pos[3];
        float tex[2];
    };

    // Hook function pointers
    typedef HRESULT(__stdcall *PresentHook)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    PresentHook Original_PresentHook = nullptr;

    /**
     * Get the VR backend (initializes if needed)
     */
    VR::IVRBackend* GetBackend() {
        if (!vrManager) {
            vrManager = &VR::VRManager::Get();
        }
        if (vrManager->IsInitialized()) {
            return vrManager->GetBackend();
        }
        return nullptr;
    }

    static bool ReadEnvFlag(const char* name, bool defaultValue) {
        char value[8] = {};
        DWORD len = GetEnvironmentVariableA(name, value, sizeof(value));
        if (len == 0 || len >= sizeof(value)) {
            return defaultValue;
        }
        char c = static_cast<char>(tolower(value[0]));
        return c == '1' || c == 'y' || c == 't';
    }

    static void ApplyOverlaySettingsOpenVR(const XR::VRSettings& settings) {
        if (overlay) {
            overlay->ApplySettings(settings.overlayDistance, settings.overlayScale, settings.overlayOpacity);
        }

        auto& repro = VR::GetReprojectionSettings();
        repro.enabled.store(settings.reprojectionEnabled);
        repro.ipd.store(settings.reprojectionIPD);
        repro.depthScale.store(settings.reprojectionDepthScale);
        repro.depthBias.store(settings.reprojectionDepthBias);
        repro.invertDepth.store(settings.reprojectionInvertDepth);
        repro.renderScale.store(settings.renderScale);
        repro.screenOffsetX.store(settings.imageOffsetX);
        repro.screenOffsetY.store(settings.imageOffsetY);
        repro.imageScale.store(settings.imageScale);

        auto& comfort = VR::GetComfortSettings();
        comfort.snapTurning.store(settings.snapTurning);
        comfort.snapTurnAngle.store(settings.snapTurnAngle);
        comfort.vignetteEnabled.store(settings.vignetteEnabled);
        comfort.vignetteIntensity.store(settings.vignetteIntensity);

        auto& stereo = VR::GetStereoSettings();
        stereo.mode.store(settings.stereoMode);
        stereo.stereoIPD.store(settings.stereoIPD);
        bool cameraReady = VR::GetRuntimeStats().cameraHookReady.load();
        bool allowHeadLookViewLock = ReadEnvFlag("GTAVR_HEADLOOK_VIEWLOCK", false);
        bool headLookActive = settings.headLookEnabled && !cameraReady && allowHeadLookViewLock;
        stereo.headTracking.store(headLookActive ? true : settings.headTracking);
        stereo.positionTracking.store(headLookActive ? false : settings.positionTracking);

        if (settings.headLookEnabled && !cameraReady && !allowHeadLookViewLock) {
            static bool loggedHeadLookFallback = false;
            if (!loggedHeadLookFallback) {
                LOGSTR("D3DHooks_VRManager: Head Look fallback disabled. Set GTAVR_HEADLOOK_VIEWLOCK=1 to force headset-locked fallback.\n");
                loggedHeadLookFallback = true;
            }
        }

        auto& camera = VR::GetCameraSettings();
        camera.worldScale.store(settings.worldScale);
        camera.playerHeight.store(settings.playerHeight);
        camera.cameraOffsetX.store(settings.cameraOffsetX);
        camera.cameraOffsetY.store(settings.cameraOffsetY);
        camera.cameraOffsetZ.store(settings.cameraOffsetZ);

        auto& input = VR::GetInputSettings();
        input.swapHands.store(settings.swapHands);
        input.triggerThreshold.store(settings.triggerThreshold);
        input.gripThreshold.store(settings.gripThreshold);

        auto& headLook = VR::GetHeadLookSettings();
        headLook.enabled.store(settings.headLookEnabled);
        headLook.maxAngleDeg.store(settings.headLookMaxAngle);
        headLook.deadzoneDeg.store(settings.headLookDeadzone);
        headLook.sensitivity.store(settings.headLookSensitivity);
        headLook.invertY.store(settings.headLookInvertY);

        auto& perf = VR::GetPerformanceSettings();
        perf.asyncReprojection.store(settings.asyncReprojection);
        perf.gameResolutionScale.store(settings.gameResolutionScale);

        auto& debug = VR::GetDebugSettings();
        debug.showDebugInfo.store(settings.showDebugInfo);
        debug.showControllerModels.store(settings.showControllerModels);

        auto& fov = VR::GetFovSettings();
        fov.enabled.store(settings.fovOverride);
        fov.perType.store(settings.fovPerType);
        fov.globalFov.store(settings.fovGlobal);
        fov.fpPedFov.store(settings.fovFpPed);
        fov.tpPedFov.store(settings.fovTpPed);
        fov.tpAimFov.store(settings.fovTpAim);
        fov.fpVehicleFov.store(settings.fovFpVehicle);
        fov.tpVehicleFov.store(settings.fovTpVehicle);
        fov.overrideOffset.store(settings.fovOffsetOverride);
        fov.manualOffset.store(settings.fovManualOffset);

        // Decoupling settings
        auto& decoupling = VR::GetDecouplingSettings();
        decoupling.enabled.store(settings.decouplingEnabled);
        decoupling.mode.store(settings.decouplingMode);
        decoupling.maxPitchDeg.store(settings.decouplingMaxPitch);
        decoupling.maxYawDeg.store(settings.decouplingMaxYaw);
        decoupling.aimConeDeg.store(settings.decouplingAimCone);

        // Cutscene settings
        auto& cutscene = VR::GetCutsceneSettings();
        cutscene.mode.store(settings.cutsceneMode);
        cutscene.screenDistance.store(settings.cutsceneScreenDistance);
        cutscene.screenScale.store(settings.cutsceneScreenScale);
        cutscene.screenCurve.store(settings.cutsceneScreenCurve);

        // Apply virtual screen settings if it's active
        if (virtualScreen) {
            virtualScreen->SetDistance(settings.cutsceneScreenDistance);
            virtualScreen->SetScale(settings.cutsceneScreenScale);
            virtualScreen->SetCurvature(settings.cutsceneScreenCurve);
        }
    }

    static void UpdateOverlayInputOpenVR(const VR::ControllerState& leftState,
                                         const VR::ControllerState& rightState) {
        if (!overlay_ui || !overlay_ui->IsInitialized()) {
            return;
        }

        XR::OverlayInputState leftInput = {};
        leftInput.thumbstickX = leftState.buttons.thumbstickX;
        leftInput.thumbstickY = leftState.buttons.thumbstickY;
        leftInput.thumbstickTouched = leftState.buttons.thumbstickTouched;
        leftInput.thumbstickPressed = leftState.buttons.thumbstickPressed;
        leftInput.thumbstickJustPressed = leftState.buttons.thumbstickJustPressed;
        leftInput.thumbstickJustReleased = leftState.buttons.thumbstickJustReleased;
        leftInput.triggerPressed = leftState.buttons.triggerPressed;
        leftInput.gripPressed = leftState.buttons.gripPressed;
        leftInput.menuPressed = leftState.buttons.menuPressed;
        leftInput.menuJustPressed = leftState.buttons.menuJustPressed;
        leftInput.primaryPressed = leftState.buttons.primaryPressed;
        leftInput.secondaryPressed = leftState.buttons.secondaryPressed;

        XR::OverlayInputState rightInput = {};
        rightInput.thumbstickX = rightState.buttons.thumbstickX;
        rightInput.thumbstickY = rightState.buttons.thumbstickY;
        rightInput.thumbstickTouched = rightState.buttons.thumbstickTouched;
        rightInput.thumbstickPressed = rightState.buttons.thumbstickPressed;
        rightInput.thumbstickJustPressed = rightState.buttons.thumbstickJustPressed;
        rightInput.thumbstickJustReleased = rightState.buttons.thumbstickJustReleased;
        rightInput.triggerPressed = rightState.buttons.triggerPressed;
        rightInput.gripPressed = rightState.buttons.gripPressed;
        rightInput.menuPressed = rightState.buttons.menuPressed;
        rightInput.menuJustPressed = rightState.buttons.menuJustPressed;
        rightInput.primaryPressed = rightState.buttons.primaryPressed;
        rightInput.secondaryPressed = rightState.buttons.secondaryPressed;

        if (leftState.buttons.menuJustPressed || rightState.buttons.menuJustPressed) {
            overlay_visible = !overlay_visible;
            overlay_ui->SetVisible(overlay_visible);
            if (overlay) {
                overlay->SetVisible(overlay_visible);
            }
        }

        if (overlay_visible) {
            overlay_ui->HandleInput(leftInput, rightInput);
        }
    }

    static void RenderOverlayOpenVR() {
        if (overlay_ui && overlay_ui->IsInitialized() && overlay_visible) {
            overlay_ui->Render();
        }
    }

    static const char* kDepthReprojectionShader = R"(
cbuffer ReprojectionParams : register(b0)
{
    float u_ipd_offset;
    float u_depth_scale;
    float u_depth_bias;
    float u_image_scale;
    float u_offset_x;
    float u_offset_y;
    float u_invert_depth;
    float u_padding;
};

cbuffer VignetteParams : register(b1)
{
    float u_vignette_intensity;
    float u_vignette_enabled;
    float2 u_vignette_padding;
};

Texture2D g_color : register(t0);
Texture2D g_depth : register(t1);
SamplerState g_sampler : register(s0);

float4 main(float4 pos : SV_POSITION, float2 tex : TEXCOORD) : SV_Target
{
    float2 uv = tex;
    float scale = max(u_image_scale, 0.01);
    uv = (uv - 0.5f) / scale + 0.5f;
    uv += float2(u_offset_x, u_offset_y);

    float depth = g_depth.Sample(g_sampler, uv).r;
    if (u_invert_depth > 0.5f) {
        depth = 1.0f - depth;
    }

    float depth_factor = saturate(1.0f - depth);
    float shift = u_ipd_offset * (u_depth_bias + depth_factor * u_depth_scale);
    uv.x += shift;

    float4 color = g_color.Sample(g_sampler, uv);
    if (u_vignette_enabled > 0.5f) {
        float2 centered = uv - 0.5f;
        float dist = length(centered);
        float vignette = smoothstep(0.4f, 0.7f, dist);
        float factor = 1.0f - saturate(u_vignette_intensity) * vignette;
        color.rgb *= factor;
    }

    return color;
}
)";

    static const char* kFullscreenVertexShader = R"(
struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

VSOut main(uint id : SV_VertexID)
{
    float2 pos[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };
    float2 uv[3] = {
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f, 1.0f)
    };

    VSOut o;
    o.pos = float4(pos[id], 0.0f, 1.0f);
    o.uv = uv[id];
    return o;
}
)";

    static const char* kBlitShader = R"(
cbuffer BlitParams : register(b0)
{
    float u_image_scale;
    float u_offset_x;
    float u_offset_y;
    float u_padding;
};

cbuffer VignetteParams : register(b1)
{
    float u_vignette_intensity;
    float u_vignette_enabled;
    float2 u_vignette_padding;
};

Texture2D g_color : register(t0);
SamplerState g_sampler : register(s0);

float4 main(float4 pos : SV_POSITION, float2 tex : TEXCOORD) : SV_Target
{
    float2 uv = tex;
    float scale = max(u_image_scale, 0.01);
    uv = (uv - 0.5f) / scale + 0.5f;
    uv += float2(u_offset_x, u_offset_y);

    float4 color = g_color.Sample(g_sampler, uv);
    if (u_vignette_enabled > 0.5f) {
        float2 centered = uv - 0.5f;
        float dist = length(centered);
        float vignette = smoothstep(0.4f, 0.7f, dist);
        float factor = 1.0f - saturate(u_vignette_intensity) * vignette;
        color.rgb *= factor;
    }
    return color;
}
)";

    static bool EnsureReprojectionShader(ID3D11Device* device) {
        if (reprojection_pixel_shader_) {
            return true;
        }

        ID3DBlob* shaderBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
        HRESULT hr = D3DCompile(kDepthReprojectionShader,
                                strlen(kDepthReprojectionShader),
                                nullptr,
                                nullptr,
                                nullptr,
                                "main",
                                "ps_5_0",
                                flags,
                                0,
                                &shaderBlob,
                                &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                LOGSTRF("D3DHooks_VRManager: Depth reprojection shader compile failed: %s\n",
                        static_cast<const char*>(errorBlob->GetBufferPointer()));
                errorBlob->Release();
            } else {
                LOGSTR("D3DHooks_VRManager: Depth reprojection shader compile failed.\n");
            }
            if (shaderBlob) shaderBlob->Release();
            return false;
        }

        hr = device->CreatePixelShader(shaderBlob->GetBufferPointer(),
                                       shaderBlob->GetBufferSize(),
                                       nullptr,
                                       &reprojection_pixel_shader_);
        shaderBlob->Release();
        if (FAILED(hr)) {
            LOGSTR("D3DHooks_VRManager: Failed to create reprojection pixel shader.\n");
            return false;
        }
        return true;
    }

    static bool EnsureFullscreenVertexShader(ID3D11Device* device) {
        if (stereo_vertex_shader_) {
            return true;
        }

        ID3DBlob* shaderBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
        HRESULT hr = D3DCompile(kFullscreenVertexShader,
                                strlen(kFullscreenVertexShader),
                                nullptr,
                                nullptr,
                                nullptr,
                                "main",
                                "vs_5_0",
                                flags,
                                0,
                                &shaderBlob,
                                &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                LOGSTRF("D3DHooks_VRManager: Fullscreen vertex shader compile failed: %s\n",
                        static_cast<const char*>(errorBlob->GetBufferPointer()));
                errorBlob->Release();
            } else {
                LOGSTR("D3DHooks_VRManager: Fullscreen vertex shader compile failed.\n");
            }
            if (shaderBlob) shaderBlob->Release();
            return false;
        }

        hr = device->CreateVertexShader(shaderBlob->GetBufferPointer(),
                                        shaderBlob->GetBufferSize(),
                                        nullptr,
                                        &stereo_vertex_shader_);
        shaderBlob->Release();
        if (FAILED(hr)) {
            LOGSTR("D3DHooks_VRManager: Failed to create fullscreen vertex shader.\n");
            return false;
        }
        return true;
    }

    static bool EnsureBlitShader(ID3D11Device* device) {
        if (blit_pixel_shader_) {
            return true;
        }

        ID3DBlob* shaderBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
        HRESULT hr = D3DCompile(kBlitShader,
                                strlen(kBlitShader),
                                nullptr,
                                nullptr,
                                nullptr,
                                "main",
                                "ps_5_0",
                                flags,
                                0,
                                &shaderBlob,
                                &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                LOGSTRF("D3DHooks_VRManager: Blit shader compile failed: %s\n",
                        static_cast<const char*>(errorBlob->GetBufferPointer()));
                errorBlob->Release();
            } else {
                LOGSTR("D3DHooks_VRManager: Blit shader compile failed.\n");
            }
            if (shaderBlob) shaderBlob->Release();
            return false;
        }

        hr = device->CreatePixelShader(shaderBlob->GetBufferPointer(),
                                       shaderBlob->GetBufferSize(),
                                       nullptr,
                                       &blit_pixel_shader_);
        shaderBlob->Release();
        if (FAILED(hr)) {
            LOGSTR("D3DHooks_VRManager: Failed to create blit pixel shader.\n");
            return false;
        }
        return true;
    }

    static void ReleaseDepthResources() {
        if (depth_srv_) { depth_srv_->Release(); depth_srv_ = nullptr; }
        if (depth_copy_) { depth_copy_->Release(); depth_copy_ = nullptr; }
        depth_format_ = DXGI_FORMAT_UNKNOWN;
        depth_srv_format_ = DXGI_FORMAT_UNKNOWN;
        depth_width_ = 0;
        depth_height_ = 0;
    }

    static bool GetDepthFormats(DXGI_FORMAT format, DXGI_FORMAT& typeless, DXGI_FORMAT& srv) {
        switch (format) {
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_TYPELESS:
            typeless = DXGI_FORMAT_R32_TYPELESS;
            srv = DXGI_FORMAT_R32_FLOAT;
            return true;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
            typeless = DXGI_FORMAT_R24G8_TYPELESS;
            srv = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            return true;
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_TYPELESS:
            typeless = DXGI_FORMAT_R16_TYPELESS;
            srv = DXGI_FORMAT_R16_UNORM;
            return true;
        default:
            return false;
        }
    }

    static bool EnsureDepthResources(ID3D11Device* device, ID3D11DepthStencilView* dsv) {
        if (!dsv) {
            return false;
        }

        ID3D11Resource* resource = nullptr;
        dsv->GetResource(&resource);
        if (!resource) {
            return false;
        }

        ID3D11Texture2D* depthTexture = nullptr;
        HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&depthTexture));
        resource->Release();
        if (FAILED(hr) || !depthTexture) {
            return false;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        depthTexture->GetDesc(&desc);

        if (desc.SampleDesc.Count > 1) {
            depthTexture->Release();
            return false;
        }

        DXGI_FORMAT typeless = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
        if (!GetDepthFormats(desc.Format, typeless, srvFormat)) {
            depthTexture->Release();
            return false;
        }

        if (!depth_copy_ || depth_width_ != desc.Width || depth_height_ != desc.Height || depth_format_ != desc.Format) {
            ReleaseDepthResources();

            D3D11_TEXTURE2D_DESC copyDesc = desc;
            copyDesc.Format = typeless;
            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            copyDesc.CPUAccessFlags = 0;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            copyDesc.MiscFlags = 0;

            if (FAILED(device->CreateTexture2D(&copyDesc, nullptr, &depth_copy_))) {
                depthTexture->Release();
                ReleaseDepthResources();
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = srvFormat;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;

            if (FAILED(device->CreateShaderResourceView(depth_copy_, &srvDesc, &depth_srv_))) {
                depthTexture->Release();
                ReleaseDepthResources();
                return false;
            }

            depth_width_ = desc.Width;
            depth_height_ = desc.Height;
            depth_format_ = desc.Format;
            depth_srv_format_ = srvFormat;
        }

        if (depth_copy_) {
            ID3D11DeviceContext* context = nullptr;
            device->GetImmediateContext(&context);
            context->CopyResource(depth_copy_, depthTexture);
            context->Release();
        }

        depthTexture->Release();
        return depth_srv_ != nullptr;
    }

    static void UpdateVignetteParams(ID3D11DeviceContext* context) {
        if (!vignette_constant_buffer_ || !context) {
            return;
        }

        auto& comfort = VR::GetComfortSettings();
        VignetteParams params = {};
        params.u_intensity = comfort.vignetteIntensity.load();
        params.u_enabled = comfort.vignetteEnabled.load() ? 1.0f : 0.0f;
        context->UpdateSubresource(vignette_constant_buffer_, 0, nullptr, &params, 0, 0);
    }

    static float ClampImageOffset(float offset, float scale, const char* axis) {
        float safeScale = (std::max)(scale, 0.05f);
        float limit = 0.5f + 0.5f / safeScale;
        float clamped = (std::max)(-limit, (std::min)(offset, limit));
        if (std::fabs(clamped - offset) > 0.0001f) {
            static bool logged = false;
            if (!logged) {
                LOGSTRF("D3DHooks_VRManager: Clamping image offset %s from %.3f to %.3f (scale=%.3f)\n",
                        axis, offset, clamped, safeScale);
                logged = true;
            }
        }
        return clamped;
    }

    static void GetImageTransform(float eyeSign,
                                  float& outScale,
                                  float& outOffsetX,
                                  float& outOffsetY,
                                  bool useUserAlignment = true) {
        if (!useUserAlignment) {
            outScale = 1.0f;
            outOffsetX = 0.0f;
            outOffsetY = 0.0f;
            return;
        }

        auto& repro = VR::GetReprojectionSettings();
        outScale = (std::max)(0.05f, repro.imageScale.load());
        constexpr float kOffsetScale = 0.01f;
        outOffsetX = repro.screenOffsetX.load() * kOffsetScale * eyeSign;
        outOffsetY = repro.screenOffsetY.load() * kOffsetScale * eyeSign;
        outOffsetX = ClampImageOffset(outOffsetX, outScale, "X");
        outOffsetY = ClampImageOffset(outOffsetY, outScale, "Y");
    }

    static void UpdateBlitParams(ID3D11DeviceContext* context,
                                 float eyeSign,
                                 bool useUserAlignment = true) {
        if (!blit_constant_buffer_ || !context) {
            return;
        }

        BlitParams params = {};
        float scale = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        GetImageTransform(eyeSign, scale, offsetX, offsetY, useUserAlignment);
        params.u_image_scale = scale;
        params.u_offset_x = offsetX;
        params.u_offset_y = offsetY;
        context->UpdateSubresource(blit_constant_buffer_, 0, nullptr, &params, 0, 0);
    }

    static void ApplyBlitStates(ID3D11DeviceContext* context) {
        if (!context) {
            return;
        }
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->CSSetShader(nullptr, nullptr, 0);
        context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        if (blit_blend_state_) {
            float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            context->OMSetBlendState(blit_blend_state_, blendFactor, 0xffffffff);
        }
        if (blit_depth_state_) {
            context->OMSetDepthStencilState(blit_depth_state_, 0);
        }
        if (blit_raster_state_) {
            context->RSSetState(blit_raster_state_);
        }
    }

    static DXGI_FORMAT ResolveColorSrvFormat(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default:
            return format;
        }
    }

    static ID3D11ShaderResourceView* GetColorSRV(ID3D11Device* device,
                                                 ID3D11DeviceContext* context,
                                                 ID3D11Texture2D* source,
                                                 bool& using_cached_srv) {
        using_cached_srv = false;

        if (!device || !context || !source) {
            return nullptr;
        }

        D3D11_TEXTURE2D_DESC srcDesc = {};
        source->GetDesc(&srcDesc);

        bool needsResolve = srcDesc.SampleDesc.Count > 1;

        static bool logged_intermediate = false;
        if (!logged_intermediate) {
            if (needsResolve) {
                LOGSTR("D3DHooks_VRManager: MSAA backbuffer detected, resolving for blit\n");
            } else {
                LOGSTR("D3DHooks_VRManager: Using intermediate copy for blit\n");
            }
            logged_intermediate = true;
        }

        if (blit_source_copy_ &&
            (blit_source_width_ != srcDesc.Width ||
             blit_source_height_ != srcDesc.Height ||
             blit_source_format_ != srcDesc.Format)) {
            if (blit_source_srv_) {
                blit_source_srv_->Release();
                blit_source_srv_ = nullptr;
            }
            blit_source_copy_->Release();
            blit_source_copy_ = nullptr;
            blit_source_width_ = 0;
            blit_source_height_ = 0;
            blit_source_format_ = DXGI_FORMAT_UNKNOWN;
        }

        if (!blit_source_copy_) {
            D3D11_TEXTURE2D_DESC copyDesc = srcDesc;
            copyDesc.SampleDesc.Count = 1;
            copyDesc.SampleDesc.Quality = 0;
            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            copyDesc.MiscFlags = 0;
            copyDesc.CPUAccessFlags = 0;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            if (SUCCEEDED(device->CreateTexture2D(&copyDesc, nullptr, &blit_source_copy_)) &&
                blit_source_copy_) {
                blit_source_width_ = srcDesc.Width;
                blit_source_height_ = srcDesc.Height;
                blit_source_format_ = srcDesc.Format;
            }
        }

        if (!blit_source_copy_) {
            return nullptr;
        }

        DXGI_FORMAT resolveFormat = ResolveColorSrvFormat(srcDesc.Format);
        if (needsResolve) {
            context->ResolveSubresource(blit_source_copy_, 0, source, 0, resolveFormat);
        } else {
            context->CopyResource(blit_source_copy_, source);
        }

        if (!blit_source_srv_) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = ResolveColorSrvFormat(srcDesc.Format);
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;
            if (FAILED(device->CreateShaderResourceView(blit_source_copy_, &srvDesc, &blit_source_srv_))) {
                return nullptr;
            }
        }

        using_cached_srv = true;
        return blit_source_srv_;
    }

    static void RenderBlit(ID3D11Device* device,
                           ID3D11DeviceContext* context,
                           ID3D11Texture2D* source,
                           ID3D11RenderTargetView* target,
                           float eyeSign,
                           bool useUserAlignment = true) {
        if (!device || !context || !source || !target || !blit_pixel_shader_) {
            return;
        }
        if (!stereo_vertex_shader_) {
            return;
        }

        D3D11_TEXTURE2D_DESC srcDesc = {};
        source->GetDesc(&srcDesc);

        bool using_cached_srv = false;
        ID3D11ShaderResourceView* color_srv = GetColorSRV(device, context, source, using_cached_srv);

        if (!color_srv) {
            static bool logged_failure = false;
            if (!logged_failure) {
                LOGSTR("D3DHooks_VRManager: Failed to create SRV for blit source\n");
                logged_failure = true;
            }

            ID3D11Resource* targetRes = nullptr;
            target->GetResource(&targetRes);
            if (targetRes) {
                ID3D11Texture2D* targetTex = nullptr;
                if (SUCCEEDED(targetRes->QueryInterface(__uuidof(ID3D11Texture2D),
                                                        reinterpret_cast<void**>(&targetTex))) &&
                    targetTex) {
                    D3D11_TEXTURE2D_DESC dstDesc = {};
                    targetTex->GetDesc(&dstDesc);
                    if (srcDesc.Width == dstDesc.Width && srcDesc.Height == dstDesc.Height) {
                        if (srcDesc.SampleDesc.Count > 1) {
                            context->ResolveSubresource(targetTex, 0, source, 0,
                                                        ResolveColorSrvFormat(srcDesc.Format));
                        } else {
                            context->CopyResource(targetTex, source);
                        }
                    } else {
                        D3D11_BOX box = {};
                        box.left = 0;
                        box.top = 0;
                        box.front = 0;
                        box.right = (std::min)(srcDesc.Width, dstDesc.Width);
                        box.bottom = (std::min)(srcDesc.Height, dstDesc.Height);
                        box.back = 1;
                        context->CopySubresourceRegion(targetTex, 0, 0, 0, 0, source, 0, &box);
                    }
                    targetTex->Release();
                }
                targetRes->Release();
            }
            return;
        }

        ID3D11Resource* targetRes = nullptr;
        target->GetResource(&targetRes);
        if (targetRes) {
            ID3D11Texture2D* targetTex = nullptr;
            if (SUCCEEDED(targetRes->QueryInterface(__uuidof(ID3D11Texture2D),
                                                    reinterpret_cast<void**>(&targetTex))) &&
                targetTex) {
                D3D11_TEXTURE2D_DESC dstDesc = {};
                targetTex->GetDesc(&dstDesc);
                D3D11_VIEWPORT viewport = {};
                viewport.Width = static_cast<float>(dstDesc.Width);
                viewport.Height = static_cast<float>(dstDesc.Height);
                viewport.MaxDepth = 1.0f;
                context->RSSetViewports(1, &viewport);
                D3D11_RECT rect = {0, 0, static_cast<LONG>(dstDesc.Width), static_cast<LONG>(dstDesc.Height)};
                context->RSSetScissorRects(1, &rect);
                targetTex->Release();
            }
            targetRes->Release();
        }

        context->OMSetRenderTargets(1, &target, nullptr);
        ApplyBlitStates(context);
        UpdateBlitParams(context, eyeSign, useUserAlignment);
        context->VSSetShader(stereo_vertex_shader_, nullptr, 0);
        context->PSSetShader(blit_pixel_shader_, nullptr, 0);
        if (blit_constant_buffer_) {
            context->PSSetConstantBuffers(0, 1, &blit_constant_buffer_);
        }
        context->PSSetConstantBuffers(1, 1, &vignette_constant_buffer_);
        context->PSSetShaderResources(0, 1, &color_srv);
        context->PSSetSamplers(0, 1, &sampler_state_);

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->Draw(3, 0);

        ID3D11ShaderResourceView* null_srv[] = { nullptr };
        context->PSSetShaderResources(0, 1, null_srv);
        if (!using_cached_srv) {
            color_srv->Release();
        }
    }

    static void ApplySnapTurning(const VR::ControllerState& rightState) {
        static bool snap_ready = true;

        auto& comfort = VR::GetComfortSettings();
        if (!comfort.snapTurning.load()) {
            snap_ready = true;
            return;
        }

        float axis = rightState.buttons.thumbstickX;
        const float threshold = 0.7f;
        const float release = 0.3f;

        if (!snap_ready) {
            if (std::fabs(axis) < release) {
                snap_ready = true;
            }
            return;
        }

        if (axis > threshold || axis < -threshold) {
            float angle = comfort.snapTurnAngle.load();
            if (axis < 0.0f) {
                angle = -angle;
            }
            auto& view = VR::GetViewSettings();
            float yaw = view.snapYawOffsetDeg.load();
            yaw += angle;
            if (std::fabs(yaw) > 360.0f) {
                yaw = std::fmod(yaw, 360.0f);
            }
            view.snapYawOffsetDeg.store(yaw);
            snap_ready = false;
        }
    }

    // Track if resize has failed to avoid spamming
    static bool resize_failed_ ;
    static float last_desired_scale_;

    static void MaybeResizeSwapchain(IDXGISwapChain* swapChain) {
        // DISABLED: Game resolution scaling via ResizeBuffers causes DXGI_ERROR_INVALID_CALL
        // because GTA V and VR runtime hold multiple references to the swap chain buffers.
        // This feature would require deeper integration with the game's rendering pipeline.
        // For now, users should adjust resolution in-game settings instead.
        //
        // The VR render scale (via HMDRenderer) still works for adjusting VR quality.

        if (!swapChain) {
            return;
        }

        // Just capture base dimensions for reference, don't resize
        if (base_swap_width == 0 || base_swap_height == 0) {
            DXGI_SWAP_CHAIN_DESC desc = {};
            if (SUCCEEDED(swapChain->GetDesc(&desc))) {
                base_swap_width = desc.BufferDesc.Width;
                base_swap_height = desc.BufferDesc.Height;
            }
            if (base_swap_width == 0 || base_swap_height == 0) {
                ID3D11Texture2D* backBuffer = nullptr;
                if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)) && backBuffer) {
                    D3D11_TEXTURE2D_DESC texDesc = {};
                    backBuffer->GetDesc(&texDesc);
                    backBuffer->Release();
                    base_swap_width = texDesc.Width;
                    base_swap_height = texDesc.Height;
                }
            }
            current_game_scale = 1.0f;
        }
    }

    void ShutdownVR();

    /**
     * Hooked Present function - main VR entry point
     */
    HRESULT __stdcall hookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        // First call - initialize VR
        if (first_present) {
            LOGSTR("D3DHooks_VRManager: First Present - Initializing VR...\n");

            ID3D11Device* device;
            pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device);

            ID3D11DeviceContext* context;
            device->GetImmediateContext(&context);

            if (!vrManager) {
                vrManager = &VR::VRManager::Get();
            }

            // Try to initialize with auto-detection
            if (vrManager->Initialize(device, VR::Runtime::Auto)) {
                LOGSTRF("D3DHooks_VRManager: VR initialized with %s\n",
                        vrManager->GetActiveRuntimeName());
            } else {
                LOGSTR("D3DHooks_VRManager: Failed to initialize VR\n");
            }

            VR::IVRBackend* backend = vrManager->GetBackend();
            if (backend) {
                using_openvr = (vrManager->GetActiveRuntime() == VR::Runtime::OpenVR);
                VR::GetRuntimeStats().activeRuntime.store(using_openvr ? 1 : 2);

                if (using_openvr) {
                    LOGSTR("D3DHooks_VRManager: Syncing desktop swapchain format for OpenVR backend\n");
                    backend->SetSwapChain(pSwapChain);
                } else {
                    LOGSTR("D3DHooks_VRManager: OpenXR backend manages its own swapchain format; skipping desktop swapchain resync\n");
                }

                float renderScale = VR::GetReprojectionSettings().renderScale.load();
                if (renderScale < 0.5f) renderScale = 0.5f;
                if (renderScale > 2.0f) renderScale = 2.0f;

                LOGSTR("D3DHooks_VRManager: Creating HMD renderer\n");
                hmdRenderer = new HMDRenderer(pSwapChain, backend, renderScale, device, context);
                LOGSTR("D3DHooks_VRManager: Initializing camera FOV scanner\n");
                cameraFov = std::make_unique<Game::GtaCameraFov>();
                cameraFov->Initialize();
                LOGSTR("D3DHooks_VRManager: Creating camera hook\n");
                cameraHook = new Game::GtaCameraHook(backend, cameraFov.get());
                cameraHook->Hook();

                // Initialize game state detection
                LOGSTR("D3DHooks_VRManager: Initializing game state detection\n");
                gameState = std::make_unique<Game::GtaGameState>();
                gameState->Initialize();

                // Initialize virtual screen for cutscenes
                LOGSTR("D3DHooks_VRManager: Initializing virtual screen\n");
                virtualScreen = std::make_unique<VirtualScreen>();
                virtualScreen->Initialize(device, backend);

                LOGSTRF("D3DHooks_VRManager: Runtime detected: %s\n", using_openvr ? "OpenVR" : "OpenXR");

                if (using_openvr) {
                    LOGSTR("D3DHooks_VRManager: Creating OpenVR overlay...\n");
                    overlay = std::make_unique<OpenVROverlaySurface>();
                    if (overlay->Init(static_cast<vr::IVRSystem*>(backend->GetSession()))) {
                        LOGSTR("D3DHooks_VRManager: OpenVR overlay initialized\n");
                        overlay_ui = std::make_unique<XR::XROverlayUI>();
                        if (overlay_ui->Initialize(device, overlay.get())) {
                            LOGSTR("D3DHooks_VRManager: Overlay UI initialized\n");
                            overlay_ui->LoadSettings();
                            ApplyOverlaySettingsOpenVR(overlay_ui->GetSettings());
                            // Default to visible - user can hide with menu button
                            overlay_visible = !ReadEnvFlag("GTAVR_OVERLAY_HIDDEN", false);
                            LOGSTRF("D3DHooks_VRManager: Overlay visible = %s\n", overlay_visible ? "true" : "false");
                            overlay_ui->SetVisible(overlay_visible);
                            overlay->SetVisible(overlay_visible);
                            overlay_ui->SetOnSettingsChanged([](const XR::VRSettings& settings) {
                                ApplyOverlaySettingsOpenVR(settings);
                            });
                        } else {
                            LOGSTR("D3DHooks_VRManager: Failed to initialize overlay UI\n");
                        }
                    } else {
                        LOGSTR("D3DHooks_VRManager: Failed to initialize OpenVR overlay\n");
                    }
                } else {
                    LOGSTR("D3DHooks_VRManager: OpenXR detected - overlay handled by XR backend\n");
                }
            }

            // Create shaders
            if (FAILED(device->CreatePixelShader(g_pStereoShader, sizeof(g_pStereoShader), nullptr, &stereo_pixel_shader_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create pixel shader.\n");
                ShutdownVR();
            }
            if (!EnsureFullscreenVertexShader(device)) {
                LOGSTR("D3DHooks_VRManager: Failed to create fullscreen vertex shader.\n");
                ShutdownVR();
            }
            EnsureReprojectionShader(device);
            EnsureBlitShader(device);

            // Create constant buffers
            D3D11_BUFFER_DESC bd = {};
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = sizeof(StereoParams);
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (FAILED(device->CreateBuffer(&bd, nullptr, &stereo_constant_buffer_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create constant buffer.\n");
                ShutdownVR();
            }
            bd.ByteWidth = sizeof(ReprojectionParams);
            if (FAILED(device->CreateBuffer(&bd, nullptr, &reprojection_constant_buffer_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create reprojection constant buffer.\n");
                ShutdownVR();
            }
            bd.ByteWidth = sizeof(VignetteParams);
            if (FAILED(device->CreateBuffer(&bd, nullptr, &vignette_constant_buffer_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create vignette constant buffer.\n");
                ShutdownVR();
            }
            bd.ByteWidth = sizeof(BlitParams);
            if (FAILED(device->CreateBuffer(&bd, nullptr, &blit_constant_buffer_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create blit constant buffer.\n");
                ShutdownVR();
            }

            // Create sampler state
            D3D11_SAMPLER_DESC sd = {};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sd.MinLOD = 0;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(device->CreateSamplerState(&sd, &sampler_state_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create sampler state.\n");
                ShutdownVR();
            }

            // Create fixed-function states for blit/reprojection
            D3D11_BLEND_DESC blendDesc = {};
            blendDesc.RenderTarget[0].BlendEnable = FALSE;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(device->CreateBlendState(&blendDesc, &blit_blend_state_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create blend state.\n");
                ShutdownVR();
            }

            D3D11_DEPTH_STENCIL_DESC depthDesc = {};
            depthDesc.DepthEnable = FALSE;
            depthDesc.StencilEnable = FALSE;
            if (FAILED(device->CreateDepthStencilState(&depthDesc, &blit_depth_state_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create depth state.\n");
                ShutdownVR();
            }

            D3D11_RASTERIZER_DESC rasterDesc = {};
            rasterDesc.FillMode = D3D11_FILL_SOLID;
            rasterDesc.CullMode = D3D11_CULL_NONE;
            rasterDesc.DepthClipEnable = TRUE;
            rasterDesc.ScissorEnable = FALSE;
            if (FAILED(device->CreateRasterizerState(&rasterDesc, &blit_raster_state_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create rasterizer state.\n");
                ShutdownVR();
            }

            // Create vertex buffers
            Vertex vertices[] =
            {
                { { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
                { { -1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
                { { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
                { { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
            };
            D3D11_BUFFER_DESC vbd = {};
            vbd.Usage = D3D11_USAGE_DEFAULT;
            vbd.ByteWidth = sizeof(vertices);
            vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vsd = { vertices, 0, 0 };
            if (FAILED(device->CreateBuffer(&vbd, &vsd, &vertex_buffer_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create vertex buffer.\n");
                ShutdownVR();
            }

            // Input layout not needed for fullscreen vertex shader


            device->Release();
            context->Release();
            first_present = false;

            // Return early on first frame to let VR init complete
            return S_OK;
        }

        // Get the active VR backend
        VR::IVRBackend* backend = GetBackend();
        if (!backend) {
            // No VR - just call original Present
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

        bool shouldRender = backend->BeginFrame();
        if (!shouldRender) {
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }
        backend->UpdateControllers();
        ApplySnapTurning(backend->GetControllerState(VR::Hand::Right));

        bool cameraReady = cameraHook && cameraHook->IsReady();

        if (cameraFov) {
            cameraFov->Update(VR::GetFovSettings());
        }

        // Update game state detection
        if (gameState) {
            gameState->Update();
        }

        // Check if cutscene virtual screen should be active
        bool showVirtualScreen = gameState && gameState->ShouldShowVirtualScreen();
        if (!cameraReady) {
            showVirtualScreen = false;
        }
        if (virtualScreen) {
            virtualScreen->SetEnabled(showVirtualScreen);
            if (showVirtualScreen) {
                virtualScreen->Update();
            }
        }

        auto& stereoSettings = VR::GetStereoSettings();
        auto& reproSettings = VR::GetReprojectionSettings();
        int stereoMode = stereoSettings.mode.load();
        bool allowStereoRendering = cameraReady &&
                                    !(gameState && (gameState->IsLoading() || gameState->IsInMenu())) &&
                                    !showVirtualScreen;

        bool wantAlternate = allowStereoRendering &&
                            stereoMode == static_cast<int>(VR::StereoMode::AlternateEye);
        bool wantReprojection = allowStereoRendering &&
                                reproSettings.enabled.load() &&
                                stereoMode == static_cast<int>(VR::StereoMode::Reprojection);
        bool monoFallbackCopyBothEyes = !cameraReady && !showVirtualScreen;

        static bool logged_camera_fallback = false;
        if (!cameraReady && !logged_camera_fallback) {
            LOGSTR("D3DHooks_VRManager: Camera hook not ready, using mono blit fallback until stereo engages\n");
            logged_camera_fallback = true;
        }

        if (stereoMode != current_stereo_mode) {
            current_stereo_mode = stereoMode;
            current_eye = VR::Eye::Left;
            next_eye = VR::Eye::Right;
        }

        float renderScale = reproSettings.renderScale.load();
        if (hmdRenderer) {
            hmdRenderer->Resize(renderScale);
        }

        MaybeResizeSwapchain(pSwapChain);

        // Get backbuffer
        ID3D11Texture2D* pBuffer;
        HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBuffer);
        if (FAILED(hr)) {
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

        static bool logged_backbuffer = false;
        if (!logged_backbuffer) {
            D3D11_TEXTURE2D_DESC desc = {};
            pBuffer->GetDesc(&desc);
            LOGSTRF("D3DHooks_VRManager: Backbuffer %ux%u fmt=%u bind=0x%08x samples=%u quality=%u\n",
                    desc.Width, desc.Height, static_cast<unsigned>(desc.Format), desc.BindFlags,
                    desc.SampleDesc.Count, desc.SampleDesc.Quality);

            if (hmdRenderer) {
                ID3D11Texture2D* eyeTex = hmdRenderer->GetEyeTexture(VR::Eye::Left);
                if (eyeTex) {
                    D3D11_TEXTURE2D_DESC eyeDesc = {};
                    eyeTex->GetDesc(&eyeDesc);
                    LOGSTRF("D3DHooks_VRManager: Eye texture %ux%u fmt=%u bind=0x%08x samples=%u quality=%u\n",
                            eyeDesc.Width, eyeDesc.Height,
                            static_cast<unsigned>(eyeDesc.Format), eyeDesc.BindFlags,
                            eyeDesc.SampleDesc.Count, eyeDesc.SampleDesc.Quality);
                }
            }
            logged_backbuffer = true;
        }

        ID3D11DeviceContext* context;
        backend->GetDevice()->GetImmediateContext(&context);
        UpdateVignetteParams(context);

        bool depthReady = false;
        if (wantReprojection) {
            ID3D11DepthStencilView* dsv = nullptr;
            ID3D11RenderTargetView* current_rtv = nullptr;
            context->OMGetRenderTargets(1, &current_rtv, &dsv);
            if (dsv) {
                depthReady = EnsureDepthResources(backend->GetDevice(), dsv);
            }
            if (!depthReady) {
                static bool logged_depth_missing = false;
                if (!logged_depth_missing) {
                    LOGSTR("D3DHooks_VRManager: Depth reprojection requested but depth unavailable, falling back to blit\n");
                    logged_depth_missing = true;
                }
            }
            if (dsv) dsv->Release();
            if (current_rtv) current_rtv->Release();
        }

        if (blit_pixel_shader_) {
            if (wantAlternate) {
                float eyeSign = (current_eye == VR::Eye::Left) ? -1.0f : 1.0f;
                RenderBlit(backend->GetDevice(), context, pBuffer, hmdRenderer->GetEyeRenderTarget(current_eye), eyeSign);
            } else {
                RenderBlit(backend->GetDevice(), context, pBuffer,
                           hmdRenderer->GetEyeRenderTarget(VR::Eye::Left), 0.0f, monoFallbackCopyBothEyes);
            }
        } else {
            if (wantAlternate) {
                hmdRenderer->Render(current_eye, pBuffer);
            } else {
                hmdRenderer->Render(VR::Eye::Left, pBuffer);
            }
        }

        if (!wantAlternate && !monoFallbackCopyBothEyes) {
            ID3D11Texture2D* rightEyeTexture = hmdRenderer->GetEyeTexture(VR::Eye::Right);
            ID3D11RenderTargetView* rtv = hmdRenderer->GetEyeRenderTarget(VR::Eye::Right);
            context->OMSetRenderTargets(1, &rtv, nullptr);

            bool using_cached_srv = false;
            ID3D11ShaderResourceView* color_srv = GetColorSRV(backend->GetDevice(), context, pBuffer, using_cached_srv);

            if (wantReprojection && depthReady && reprojection_pixel_shader_) {
                ApplyBlitStates(context);
                context->VSSetShader(stereo_vertex_shader_, nullptr, 0);
                context->PSSetShader(reprojection_pixel_shader_, nullptr, 0);

                float imageScale = 1.0f;
                float offsetX = 0.0f;
                float offsetY = 0.0f;
                GetImageTransform(1.0f, imageScale, offsetX, offsetY);

                ReprojectionParams params = {};
                params.u_ipd_offset = reproSettings.ipd.load();
                params.u_depth_scale = reproSettings.depthScale.load();
                params.u_depth_bias = reproSettings.depthBias.load();
                params.u_image_scale = imageScale;
                params.u_offset_x = offsetX;
                params.u_offset_y = offsetY;
                params.u_invert_depth = reproSettings.invertDepth.load() ? 1.0f : 0.0f;

                context->UpdateSubresource(reprojection_constant_buffer_, 0, nullptr, &params, 0, 0);
                context->PSSetConstantBuffers(0, 1, &reprojection_constant_buffer_);
                context->PSSetConstantBuffers(1, 1, &vignette_constant_buffer_);

                ID3D11ShaderResourceView* srvs[] = { color_srv, depth_srv_ };
                context->PSSetShaderResources(0, 2, srvs);
                context->PSSetSamplers(0, 1, &sampler_state_);

                context->IASetInputLayout(nullptr);
                context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                context->Draw(3, 0);

                ID3D11ShaderResourceView* null_srvs[] = { nullptr, nullptr };
                context->PSSetShaderResources(0, 2, null_srvs);
            } else {
                if (blit_pixel_shader_) {
                    RenderBlit(backend->GetDevice(), context, pBuffer, rtv, 0.0f, false);
                } else {
                    context->CopyResource(rightEyeTexture, pBuffer);
                }
            }

            if (color_srv && !using_cached_srv) {
                color_srv->Release();
            }
        }

        if (using_openvr) {
            const auto& leftState = backend->GetControllerState(VR::Hand::Left);
            const auto& rightState = backend->GetControllerState(VR::Hand::Right);
            UpdateOverlayInputOpenVR(leftState, rightState);
            RenderOverlayOpenVR();
            if (overlay_ui && overlay_ui->ConsumeRecenterRequest()) {
                backend->Recenter();
                VR::GetViewSettings().snapYawOffsetDeg.store(0.0f);
                VR::GetStereoSettings().recenterRequested.store(true);
                VR::GetHeadLookSettings().recenterRequested.store(true);
            }
        }

        // Submit to VR
        // If virtual screen is active (cutscene), render the game frame on the virtual screen
        if (showVirtualScreen && virtualScreen && virtualScreen->IsInitialized()) {
            // Render game frame to virtual screen
            virtualScreen->Render(pBuffer);

            // Submit virtual screen textures
            backend->SubmitEyeTexture(VR::Eye::Left, virtualScreen->GetEyeTexture(VR::Eye::Left));
            backend->SubmitEyeTexture(VR::Eye::Right, virtualScreen->GetEyeTexture(VR::Eye::Right));
        } else if (monoFallbackCopyBothEyes) {
            ID3D11Texture2D* monoTexture = hmdRenderer->GetEyeTexture(VR::Eye::Left);
            backend->SubmitEyeTexture(VR::Eye::Left, monoTexture);
            backend->SubmitEyeTexture(VR::Eye::Right, monoTexture);
        } else {
            // Normal rendering
            backend->SubmitEyeTexture(VR::Eye::Left, hmdRenderer->GetEyeTexture(VR::Eye::Left));
            backend->SubmitEyeTexture(VR::Eye::Right, hmdRenderer->GetEyeTexture(VR::Eye::Right));
        }

        if (cameraHook) {
            // Pass game state for decoupling and cutscene handling
            Game::GtaGameState* gs = gameState.get();

            if (wantAlternate && cameraHook->IsReady()) {
                cameraHook->Update(next_eye, gs);
                std::swap(current_eye, next_eye);
            } else {
                cameraHook->Update(VR::Eye::Left, gs);
                current_eye = VR::Eye::Left;
                next_eye = VR::Eye::Right;
            }

            // Handle recenter request
            auto& stereoSettings = VR::GetStereoSettings();
            if (stereoSettings.recenterRequested.exchange(false)) {
                cameraHook->RecenterPose();
            }
        }

        context->Release();
        pBuffer->Release();

		backend->EndFrame();

        // Desktop mirroring
        static bool logged_mirror_unsync = false;
        if (!logged_mirror_unsync && SyncInterval != 0) {
            LOGSTR("D3DHooks_VRManager: Forcing desktop mirror Present sync interval to 0 in VR mode\n");
            logged_mirror_unsync = true;
        }
        HRESULT result = Original_PresentHook(pSwapChain, 0, Flags);

        return result;
    }

    /**
     * D3D11CreateDeviceAndSwapChain proxy
     */
    void* Original_D3D11CreateDeviceAndSwapChain = nullptr;

    HRESULT Proxy_D3D11CreateDeviceAndSwapChain(
        _In_opt_        IDXGIAdapter         *pAdapter,
        D3D_DRIVER_TYPE     DriverType,
        HMODULE             Software,
        UINT                Flags,
        _In_opt_ const  D3D_FEATURE_LEVEL    *pFeatureLevels,
        UINT                FeatureLevels,
        UINT                SDKVersion,
        _In_opt_ const  DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
        _Out_opt_       IDXGISwapChain       **ppSwapChain,
        _Out_opt_       ID3D11Device         **ppDevice,
        _Out_opt_       D3D_FEATURE_LEVEL    *pFeatureLevel,
        _Out_opt_       ID3D11DeviceContext  **ppImmediateContext
    ) {
        LOGSTRF("D3DHooks_VRManager: D3D11CreateDeviceAndSwapChain called\n");

        HRESULT result = ((PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)Original_D3D11CreateDeviceAndSwapChain)(
            pAdapter,
            DriverType,
            Software,
            Flags,
            pFeatureLevels,
            FeatureLevels,
            SDKVersion,
            pSwapChainDesc,
            ppSwapChain,
            ppDevice,
            pFeatureLevel,
            ppImmediateContext
        );

        // Hook Present if we got a swap chain
        if (ppSwapChain && *ppSwapChain) {
            if (present_hook_installed) {
                LOGSTR("D3DHooks_VRManager: Ignoring additional swap chain creation after primary Present hook\n");
                return result;
            }

            IDXGISwapChain* pSwapChain = *ppSwapChain;

            DWORD64* vtable = (DWORD64*)pSwapChain;
            vtable = (DWORD64*)vtable[0];

            int presentIndex = 8; // Present is at index 8 for IDXGISwapChain
            Original_PresentHook = (PresentHook)((void**)vtable)[presentIndex];

            // Using MinHook to be safe
            MH_CreateHook(((void**)vtable)[presentIndex], hookedPresent, (void**)&Original_PresentHook);
            MH_EnableHook(((void**)vtable)[presentIndex]);
            present_hook_installed = true;

            LOGSTRF("D3DHooks_VRManager: Hooked Present at vtable index %d\n", presentIndex);

            if (create_device_and_swapchain_target && !create_hook_disabled) {
                if (MH_DisableHook(create_device_and_swapchain_target) == MH_OK) {
                    create_hook_disabled = true;
                    LOGSTR("D3DHooks_VRManager: Disabled D3D11CreateDeviceAndSwapChain hook after primary Present hook\n");
                }
            }
        }

        return result;
    }

    /**
     * Shutdown VR - call before DLL unload
     */
    void ShutdownVR() {
        if (stereo_pixel_shader_) { stereo_pixel_shader_->Release(); stereo_pixel_shader_ = nullptr; }
        if (stereo_vertex_shader_) { stereo_vertex_shader_->Release(); stereo_vertex_shader_ = nullptr; }
        if (stereo_constant_buffer_) { stereo_constant_buffer_->Release(); stereo_constant_buffer_ = nullptr; }
        if (reprojection_pixel_shader_) { reprojection_pixel_shader_->Release(); reprojection_pixel_shader_ = nullptr; }
        if (reprojection_constant_buffer_) { reprojection_constant_buffer_->Release(); reprojection_constant_buffer_ = nullptr; }
        if (blit_pixel_shader_) { blit_pixel_shader_->Release(); blit_pixel_shader_ = nullptr; }
        if (blit_constant_buffer_) { blit_constant_buffer_->Release(); blit_constant_buffer_ = nullptr; }
        if (vignette_constant_buffer_) { vignette_constant_buffer_->Release(); vignette_constant_buffer_ = nullptr; }
        if (blit_blend_state_) { blit_blend_state_->Release(); blit_blend_state_ = nullptr; }
        if (blit_depth_state_) { blit_depth_state_->Release(); blit_depth_state_ = nullptr; }
        if (blit_raster_state_) { blit_raster_state_->Release(); blit_raster_state_ = nullptr; }
        if (blit_source_srv_) { blit_source_srv_->Release(); blit_source_srv_ = nullptr; }
        if (blit_source_copy_) { blit_source_copy_->Release(); blit_source_copy_ = nullptr; }
        blit_source_width_ = 0;
        blit_source_height_ = 0;
        blit_source_format_ = DXGI_FORMAT_UNKNOWN;
        if (sampler_state_) { sampler_state_->Release(); sampler_state_ = nullptr; }
        if (vertex_buffer_) { vertex_buffer_->Release(); vertex_buffer_ = nullptr; }
        if (input_layout_) { input_layout_->Release(); input_layout_ = nullptr; }
        ReleaseDepthResources();

        overlay_ui.reset();
        overlay.reset();
        virtualScreen.reset();
        gameState.reset();
        cameraFov.reset();

        if (hmdRenderer) {
            delete hmdRenderer;
            hmdRenderer = nullptr;
        }
        if (cameraHook) {
            delete cameraHook;
            cameraHook = nullptr;
        }

        if (vrManager && vrManager->IsInitialized()) {
            vrManager->Shutdown();
        }
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }

    void Initialize() {
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            LOGSTR("D3DHooks_VRManager: MinHook initialization failed.\n");
            return;
        }

        HMODULE d3d11_module = GetModuleHandleA("d3d11.dll");
        if (d3d11_module) {
            void* pD3D11CreateDeviceAndSwapChain = GetProcAddress(d3d11_module, "D3D11CreateDeviceAndSwapChain");
            if (pD3D11CreateDeviceAndSwapChain) {
                create_device_and_swapchain_target = pD3D11CreateDeviceAndSwapChain;
                MH_CreateHook(pD3D11CreateDeviceAndSwapChain, Proxy_D3D11CreateDeviceAndSwapChain, &Original_D3D11CreateDeviceAndSwapChain);
                MH_EnableHook(pD3D11CreateDeviceAndSwapChain);
            }
        }
    }

} // namespace VRMgr
} // namespace OVRInject
