#pragma once

/**
 * StereoEngine - the per-frame stereo sequence, extracted from hookedPresent
 * (Phase 4).
 *
 * hookedPresent keeps only the guard rails (ordered-unload interlock,
 * OnlineGuard kill-switch, inert/pass-through states, first-present init,
 * swapchain resize, minimized-window bypass) and delegates the actual VR
 * frame to StereoEngine::OnPresent. The explicit per-frame sequence is:
 *
 *   1) OnlineGuard pass-through      - stays at the top of hookedPresent,
 *                                      never reaches the engine when fired.
 *   2) backend->BeginFrame           - frame pacing; on failure the frame
 *                                      passes through cleanly.
 *   3) LATE-LATCH head pose          - sampled immediately before the camera
 *                                      write, never at game-camera-update
 *                                      time (see LatchHeadPose).
 *   4) Mode policy                   - AER (default) / Z3D Reprojection /
 *                                      DualPass (EXPERIMENTAL stub -> AER).
 *   5) Per-eye projection            - cached from
 *                                      backend->GetProjectionMatrix(eye, near,
 *                                      far) with manifest-driven clip planes
 *                                      when available, else 0.1 / 1500.
 *   6) Submit + EndFrame             - in AER BOTH eye layers are submitted
 *                                      every frame (fresh eye's new blit +
 *                                      stale eye's own persistent texture,
 *                                      see Stereo/EyeDelivery.hpp); then
 *                                      the desktop mirror via the original
 *                                      Present.
 *   7) Camera write                  - AFTER the original Present returns
 *                                      (write-timing contract documented at
 *                                      the write site in StereoEngine.cpp):
 *                                      the NEXT eye's pose, so the next game
 *                                      render uses it.
 *
 * HMDRenderer remains the blit/copy mechanism; the D3D mechanics stay in the
 * D3DHook layer and are invoked through the FrameServices op pointers. This
 * class owns ordering and policy only - no D3D resources.
 */

#include "../VR/IVRBackend.hpp"
#include "../VR/SharedSettings.hpp"
#include "EyeDelivery.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <DirectXMath.h>

namespace OVRInject {

class HMDRenderer;
class VirtualScreen;

namespace Game {
class GtaCameraHook;
class GtaCameraFov;
class GtaGameState;
class VRCamera;
}

namespace Stereo {

/**
 * FrameServices - everything the engine needs from the D3D hook layer for one
 * frame. Pointers are refreshed by hookedPresent every frame (hmdRenderer is
 * recreated on swapchain resize; backends can re-init after device-lost), and
 * the op functions point at the existing statics in D3DHooks_VRManager.hpp.
 */
struct FrameServices {
    VR::IVRBackend* backend = nullptr;
    HMDRenderer* hmdRenderer = nullptr;
    Game::GtaCameraHook* cameraHook = nullptr;
    // Scripted-camera path (GTAVRBridge). When available it wins over the
    // memory-matrix hook (which the b3889 renderer provably does not read).
    Game::VRCamera* vrCamera = nullptr;
    Game::GtaCameraFov* cameraFov = nullptr;
    Game::GtaGameState* gameState = nullptr;
    VirtualScreen* virtualScreen = nullptr;
    bool usingOpenVR = false;
    bool forceMirrorSync0 = false;

    typedef HRESULT(__stdcall* PresentFn)(IDXGISwapChain*, UINT, UINT);
    PresentFn originalPresent = nullptr;

    // Ops implemented by the D3D hook layer (see D3DHooks_VRManager.hpp).
    void (*applySnapTurning)(VR::IVRBackend*) = nullptr;
    void (*maybeResizeSwapchain)(IDXGISwapChain*) = nullptr;
    // Updates the vignette constant buffer; the backend is used for the
    // locomotion proxy (head-translation delta + left stick).
    void (*updateVignette)(ID3D11DeviceContext*, VR::IVRBackend*) = nullptr;
    // Phase 6 HUD infrastructure (config-gated; both no-op when the [hud]
    // manifest section is disabled or no HUD pass was substituted this frame).
    // compositeHud alpha-blits the offscreen HUD target onto one eye target.
    void (*compositeHud)(ID3D11Device*, ID3D11DeviceContext*, VR::Eye) = nullptr;
    // finishHudFrame clears the HUD target for the next frame and re-caches
    // the presented backbuffer for the substitution hook. Called once per
    // Present after the eye composites.
    void (*finishHudFrame)(ID3D11DeviceContext*, ID3D11Texture2D*) = nullptr;
    // Copies the current depth-stencil into a shader-readable texture for the
    // Z3D path; returns false when depth is unavailable (caller then blits).
    bool (*prepareDepth)(ID3D11Device*, ID3D11DeviceContext*) = nullptr;
    // Blits (or, without the blit shader, copies via HMDRenderer) the mono
    // backbuffer into one eye target. eyeSign selects the alignment side
    // (-1 left / +1 right / 0 shared mono); useUserAlignment=false forces
    // scale 1 / offset 0. aspectFit=true contain-fits the backbuffer into
    // the eye target (mono fallback mapping: aspect preserved, centered,
    // black bars - see Stereo/ImageFit.hpp). angularCrop=true (AER path)
    // additionally crops the game's ultrawide frustum to the XR per-eye
    // frustum (ComputeAngularCrop - angle-true per-eye image).
    void (*produceEye)(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*,
                       VR::Eye, float eyeSign, bool useUserAlignment, bool aspectFit,
                       bool angularCrop) = nullptr;
    // Produces the right eye from the same backbuffer: depth-displaced when
    // useReprojection is true and the shader is available, else a plain blit.
    void (*renderRightEye)(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*,
                           bool useReprojection) = nullptr;
    // OpenVR overlay input + render + recenter plumbing (no-op on OpenXR).
    void (*updateOverlay)() = nullptr;
};

class StereoEngine {
public:
    static StereoEngine& Get();

    // Idempotent. Called from first-present (re)init: resolves clip planes,
    // caches the per-eye runtime projection and syncs the runtime IPD into
    // the shared stereo settings. Safe to call again after device-lost
    // re-init.
    void Initialize(VR::IVRBackend* backend);

    // The full per-frame stereo sequence (steps 2-6 above). Returns the
    // result of the desktop-mirror Present (or of a clean pass-through).
    HRESULT OnPresent(IDXGISwapChain* pSwapChain, UINT syncInterval, UINT flags,
                      const FrameServices& services);

    // Introspection (logging / future overlay use).
    int GetEffectiveMode() const { return effectiveMode_; }
    uint64_t GetFrameIndex() const { return eyeDelivery_.FrameIndex(); }
    float GetRuntimeIpd() const { return runtimeIpd_; }
    float GetNearPlane() const { return nearPlane_; }
    float GetFarPlane() const { return farPlane_; }
    // True while the vehicle horizon-lock correction is engaged this session
    // (set when the first correction is actually applied).
    bool WasVehicleHorizonLockEngaged() const { return horizonLockEngaged_; }
    DirectX::XMMATRIX GetEyeProjection(VR::Eye eye) const {
        return projectionCached_[static_cast<int>(eye) & 1];
    }

private:
    StereoEngine() = default;

    void ResolveClipPlanes();
    void CacheProjections(VR::IVRBackend* backend);
    void SyncRuntimeIpd(VR::IVRBackend* backend);
    void LatchHeadPose(VR::IVRBackend* backend);
    void LogBackbufferOnce(ID3D11Texture2D* pBuffer, HMDRenderer* hmdRenderer);

    VR::IVRBackend* ipdBackend_ = nullptr;   // backend the IPD was sampled from
    float runtimeIpd_ = 0.0f;                // 0 = runtime reported none
    float nearPlane_ = 0.1f;
    float farPlane_ = 1500.0f;
    DirectX::XMMATRIX projectionCached_[2] = {
        DirectX::XMMatrixIdentity(), DirectX::XMMatrixIdentity()
    };
    DirectX::XMMATRIX headPoseLatched_ = DirectX::XMMatrixIdentity();

    EyeDelivery eyeDelivery_;                  // AER parity + delivery planner
    int effectiveMode_ = static_cast<int>(VR::StereoMode::AlternateEye);
    int lastLoggedMode_ = -1;

    bool loggedDualPass_ = false;
    bool loggedCameraFallback_ = false;
    bool loggedAerDelivery_ = false;
    bool loggedMirrorSync_ = false;
    bool loggedBackbuffer_ = false;
    bool loggedLateLatch_ = false;
    bool horizonLockEngaged_ = false;   // first applied correction logs + latches
};

} // namespace Stereo
} // namespace OVRInject
