#include "StereoEngine.hpp"

#include "ComfortRuntime.hpp"
#include "../Log.hpp"
#include "../Perf/PerfStats.hpp"
#include "../VR/SharedSettings.hpp"
#include "../Game/BuildManifest.hpp"
#include "../Game/GtaCameraFov.hpp"
#include "../Game/GtaCameraHook.hpp"
#include "../Game/GtaGameState.hpp"
#include "../Vive/HMDRenderer.hpp"
#include "../Overlay/VirtualScreen.hpp"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace OVRInject {
namespace Stereo {

// EyeDelivery uses plain int eye indices that must match VR::Eye.
static_assert(static_cast<int>(VR::Eye::Left) == EyeDelivery::kLeft &&
              static_cast<int>(VR::Eye::Right) == EyeDelivery::kRight,
              "EyeDelivery eye indices must match VR::Eye");

namespace {

// Millisecond timer for engine-section instrumentation (QPC-backed).
double NowMs() {
    static double freqMs = [] {
        LARGE_INTEGER freq = {};
        QueryPerformanceFrequency(&freq);
        return freq.QuadPart > 0 ? static_cast<double>(freq.QuadPart) / 1000.0 : 1.0;
    }();
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / freqMs;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return std::string();
    }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &out[0], needed, nullptr, nullptr);
    return out;
}

float ReadManifestFloat(const std::string& manifestPath, const char* key, float fallback) {
    if (manifestPath.empty()) {
        return fallback;
    }
    // Manual parse, NOT GetPrivateProfileStringA: the repo manifests are
    // LF-only and the Win32 INI API silently finds nothing in them (same
    // parser style as HudRedirect's [hud] loader).
    std::ifstream file(manifestPath);
    if (!file.is_open()) {
        return fallback;
    }
    bool inStereo = false;
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line[0] == '[') {
            inStereo = (line == "[stereo]");
            continue;
        }
        if (!inStereo) {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string name = line.substr(0, eq);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
            name.pop_back();
        }
        if (name != key) {
            continue;
        }
        float parsed = 0.0f;
        if (sscanf_s(line.substr(eq + 1).c_str(), "%f", &parsed) == 1 && parsed > 0.0f) {
            return parsed;
        }
        return fallback;
    }
    return fallback;
}

// ---- Vehicle horizon lock --------------------------------------------------
// GTA V world up is +Z. While in a vehicle the game camera pitches and rolls
// with the chassis; rotating the VR view with it is a top sickness trigger.
// GtaCameraHook (read-only for this layer) composes finalRotation =
// gameRotation * vrDeltaRotation and writes it to the game camera matrix.
// This layer post-filters that write: the game's own rotation is read before
// the hook runs (via the address the hook publishes in RuntimeStats), and
// after the write the stored matrix is replaced with
//
//     corrected = yawOnly(gameRot) * inverse(gameRot) * stored
//
// which removes exactly the vehicle pitch/roll from the game component while
// preserving yaw and the full VR head delta (GtaCameraHook.cpp:1393,
// ComposeRotations = base * delta). When the hook did not write this frame,
// the correction degrades to a plain horizon lock of the game's own camera.
//
// Layout mirror of Game::GtaCameraHook::GtaCameraMatrix (GtaCameraHook.hpp:71)
// - the type itself is private to the hook, so the raw 64-byte layout is
// restated here. Keep in sync.
struct CameraMatrixSnapshot {
    float right[4];
    float forward[4];
    float up[4];
    float position[4];
};
static_assert(sizeof(CameraMatrixSnapshot) == 64, "GTA camera matrix layout drift");

// Reads the current game camera matrix using the address the camera hook
// publishes in RuntimeStats (set only after the hook validated writability).
bool TryReadGameCameraSnapshot(CameraMatrixSnapshot& out) {
    auto& stats = VR::GetRuntimeStats();
    uintptr_t address = static_cast<uintptr_t>(stats.cameraMatrixAddress.load());
    if (address == 0 || !stats.cameraMatrixWritable.load()) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_READONLY |
                        PAGE_EXECUTE_READ | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) == 0) {
        return false;
    }
    memcpy(&out, reinterpret_cast<const void*>(address), sizeof(out));
    return true;
}

// Rebuilds a rotation matrix from stored GTA basis vectors, matching
// GtaCameraHook::ExtractRotationMatrix: rows are (right, up, forward).
DirectX::XMMATRIX SnapshotToRotation(const CameraMatrixSnapshot& snapshot) {
    DirectX::XMMATRIX rotation;
    rotation.r[0] = DirectX::XMVector3Normalize(DirectX::XMVectorSet(
        snapshot.right[0], snapshot.right[1], snapshot.right[2], 0.0f));
    rotation.r[1] = DirectX::XMVector3Normalize(DirectX::XMVectorSet(
        snapshot.up[0], snapshot.up[1], snapshot.up[2], 0.0f));
    rotation.r[2] = DirectX::XMVector3Normalize(DirectX::XMVectorSet(
        snapshot.forward[0], snapshot.forward[1], snapshot.forward[2], 0.0f));
    rotation.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    return rotation;
}

// Builds the yaw-only variant of a game rotation: vehicle pitch/roll removed,
// yaw preserved, basis kept orthonormal with the original handedness.
// Returns false when the camera looks near-vertical (projection degenerate -
// aerobatics edge case; the correction is skipped for that frame).
bool BuildYawOnlyRotation(const DirectX::XMMATRIX& rotation, DirectX::XMMATRIX& out) {
    using namespace DirectX;
    XMVECTOR forward = rotation.r[2];
    float fx = XMVectorGetX(forward);
    float fy = XMVectorGetY(forward);
    float horizontalSq = fx * fx + fy * fy;
    if (horizontalSq < 0.0025f) {  // |horizontal forward| < 0.05
        return false;
    }
    XMVECTOR flatForward = XMVector3Normalize(XMVectorSet(fx, fy, 0.0f, 0.0f));

    // Keep the original right vector's azimuth: project it flat and
    // orthonormalize against flatForward (Gram-Schmidt).
    XMVECTOR right = rotation.r[0];
    XMVECTOR flatRight = XMVectorSet(XMVectorGetX(right), XMVectorGetY(right), 0.0f, 0.0f);
    XMVECTOR orthoRight = XMVectorSubtract(
        flatRight, XMVectorScale(flatForward, XMVectorGetX(XMVector3Dot(flatRight, flatForward))));
    if (XMVectorGetX(XMVector3Length(orthoRight)) < 0.01f) {
        return false;  // 90-degree roll: azimuth of "right" undefined this frame
    }
    orthoRight = XMVector3Normalize(orthoRight);

    // up = forward x right for a right-handed basis; pick the sign that
    // matches the original basis so either handedness survives.
    XMVECTOR upCandidate = XMVector3Cross(flatForward, orthoRight);
    if (XMVectorGetX(XMVector3Dot(upCandidate, rotation.r[1])) < 0.0f) {
        upCandidate = XMVectorNegate(upCandidate);
    }

    out.r[0] = orthoRight;
    out.r[1] = upCandidate;
    out.r[2] = flatForward;
    out.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    return true;
}

// Gate for the horizon-lock correction. Requires the camera hook (its write
// is what we filter), the in-vehicle state, and the decoupling compose path:
// without decoupling the hook writes a head-only rotation and filtering would
// inject game yaw into it. Cutscenes are handled by the theater instead.
bool ShouldApplyVehicleHorizonLock(const FrameServices& services, bool cameraReady) {
    if (!cameraReady || !services.gameState) {
        return false;
    }
    if (!GetComfortRuntime().vehicleHorizonLock.load()) {
        return false;
    }
    Game::GtaGameState* gameState = services.gameState;
    if (!gameState->IsInVehicle()) {
        return false;
    }
    if (gameState->IsCutsceneActive() || gameState->IsLoading() || gameState->IsInMenu()) {
        return false;
    }
    if (gameState->ShouldShowVirtualScreen()) {
        return false;
    }
    if (!gameState->ShouldApplyDecoupling()) {
        return false;
    }
    return true;
}

void StoreBasisRow(float* dst, DirectX::XMVECTOR row, float preservedW) {
    dst[0] = DirectX::XMVectorGetX(row);
    dst[1] = DirectX::XMVectorGetY(row);
    dst[2] = DirectX::XMVectorGetZ(row);
    dst[3] = preservedW;
}

// Replaces the stored post-write camera matrix with the horizon-locked
// variant (see the block comment above). Returns true when a correction was
// actually written. Position and the w components are preserved from the
// hook's write; only the three basis vectors change.
bool ApplyVehicleHorizonLockCorrection(const CameraMatrixSnapshot& pre) {
    auto& stats = VR::GetRuntimeStats();
    uintptr_t address = static_cast<uintptr_t>(stats.cameraMatrixAddress.load());
    if (address == 0 || !stats.cameraMatrixWritable.load()) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                        PAGE_EXECUTE_WRITECOPY)) == 0) {
        return false;
    }

    CameraMatrixSnapshot post = {};
    memcpy(&post, reinterpret_cast<const void*>(address), sizeof(post));

    DirectX::XMMATRIX preRotation = SnapshotToRotation(pre);
    DirectX::XMMATRIX composed = SnapshotToRotation(post);
    DirectX::XMMATRIX yawOnly;
    if (!BuildYawOnlyRotation(preRotation, yawOnly)) {
        static bool loggedSkip = false;
        if (!loggedSkip) {
            LOGSTR("StereoEngine: Vehicle horizon lock skipped a frame (near-vertical camera)\n");
            loggedSkip = true;
        }
        return false;
    }

    DirectX::XMMATRIX corrected = DirectX::XMMatrixMultiply(
        DirectX::XMMatrixMultiply(yawOnly, DirectX::XMMatrixInverse(nullptr, preRotation)),
        composed);

    CameraMatrixSnapshot out = post;
    StoreBasisRow(out.right, corrected.r[0], post.right[3]);
    StoreBasisRow(out.up, corrected.r[1], post.up[3]);
    StoreBasisRow(out.forward, corrected.r[2], post.forward[3]);
    memcpy(reinterpret_cast<void*>(address), &out, sizeof(out));

    static bool loggedEngaged = false;
    if (!loggedEngaged) {
        LOGSTR("StereoEngine: Vehicle horizon lock engaged - vehicle pitch/roll filtered from the VR view (yaw passes through)\n");
        loggedEngaged = true;
    }
    return true;
}

} // namespace

StereoEngine& StereoEngine::Get() {
    static StereoEngine engine;
    return engine;
}

void StereoEngine::Initialize(VR::IVRBackend* backend) {
    eyeDelivery_.Reset();
    lastLoggedMode_ = -1;
    loggedBackbuffer_ = false;
    horizonLockEngaged_ = false;
    ResolveClipPlanes();
    if (backend) {
        CacheProjections(backend);
        // Force a fresh IPD sample even if the backend pointer is unchanged
        // (device-lost re-init reuses the VRManager singleton backend).
        ipdBackend_ = nullptr;
        SyncRuntimeIpd(backend);
    }
}

void StereoEngine::ResolveClipPlanes() {
    // Near/far for the VR projection: manifest-driven plugin values when the
    // loaded build manifest carries a [stereo] section, else sane defaults.
    // manifests/gtav_legacy.ini defines no such keys today -> defaults apply.
    nearPlane_ = 0.1f;
    farPlane_ = 1500.0f;
    const char* source = "defaults (no [stereo] keys in manifest)";

    Game::BuildManifest& manifest = Game::BuildManifest::Get();
    manifest.Initialize();  // idempotent; camera hook already triggered it
    const std::string path = WideToUtf8(manifest.GetManifestPath());
    if (!path.empty()) {
        float manifestNear = ReadManifestFloat(path, "nearPlane", nearPlane_);
        float manifestFar = ReadManifestFloat(path, "farPlane", farPlane_);
        if (manifestNear != nearPlane_ || manifestFar != farPlane_) {
            nearPlane_ = manifestNear;
            farPlane_ = manifestFar;
            source = "manifest [stereo] section";
        }
    }
    LOGSTRF("StereoEngine: VR clip planes near=%.3f far=%.1f (%s)\n",
            static_cast<double>(nearPlane_), static_cast<double>(farPlane_), source);
}

void StereoEngine::CacheProjections(VR::IVRBackend* backend) {
    // The projection used for VR comes from the runtime FOV via
    // backend->GetProjectionMatrix - never the game's own projection. There is
    // currently NO game-memory projection write path anywhere in the mod (the
    // game frustum is influenced only through the FOV float override in
    // GtaCameraFov), so these cached matrices are the single authoritative
    // per-eye projection for every VR consumer.
    projectionCached_[0] = backend->GetProjectionMatrix(VR::Eye::Left, nearPlane_, farPlane_);
    projectionCached_[1] = backend->GetProjectionMatrix(VR::Eye::Right, nearPlane_, farPlane_);
    LOGSTRF("StereoEngine: Per-eye VR projection cached from %s (near=%.3f far=%.1f)\n",
            backend->GetRuntimeName(), static_cast<double>(nearPlane_), static_cast<double>(farPlane_));
}

void StereoEngine::SyncRuntimeIpd(VR::IVRBackend* backend) {
    auto& stereo = VR::GetStereoSettings();

    if (backend != ipdBackend_) {
        ipdBackend_ = backend;
        runtimeIpd_ = 0.0f;

        if (backend) {
            // Eye-to-head translations from the runtime (OpenVR
            // GetEyeToHeadTransform / OpenXR view poses). IPD is the distance
            // between the two eye positions.
            DirectX::XMMATRIX leftEye = backend->GetEyeMatrix(VR::Eye::Left);
            DirectX::XMMATRIX rightEye = backend->GetEyeMatrix(VR::Eye::Right);
            float dx = rightEye.r[3].m128_f32[0] - leftEye.r[3].m128_f32[0];
            float dy = rightEye.r[3].m128_f32[1] - leftEye.r[3].m128_f32[1];
            float dz = rightEye.r[3].m128_f32[2] - leftEye.r[3].m128_f32[2];
            float ipd = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (ipd >= 0.001f) {
                runtimeIpd_ = ipd;
                LOGSTRF("StereoEngine: IPD %.4f m from %s runtime (overrides INI stereoIPD)\n",
                        static_cast<double>(runtimeIpd_), backend->GetRuntimeName());
            } else {
                // Runtime reported no eye offset: the INI stereoIPD value is
                // the fallback for exactly this case (logged once per backend).
                LOGSTRF("StereoEngine: Runtime reported no eye offset - using INI stereoIPD fallback %.4f m\n",
                        static_cast<double>(stereo.stereoIPD.load()));
            }
        }
    }

    // Re-assert every frame: overlay settings changes rewrite stereoIPD from
    // the INI, and the runtime value must win whenever it is available.
    if (runtimeIpd_ > 0.0f) {
        stereo.stereoIPD.store(runtimeIpd_);
    }
}

void StereoEngine::LatchHeadPose(VR::IVRBackend* backend) {
    // LATE-LATCH: sample the head pose NOW, immediately before the camera
    // write - never at game-camera-update time. The matrix we are about to
    // write is consumed by the game's NEXT render; a pose latched earlier in
    // the frame would be up to a full frame stale by the time it reaches the
    // display (~16 ms at 60 fps, up to ~40 ms at 25 fps), added directly to
    // motion-to-photon latency. Both backends hand us the freshest per-frame
    // snapshot here (OpenVR: this frame's WaitGetPoses cache; OpenXR: this
    // frame's located views), so the write carries the newest pose available.
    headPoseLatched_ = backend->GetHeadPoseMatrix();

    if (!loggedLateLatch_) {
        LOGSTR("StereoEngine: Late-latch active - head pose sampled immediately before camera write\n");
        loggedLateLatch_ = true;
    }
}

void StereoEngine::LogBackbufferOnce(ID3D11Texture2D* pBuffer, HMDRenderer* hmdRenderer) {
    if (loggedBackbuffer_ || !pBuffer) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    pBuffer->GetDesc(&desc);
    LOGSTRF("StereoEngine: Backbuffer %ux%u fmt=%u bind=0x%08x samples=%u quality=%u\n",
            desc.Width, desc.Height, static_cast<unsigned>(desc.Format), desc.BindFlags,
            desc.SampleDesc.Count, desc.SampleDesc.Quality);

    if (hmdRenderer) {
        ID3D11Texture2D* eyeTex = hmdRenderer->GetEyeTexture(VR::Eye::Left);
        if (eyeTex) {
            D3D11_TEXTURE2D_DESC eyeDesc = {};
            eyeTex->GetDesc(&eyeDesc);
            LOGSTRF("StereoEngine: Eye texture %ux%u fmt=%u bind=0x%08x samples=%u quality=%u\n",
                    eyeDesc.Width, eyeDesc.Height,
                    static_cast<unsigned>(eyeDesc.Format), eyeDesc.BindFlags,
                    eyeDesc.SampleDesc.Count, eyeDesc.SampleDesc.Quality);
        }
    }
    loggedBackbuffer_ = true;
}

HRESULT StereoEngine::OnPresent(IDXGISwapChain* pSwapChain, UINT syncInterval, UINT flags,
                                const FrameServices& services) {
    if (!services.originalPresent) {
        return E_FAIL;
    }

    VR::IVRBackend* backend = services.backend;
    HMDRenderer* hmdRenderer = services.hmdRenderer;
    if (!backend || !hmdRenderer || !services.produceEye) {
        return services.originalPresent(pSwapChain, syncInterval, flags);
    }

    // (2) Frame pacing: block on compositor timing. On failure pass the frame
    // through cleanly - no camera writes, no XR submit, no overlay.
    if (!backend->BeginFrame()) {
        return services.originalPresent(pSwapChain, syncInterval, flags);
    }

    backend->UpdateControllers();
    if (services.applySnapTurning) {
        services.applySnapTurning(backend);
    }

    Game::GtaCameraHook* cameraHook = services.cameraHook;
    bool cameraReady = cameraHook && cameraHook->IsReady();

    if (services.cameraFov) {
        services.cameraFov->Update(VR::GetFovSettings());
    }

    Game::GtaGameState* gameState = services.gameState;
    if (gameState) {
        gameState->Update();
    }

    bool showVirtualScreen = gameState && gameState->ShouldShowVirtualScreen();
    if (!cameraReady) {
        showVirtualScreen = false;
    }
    VirtualScreen* virtualScreen = services.virtualScreen;
    if (virtualScreen) {
        virtualScreen->SetEnabled(showVirtualScreen);
        if (showVirtualScreen) {
            virtualScreen->Update();
        }
    }

    // (4) Mode policy -------------------------------------------------------
    auto& stereoSettings = VR::GetStereoSettings();
    auto& reproSettings = VR::GetReprojectionSettings();
    int stereoMode = stereoSettings.mode.load();

    if (stereoMode == static_cast<int>(VR::StereoMode::DualPass)) {
        // DualPass (render the scene twice, once per eye) is an EXPERIMENTAL
        // stub: selecting it logs once and falls back to AER. A real
        // implementation is deliberately not attempted.
        if (!loggedDualPass_) {
            LOGSTR("StereoEngine: DualPass stereo mode is EXPERIMENTAL and not functional - falling back to AlternateEye\n");
            loggedDualPass_ = true;
        }
        stereoMode = static_cast<int>(VR::StereoMode::AlternateEye);
    }
    effectiveMode_ = stereoMode;

    bool allowStereoRendering = cameraReady &&
                                !(gameState && (gameState->IsLoading() || gameState->IsInMenu())) &&
                                !showVirtualScreen;

    bool wantAlternate = allowStereoRendering &&
                         stereoMode == static_cast<int>(VR::StereoMode::AlternateEye);
    bool wantReprojection = allowStereoRendering &&
                            reproSettings.enabled.load() &&
                            stereoMode == static_cast<int>(VR::StereoMode::Reprojection);
    bool monoFallbackCopyBothEyes = !cameraReady && !showVirtualScreen;

    if (stereoMode != lastLoggedMode_) {
        LOGSTRF("StereoEngine: Stereo mode = %s\n",
                stereoMode == static_cast<int>(VR::StereoMode::AlternateEye) ? "AlternateEye (AER)"
                                                                             : "Reprojection (Z3D)");
        lastLoggedMode_ = stereoMode;
    }
    if (!cameraReady && !loggedCameraFallback_) {
        LOGSTR("StereoEngine: Camera hook not ready, using mono blit fallback until stereo engages\n");
        loggedCameraFallback_ = true;
    }

    // Render scale changes recreate every eye texture; dragging the overlay
    // slider would otherwise churn dozens of texture allocations per second
    // (observed live: 15 re-inits in 7 s). Debounce: apply at most once per
    // second, always converging to the latest requested value.
    {
        float requestedScale = reproSettings.renderScale.load();
        static float lastAppliedScale = -1.0f;
        static float pendingScale = -1.0f;
        static uint64_t pendingSinceMs = 0;
        uint64_t nowMs = GetTickCount64();
        if (requestedScale != pendingScale) {
            pendingScale = requestedScale;
            pendingSinceMs = nowMs;
        }
        if (pendingScale >= 0.0f && pendingScale != lastAppliedScale &&
            nowMs - pendingSinceMs >= 1000) {
            hmdRenderer->Resize(pendingScale);
            lastAppliedScale = pendingScale;
        }
    }

    if (services.maybeResizeSwapchain) {
        services.maybeResizeSwapchain(pSwapChain);
    }

    // IPD always comes from the runtime; the INI stereoIPD is only a fallback
    // when the runtime reports none (see SyncRuntimeIpd).
    SyncRuntimeIpd(backend);

    // Get backbuffer
    ID3D11Texture2D* pBuffer = nullptr;
    HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBuffer);
    if (FAILED(hr) || !pBuffer) {
        return services.originalPresent(pSwapChain, syncInterval, flags);
    }
    LogBackbufferOnce(pBuffer, hmdRenderer);

    ID3D11DeviceContext* context = nullptr;
    backend->GetDevice()->GetImmediateContext(&context);
    if (!context) {
        pBuffer->Release();
        return services.originalPresent(pSwapChain, syncInterval, flags);
    }
    if (services.updateVignette) {
        services.updateVignette(context, backend);
    }

    // AER parity: the eye the game just rendered (and that the backbuffer
    // therefore contains) is frameIndex & 1. Outside AER the phase is pinned
    // to 0 so the first AER frame after a mode switch / camera-ready
    // transition always captures the Left-rendered backbuffer. The full
    // per-frame delivery plan (blit target, per-layer texture, camera-write
    // eye) comes from the EyeDelivery state machine (Stereo/EyeDelivery.hpp);
    // it advances only on completed frames, so pass-through frames cannot
    // desync the blit parity from the camera-write parity.
    if (!wantAlternate) {
        eyeDelivery_.Reset();
    }
    const EyeDelivery::FramePlan aerPlan = eyeDelivery_.PlanAlternateEye();
    VR::Eye renderEye = static_cast<VR::Eye>(aerPlan.renderEye);

    // --- Blit section: backbuffer -> eye texture(s) -------------------------
    double sectionStart = NowMs();

    bool depthReady = false;
    if (wantReprojection && services.prepareDepth) {
        depthReady = services.prepareDepth(backend->GetDevice(), context);
    }

    if (wantAlternate) {
        float eyeSign = (renderEye == VR::Eye::Left) ? -1.0f : 1.0f;
        services.produceEye(backend->GetDevice(), context, pBuffer, renderEye, eyeSign, true, false);
    } else {
        // Mono fallback (camera unresolved): ONE aspect-fitted blit into the
        // left eye texture, which is then submitted for BOTH eyes - so both
        // eyes show the identical, centered, aspect-correct image (the old
        // full-stretch filled the near-square eye texture with the
        // ultrawide backbuffer, ~2.4x vertical over-stretch). The plain
        // non-AER path (camera ready but loading/menu) keeps full-fill.
        services.produceEye(backend->GetDevice(), context, pBuffer,
                            VR::Eye::Left, 0.0f, monoFallbackCopyBothEyes,
                            monoFallbackCopyBothEyes);
        if (!monoFallbackCopyBothEyes && services.renderRightEye) {
            services.renderRightEye(backend->GetDevice(), context, pBuffer,
                                    wantReprojection && depthReady);
        }
    }

    Perf::PerfStats::Get().AddBlitMs(NowMs() - sectionStart);

    // Phase 6 HUD infrastructure: alpha-blit the offscreen HUD target onto
    // each eye produced this frame, then clear it for the next frame. Both
    // ops no-op unless the [hud] manifest section is enabled AND a HUD pass
    // was actually substituted. On the virtual-screen (cutscene) path the HUD
    // is not composited (documented v1 limitation).
    if (services.compositeHud) {
        if (wantAlternate) {
            services.compositeHud(backend->GetDevice(), context, renderEye);
        } else if (monoFallbackCopyBothEyes) {
            services.compositeHud(backend->GetDevice(), context, VR::Eye::Left);
        } else if (!showVirtualScreen) {
            services.compositeHud(backend->GetDevice(), context, VR::Eye::Left);
            services.compositeHud(backend->GetDevice(), context, VR::Eye::Right);
        }
    }
    if (services.finishHudFrame) {
        services.finishHudFrame(context, pBuffer);
    }

    if (services.updateOverlay) {
        services.updateOverlay();
    }

    // --- Submit section ------------------------------------------------------
    sectionStart = NowMs();

    // If the virtual screen is active (cutscene), the game frame is rendered
    // on the floating screen instead of going through the stereo path.
    if (showVirtualScreen && virtualScreen && virtualScreen->IsInitialized()) {
        virtualScreen->Render(pBuffer);
        backend->SubmitEyeTexture(VR::Eye::Left, virtualScreen->GetEyeTexture(VR::Eye::Left));
        backend->SubmitEyeTexture(VR::Eye::Right, virtualScreen->GetEyeTexture(VR::Eye::Right));
    } else if (monoFallbackCopyBothEyes) {
        ID3D11Texture2D* monoTexture = hmdRenderer->GetEyeTexture(VR::Eye::Left);
        backend->SubmitEyeTexture(VR::Eye::Left, monoTexture);
        backend->SubmitEyeTexture(VR::Eye::Right, monoTexture);
    } else if (wantAlternate) {
        // AER eye delivery: submit BOTH layers every frame. The fresh eye's
        // layer carries this frame's new blit; the stale eye's layer
        // re-submits that eye's OWN persistent texture - its previous
        // own-rendered frame - so each eye always displays its own viewpoint
        // and there is no cross-eye contamination (layer i is backed by eye
        // texture i; the single warmup frame after a reset, where the stale
        // eye was never produced, borrows the fresh eye's texture - see
        // EyeDelivery). The runtime's ATW/ASW still reprojects the stale
        // eye's older content to the current pose: that is the intended AER
        // behavior (ADR-0002), now made explicit instead of relying on the
        // runtime's last-released-image retention (OpenXR: "xrEndFrame will
        // use the most recently released swapchain image"; OpenVR holds the
        // last submitted texture per eye).
        if (!loggedAerDelivery_) {
            LOGSTR("StereoEngine: AER active - submitting both layers per frame "
                   "(fresh eye + the other eye's own previous frame; runtime reprojection covers timing)\n");
            loggedAerDelivery_ = true;
        }
        backend->SubmitEyeTexture(
            VR::Eye::Left,
            hmdRenderer->GetEyeTexture(static_cast<VR::Eye>(aerPlan.layerTexture[EyeDelivery::kLeft])));
        backend->SubmitEyeTexture(
            VR::Eye::Right,
            hmdRenderer->GetEyeTexture(static_cast<VR::Eye>(aerPlan.layerTexture[EyeDelivery::kRight])));
    } else {
        // Z3D reprojection / plain stereo: both eyes were produced from this
        // frame's backbuffer, submit both.
        backend->SubmitEyeTexture(VR::Eye::Left, hmdRenderer->GetEyeTexture(VR::Eye::Left));
        backend->SubmitEyeTexture(VR::Eye::Right, hmdRenderer->GetEyeTexture(VR::Eye::Right));
    }

    Perf::PerfStats::Get().AddSubmitMs(NowMs() - sectionStart);

    context->Release();
    pBuffer->Release();

    backend->EndFrame();

    // (6) Desktop mirroring. Sync interval preserved by default; the
    // desktopMirrorSyncOverride settings key (or GTAVR_DESKTOP_MIRROR_SYNC0)
    // restores the legacy forced-0 behavior.
    if (!loggedMirrorSync_ && services.forceMirrorSync0 && syncInterval != 0) {
        LOGSTR("StereoEngine: desktopMirrorSyncOverride active - forcing desktop mirror sync interval to 0\n");
        loggedMirrorSync_ = true;
    }
    HRESULT result = services.originalPresent(pSwapChain,
                                              services.forceMirrorSync0 ? 0 : syncInterval,
                                              flags);

    // --- Camera write section (AFTER the desktop-mirror Present) -------------
    sectionStart = NowMs();

    // Write-timing contract: this write must land after the game's render of
    // the CURRENT frame and before the game's render of the NEXT frame - it
    // is the next frame that is rendered with this eye's pose. Both points
    // live on the game's render thread; hookedPresent runs on that thread
    // between them, so ANY position inside the hook is ordered before the
    // next render by program order (the one-frame delay this creates is the
    // basis of the AER parity, see EyeDelivery). The position within the
    // hook still matters because of the SECOND writer: the game itself
    // refreshes this camera matrix from its own camera update (simulation
    // side, pipelined against the render thread). If that refresh lands
    // after our write, our matrix is overwritten before the next render
    // reads it and the frame falls back to the game's own (mono, untracked)
    // camera for one frame. Writing at the LATEST point still inside the
    // hook - immediately after originalPresent returns, i.e. after any
    // vsync / frame-latency block inside Present and any xrEndFrame/
    // xrWaitFrame blocking in EndFrame, with no further hook work between
    // the write and returning to the game - minimizes the window in which
    // the game's refresh can land after us. Whether that refresh can EVER
    // land after us is engine-internal and cannot be proven without the
    // running game (docs/known-issues.md, UNVERIFIED); the working
    // assumption - "last writer before the next render wins, and that is
    // us" - is the same one the head-tracking write path has always relied
    // on, and a permanently lost write would kill head tracking outright,
    // not just stereo.

    // (3) LATE-LATCH the head pose immediately before the camera write.
    LatchHeadPose(backend);

    // Vehicle horizon lock: snapshot the game's own camera rotation before the
    // hook composes over it; the correction below removes the vehicle
    // pitch/roll from the composed result.
    bool horizonLock = ShouldApplyVehicleHorizonLock(services, cameraReady);
    CameraMatrixSnapshot preSnapshot = {};
    bool havePreSnapshot = horizonLock && TryReadGameCameraSnapshot(preSnapshot);

    if (cameraHook) {
        if (wantAlternate && cameraHook->IsReady()) {
            // The game renders the OTHER eye next frame; write that eye's
            // camera now so it is in place before the next game render.
            cameraHook->Update(static_cast<VR::Eye>(aerPlan.cameraWriteEye), gameState);
        } else {
            cameraHook->Update(VR::Eye::Left, gameState);
        }

        if (stereoSettings.recenterRequested.exchange(false)) {
            cameraHook->RecenterPose();
        }
    }

    if (havePreSnapshot && ApplyVehicleHorizonLockCorrection(preSnapshot)) {
        horizonLockEngaged_ = true;
    }

    Perf::PerfStats::Get().AddCameraWriteMs(NowMs() - sectionStart);

    // One fully completed frame: advance the AER parity. Missed/pass-through
    // frames never reach here, so they cannot desync blit parity from
    // camera-write parity (both derive from the same counter).
    eyeDelivery_.Advance();
    return result;
}

} // namespace Stereo
} // namespace OVRInject
