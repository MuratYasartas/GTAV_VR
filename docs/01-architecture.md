# Phase 1 — Architecture

Status: accepted as design; migration in progress (see §10).
Builds on `docs/00-feasibility.md` (salvage decision) and the codebase audit.

## 1. Design constraints (from the mission)

- Single-player only; online detection is a hard kill-switch (ADR-0003).
- No anti-tamper circumvention; unsupported titles are documented, not engineered around.
- Every signature/offset/shader-hash lives in a per-version manifest, never hardcoded (ADR-0004).
- Comfort is a safety requirement: any path that can produce sustained sub-target
  framerate, uncommanded camera motion, or wrong IPD/scale is a P0 defect.
- Measure, don't guess; p99 frametime, never averages.

## 2. Layer overview

```
┌─────────────────────────────────────────────────────────┐
│ per-title plugin   (GTAVLegacyPlugin → GTAVE, RDR2 …)   │  ITitlePlugin + BuildManifest
├─────────────────────────────────────────────────────────┤
│ XR runtime adapter (OpenXR primary / OpenVR fallback)   │  IVRBackend   [exists]
├─────────────────────────────────────────────────────────┤
│ stereo engine      (AER / dual-pass / Z3D modes)        │  StereoEngine [new]
├─────────────────────────────────────────────────────────┤
│ RHI abstraction    (D3D11 backend → D3D12/Vulkan later) │  IRHI*        [new]
├─────────────────────────────────────────────────────────┤
│ hook core          (Present/ResizeBuffers/device hooks) │  MinHook + vtable discovery
├─────────────────────────────────────────────────────────┤
│ loader             (launcher exe / dxgi proxy shim)     │  [exists]
└─────────────────────────────────────────────────────────┘
   cross-cutting: OnlineGuard │ Config/INI │ Log │ PerfStats │ CrashDump
```

Hard boundary rules:
1. A layer may only call the layer directly below it (plugins also read Config).
2. **No concrete device types above the RHI layer.** Any stereo/XR code that names
   `ID3D11Device` et al. is a design defect (see §4 for the proof).
3. **Zero title-specific code in core.** GTA-specific knowledge (AOBs, camera struct
   layout, FOV quirks, cutscene signals) lives only in plugins + manifests.
4. The kill-switch (OnlineGuard) is cross-cutting and reachable from every layer;
   it cannot be disabled by config.

## 3. Layer responsibilities and current-code mapping

| Layer | Responsibility | Where it lives today | Gap → action |
|---|---|---|---|
| Loader | Get our DLL into the process; preflight (build detect, runtime detect, BattlEye posture) | `GTAVOVR/GTAVOVR.cpp` (CreateRemoteThread), `OVRInjectShim/` (dxgi proxy) | Parameterize target (done in wave 1); preflight hardening in Phase 10 |
| Hook core | Install/remove function + vtable hooks; survive resize, device-loss, alt-tab, unload | `D3DHook/D3DHooks_VRManager.hpp`, MinHook in `ThirdParty/minhook` | Robustness pass (wave 2); move out of the 1600-line header |
| RHI | Device/swapchain/texture abstraction; eye-target management; blit/resolve; format+colorspace conversion | none — D3D11 types used directly everywhere | New `IRHI*` (§4); existing D3D11 code becomes `RHID3D11*` behind it |
| Stereo engine | Stereo mode policy (AER/dual-pass/Z3D); per-eye view/projection composition; late-latch; frame pacing | scattered through `hookedPresent`, `GtaCameraHook`, `HMDRenderer` | New `StereoEngine` (§5); consumes IRHI + IVRBackend |
| XR adapter | Session/frame loop/swapchains/poses/actions/overlay, runtime-agnostic | `VR/IVRBackend.hpp` + `OpenXR/`, `VR/Open*Backend.*` | Exists; wave-1 fixes (action attach, frame wedge, D3D projection, pose caching) |
| Title plugin | Camera/FOV/game-state resolution per title+build; cutscene/vehicle signals; quirks | `Game/GtaCameraHook`, `GtaCameraFov`, `GtaGameState`, `PatternScanner` | Put behind `ITitlePlugin` (§6); patterns sourced from `BuildManifest` (wave 1) |

Cross-cutting: `Game/OnlineGuard` (new, wave 1), `Log` (rewritten, wave 1),
`VR/SharedSettings.hpp` (config/stats atomics), PerfStats (new, Phase 8),
CrashDump (new, wave 2).

## 4. The RHI abstraction (the load-bearing decision)

Targets span D3D11 (GTA V Legacy) → D3D12 (GTA V Enhanced, RDR2) → Vulkan (RDR2 alt)
→ D3D9 (GTA IV, likely unsupported). The abstraction exists so that *stereo* and
*XR submission* code is written once.

### 4.1 Interface sketch

```cpp
enum class RHIApi { D3D11, D3D12, Vulkan, D3D9 };
enum class Eye { Left, Right };

struct RHITextureDesc { uint32_t w, h; uint32_t format; uint32_t miscFlags; };

class IRHITexture { // an eye target or the game's backbuffer, opaque to upper layers
public:
    virtual RHITextureDesc Desc() const = 0;
    virtual void* NativeHandle() = 0;        // ID3D11Texture2D*, ID3D12Resource*, VkImage
    virtual void* NativeSharedHandle() = 0;  // NT handle for cross-device/API sharing
};

class IRHIDevice {
public:
    virtual RHIApi Api() const = 0;
    virtual IRHITexture* CreateEyeTarget(const RHITextureDesc&) = 0; // shared with XR runtime
    virtual void CopyForSubmit(IRHITexture* dst, IRHITexture* src) = 0; // blit/resolve/convert
    virtual bool ReadbackDepth(IRHITexture* depthSrc, DepthReadback&) = 0; // Z3D only
    virtual void* NativeDevice() = 0;
};

class IRHISwapchain { // wraps the game's swapchain; produced by the hook core
public:
    virtual IRHITexture* Backbuffer() = 0;       // valid only during Present callback
    virtual IRHITexture* DepthBufferHint() = 0;  // may be null; Z3D fallback
    virtual void GetDesc(SwapchainDesc&) = 0;
};
```

### 4.2 Proof — D3D11 immediate-context path (GTA V Legacy)

- `RHID3D11Device::CopyForSubmit`: `ID3D11DeviceContext::CopyResource` when formats/sizes
  match (today's `HMDRenderer::Render` path), else a blit shader pass (already runtime-compiled
  in `D3DHooks_VRManager.hpp`); MSAA backbuffer resolved first (exists).
- Eye targets created `D3D11_RESOURCE_MISC_SHARED` so the OpenXR D3D11 binding can consume
  them directly (current path copies into the XR swapchain image — acceptable; a shared-texture
  zero-copy variant slots in without interface change).
- Frame ordering: all work happens on the game's immediate context inside the Present hook;
  no queues, no fences. CopyForSubmit is synchronous by construction.

### 4.3 Proof — D3D12 command-list path (GTA V Enhanced / RDR2)

- Hook core hooks `IDXGISwapChain3::Present`/`Present1` the same way, but there is no
  immediate context: `RHID3D12Device::CopyForSubmit` records the copy on **our own
  direct command queue** (created at init on the game's device), signals a fence, and
  waits on it before returning — presenting the same synchronous contract upward.
- Backbuffer lifetime: `GetBuffer` returns a resource with no refcount guarantees across
  frames; `RHID3D12Swapchain::Backbuffer()` wraps it per-Present and never caches.
- Resource states: transition backbuffer RENDER_TARGET→COPY_SOURCE→back around the copy;
  common-state assumptions are validated by debug-layer messages during development.
- The stereo engine and XR adapter see the same `IRHI*` calls in both sketches; only the
  backend differs. That is the proof the boundary holds.

### 4.4 Deliberately outside the abstraction

- Draw-call interception for culling/HUD (title-plugin concern, API-specific by nature).
- XR-runtime swapchain specifics (IVRBackend already abstracts those; the RHI hands it
  textures, it does not care how).
- Shader-based post steps (vignette, Z3D displacement) are RHI-backend plugins behind
  `IRHIPostFX` later; until D3D12 work starts they stay D3D11-internal — documented debt,
  not hidden debt.

## 5. Stereo engine

Modes (ADR-0002): **AER default**, dual-pass experimental, Z3D fallback.

```
Per Present (game render thread):
  if OnlineGuard.IsDisabled() → pass through, no camera writes, no XR submit
  backend->BeginFrame()                       // visibility-gated: skipped entirely when
                                              // the session is not VISIBLE/FOCUSED
                                              // (never blocks the render thread)
  plan = EyeDelivery.Plan(frameIndex)         // AER: which eye is fresh, per-layer
                                              // texture mapping (layer i <- eye tex i)
  blit backbuffer → fresh eye texture         // stale eye keeps its OWN previous frame
  submit BOTH layers every EndFrame           // fresh eye new frame, stale eye its own
                                              // last frame (runtime ATW/ASW covers it)
  original Present (desktop mirror)
  pose = backend->LateLatchPose()             // sampled NOW, not at game-camera time
  plugin->ApplyCamera(nextEye, pose, ipd)     // LATEST safe point: right after Present
                                              // returns, before the next game render
  PerfStats.Record(frame)                     // p99 ring buffer, Phase 8
```

Camera resolution (patterns/sweeps) never runs on this thread: a bounded
background worker in `Game/` resolves and hands off candidates (30 s budget,
clean give-up to mono, cheap periodic retry); the render thread adopts in O(1).

- **IPD always from the runtime** (`xrLocateViews` eye poses / OpenVR eye-to-head), never
  a constant; world scale depends on it.
- **Projection always from runtime FOV** (`XrFovToProjectionMatrixD3D`), asymmetric,
  never the game's own projection. Near/far per-title (manifest).
- **Late-latch**: pose sampled immediately before camera write + submission. The game
  camera update time is arbitrarily early; latching there ships ~40 ms of latency.
- **Culling**: the game culls against its own frustum. Mitigation v1: FOV override via
  plugin (`GtaCameraFov`) per camera type (LukeRoss's `FOVUni` lesson); full culling-frustum
  patching is a per-title RE task tracked in known-issues. Shadow/LOD artifacts documented.
- **Frame pacing**: `xrWaitFrame` on the render thread is accepted for v1 (it *is* the
  marriage of game loop to XR loop); the failure-mode fixes (wave 1) make it non-wedging.
  Decoupling into a present-thread + XR-thread split is a Phase 8 perf decision, not v1.

## 6. Title plugins

```cpp
class ITitlePlugin {
public:
    virtual bool Identify(const ProcessInfo&) = 0;        // exe name + version manifest
    virtual bool Resolve(BuildManifest&) = 0;             // AOB scan; false = unsupported build
    virtual void ApplyCamera(Eye, const Pose&, float ipd, const Fov&) = 0;
    virtual GameState State() = 0;                        // onfoot/vehicle/cutscene/menu
    virtual void SetFovOverride(float) = 0;               // culling mitigation
    virtual const QuirkTable& Quirks() = 0;               // e.g. "ViewInverse write needed"
};
```

- `GTAVLegacyPlugin` = today's `GtaCameraHook`+`GtaCameraFov`+`GtaGameState`, manifest-fed.
- Plugin loading is static (compiled in) for v1; the *interface* is the boundary that lets
  GTAVE/RDR2 land without core changes.
- OnlineGuard sits below plugins: a plugin is never even resolved when the guard fails-closed.

## 7. XR runtime adapter

- Existing `IVRBackend` retained (audit: clean, no runtime types leak).
- OpenXR primary (ADR-0006): wave-1 fixes action-attach ordering, frame-loop wedge,
  D3D projection (`XrFovToProjectionMatrixD3D`), pose caching.
- OpenVR backend kept for headsets/runtimes without OpenXR D3D11 support; units + pose
  caching fixed in wave 1.
- Overlay (settings UI) is per-runtime surface + shared ImGui layer — already structured
  that way (`IOverlaySurface`, `XROverlayUI`).

## 8. Config system

- INI (no new dependencies), three tiers, later wins: shipped defaults < per-title profile
  (`manifests/<title>.ini` + `gtavr_settings.ini` sections) < user override
  (`GTAVR_SETTINGS_DIR` or beside GTA5.exe).
- Hot-reload: settings re-read on file-change tick (checked once per second from the
  Present hook); camera/manifest patterns are load-once at resolve time.
- Runtime selection: `GTAVR_BACKEND=openxr|openvr`, `XR_RUNTIME_JSON` passthrough.
- **No config key can weaken the online guard** (ADR-0003).

## 9. Threading model

- Game render thread: hook core, stereo engine, RHI copies, XR frame loop, plugin camera
  writes (racing the game's own writes — see known-issues; mitigation: write timing +
  ViewInverse-style correction per quirks).
- XR event pump: existing OpenXR session thread (poll events, state machine).
- No other threads. Logging and stats are mutex/atomic-protected for the rare cross-thread
  touch (overlay input, guard timers).

## 10. Robustness model (what must never crash the game)

| Event | Required behavior | Status |
|---|---|---|
| DLL unload / process detach | Ordered: stop camera writes → disable hooks → XR session end → MinHook uninit; never `MH_Uninitialize` mid-hook | done (wave 2) |
| ResizeBuffers / resolution change | Release eye targets + depth hints; recreate lazily next frame | done (wave 2) |
| Device removed/lost | Log, pass through, attempt re-init once; never dereference stale device | done (wave 2) |
| Alt-tab / fullscreen flip | XR session visibility loss handled by runtime events; submission pauses cleanly | done (wave 2) |
| Online session detected | Immediate pass-through + camera-write stop + log reason; sticky until process restart | done (wave 1 module, wave 2 wiring) |
| Unsupported game build | Manifest miss → clear diagnostic, mod inert (no heuristic retries forever) | done (wave 1) |
| xrBeginFrame failure | Recoverable frame state, no wedge | done (wave 1) |
| Crash inside our code | Vectored handler → minidump + log bundle; never `exit()` the host | done (wave 2) |

## 11. Out of scope (v1)

- D3D12/Vulkan backends (GTAVE/RDR2) — interface-proven here, implemented in their own phases.
- GTA IV — expected unsupported (see feasibility §2.4).
- Motion-controller→game-input emulation beyond head-look/aim decoupling already present.
- Any online-mode functionality, permanently.
