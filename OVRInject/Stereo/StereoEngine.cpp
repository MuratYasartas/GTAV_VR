#include "StereoEngine.hpp"

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
#include <string>

namespace OVRInject {
namespace Stereo {

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
    char value[64] = {};
    DWORD len = GetPrivateProfileStringA("stereo", key, "", value, sizeof(value), manifestPath.c_str());
    if (len == 0) {
        return fallback;
    }
    float parsed = 0.0f;
    if (sscanf_s(value, "%f", &parsed) == 1 && parsed > 0.0f) {
        return parsed;
    }
    return fallback;
}

} // namespace

StereoEngine& StereoEngine::Get() {
    static StereoEngine engine;
    return engine;
}

void StereoEngine::Initialize(VR::IVRBackend* backend) {
    frameIndex_ = 0;
    lastLoggedMode_ = -1;
    loggedBackbuffer_ = false;
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

    float renderScale = reproSettings.renderScale.load();
    hmdRenderer->Resize(renderScale);

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
        services.updateVignette(context);
    }

    // AER parity: the eye the game just rendered (and that the backbuffer
    // therefore contains) is frameIndex & 1. Outside AER the phase is pinned
    // to 0 so the first AER frame after a mode switch / camera-ready
    // transition always captures the Left-rendered backbuffer.
    if (!wantAlternate) {
        frameIndex_ = 0;
    }
    VR::Eye renderEye = (frameIndex_ & 1) != 0 ? VR::Eye::Right : VR::Eye::Left;

    // --- Blit section: backbuffer -> eye texture(s) -------------------------
    double sectionStart = NowMs();

    bool depthReady = false;
    if (wantReprojection && services.prepareDepth) {
        depthReady = services.prepareDepth(backend->GetDevice(), context);
    }

    if (wantAlternate) {
        float eyeSign = (renderEye == VR::Eye::Left) ? -1.0f : 1.0f;
        services.produceEye(backend->GetDevice(), context, pBuffer, renderEye, eyeSign, true);
    } else {
        services.produceEye(backend->GetDevice(), context, pBuffer,
                            VR::Eye::Left, 0.0f, monoFallbackCopyBothEyes);
        if (!monoFallbackCopyBothEyes && services.renderRightEye) {
            services.renderRightEye(backend->GetDevice(), context, pBuffer,
                                    wantReprojection && depthReady);
        }
    }

    Perf::PerfStats::Get().AddBlitMs(NowMs() - sectionStart);

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
        // AER: submit ONLY the freshly rendered eye. The other eye is
        // reprojected by the runtime (ATW/ASW) from its last submission -
        // that is the expected, intended behavior of alternating-eye
        // rendering, not a missing frame.
        if (!loggedAerSingleSubmit_) {
            LOGSTR("StereoEngine: AER active - submitting only the freshly rendered eye; "
                   "the other eye relies on runtime reprojection (expected)\n");
            loggedAerSingleSubmit_ = true;
        }
        backend->SubmitEyeTexture(renderEye, hmdRenderer->GetEyeTexture(renderEye));
    } else {
        // Z3D reprojection / plain stereo: both eyes were produced from this
        // frame's backbuffer, submit both.
        backend->SubmitEyeTexture(VR::Eye::Left, hmdRenderer->GetEyeTexture(VR::Eye::Left));
        backend->SubmitEyeTexture(VR::Eye::Right, hmdRenderer->GetEyeTexture(VR::Eye::Right));
    }

    Perf::PerfStats::Get().AddSubmitMs(NowMs() - sectionStart);

    // --- Camera write section ------------------------------------------------
    sectionStart = NowMs();

    // (3) LATE-LATCH the head pose immediately before the camera write.
    LatchHeadPose(backend);

    if (cameraHook) {
        if (wantAlternate && cameraHook->IsReady()) {
            // The game renders the OTHER eye next frame; write that eye's
            // camera now so it is in place before the next game render.
            VR::Eye writeEye = (renderEye == VR::Eye::Left) ? VR::Eye::Right : VR::Eye::Left;
            cameraHook->Update(writeEye, gameState);
        } else {
            cameraHook->Update(VR::Eye::Left, gameState);
        }

        if (stereoSettings.recenterRequested.exchange(false)) {
            cameraHook->RecenterPose();
        }
    }

    Perf::PerfStats::Get().AddCameraWriteMs(NowMs() - sectionStart);

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
    ++frameIndex_;
    return result;
}

} // namespace Stereo
} // namespace OVRInject
