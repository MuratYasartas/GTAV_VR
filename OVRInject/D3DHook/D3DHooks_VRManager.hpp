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
#include <cstdio>
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
#include "../Game/BuildManifest.hpp"
#include "../Overlay/OpenVROverlaySurface.hpp"
#include "../Overlay/VirtualScreen.hpp"
#include "../OpenXR/XROverlayUI.hpp"
#include "../Game/OnlineGuard.hpp"
#include "../Game/ShvNatives.hpp"
#include "../Game/VRCamera.hpp"
#include "../Stereo/StereoEngine.hpp"
#include "../Stereo/ComfortRuntime.hpp"
#include "../Stereo/ImageFit.hpp"
#include "../Perf/PerfStats.hpp"
#include "HudRedirect.hpp"
#include <atomic>
#include <mutex>
#include <vector>


extern HMODULE dxgiModule;

namespace OVRInject {
namespace VRMgr {

    // VR Manager instance
    VR::VRManager* vrManager = nullptr;
	HMDRenderer* hmdRenderer = nullptr;
	Game::GtaCameraHook* cameraHook = nullptr;
	Game::VRCamera* vrCamera = nullptr;
    std::unique_ptr<Game::GtaCameraFov> cameraFov;
    std::unique_ptr<Game::GtaGameState> gameState;
    std::unique_ptr<VirtualScreen> virtualScreen;
    std::unique_ptr<OpenVROverlaySurface> overlay;
    std::unique_ptr<XR::XROverlayUI> overlay_ui;

    // D3D11 objects
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
    uint32_t base_swap_width = 0;
    uint32_t base_swap_height = 0;
    float current_game_scale = 1.0f;
    DWORD last_swap_resize_ms = 0;
    bool present_hook_installed = false;
    bool create_hook_disabled = false;
    void* create_device_and_swapchain_target = nullptr;

    // ---- Phase 3 robustness state -------------------------------------------
    // Ordered-unload interlock: set on DLL_PROCESS_DETACH before any teardown;
    // in_hook_count_ counts frames currently inside a hooked call so the
    // unload path can drain them instead of tearing down mid-hook.
    std::atomic<bool> shutting_down_{false};
    std::atomic<long> in_hook_count_{0};
    // Hard failure (init or exhausted device-lost re-init) -> stay inert and
    // pass every frame straight through to the original Present.
    std::atomic<bool> vr_disabled_inert_{false};
    // Set by the ResizeBuffers hook; targets are lazily recreated on the next
    // Present (same render thread) so D3D objects are never released mid-frame.
    std::atomic<bool> resize_pending_{false};
    // One clean VR re-init attempt is allowed after device removal/reset.
    uint32_t device_lost_reinit_remaining_ = 1;
    bool device_lost_logged_ = false;
    void* present_hook_target_ = nullptr;
    void* resize_buffers_hook_target_ = nullptr;
    void* create_device_target_ = nullptr;
    void* create_dxgi_factory1_target_ = nullptr;
    void* create_dxgi_factory2_target_ = nullptr;
    void* create_swapchain_target_ = nullptr;
    // Late-injection dummy device state (Kiero method; owned by
    // CreateDummyDeviceAndHookPresent below). dummy_swapchain_ keeps ONE
    // reference alive for the DLL lifetime so the tag pointer can never be
    // recycled into a real swapchain; released in UninstallHooks.
    std::atomic<IDXGISwapChain*> dummy_swapchain_{nullptr};
    HWND dummy_window_ = nullptr;
    // Swapchain whose vtable the Present hook was installed through; lets the
    // dummy path tell whether it (vs a live game swapchain) landed the hook.
    void* hooked_swapchain_ = nullptr;
    // True when the global Present hook was installed via the dummy device,
    // i.e. the game device/swapchain/factory all predate our injection.
    bool late_injection_dummy_hooked_ = false;
    // Initialize() idempotency guard.
    std::atomic<bool> initialize_started_{false};
    HWND game_window_ = nullptr;

    // WndProc hook state for overlay input blocking.
    WNDPROC original_wndproc_ = nullptr;

    static bool ShouldSwallowInput(UINT msg) {
        switch (msg) {
        case WM_INPUT:
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR: case WM_SYSCHAR:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEMOVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            return true;
        default:
            return false;
        }
    }

    static LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (VR::GetRuntimeStats().overlayVisible.load() && ShouldSwallowInput(msg)) {
            return true; // overlay owns the input while visible
        }
        return CallWindowProcA(original_wndproc_, hwnd, msg, wParam, lParam);
    }
    // desktopMirrorSyncOverride=1 (gtavr_settings.ini [Performance]) or
    // GTAVR_DESKTOP_MIRROR_SYNC0=1 forces the desktop mirror Present sync
    // interval to 0. Default (0) preserves the game's own sync interval.
    bool force_mirror_sync0_ = false;
    HANDLE watchdog_thread_ = nullptr;

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
        float u_scale_x;
        float u_scale_y;
        float u_offset_x;
        float u_offset_y;
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

    typedef HRESULT(__stdcall *ResizeBuffersHook)(IDXGISwapChain* pSwapChain,
                                                  UINT BufferCount, UINT Width, UINT Height,
                                                  DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    ResizeBuffersHook Original_ResizeBuffersHook = nullptr;

    typedef HRESULT(__stdcall *CreateSwapChainHook)(IDXGIFactory* pFactory,
                                                    IUnknown* pDevice,
                                                    DXGI_SWAP_CHAIN_DESC* pDesc,
                                                    IDXGISwapChain** ppSwapChain);
    CreateSwapChainHook Original_CreateSwapChainHook = nullptr;

    typedef HRESULT(WINAPI *PFN_DXGI_CreateDXGIFactory1)(REFIID riid, void** ppFactory);
    typedef HRESULT(WINAPI *PFN_DXGI_CreateDXGIFactory2)(UINT flags, REFIID riid, void** ppFactory);
    PFN_DXGI_CreateDXGIFactory1 Original_CreateDXGIFactory1 = nullptr;
    PFN_DXGI_CreateDXGIFactory2 Original_CreateDXGIFactory2 = nullptr;
    void* Original_D3D11CreateDevice = nullptr;

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

        // Phase 5/6 comfort additions (live-applied here on the OpenVR path;
        // the OpenXR apply path stores the same values in
        // XRHMDSupport::ApplyOverlaySettings - parity).
        auto& comfortRuntime = Stereo::GetComfortRuntime();
        comfortRuntime.vehicleHorizonLock.store(settings.vehicleHorizonLock);
        comfortRuntime.smoothTurnSpeedDeg.store(settings.smoothTurnSpeed);

        auto& stereo = VR::GetStereoSettings();
        stereo.mode.store(settings.stereoMode);
        stereo.stereoIPD.store(settings.stereoIPD);
        stereo.ipdAuto.store(settings.ipdAuto);
        stereo.headPredictMs.store(settings.headPredictMs);
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

        // Overlay toggle: controller menu button OR keyboard (Delete/Insert).
        // The keyboard edge-detect lets flat-screen users drive the same UI
        // before/without controllers, and is the fallback when the controller
        // action mapping is unavailable.
        bool overlayToggle = leftState.buttons.menuJustPressed || rightState.buttons.menuJustPressed;
        static bool keyboardToggleHeld = false;
        const bool keyboardToggleDown = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0 ||
                                        (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (keyboardToggleDown && !keyboardToggleHeld) {
            overlayToggle = true;
        }
        keyboardToggleHeld = keyboardToggleDown;

        if (overlayToggle) {
            overlay_visible = !overlay_visible;
            overlay_ui->SetVisible(overlay_visible);
            if (overlay) {
                overlay->SetVisible(overlay_visible);
            }
            LOGDBGF("D3DHooks_VRManager: overlay visibility -> %s\n", overlay_visible ? "visible" : "hidden");
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
    float u_scale_x;
    float u_scale_y;
    float u_offset_x;
    float u_offset_y;
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
    // Anisotropic scale: >1 zooms into the center subregion on that axis
    // (angular crop - the AER path maps the game's ultrawide frustum down to
    // the per-eye XR frustum, which needs different X/Y factors).
    float2 uv = tex;
    uv.x = (uv.x - 0.5f) / max(u_scale_x, 0.01f) + 0.5f + u_offset_x;
    uv.y = (uv.y - 0.5f) / max(u_scale_y, 0.01f) + 0.5f + u_offset_y;

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

    // Releases every cached D3D resource whose size is tied to the swapchain
    // backbuffer. Called from the ResizeBuffers hook BEFORE the original
    // resize runs. None of these hold a reference to the swapchain's buffer
    // itself (they are independent allocations), so the original ResizeBuffers
    // cannot fail because of us; they are dropped so no stale-sized resource
    // survives the transition.
    static void ReleaseSwapchainSizedResources() {
        if (blit_source_srv_) { blit_source_srv_->Release(); blit_source_srv_ = nullptr; }
        if (blit_source_copy_) { blit_source_copy_->Release(); blit_source_copy_ = nullptr; }
        blit_source_width_ = 0;
        blit_source_height_ = 0;
        blit_source_format_ = DXGI_FORMAT_UNKNOWN;
        ReleaseDepthResources();
        // Phase 6 HUD: the HUD target is backbuffer-sized; drop it (and the
        // cached backbuffer pointer) so the next Present re-caches cleanly.
        Hud::ReleaseResources();
        base_swap_width = 0;
        base_swap_height = 0;
    }

    // desktopMirrorSyncOverride: gtavr_settings.ini [Performance] key,
    // GTAVR_DESKTOP_MIRROR_SYNC0 env var wins when set. Default false =
    // preserve the game's Present sync interval on the desktop mirror.
    static bool ReadDesktopMirrorSyncOverride() {
        if (ReadEnvFlag("GTAVR_DESKTOP_MIRROR_SYNC0", false)) {
            return true;
        }
        char path[MAX_PATH] = {};
        DWORD len = GetEnvironmentVariableA("GTAVR_SETTINGS_PATH", path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            char dir[MAX_PATH] = {};
            len = GetEnvironmentVariableA("GTAVR_SETTINGS_DIR", dir, MAX_PATH);
            if (len > 0 && len < MAX_PATH) {
                snprintf(path, sizeof(path), "%s\\gtavr_settings.ini", dir);
            } else {
                strncpy_s(path, sizeof(path), "gtavr_settings.ini", _TRUNCATE);
            }
        }
        return GetPrivateProfileIntA("Performance", "desktopMirrorSyncOverride", 0, path) != 0;
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

    // Locomotion vignette (comfort default ON per docs/user/comfort.md):
    // fades in with movement, off while stationary. Game-speed input state is
    // not available without Game/ edits, so the locomotion proxy is
    // max(smoothed head-translation speed, left-stick magnitude), plus a
    // constant floor while in a vehicle (the comfort.md vehicle profile calls
    // for a stronger vignette while driving).
    static void UpdateVignetteParams(ID3D11DeviceContext* context, VR::IVRBackend* backend) {
        if (!vignette_constant_buffer_ || !context) {
            return;
        }

        static float activity = 0.0f;
        static bool have_last_pos = false;
        static float last_pos[3] = {0.0f, 0.0f, 0.0f};
        static uint64_t last_tick = 0;

        uint64_t now = GetTickCount64();
        float dt = (last_tick != 0) ? static_cast<float>(now - last_tick) / 1000.0f : 0.0f;
        last_tick = now;
        if (dt > 0.25f) {
            dt = 0.0f;  // long stall: do not integrate a bogus delta
        }

        float target = 0.0f;
        if (backend && dt > 0.0f) {
            DirectX::XMMATRIX headPose = backend->GetHeadPoseMatrix();
            float pos[3] = {headPose.r[3].m128_f32[0],
                            headPose.r[3].m128_f32[1],
                            headPose.r[3].m128_f32[2]};
            if (have_last_pos) {
                float dx = pos[0] - last_pos[0];
                float dy = pos[1] - last_pos[1];
                float dz = pos[2] - last_pos[2];
                float speed = std::sqrt(dx * dx + dy * dy + dz * dz) / dt;  // m/s
                // Full activity at ~0.4 m/s of head translation (room-scale
                // steps, walk-in-place bob); pure head rotation contributes
                // nothing.
                target = (std::max)(target, (std::min)(speed / 0.4f, 1.0f));
            }
            last_pos[0] = pos[0];
            last_pos[1] = pos[1];
            last_pos[2] = pos[2];
            have_last_pos = true;

            const auto& leftState = backend->GetControllerState(VR::Hand::Left);
            float stickMag = std::sqrt(leftState.buttons.thumbstickX * leftState.buttons.thumbstickX +
                                       leftState.buttons.thumbstickY * leftState.buttons.thumbstickY);
            if (stickMag < 0.15f) {
                stickMag = 0.0f;
            }
            target = (std::max)(target, (std::min)(stickMag, 1.0f));
        } else if (!backend) {
            have_last_pos = false;
        }

        if (gameState && gameState->IsInVehicle()) {
            target = (std::max)(target, 0.5f);  // vehicle floor: stronger vignette while driving
        }

        if (dt > 0.0f) {
            // Fast attack (~100 ms), slower release (~600 ms).
            float tau = (target > activity) ? 0.10f : 0.60f;
            activity += (target - activity) * (1.0f - std::exp(-dt / tau));
        }
        Stereo::GetComfortRuntime().vignetteActivity.store(activity);

        auto& comfort = VR::GetComfortSettings();
        bool enabled = comfort.vignetteEnabled.load() && activity > 0.02f;
        VignetteParams params = {};
        params.u_intensity = enabled ? comfort.vignetteIntensity.load() * activity : 0.0f;
        params.u_enabled = enabled ? 1.0f : 0.0f;
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
        // Alignment contract (Stereo/ImageFit.hpp): X is a per-eye
        // convergence shift (opposite directions, signed by eyeSign); Y is
        // common-mode - identical in both eyes. Signing Y per eye (the old
        // behavior) produced vertical disparity: unfusable, "eyes not
        // aligned".
        Stereo::ComputeUserImageOffsets(repro.screenOffsetX.load(),
                                        repro.screenOffsetY.load(),
                                        eyeSign, outOffsetX, outOffsetY);
        outOffsetX = ClampImageOffset(outOffsetX, outScale, "X");
        outOffsetY = ClampImageOffset(outOffsetY, outScale, "Y");
    }

    static void UpdateBlitParams(ID3D11DeviceContext* context,
                                 float scaleX,
                                 float scaleY,
                                 float offsetX,
                                 float offsetY) {
        if (!blit_constant_buffer_ || !context) {
            return;
        }

        BlitParams params = {};
        params.u_scale_x = scaleX;
        params.u_scale_y = scaleY;
        params.u_offset_x = offsetX;
        params.u_offset_y = offsetY;
        context->UpdateSubresource(blit_constant_buffer_, 0, nullptr, &params, 0, 0);
    }

    // Angular crop factors for the AER path: the game renders its (ultrawide)
    // frustum with vertical FOV = RuntimeStats.activeFov; each eye must show
    // only the central sub-frustum matching the XR per-eye FOV. Returned
    // factors are >= 1 (zoom into the center subregion) for the blit shader.
    static void ComputeAngularCrop(uint32_t srcWidth,
                                   uint32_t srcHeight,
                                   float eyeSign,
                                   float& outCropX,
                                   float& outCropY) {
        outCropX = 1.0f;
        outCropY = 1.0f;
        const float vfovB = VR::GetRuntimeStats().activeFov.load();
        VR::IVRBackend* backend = GetBackend();
        if (vfovB <= 1.0f || !backend || srcHeight == 0) {
            return;
        }
        constexpr float kDegToRad = 0.01745329251994329577f;
        const float tanVb = tanf(vfovB * kDegToRad * 0.5f);
        const float tanHb = tanVb * static_cast<float>(srcWidth) /
                            static_cast<float>(srcHeight);
        const VR::Eye eye = (eyeSign < 0.0f) ? VR::Eye::Left : VR::Eye::Right;
        const DirectX::XMMATRIX proj = backend->GetProjectionMatrix(eye, 0.1f, 100.0f);
        const float m00 = proj.r[0].m128_f32[0];
        const float m11 = proj.r[1].m128_f32[1];
        if (m00 < 0.01f || m11 < 0.01f) {
            return;
        }
        outCropX = tanHb * m00;  // tan(hfovB/2) / tan(hfovE/2)
        outCropY = tanVb * m11;  // tan(vfovB/2) / tan(vfovE/2)
        // scale < 1 would sample outside the backbuffer (game FOV narrower
        // than XR FOV): clamp, accepting the resulting over-zoom.
        outCropX = (std::max)(1.0f, outCropX);
        outCropY = (std::max)(1.0f, outCropY);
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
                           bool useUserAlignment = true,
                           bool aspectFit = false,
                           bool angularCrop = false) {
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
                if (aspectFit) {
                    // Mono-fallback mapping (camera unresolved): contain-fit
                    // the backbuffer into the eye target, aspect preserved
                    // and centered, bars cleared to opaque black (math and
                    // rationale in Stereo/ImageFit.hpp). The blit shader is
                    // unchanged: UV 0..1 spans the fitted viewport, so
                    // imageScale zooms the fitted image around its center
                    // (the user "virtual distance" knob) and the offsets
                    // shift it. Only the mono path passes aspectFit=true -
                    // the AER/Z3D paths keep the historical full-fill.
                    const Stereo::ContainFitRect fit =
                        Stereo::ComputeContainFit(srcDesc.Width, srcDesc.Height,
                                                  dstDesc.Width, dstDesc.Height);
                    if (fit.letterboxed) {
                        const float kOpaqueBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                        context->ClearRenderTargetView(target, kOpaqueBlack);
                        static bool logged_fit = false;
                        if (!logged_fit) {
                            LOGSTRF("D3DHooks_VRManager: Mono blit aspect-fit %ux%u -> %ux%u: image %ux%u at (%u,%u), bars black\n",
                                    srcDesc.Width, srcDesc.Height, dstDesc.Width, dstDesc.Height,
                                    fit.width, fit.height, fit.x, fit.y);
                            logged_fit = true;
                        }
                    }
                    viewport.TopLeftX = static_cast<float>(fit.x);
                    viewport.TopLeftY = static_cast<float>(fit.y);
                    viewport.Width = static_cast<float>(fit.width);
                    viewport.Height = static_cast<float>(fit.height);
                }
                context->RSSetViewports(1, &viewport);
                D3D11_RECT rect = {0, 0, static_cast<LONG>(dstDesc.Width), static_cast<LONG>(dstDesc.Height)};
                context->RSSetScissorRects(1, &rect);
                targetTex->Release();
            }
            targetRes->Release();
        }

        context->OMSetRenderTargets(1, &target, nullptr);
        ApplyBlitStates(context);
        {
            // User alignment (imageScale zoom + fine offsets), then the
            // angular crop on top for the AER path.
            float scaleX = 1.0f;
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            GetImageTransform(eyeSign, scaleX, offsetX, offsetY, useUserAlignment);
            float scaleY = scaleX;
            if (angularCrop) {
                float cropX = 1.0f;
                float cropY = 1.0f;
                ComputeAngularCrop(srcDesc.Width, srcDesc.Height, eyeSign, cropX, cropY);
                scaleX *= cropX;
                scaleY *= cropY;
            }
            UpdateBlitParams(context, scaleX, scaleY, offsetX, offsetY);
        }
        // Never leave stale content in uncovered areas (shows as smeared
        // "not rendered" artifacts around the image, reported live).
        {
            const float kOpaqueBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            context->ClearRenderTargetView(target, kOpaqueBlack);
        }
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

    // Quick recenter bind: right thumbstick click. (Both-grips is the OpenXR
    // overlay toggle and the menu button toggles the overlay on both
    // runtimes, so stick-click is the free bind everywhere.) Mirrors the
    // overlay recenter path: runtime recenter + clear the turn offset + ask
    // the camera hook to re-reference the head pose.
    static void DoQuickRecenter(VR::IVRBackend* backend) {
        if (!backend) {
            return;
        }
        backend->Recenter();
        VR::GetViewSettings().snapYawOffsetDeg.store(0.0f);
        VR::GetStereoSettings().recenterRequested.store(true);
        VR::GetHeadLookSettings().recenterRequested.store(true);
        LOGSTR("D3DHooks_VRManager: Quick recenter (right stick click)\n");
    }

    // Comfort turning + quick recenter. Snap turn (the comfort default) is
    // edge-triggered at snapTurnAngle; with snapTurning=0 the right stick
    // smooth-turns at smoothTurnSpeed deg/s. Both write snapYawOffsetDeg,
    // which the camera hook applies to the head pose every frame
    // (GtaCameraHook.cpp), so the turn rotates the camera reference, not the
    // rendered image.
    static void ApplyTurningAndComfortInput(VR::IVRBackend* backend) {
        static bool snap_ready = true;
        static bool recenter_latched = false;
        static uint64_t last_tick = 0;

        if (!backend) {
            return;
        }

        const auto& rightState = backend->GetControllerState(VR::Hand::Right);

        if (rightState.buttons.thumbstickJustPressed) {
            if (!recenter_latched) {
                DoQuickRecenter(backend);
                recenter_latched = true;
            }
        } else {
            recenter_latched = false;
        }

        auto& comfort = VR::GetComfortSettings();
        auto& view = VR::GetViewSettings();
        float axis = rightState.buttons.thumbstickX;

        uint64_t now = GetTickCount64();
        float dt = (last_tick != 0) ? static_cast<float>(now - last_tick) / 1000.0f : 0.0f;
        last_tick = now;
        if (dt > 0.25f) {
            dt = 0.0f;
        }

        if (comfort.snapTurning.load()) {
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
                float yaw = view.snapYawOffsetDeg.load() + angle;
                if (std::fabs(yaw) > 360.0f) {
                    yaw = std::fmod(yaw, 360.0f);
                }
                view.snapYawOffsetDeg.store(yaw);
                snap_ready = false;
            }
            return;
        }

        // Smooth turn: snapTurning=0 alternative, configurable speed.
        snap_ready = true;
        const float deadzone = 0.2f;
        if (dt > 0.0f && std::fabs(axis) > deadzone) {
            float speed = Stereo::GetComfortRuntime().smoothTurnSpeedDeg.load();
            float yaw = view.snapYawOffsetDeg.load() + axis * speed * dt;
            if (std::fabs(yaw) > 360.0f) {
                yaw = std::fmod(yaw, 360.0f);
            }
            view.snapYawOffsetDeg.store(yaw);
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

    // ---- StereoEngine ops ---------------------------------------------------
    // These statics are the D3D mechanics the StereoEngine drives through
    // Stereo::FrameServices op pointers. Policy (which eye, which mode) lives
    // in the engine; the mechanics stay here, unchanged from the original
    // inline hookedPresent code.

    // Copy the current depth-stencil into a shader-readable texture for the
    // Z3D path. Returns false when depth is unavailable (engine then blits).
    static bool PrepareDepthForReprojection(ID3D11Device* device, ID3D11DeviceContext* context) {
        bool depthReady = false;
        ID3D11DepthStencilView* dsv = nullptr;
        ID3D11RenderTargetView* current_rtv = nullptr;
        context->OMGetRenderTargets(1, &current_rtv, &dsv);
        if (dsv) {
            depthReady = EnsureDepthResources(device, dsv);
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
        return depthReady;
    }

    // Backbuffer -> one eye target (blit shader when available, else the
    // HMDRenderer copy path). aspectFit=true contain-fits the backbuffer in
    // the eye target (mono fallback only; see Stereo/ImageFit.hpp).
    static void ProduceEyeFromBackbuffer(ID3D11Device* device,
                                         ID3D11DeviceContext* context,
                                         ID3D11Texture2D* pBuffer,
                                         VR::Eye eye,
                                         float eyeSign,
                                         bool useUserAlignment,
                                         bool aspectFit,
                                         bool angularCrop) {
        if (blit_pixel_shader_) {
            RenderBlit(device, context, pBuffer, hmdRenderer->GetEyeRenderTarget(eye), eyeSign, useUserAlignment, aspectFit, angularCrop);
        } else {
            hmdRenderer->Render(eye, pBuffer);
        }
    }

    // --- F12 debug dump ("see what the user sees") ---------------------------

    // Write a 24-bit BMP (bottom-up, BGR rows) from an 8-bit 4-channel buffer.
    static bool WriteBmp24(const wchar_t* path, const uint8_t* data,
                           uint32_t width, uint32_t height, uint32_t rowPitch,
                           bool swapRB) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"wb") != 0 || !f) return false;
        const uint32_t rowBytes = width * 3;
        const uint32_t rowPadded = (rowBytes + 3u) & ~3u;
        const uint32_t imageSize = rowPadded * height;
#pragma pack(push, 1)
        struct BmpHeader {
            char sig[2]; uint32_t fileSize; uint32_t reserved; uint32_t dataOffset;
            uint32_t infoSize; int32_t width; int32_t height;
            uint16_t planes; uint16_t bpp; uint32_t compression; uint32_t imageSize;
            int32_t xPpm; int32_t yPpm; uint32_t colorsUsed; uint32_t colorsImportant;
        };
#pragma pack(pop)
        BmpHeader h = {};
        h.sig[0] = 'B'; h.sig[1] = 'M';
        h.fileSize = sizeof(h) + imageSize;
        h.dataOffset = sizeof(h);
        h.infoSize = 40;
        h.width = static_cast<int32_t>(width);
        h.height = static_cast<int32_t>(height);
        h.planes = 1;
        h.bpp = 24;
        h.imageSize = imageSize;
        fwrite(&h, 1, sizeof(h), f);
        uint8_t* row = static_cast<uint8_t*>(malloc(rowPadded));
        for (int32_t y = static_cast<int32_t>(height) - 1; y >= 0; --y) {
            const uint8_t* src = data + static_cast<size_t>(y) * rowPitch;
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t* p = src + static_cast<size_t>(x) * 4;
                row[x * 3 + 0] = swapRB ? p[2] : p[0];
                row[x * 3 + 1] = p[1];
                row[x * 3 + 2] = swapRB ? p[0] : p[2];
            }
            memset(row + rowBytes, 0, rowPadded - rowBytes);
            fwrite(row, 1, rowPadded, f);
        }
        free(row);
        fclose(f);
        return true;
    }

    static bool DumpTextureToBmp(ID3D11Device* device, ID3D11DeviceContext* context,
                                 ID3D11Texture2D* tex, const wchar_t* path) {
        D3D11_TEXTURE2D_DESC desc = {};
        tex->GetDesc(&desc);
        bool bgra = true;
        switch (desc.Format) {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            bgra = true; break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            bgra = false; break;
        default:
            LOGSTRF("D3DHooks_VRManager: dump skipped, unsupported fmt %u\n", desc.Format);
            return false;
        }
        D3D11_TEXTURE2D_DESC sd = desc;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        ID3D11Texture2D* staging = nullptr;
        if (FAILED(device->CreateTexture2D(&sd, nullptr, &staging)) || !staging) return false;
        context->CopyResource(staging, tex);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        bool ok = false;
        if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
            ok = WriteBmp24(path, static_cast<const uint8_t*>(mapped.pData),
                            desc.Width, desc.Height, mapped.RowPitch, !bgra);
            context->Unmap(staging, 0);
        }
        staging->Release();
        return ok;
    }

    static void DumpEyeDebugTextures(IDXGISwapChain* pSwapChain) {
        VR::IVRBackend* backend = GetBackend();
        if (!backend || !hmdRenderer) return;
        ID3D11Device* device = backend->GetDevice();
        if (!device) return;
        ID3D11DeviceContext* context = nullptr;
        device->GetImmediateContext(&context);
        if (!context) return;

        wchar_t dir[MAX_PATH];
        if (GetTempPathW(MAX_PATH, dir) == 0) {
            context->Release();
            return;
        }
        wcscat_s(dir, L"gtavr_dump");
        CreateDirectoryW(dir, nullptr);

        static uint32_t dumpIndex = 0;
        dumpIndex++;

        ID3D11Texture2D* back = nullptr;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                            reinterpret_cast<void**>(&back))) && back) {
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%s\\backbuffer_%03u.bmp", dir, dumpIndex);
            DumpTextureToBmp(device, context, back, path);
            back->Release();
        }
        for (int e = 0; e < 2; ++e) {
            ID3D11Texture2D* tex = hmdRenderer->GetEyeTexture(e == 0 ? VR::Eye::Left : VR::Eye::Right);
            if (!tex) continue;
            wchar_t path[MAX_PATH];
            swprintf_s(path, L"%s\\eye_%s_%03u.bmp", dir, e == 0 ? L"L" : L"R", dumpIndex);
            DumpTextureToBmp(device, context, tex, path);
        }
        context->Release();
        LOGSTRF("D3DHooks_VRManager: F12 dump #%u written to %ls\n", dumpIndex, dir);
    }

    // Produce the right eye from the same backbuffer: depth-displaced when
    // useReprojection is set and the shader exists, else a plain blit/copy.
    static void RenderRightEyeStereo(ID3D11Device* device,
                                     ID3D11DeviceContext* context,
                                     ID3D11Texture2D* pBuffer,
                                     bool useReprojection) {
        auto& reproSettings = VR::GetReprojectionSettings();

        ID3D11Texture2D* rightEyeTexture = hmdRenderer->GetEyeTexture(VR::Eye::Right);
        ID3D11RenderTargetView* rtv = hmdRenderer->GetEyeRenderTarget(VR::Eye::Right);
        context->OMSetRenderTargets(1, &rtv, nullptr);

        bool using_cached_srv = false;
        ID3D11ShaderResourceView* color_srv = GetColorSRV(device, context, pBuffer, using_cached_srv);

        if (useReprojection && reprojection_pixel_shader_) {
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
                RenderBlit(device, context, pBuffer, rtv, 0.0f, false);
            } else {
                context->CopyResource(rightEyeTexture, pBuffer);
            }
        }

        if (color_srv && !using_cached_srv) {
            color_srv->Release();
        }
    }

    // OpenVR overlay input + render + recenter plumbing (no-op on OpenXR).
    static void UpdateOverlayOpenVRAndRecenter() {
        if (using_openvr) {
            VR::IVRBackend* backend = GetBackend();
            if (!backend) {
                return;
            }
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
    }

    static void ApplySnapTurningFromBackend(VR::IVRBackend* backend) {
        ApplyTurningAndComfortInput(backend);
    }

    // Phase 6 HUD ops (wired into Stereo::FrameServices): composite the
    // offscreen HUD target onto one eye target, and finish the HUD frame
    // (clear the target + re-cache the backbuffer for the substitution hook).
    static void CompositeHudOntoEye(ID3D11Device* device, ID3D11DeviceContext* context, VR::Eye eye) {
        if (!Hud::GetState().hooksInstalled || !hmdRenderer) {
            return;
        }
        Hud::Composite(device, context, hmdRenderer->GetEyeRenderTarget(eye));
    }

    static void FinishHudFrameOp(ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer) {
        if (!Hud::GetState().hooksInstalled) {
            return;
        }
        Hud::FinishFrame(context, backbuffer);
    }

    // Loads the [hud] registry from the build manifest (once) and installs
    // the identification/substitution hooks on the shared d3d11
    // implementation. Called at every early device-sighting point (creation
    // proxies, swapchain hook) so it predates the game's shader compilation;
    // no-op unless the manifest sets [hud] enabled=1.
    static void EnsureHudIdentification(ID3D11Device* device) {
        if (!device || Hud::GetState().hooksInstalled) {
            return;
        }
        static std::mutex hud_init_mutex;
        std::lock_guard<std::mutex> lock(hud_init_mutex);
        if (Hud::GetState().hooksInstalled) {
            return;
        }

        static bool registry_loaded = false;
        if (!registry_loaded) {
            registry_loaded = true;
            Game::BuildManifest& manifest = Game::BuildManifest::Get();
            manifest.Initialize();  // idempotent
            std::wstring widePath = manifest.GetManifestPath();
            char path[MAX_PATH * 2] = {};
            if (!widePath.empty()) {
                WideCharToMultiByte(CP_UTF8, 0, widePath.c_str(), -1, path, sizeof(path), nullptr, nullptr);
            }
            Hud::LoadRegistry(path);
        }
        Hud::EnsureHooksInstalled(device);
    }

    // gtavr_settings.ini [Comfort] keys that have no slot in
    // VR::SharedSettings (owned by the VR layer): vehicleHorizonLock and
    // smoothTurnSpeed. Loaded directly here so they apply on BOTH runtimes at
    // startup; on the OpenVR path the overlay additionally live-applies them
    // via ApplyOverlaySettingsOpenVR. Path resolution matches
    // XROverlayUI::ResolveSettingsPath (SETTINGS_DIR-if-file-exists, then
    // SETTINGS_PATH, then plain filename).
    static void LoadComfortRuntimeFromIni() {
        char path[MAX_PATH] = {};
        char dir[MAX_PATH] = {};
        DWORD len = GetEnvironmentVariableA("GTAVR_SETTINGS_DIR", dir, MAX_PATH);
        bool resolved = false;
        if (len > 0 && len < MAX_PATH) {
            snprintf(path, sizeof(path), "%s\\gtavr_settings.ini", dir);
            DWORD attrs = GetFileAttributesA(path);
            resolved = (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
        }
        if (!resolved) {
            len = GetEnvironmentVariableA("GTAVR_SETTINGS_PATH", path, MAX_PATH);
            resolved = (len > 0 && len < MAX_PATH);
        }
        if (!resolved) {
            strncpy_s(path, sizeof(path), "gtavr_settings.ini", _TRUNCATE);
        }

        auto& runtime = Stereo::GetComfortRuntime();
        int horizonLock = GetPrivateProfileIntA("Comfort", "vehicleHorizonLock", -1, path);
        if (horizonLock >= 0) {
            runtime.vehicleHorizonLock.store(horizonLock != 0);
        }
        char value[64] = {};
        if (GetPrivateProfileStringA("Comfort", "smoothTurnSpeed", "", value, sizeof(value), path) > 0) {
            float speed = 0.0f;
            if (sscanf_s(value, "%f", &speed) == 1 && speed >= 10.0f && speed <= 720.0f) {
                runtime.smoothTurnSpeedDeg.store(speed);
            }
        }
        LOGSTRF("D3DHooks_VRManager: Comfort runtime (ini): vehicleHorizonLock=%d smoothTurnSpeed=%.0f deg/s\n",
                runtime.vehicleHorizonLock.load() ? 1 : 0,
                static_cast<double>(runtime.smoothTurnSpeedDeg.load()));
    }

    void ShutdownVR();
    void ReleaseVRResources();

    /**
     * First-present VR initialization (extracted from hookedPresent so every
     * failure path can bail out cleanly). Returns false on hard failure; the
     * caller then marks the mod inert and passes frames through untouched.
     * NOTE: the legacy body below keeps its original deep indentation.
     */
    static bool InitializeOnFirstPresent(IDXGISwapChain* pSwapChain) {
        LOGSTR("D3DHooks_VRManager: First Present - Initializing VR...\n");

        // OnlineGuard kill-switch wiring: install detectors once here; the
        // per-frame verdict poll lives at the top of hookedPresent.
        Game::OnlineGuard::Get().Initialize();

        // ScriptHookV bridge (camera natives via the game's main thread).
        // Best-effort: without it the old matrix-write path stays in use.
        Game::ShvNatives::Get().Initialize();

        // Boot summary: one structured block capturing the environment, so a
        // single log file answers "what was wrong" without guesswork.
        {
            char procPath[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, procPath, MAX_PATH);
            const char* procName = strrchr(procPath, '\\');
            procName = procName ? procName + 1 : procPath;
            LOGSTR("=================== GTAVR session summary ===================\n");
            LOGSTRF("  mod build: %s %s | host: %s (pid %lu)\n",
                    __DATE__, __TIME__, procName, GetCurrentProcessId());
            LOGSTRF("  log file: %s | verbose: %s\n", LOGGetPath(), LOG_IsVerbose() ? "on" : "off");

            Game::BuildManifest& bootManifest = Game::BuildManifest::Get();
            bootManifest.Initialize(); // idempotent; logs its own detect lines
            const Game::BuildInfo& bi = bootManifest.GetBuildInfo();
            if (bootManifest.IsBuildSupported()) {
                LOGSTRF("  game build: %s (%s) -> manifest section [%s]%s\n",
                        bi.fileVersion.c_str(), bi.moduleName.c_str(), bi.section.c_str(),
                        bi.verified ? " (verified)" : " (UNVERIFIED values)");
            } else {
                LOGSTRF("  game build: %s (%s, size 0x%llx) -> NO manifest section - "
                        "camera patterns unavailable, mod runs WITHOUT camera control\n",
                        bi.fileVersion.c_str(), bi.moduleName.c_str(),
                        static_cast<unsigned long long>(bi.moduleSize));
            }
        }

        // Settings key (default: preserve the game's sync interval).
        force_mirror_sync0_ = ReadDesktopMirrorSyncOverride();
        if (force_mirror_sync0_) {
            LOGSTR("D3DHooks_VRManager: desktopMirrorSyncOverride enabled - desktop mirror Present will use sync interval 0\n");
        }

        // Resolve the game window from the swapchain (never trust hardcoded
        // window class names); used for the minimized/alt-tab pass-through.
        DXGI_SWAP_CHAIN_DESC windowDesc = {};
        if (SUCCEEDED(pSwapChain->GetDesc(&windowDesc))) {
            game_window_ = windowDesc.OutputWindow;
        }

        // WndProc hook: while the settings overlay is visible, swallow
        // game-bound input (raw input, keys, mouse) so menu interactions do
        // not leak into the game. GTA V reads input via WM_INPUT, so that
        // message must be swallowed too - swallowing keys alone is not enough.
        if (game_window_ && !original_wndproc_) {
            original_wndproc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(
                game_window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GameWndProc)));
            LOGSTRF("D3DHooks_VRManager: WndProc hook installed on %p (input blocked while overlay visible)\n",
                    game_window_);
        }

        ID3D11Device* device = nullptr;
        if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device)) || !device) {
            LOGSTR("D3DHooks_VRManager: Failed to get D3D11 device from swapchain.\n");
            return false;
        }

        {
            IDXGIDevice* dxgiDevice = nullptr;
            if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice)) && dxgiDevice) {
                IDXGIAdapter* adapter = nullptr;
                if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter) {
                    DXGI_ADAPTER_DESC ad = {};
                    adapter->GetDesc(&ad);
                    LOGSTRF("  gpu: %ls (vendor 0x%04x device 0x%04x, %.0f MB dedicated)\n",
                            ad.Description, ad.VendorId, ad.DeviceId,
                            ad.DedicatedVideoMemory / 1048576.0);
                    adapter->Release();
                }
                dxgiDevice->Release();
            }
        }

        // Latency (LukeRoss R.E.A.L. does the same): cap the DXGI frame
        // latency at 1 so the game's present queue cannot run ahead of the
        // VR camera write - less head-motion-to-photon lag when turning.
        {
            IDXGIDevice1* dxgiDevice1 = nullptr;
            if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice1),
                                                 reinterpret_cast<void**>(&dxgiDevice1))) && dxgiDevice1) {
                dxgiDevice1->SetMaximumFrameLatency(1);
                LOGSTR("D3DHooks_VRManager: DXGI maximum frame latency set to 1\n");
                dxgiDevice1->Release();
            }
        }

        // Phase 5/6: comfort runtime keys + HUD identification (both no-op
        // unless enabled in config/manifest).
        LoadComfortRuntimeFromIni();
        EnsureHudIdentification(device);

        ID3D11DeviceContext* context = nullptr;
        device->GetImmediateContext(&context);
        if (!context) {
            LOGSTR("D3DHooks_VRManager: Failed to get immediate context.\n");
            device->Release();
            return false;
        }

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

                // Scripted VR camera (bridge channel). Constructed even when
                // the bridge is absent - IsAvailable() just stays false.
                vrCamera = new Game::VRCamera(backend);

                // StereoEngine: resolve clip planes, cache the per-eye runtime
                // projection, sync the runtime IPD into shared settings.
                Stereo::StereoEngine::Get().Initialize(backend);

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

            // Create shaders (the precompiled g_pStereoShader/g_pVertexShader
            // blobs were dead code - everything is runtime-compiled now)
            if (!EnsureFullscreenVertexShader(device)) {
                LOGSTR("D3DHooks_VRManager: Failed to create fullscreen vertex shader.\n");
                ReleaseVRResources();
                return false;
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
                ReleaseVRResources();
                return false;
            }
            bd.ByteWidth = sizeof(ReprojectionParams);
            if (FAILED(device->CreateBuffer(&bd, nullptr, &reprojection_constant_buffer_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create reprojection constant buffer.\n");
                ReleaseVRResources();
                return false;
            }
            bd.ByteWidth = sizeof(VignetteParams);
            if (FAILED(device->CreateBuffer(&bd, nullptr, &vignette_constant_buffer_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create vignette constant buffer.\n");
                ReleaseVRResources();
                return false;
            }
            bd.ByteWidth = sizeof(BlitParams);
            if (FAILED(device->CreateBuffer(&bd, nullptr, &blit_constant_buffer_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create blit constant buffer.\n");
                ReleaseVRResources();
                return false;
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
                ReleaseVRResources();
                return false;
            }

            // Create fixed-function states for blit/reprojection
            D3D11_BLEND_DESC blendDesc = {};
            blendDesc.RenderTarget[0].BlendEnable = FALSE;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(device->CreateBlendState(&blendDesc, &blit_blend_state_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create blend state.\n");
                ReleaseVRResources();
                return false;
            }

            D3D11_DEPTH_STENCIL_DESC depthDesc = {};
            depthDesc.DepthEnable = FALSE;
            depthDesc.StencilEnable = FALSE;
            if (FAILED(device->CreateDepthStencilState(&depthDesc, &blit_depth_state_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create depth state.\n");
                ReleaseVRResources();
                return false;
            }

            D3D11_RASTERIZER_DESC rasterDesc = {};
            rasterDesc.FillMode = D3D11_FILL_SOLID;
            rasterDesc.CullMode = D3D11_CULL_NONE;
            rasterDesc.DepthClipEnable = TRUE;
            rasterDesc.ScissorEnable = FALSE;
            if (FAILED(device->CreateRasterizerState(&rasterDesc, &blit_raster_state_))) {
                LOGSTR("D3DHooks_VRManager: Failed to create rasterizer state.\n");
                ReleaseVRResources();
                return false;
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
                ReleaseVRResources();
                return false;
            }

            // Input layout not needed for fullscreen vertex shader

            {
                VR::IVRBackend* activeBackend = vrManager ? vrManager->GetBackend() : nullptr;
                const auto& stereo = VR::GetStereoSettings();
                int mode = stereo.mode.load();
                const char* modeName = (mode == 1) ? "alternate-eye" : (mode == 2) ? "dual-pass(experimental)" : "reprojection(Z3D)";
                if (activeBackend) {
                    LOGSTRF("=== GTAVR MOD ACTIVE === runtime=%s stereo=%s camera=%s\n",
                            vrManager->GetActiveRuntimeName(), modeName,
                            VR::GetRuntimeStats().cameraHookReady.load() ? "hooked" : "NOT resolved (see GtaCameraHook lines)");
                } else {
                    LOGSTR("=== GTAVR MOD INERT === VR backend unavailable (runtime not detected?) - pass-through mode\n");
                }
            }

            // EXPERIMENTAL (env GTAVR_BACKBUFFER_SCALE or %TEMP% file of the
            // same name, e.g. 1.5 / 2.0): force the game swapchain larger than
            // the desktop window so the 3D render happens at a higher
            // resolution (VR sharpness lever). The %TEMP% file fallback exists
            // because GTA5 inherits its environment from the Rockstar service
            // (user-level env vars never reach it). UNVERIFIED whether RAGE
            // renders bigger or just letterboxes - check with an F12 dump.
            {
                float bbScale = 0.0f;
                char scaleBuf[16] = {};
                DWORD scaleLen = GetEnvironmentVariableA("GTAVR_BACKBUFFER_SCALE", scaleBuf, sizeof(scaleBuf));
                if (scaleLen > 0) bbScale = static_cast<float>(atof(scaleBuf));
                if (bbScale <= 0.0f) {
                    char tmpPath[MAX_PATH];
                    if (GetTempPathA(MAX_PATH, tmpPath) > 0) {
                        strncat_s(tmpPath, "gtavr_backbuffer_scale.txt", _TRUNCATE);
                        FILE* sf = nullptr;
                        if (fopen_s(&sf, tmpPath, "r") == 0 && sf) {
                            if (fgets(scaleBuf, sizeof(scaleBuf), sf)) {
                                bbScale = static_cast<float>(atof(scaleBuf));
                            }
                            fclose(sf);
                        }
                    }
                }
                if (bbScale > 1.01f) {
                    DXGI_SWAP_CHAIN_DESC scDesc = {};
                    if (SUCCEEDED(pSwapChain->GetDesc(&scDesc))) {
                        UINT newW = static_cast<UINT>(scDesc.BufferDesc.Width * bbScale + 0.5f);
                        UINT newH = static_cast<UINT>(scDesc.BufferDesc.Height * bbScale + 0.5f);
                        LOGSTRF("D3DHooks_VRManager: GTAVR_BACKBUFFER_SCALE=%.2f - forcing backbuffer %ux%u -> %ux%u (EXPERIMENTAL)\n",
                                bbScale, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height, newW, newH);
                        HRESULT rbHr = pSwapChain->ResizeBuffers(0, newW, newH,
                                                                 DXGI_FORMAT_UNKNOWN, 0);
                        LOGSTRF("D3DHooks_VRManager: forced ResizeBuffers -> 0x%08lX\n",
                                static_cast<long>(rbHr));
                    }
                }
            }

            device->Release();
            context->Release();
            return true;
    }

    /**
     * Lazy recreate after a swapchain resize. The ResizeBuffers hook already
     * released the eye targets and swapchain-sized resources; recreate the
     * renderer here, on the render thread, before any VR work this frame.
     */
    static void RecreateAfterResize(IDXGISwapChain* pSwapChain) {
        VR::IVRBackend* backend = GetBackend();
        if (!backend || !backend->GetDevice()) {
            LOGWNDF("D3DHooks_VRManager: Resize recreate skipped - no VR backend/device\n");
            return;
        }

        ID3D11DeviceContext* context = nullptr;
        backend->GetDevice()->GetImmediateContext(&context);

        float renderScale = VR::GetReprojectionSettings().renderScale.load();
        if (renderScale < 0.5f) renderScale = 0.5f;
        if (renderScale > 2.0f) renderScale = 2.0f;

        hmdRenderer = new HMDRenderer(pSwapChain, backend, renderScale, backend->GetDevice(), context);
        if (context) {
            context->Release();
        }
        LOGSTR("D3DHooks_VRManager: Recreated VR render targets after swapchain resize\n");
    }

    /**
     * DXGI_ERROR_DEVICE_REMOVED / DEVICE_RESET handling: release every
     * device-derived resource (never touch the stale device/context again),
     * shut down the XR backend, and allow exactly one clean re-init attempt
     * on the next frame. No MinHook calls here - hooks stay installed.
     */
    static void HandleDeviceLost(HRESULT hr) {
        if (!device_lost_logged_) {
            LOGWNDF("D3DHooks_VRManager: Present returned %s - releasing VR resources\n",
                    hr == DXGI_ERROR_DEVICE_REMOVED ? "DXGI_ERROR_DEVICE_REMOVED"
                                                    : "DXGI_ERROR_DEVICE_RESET");
            device_lost_logged_ = true;
        }
        ReleaseVRResources();
        if (device_lost_reinit_remaining_ > 0) {
            --device_lost_reinit_remaining_;
            first_present = true; // one clean re-init attempt on the next frame
            LOGSTR("D3DHooks_VRManager: Will attempt one clean VR re-init on the next frame\n");
        } else {
            vr_disabled_inert_.store(true);
            LOGWNDF("D3DHooks_VRManager: Device-lost re-init budget exhausted - VR disabled, passing through\n");
        }
    }

    /**
     * Hooked Present function - main VR entry point
     */
    HRESULT __stdcall hookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        // Ordered-unload interlock: count in-flight frames so the detach path
        // can drain us before tearing anything down.
        in_hook_count_.fetch_add(1);
        struct HookScopeGuard {
            ~HookScopeGuard() { in_hook_count_.fetch_sub(1); }
        } hookScopeGuard;

        // Phase 8 instrumentation: every hooked Present is recorded, including
        // pass-through frames. F11 = on-demand CSV export (the mod installs no
        // WndProc hook, so the hotkey is polled here on the render thread).
        Perf::PerfStats::Get().Record();
        static bool f11_was_down = false;
        bool f11_down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        if (f11_down && !f11_was_down) {
            Perf::PerfStats::Get().ExportCsv();
        }
        f11_was_down = f11_down;

        // Ordered unload in progress: pure pass-through.
        if (shutting_down_.load()) {
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

        // OnlineGuard kill-switch: polled EVERY frame before any VR/camera
        // work. When the guard fires, present untouched - no camera writes,
        // no XR submit, no overlay.
        if (Game::OnlineGuard::Get().ShouldDisableMod()) {
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

        // Hard failure earlier (init or exhausted re-init): stay inert.
        if (vr_disabled_inert_.load()) {
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

        // Late-injection dummy device: tagged at init, never presented by us
        // and never submitted to the XR pipeline. Pass straight through if it
        // ever shows up here (defensive - it should not; we never call
        // Present on it). This check sits BEFORE the first_present init so
        // InitializeOnFirstPresent can only bind to the game's real
        // swapchain (the first non-dummy one seen here).
        if (pSwapChain == dummy_swapchain_.load(std::memory_order_acquire)) {
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

        if (first_present) {
            if (late_injection_dummy_hooked_) {
                LOGSTR("D3DHooks_VRManager: hooked into existing game swapchain (late injection)\n");
            }
            if (!InitializeOnFirstPresent(pSwapChain)) {
                vr_disabled_inert_.store(true);
                return Original_PresentHook(pSwapChain, SyncInterval, Flags);
            }
            first_present = false;
            device_lost_logged_ = false;
            // Frame 0 is presented like any other frame (never swallowed).
        }

        // A ResizeBuffers arrived since the last frame: recreate targets now,
        // before any VR work, still on the render thread.
        if (resize_pending_.exchange(false)) {
            RecreateAfterResize(pSwapChain);
        }

        // Alt-tab / minimized: VR submission paused, pass through untouched.
        if (game_window_ && IsIconic(game_window_)) {
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

        // Get the active VR backend
        VR::IVRBackend* backend = GetBackend();
        if (!backend || !hmdRenderer) {
            // No VR (or targets not (re)created yet) - just call original Present
            return Original_PresentHook(pSwapChain, SyncInterval, Flags);
        }

        // Phase 4: the stereo frame sequence (BeginFrame -> late-latch head
        // pose -> mode policy -> produce eye(s) -> submit -> camera write ->
        // EndFrame -> desktop mirror) lives in StereoEngine::OnPresent. This
        // hook keeps the guard rails above (OnlineGuard, inert, init, resize,
        // minimized) and the device-lost handling below; the D3D mechanics
        // stay here as statics and are passed in as ops.
        Stereo::FrameServices services;
        services.backend = backend;
        services.hmdRenderer = hmdRenderer;
        services.cameraHook = cameraHook;
        services.vrCamera = vrCamera;
        services.cameraFov = cameraFov.get();
        services.gameState = gameState.get();
        services.virtualScreen = virtualScreen.get();
        services.usingOpenVR = using_openvr;
        services.forceMirrorSync0 = force_mirror_sync0_;
        services.originalPresent = Original_PresentHook;
        services.applySnapTurning = &ApplySnapTurningFromBackend;
        services.maybeResizeSwapchain = &MaybeResizeSwapchain;
        services.updateVignette = &UpdateVignetteParams;
        services.compositeHud = &CompositeHudOntoEye;
        services.finishHudFrame = &FinishHudFrameOp;
        services.prepareDepth = &PrepareDepthForReprojection;
        services.produceEye = &ProduceEyeFromBackbuffer;
        services.renderRightEye = &RenderRightEyeStereo;
        services.updateOverlay = &UpdateOverlayOpenVRAndRecenter;

        HRESULT result = Stereo::StereoEngine::Get().OnPresent(pSwapChain, SyncInterval, Flags, services);

        // F12 debug dump: save the backbuffer + both eye textures as BMPs so
        // the submitted image can be inspected without a headset ("see what
        // the user sees"). Edge-triggered, one dump per keypress.
        {
            static bool prevF12 = false;
            const bool f12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
            if (f12Down && !prevF12) {
                DumpEyeDebugTextures(pSwapChain);
            }
            prevF12 = f12Down;
        }

        // Device-lost/removed: log once, release everything, allow one clean
        // re-init on the next frame; the frame itself passes through.
        if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
            HandleDeviceLost(result);
        }
        // Occlusion/fullscreen-transition statuses (e.g. DXGI_STATUS_OCCLUDED)
        // are not errors: submission is simply paused for that frame.

        return result;
    }

    /**
     * Hooked IDXGISwapChain::ResizeBuffers (vtable index 13).
     *
     * The game calls this on the render thread for resolution changes and
     * fullscreen transitions. We release our eye targets and every cached
     * resource sized against the old backbuffer BEFORE the original resize
     * runs, then mark targets for lazy recreation on the next Present.
     * (All released objects are independent allocations - none reference the
     * swapchain's own buffer - so the original ResizeBuffers is not blocked
     * by us. Overlay/ImGui holds only device+context refs, which survive a
     * buffer resize unchanged.)
     */
    HRESULT __stdcall hookedResizeBuffers(IDXGISwapChain* pSwapChain,
                                          UINT BufferCount, UINT Width, UINT Height,
                                          DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        in_hook_count_.fetch_add(1);
        struct HookScopeGuard {
            ~HookScopeGuard() { in_hook_count_.fetch_sub(1); }
        } hookScopeGuard;

        if (shutting_down_.load() || !Original_ResizeBuffersHook) {
            return Original_ResizeBuffersHook
                       ? Original_ResizeBuffersHook(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags)
                       : E_FAIL;
        }

        if (hmdRenderer) {
            delete hmdRenderer;
            hmdRenderer = nullptr;
        }
        ReleaseSwapchainSizedResources();

        HRESULT hr = Original_ResizeBuffersHook(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        // Recreate on next Present even on failure: the eye targets are gone
        // either way and must come back before VR rendering continues.
        resize_pending_.store(true);
        if (SUCCEEDED(hr)) {
            LOGSTRF("D3DHooks_VRManager: ResizeBuffers(%ux%u fmt=%u) - VR targets recreate on next Present\n",
                    Width, Height, static_cast<unsigned>(NewFormat));
        } else {
            LOGWNDF("D3DHooks_VRManager: ResizeBuffers failed (hr=0x%08lx) - VR targets recreate on next Present\n",
                    static_cast<unsigned long>(hr));
        }
        return hr;
    }

    /**
     * Install the Present + ResizeBuffers hooks on a swapchain's vtable.
     * MinHook patches the target function's code, and COM objects created by
     * the same DXGI implementation share one function body, so hooking the
     * vtable entry of one swapchain effectively covers every swapchain on the
     * system: ones the game creates later (including a device recreated after
     * device-lost) AND, via the late-injection dummy device, ones that
     * already existed before we were injected.
     * Idempotent: only the first swapchain wins; later ones are ignored.
     */
    void InstallSwapChainHooks(IDXGISwapChain* pSwapChain, const char* origin) {
        if (!pSwapChain) {
            return;
        }
        // Creation proxies can fire on any thread (the game's render thread,
        // or our init thread running the late-injection dummy creation);
        // serialize so two simultaneous swapchains cannot race into a double
        // MH_CreateHook on the same target (the loser would MH_RemoveHook the
        // winner's live hook).
        static std::mutex install_mutex;
        std::lock_guard<std::mutex> install_lock(install_mutex);
        if (present_hook_installed) {
            LOGSTRF("D3DHooks_VRManager: Ignoring additional swap chain (%s) after primary Present hook\n", origin);
            return;
        }

        void** vtable = *reinterpret_cast<void***>(pSwapChain);
        const int presentIndex = 8;       // IDXGISwapChain::Present
        const int resizeBuffersIndex = 13; // IDXGISwapChain::ResizeBuffers

        void* presentTarget = vtable[presentIndex];
        void* resizeTarget = vtable[resizeBuffersIndex];

        if (MH_CreateHook(presentTarget, hookedPresent, (void**)&Original_PresentHook) != MH_OK ||
            MH_EnableHook(presentTarget) != MH_OK) {
            MH_RemoveHook(presentTarget);
            LOGSTR("D3DHooks_VRManager: Failed to hook Present - mod stays inert\n");
            return;
        }
        present_hook_target_ = presentTarget;
        present_hook_installed = true;
        hooked_swapchain_ = pSwapChain;
        LOGSTRF("D3DHooks_VRManager: Hooked Present at vtable index %d (via %s)\n", presentIndex, origin);

        if (MH_CreateHook(resizeTarget, hookedResizeBuffers, (void**)&Original_ResizeBuffersHook) == MH_OK &&
            MH_EnableHook(resizeTarget) == MH_OK) {
            resize_buffers_hook_target_ = resizeTarget;
            LOGSTR("D3DHooks_VRManager: Hooked ResizeBuffers at vtable index 13\n");
        } else {
            LOGWNDF("D3DHooks_VRManager: Failed to hook ResizeBuffers - resize handling degraded\n");
        }

        if (create_device_and_swapchain_target && !create_hook_disabled) {
            if (MH_DisableHook(create_device_and_swapchain_target) == MH_OK) {
                create_hook_disabled = true;
                LOGSTR("D3DHooks_VRManager: Disabled D3D11CreateDeviceAndSwapChain hook after primary Present hook\n");
            }
        }
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

        // EXPERIMENTAL game-render-scale override (%TEMP%\gtavr_backbuffer_
        // scale.txt, e.g. 1.5): multiply the swapchain backbuffer size at
        // creation time - the only point where an external override is legal
        // (post-creation ResizeBuffers is rejected by DXGI). Requires the
        // dxgi shim so this proxy runs before the game's device creation.
        // UNVERIFIED whether RAGE renders bigger into the larger buffer.
        DXGI_SWAP_CHAIN_DESC scaledDesc = {};
        if (pSwapChainDesc) {
            char tmpPath[MAX_PATH];
            float bbScale = 0.0f;
            if (GetTempPathA(MAX_PATH, tmpPath) > 0) {
                strncat_s(tmpPath, "gtavr_backbuffer_scale.txt", _TRUNCATE);
                FILE* sf = nullptr;
                if (fopen_s(&sf, tmpPath, "r") == 0 && sf) {
                    char buf[16] = {};
                    if (fgets(buf, sizeof(buf), sf)) {
                        bbScale = static_cast<float>(atof(buf));
                    }
                    fclose(sf);
                }
            }
            if (bbScale > 1.01f) {
                scaledDesc = *pSwapChainDesc;
                scaledDesc.BufferDesc.Width = static_cast<UINT>(pSwapChainDesc->BufferDesc.Width * bbScale + 0.5f);
                scaledDesc.BufferDesc.Height = static_cast<UINT>(pSwapChainDesc->BufferDesc.Height * bbScale + 0.5f);
                LOGSTRF("D3DHooks_VRManager: game render scale %.2f - swapchain %ux%u -> %ux%u (EXPERIMENTAL)\n",
                        bbScale,
                        pSwapChainDesc->BufferDesc.Width, pSwapChainDesc->BufferDesc.Height,
                        scaledDesc.BufferDesc.Width, scaledDesc.BufferDesc.Height);
                pSwapChainDesc = &scaledDesc;
            }
        }

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
            InstallSwapChainHooks(*ppSwapChain, "D3D11CreateDeviceAndSwapChain");
        }
        if (SUCCEEDED(result) && ppDevice && *ppDevice) {
            EnsureHudIdentification(*ppDevice);
        }

        return result;
    }

    /**
     * Hooked IDXGIFactory::CreateSwapChain (vtable index 10) - the secondary
     * entry point for games that create their device and swapchain separately
     * (and the practical late-injection fallback when the primary
     * D3D11CreateDeviceAndSwapChain never fires for us).
     */
    HRESULT __stdcall hookedCreateSwapChain(IDXGIFactory* pFactory,
                                            IUnknown* pDevice,
                                            DXGI_SWAP_CHAIN_DESC* pDesc,
                                            IDXGISwapChain** ppSwapChain) {
        // Same game-render-scale override as Proxy_D3D11CreateDeviceAndSwap
        // Chain (games that create device+swapchain separately take this
        // path). Guard against double-scaling when both hooks fire for the
        // same creation: the D3D11 wrapper hook already scaled the desc.
        if (pDesc) {
            static uint32_t lastScaledW = 0;
            static uint32_t lastScaledH = 0;
            char tmpPath[MAX_PATH];
            float bbScale = 0.0f;
            if (GetTempPathA(MAX_PATH, tmpPath) > 0) {
                strncat_s(tmpPath, "gtavr_backbuffer_scale.txt", _TRUNCATE);
                FILE* sf = nullptr;
                if (fopen_s(&sf, tmpPath, "r") == 0 && sf) {
                    char buf[16] = {};
                    if (fgets(buf, sizeof(buf), sf)) {
                        bbScale = static_cast<float>(atof(buf));
                    }
                    fclose(sf);
                }
            }
            if (bbScale > 1.01f &&
                !(pDesc->BufferDesc.Width == lastScaledW && pDesc->BufferDesc.Height == lastScaledH)) {
                uint32_t origW = pDesc->BufferDesc.Width;
                uint32_t origH = pDesc->BufferDesc.Height;
                pDesc->BufferDesc.Width = static_cast<UINT>(origW * bbScale + 0.5f);
                pDesc->BufferDesc.Height = static_cast<UINT>(origH * bbScale + 0.5f);
                lastScaledW = pDesc->BufferDesc.Width;
                lastScaledH = pDesc->BufferDesc.Height;
                LOGSTRF("D3DHooks_VRManager: game render scale %.2f - CreateSwapChain %ux%u -> %ux%u (EXPERIMENTAL)\n",
                        bbScale, origW, origH, lastScaledW, lastScaledH);
            }
        }
        HRESULT result = Original_CreateSwapChainHook(pFactory, pDevice, pDesc, ppSwapChain);
        if (SUCCEEDED(result) && ppSwapChain && *ppSwapChain && pDevice) {
            // Only D3D11-device swapchains are interesting; anything else
            // (D3D12, video, other runtimes) is left alone.
            ID3D11Device* device11 = nullptr;
            if (SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D11Device), (void**)&device11)) && device11) {
                EnsureHudIdentification(device11);
                device11->Release();
                InstallSwapChainHooks(*ppSwapChain, "IDXGIFactory::CreateSwapChain");
            }
        }
        return result;
    }

    // Hooks IDXGIFactory::CreateSwapChain (vtable[10]) once, using any live
    // factory object. All factories from the same dxgi.dll share the
    // implementation, so one hook covers every factory.
    static void HookFactoryCreateSwapChain(IUnknown* factory) {
        if (!factory || create_swapchain_target_) {
            return;
        }
        void** vtable = *reinterpret_cast<void***>(factory);
        void* target = vtable[10]; // IDXGIFactory::CreateSwapChain
        if (MH_CreateHook(target, hookedCreateSwapChain, (void**)&Original_CreateSwapChainHook) == MH_OK &&
            MH_EnableHook(target) == MH_OK) {
            create_swapchain_target_ = target;
            LOGSTR("D3DHooks_VRManager: Hooked IDXGIFactory::CreateSwapChain at vtable index 10\n");
        } else {
            MH_RemoveHook(target);
            LOGWNDF("D3DHooks_VRManager: Failed to hook IDXGIFactory::CreateSwapChain\n");
        }
    }

    HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
        HRESULT result = Original_CreateDXGIFactory1(riid, ppFactory);
        if (SUCCEEDED(result) && ppFactory && *ppFactory) {
            HookFactoryCreateSwapChain(static_cast<IUnknown*>(*ppFactory));
        }
        return result;
    }

    HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) {
        HRESULT result = Original_CreateDXGIFactory2(flags, riid, ppFactory);
        if (SUCCEEDED(result) && ppFactory && *ppFactory) {
            HookFactoryCreateSwapChain(static_cast<IUnknown*>(*ppFactory));
        }
        return result;
    }

    // d3d11!D3D11CreateDevice creates no swapchain itself; hooked as a
    // canary so the split creation path is visible in the log.
    HRESULT WINAPI Proxy_D3D11CreateDevice(
        _In_opt_  IDXGIAdapter         *pAdapter,
        D3D_DRIVER_TYPE     DriverType,
        HMODULE             Software,
        UINT                Flags,
        _In_opt_ const  D3D_FEATURE_LEVEL    *pFeatureLevels,
        UINT                FeatureLevels,
        UINT                SDKVersion,
        _Out_opt_       ID3D11Device         **ppDevice,
        _Out_opt_       D3D_FEATURE_LEVEL    *pFeatureLevel,
        _Out_opt_       ID3D11DeviceContext  **ppImmediateContext
    ) {
        HRESULT result = ((PFN_D3D11_CREATE_DEVICE)Original_D3D11CreateDevice)(
            pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
            SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
        if (SUCCEEDED(result) && !present_hook_installed) {
            LOGSTR("D3DHooks_VRManager: D3D11CreateDevice called (split creation path) - swapchain hook will arrive via CreateSwapChain\n");
        }
        if (SUCCEEDED(result) && ppDevice && *ppDevice) {
            EnsureHudIdentification(*ppDevice);
        }
        return result;
    }

    /**
     * Release every VR/D3D resource we own. Null-safe and idempotent; never
     * touches MinHook, so it is safe to call on any non-hook path (init
     * failure cleanup, device-lost, ordered unload).
     */
    void ReleaseVRResources() {
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
        Hud::ReleaseResources();

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
        if (vrCamera) {
            delete vrCamera;  // destructor releases the scripted cam
            vrCamera = nullptr;
        }

        if (vrManager && vrManager->IsInitialized()) {
            vrManager->Shutdown();
        }
    }

    /**
     * Shutdown VR - idempotent. Releases all VR/D3D resources and resets
     * frame state. Deliberately contains NO MinHook calls: hook teardown only
     * happens on the clean DLL_PROCESS_DETACH path (UninstallHooks), never
     * from inside a hooked frame.
     */
    void ShutdownVR() {
        // Phase 8: flush + export gtavr_perf.csv before teardown. Idempotent;
        // also reachable on demand via the F11 hotkey.
        Perf::PerfStats::Get().Shutdown();
        ReleaseVRResources();          // deletes vrCamera (releases scripted cam ops)
        Game::ShvNatives::Get().Shutdown();
        overlay_visible = false;
        using_openvr = false;
    }

    /**
     * Late-injection fallback (Kiero dummy-device method).
     *
     * When the mod is injected into an ALREADY-RUNNING game, the game's
     * device + swapchain + DXGI factory all predate our hooks and none of the
     * creation-path proxies ever fires again. IDXGISwapChain vtables are
     * per-D3D11-driver, identical for every swapchain on the system, so we
     * synchronously create a HIDDEN dummy device+swapchain (offscreen 8x8,
     * never presented) through the hooked D3D11CreateDeviceAndSwapChain
     * export: the call lands in our own Proxy, which runs the existing
     * InstallSwapChainHooks path on the dummy's vtable -> Present and
     * ResizeBuffers become hooked globally, INCLUDING the game's pre-existing
     * swapchain (same vtable addresses).
     *
     * The dummy never reaches the XR pipeline: we never call Present on it,
     * and hookedPresent additionally skips the tagged pointer defensively, so
     * InitializeOnFirstPresent binds to the first NON-dummy (game) swapchain.
     * The swapchain (which keeps the dummy device alive) and its hidden
     * window stay referenced until UninstallHooks so the tag address can
     * never be recycled into a real swapchain. No-op if Present is already
     * hooked. Called from Initialize() on the init thread - never from
     * DllMain (window + driver creation would take the loader lock).
     */
    static void CreateDummyDeviceAndHookPresent() {
        if (present_hook_installed) {
            return; // a live swapchain already landed the hook
        }

        HMODULE d3d11 = GetModuleHandleA("d3d11.dll");
        PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN createFn = d3d11
            ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
                  GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain"))
            : nullptr;
        if (!createFn) {
            LOGWNDF("D3DHooks_VRManager: late-injection: dummy swapchain creation failed - D3D11CreateDeviceAndSwapChain unavailable\n");
            return;
        }

        // Hidden 8x8 offscreen window: created without WS_VISIBLE, never
        // shown, never presented to.
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "GTAVR_DummySwapchainWnd";
        if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            LOGWNDF("D3DHooks_VRManager: late-injection: dummy window class registration failed (err=%lu)\n",
                    static_cast<unsigned long>(GetLastError()));
            return;
        }
        HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "GTAVR Dummy",
                                    WS_OVERLAPPEDWINDOW, 0, 0, 8, 8,
                                    nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd) {
            LOGWNDF("D3DHooks_VRManager: late-injection: dummy window creation failed (err=%lu)\n",
                    static_cast<unsigned long>(GetLastError()));
            return;
        }

        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Width = 8;
        sd.BufferDesc.Height = 8;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* swapChain = nullptr;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        // Deliberately the hooked export: this routes through
        // Proxy_D3D11CreateDeviceAndSwapChain, which runs
        // InstallSwapChainHooks on the dummy's vtable synchronously.
        HRESULT hr = createFn(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                              nullptr, 0, D3D11_SDK_VERSION,
                              &sd, &swapChain, &device, nullptr, &context);
        if (FAILED(hr)) {
            // Last resort; WARP vtables may differ from the hardware driver,
            // so the hook is not guaranteed to match the game's swapchain.
            hr = createFn(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                          nullptr, 0, D3D11_SDK_VERSION,
                          &sd, &swapChain, &device, nullptr, &context);
            if (SUCCEEDED(hr)) {
                LOGWNDF("D3DHooks_VRManager: late-injection: hardware dummy device failed; fell back to WARP - vtable match not guaranteed\n");
            }
        }
        if (FAILED(hr)) {
            LOGWNDF("D3DHooks_VRManager: late-injection: dummy swapchain creation failed (hr=0x%08lx) - relying on creation-path hooks + watchdog\n",
                    static_cast<unsigned long>(hr));
            DestroyWindow(hwnd);
            return;
        }

        if (context) { context->Release(); }
        if (device) { device->Release(); }
        // The swapchain holds the device alive; keep one swapchain reference
        // (and the window) until UninstallHooks - see the tag-reuse note in
        // the function comment above.

        dummy_window_ = hwnd;
        dummy_swapchain_.store(swapChain, std::memory_order_release);

        if (present_hook_installed && hooked_swapchain_ == swapChain) {
            late_injection_dummy_hooked_ = true;
            LOGSTR("D3DHooks_VRManager: late-injection: dummy swapchain created, Present hooked globally\n");
        } else if (present_hook_installed) {
            // A live game swapchain won the race while the dummy was being
            // created - equally good; the dummy just stays tagged/idle.
            LOGSTR("D3DHooks_VRManager: late-injection: Present already hooked via a live swapchain; dummy kept tagged but unused\n");
        } else {
            LOGWNDF("D3DHooks_VRManager: late-injection: dummy swapchain created but Present hook did not install - mod stays inert unless a creation hook fires\n");
        }
    }

    /**
     * Swapchain watchdog: if no swapchain was hooked within N seconds of
     * Initialize() (e.g. late injection after the device already existed and
     * no new swapchain is ever created), log clearly and stay inert.
     */
    DWORD WINAPI SwapchainWatchdogThread(LPVOID) {
        const DWORD kTimeoutMs = 30000;
        const DWORD kStepMs = 100;
        DWORD waited = 0;
        while (!shutting_down_.load() && waited < kTimeoutMs) {
            if (present_hook_installed) {
                return 0;
            }
            Sleep(kStepMs);
            waited += kStepMs;
        }
        if (!present_hook_installed && !shutting_down_.load()) {
            LOGWNDF("D3DHooks_VRManager: No D3D11 swapchain hooked within %lu s. "
                    "If the game was already running when the mod was injected (late injection) "
                    "its device may predate our hooks - the mod stays inert this session.\n",
                    static_cast<unsigned long>(kTimeoutMs / 1000));
        }
        return 0;
    }

    /**
     * Ordered hook teardown for DLL unload (call from DLL_PROCESS_DETACH with
     * lpReserved == NULL only - never during process termination and never
     * from inside a hooked frame):
     *   1. signal shutdown so hooked calls pass through,
     *   2. stop the watchdog,
     *   3. disable the Present/ResizeBuffers hooks and drain in-flight frames
     *      (this also stops camera writes, which only happen from Present),
     *   4. shut down overlay/imgui, XR backend and D3D resources,
     *   5. remove all hooks and uninitialize MinHook.
     */
    void UninstallHooks() {
        if (shutting_down_.exchange(true)) {
            return; // already torn down
        }
        LOGSTR("D3DHooks_VRManager: Uninstalling hooks (DLL unload)\n");

        if (original_wndproc_ && game_window_) {
            SetWindowLongPtrA(game_window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wndproc_));
            original_wndproc_ = nullptr;
        }

        if (watchdog_thread_) {
            WaitForSingleObject(watchdog_thread_, 2000);
            CloseHandle(watchdog_thread_);
            watchdog_thread_ = nullptr;
        }

        if (present_hook_target_) {
            MH_DisableHook(present_hook_target_);
        }
        if (resize_buffers_hook_target_) {
            MH_DisableHook(resize_buffers_hook_target_);
        }

        const DWORD drainDeadline = GetTickCount() + 500;
        while (in_hook_count_.load() > 0 && GetTickCount() < drainDeadline) {
            Sleep(1);
        }
        if (in_hook_count_.load() > 0) {
            LOGWNDF("D3DHooks_VRManager: Timed out draining in-flight hooked frames; continuing teardown\n");
        }

        ShutdownVR();

        if (present_hook_target_) { MH_RemoveHook(present_hook_target_); present_hook_target_ = nullptr; }
        if (resize_buffers_hook_target_) { MH_RemoveHook(resize_buffers_hook_target_); resize_buffers_hook_target_ = nullptr; }
        Hud::UninstallHooks();
        if (create_device_and_swapchain_target) { MH_RemoveHook(create_device_and_swapchain_target); create_device_and_swapchain_target = nullptr; }
        if (create_device_target_) { MH_RemoveHook(create_device_target_); create_device_target_ = nullptr; }
        if (create_dxgi_factory1_target_) { MH_RemoveHook(create_dxgi_factory1_target_); create_dxgi_factory1_target_ = nullptr; }
        if (create_dxgi_factory2_target_) { MH_RemoveHook(create_dxgi_factory2_target_); create_dxgi_factory2_target_ = nullptr; }
        if (create_swapchain_target_) { MH_RemoveHook(create_swapchain_target_); create_swapchain_target_ = nullptr; }
        MH_Uninitialize();

        // Late-injection dummy device: hooks are disabled/removed and no
        // hooked frame can still be in flight, so the tagged swapchain
        // (whose reference kept the dummy device alive) and its hidden
        // window can finally be released.
        IDXGISwapChain* dummy = dummy_swapchain_.exchange(nullptr);
        if (dummy) {
            dummy->Release();
        }
        if (dummy_window_) {
            DestroyWindow(dummy_window_);
            dummy_window_ = nullptr;
        }
        hooked_swapchain_ = nullptr;
        late_injection_dummy_hooked_ = false;
    }

    /**
     * Install the creation-path hooks.
     *
     * Primary entry point: d3d11!D3D11CreateDeviceAndSwapChain.
     * Secondary (late-injection / split creation path): d3d11!D3D11CreateDevice
     * (canary only) and dxgi!CreateDXGIFactory1/2, which lead to a hook on
     * IDXGIFactory::CreateSwapChain - together these catch swapchains created
     * after our init even when the AndSwapChain export is never used.
     *
     * Late injection (the game was already running, so its device/swapchain/
     * factory predate our hooks and none of the above fires again): right
     * after the creation hooks go in, we synchronously create a hidden dummy
     * D3D11 device+swapchain through the hooked export (Kiero dummy-device
     * method - see CreateDummyDeviceAndHookPresent). The proxy runs
     * InstallSwapChainHooks on the dummy's vtable, which patches the shared
     * driver implementation and therefore hooks Present/ResizeBuffers for
     * EVERY swapchain on the system, including the game's pre-existing one.
     * The dummy is tagged and excluded from the XR path; the watchdog below
     * remains as the diagnostic if even the dummy path fails.
     *
     * Idempotent: a second call logs and returns immediately.
     */
    void Initialize() {
        if (initialize_started_.exchange(true)) {
            LOGSTR("D3DHooks_VRManager: Initialize called twice - ignored\n");
            return;
        }

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

            void* pD3D11CreateDevice = GetProcAddress(d3d11_module, "D3D11CreateDevice");
            if (pD3D11CreateDevice) {
                if (MH_CreateHook(pD3D11CreateDevice, Proxy_D3D11CreateDevice, &Original_D3D11CreateDevice) == MH_OK &&
                    MH_EnableHook(pD3D11CreateDevice) == MH_OK) {
                    create_device_target_ = pD3D11CreateDevice;
                } else {
                    MH_RemoveHook(pD3D11CreateDevice);
                    LOGWNDF("D3DHooks_VRManager: Failed to hook D3D11CreateDevice (canary disabled)\n");
                }
            }
        }

        HMODULE dxgi_module = GetModuleHandleA("dxgi.dll");
        if (dxgi_module) {
            void* pCreateDXGIFactory1 = GetProcAddress(dxgi_module, "CreateDXGIFactory1");
            if (pCreateDXGIFactory1) {
                if (MH_CreateHook(pCreateDXGIFactory1, Proxy_CreateDXGIFactory1, (void**)&Original_CreateDXGIFactory1) == MH_OK &&
                    MH_EnableHook(pCreateDXGIFactory1) == MH_OK) {
                    create_dxgi_factory1_target_ = pCreateDXGIFactory1;
                } else {
                    MH_RemoveHook(pCreateDXGIFactory1);
                    LOGWNDF("D3DHooks_VRManager: Failed to hook CreateDXGIFactory1\n");
                }
            }

            void* pCreateDXGIFactory2 = GetProcAddress(dxgi_module, "CreateDXGIFactory2");
            if (pCreateDXGIFactory2) {
                if (MH_CreateHook(pCreateDXGIFactory2, Proxy_CreateDXGIFactory2, (void**)&Original_CreateDXGIFactory2) == MH_OK &&
                    MH_EnableHook(pCreateDXGIFactory2) == MH_OK) {
                    create_dxgi_factory2_target_ = pCreateDXGIFactory2;
                } else {
                    MH_RemoveHook(pCreateDXGIFactory2);
                    LOGWNDF("D3DHooks_VRManager: Failed to hook CreateDXGIFactory2\n");
                }
            }
        }

        // Late injection: hook Present/ResizeBuffers globally via a hidden
        // dummy device+swapchain (Kiero method). Runs synchronously here so
        // the hook is live before the watchdog starts; no-op if a live
        // swapchain already landed the hook above.
        CreateDummyDeviceAndHookPresent();

        // Late-injection watchdog: logs clearly and stays inert if no
        // swapchain ever shows up (i.e. the dummy path also failed).
        watchdog_thread_ = CreateThread(nullptr, 0, SwapchainWatchdogThread, nullptr, 0, nullptr);
    }

} // namespace VRMgr
} // namespace OVRInject
