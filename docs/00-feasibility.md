# Phase 0 — Feasibility & Prior Art

Status: complete (documentation-only phase; no in-headset claims herein are verified).
Date: 2026-07-24. Author: senior graphics/systems assessment.

Evidence legend:
- **[repo]** — verified against files in this repository (path cited).
- **[web]** — verified against public sources (URL cited), retrieved 2026-07-24.
- **UNVERIFIED** — not confirmed on real hardware / not confirmed against a live game build.

---

## 0. Mission context (filled from evidence, not placeholders)

| Field | Value | Basis |
|---|---|---|
| Primary target | **GTA V Legacy** (D3D11), installed at `D:\Rockstar Games\Grand Theft Auto V Legacy` | `GTAV_INSTALL_DIR` env var consumed by `OVRInject/OVRInject.vcxproj` post-build step [repo] |
| Secondary targets | GTA V Enhanced (D3D12), RDR2 (D3D12/Vulkan), GTA IV (D3D9) | mission brief |
| Runtime | **OpenXR primary**, OpenVR legacy fallback — both backends already exist | `OVRInject/VR/`, `OVRInject/OpenXR/` [repo] |
| Toolchain | C++ / MSVC, PlatformToolset **v145**, MSBuild 18.0.5, VS solution (not CMake) | builds clean: 3/3 projects, 0 errors, `x64/Release/` [repo] |
| Headset / GPU / CPU | unknown | **UNVERIFIED** — needed before Phase 8 budgets |
| Distribution intent | undecided; recommend free/open-source (see §5 legal exposure) | — |

---

## 1. Starting position: assessment of the existing codebase

This repo is **not greenfield**. Six parallel audits (VR layer, D3D/game layer,
launcher/shim, build, prior-art intel) were run; full detail in the checkpoint
that accompanies this document.

**Verdict: SALVAGE, do not restart.** The tree compiles and links as-is
(`Release|x64`, all three projects). The architecture already matches the layered
shape the mission demands; the gaps map onto mission phases rather than onto
design flaws.

### 1.1 What is real and worth keeping

- **VR backend abstraction** (`OVRInject/VR/IVRBackend.hpp`) — clean, runtime-agnostic,
  no OpenVR/OpenXR types leak. KEEP.
- **OpenXR plumbing** (`OVRInject/OpenXR/`) — instance/session/frame loop
  (`xrWaitFrame`/`xrBeginFrame`/`xrEndFrame`), per-eye swapchains, `xrLocateViews`
  with STAGE→LOCAL fallback, full action system. ~90% spec-correct. KEEP-WITH-FIXES:
  - `xrAttachSessionActionSets` is never reachable (attach must precede `xrBeginSession`;
    `XRSession.cpp:197-201` vs `XRHMDSupport.cpp:308-312`) → OpenXR controllers dead.
  - Frame-loop wedge if `xrBeginFrame` fails (`XRHMDSupport.cpp:364-366`,
    `XRFrameManager.cpp:34-37`).
  - ~~`XrFovToProjectionMatrix` builds an OpenGL-style projection~~ FIXED (wave 1):
    replaced by `XrFovToProjectionMatrixD3D` (`XMMatrixPerspectiveOffCenterRH`,
    z∈[0,1]); unit-tested by the projection oracle in `tests/TestProjection.cpp`.
- **Present hook chain** (`OVRInject/D3DHook/D3DHooks_VRManager.hpp`) — MinHook on
  `D3D11CreateDeviceAndSwapChain`, then on the swapchain's `Present` target. Coherent,
  carries months of fixes. KEEP-WITH-FIXES (robustness gaps below).
- **Camera/game layer** (`OVRInject/Game/`) — working 6DOF camera-matrix injection
  (per `GTAV_VR_6DoF_Implementation_Report.md`), pattern scanner, FOV control,
  INI-overridable patterns (`gtavr_camera.ini`). Fragile heuristics, but hard to
  re-create. KEEP-WITH-FIXES.
- **Launcher + dxgi-proxy shim** (`GTAVOVR/`, `OVRInjectShim/`) — CreateRemoteThread
  injector plus the correct vehicle (proxy `dxgi.dll`) for early hooking. KEEP-WITH-FIXES.
- **Overlay stack** (`XROverlay`/`XROverlayUI`/`OpenVROverlaySurface` + ImGui) — in-headset
  settings UI exists and is wired. KEEP.

### 1.2 What is missing or broken (the actual work)

| Gap | Severity | Mission phase |
|---|---|---|
| **No GTA Online / multiplayer detection anywhere** — per-frame memory writes with zero online guard | **P0 — Hard Constraint 1 violation** | 7 |
| Stereo is fake: same 2D backbuffer to both eyes, or depth-displaced reprojection; no per-eye scene render | P0 — the core feature | 4 |
| ~~No `ResizeBuffers` / device-lost / alt-tab handling~~ DONE (wave 2): `ResizeBuffers` vtable hook + lazy recreate, device-lost one-shot re-init, minimized/occluded pass-through; mod-initiated game-resolution scaling stays disabled (game holds buffer refs) | P1 | 3 |
| ~~No unload path~~ DONE (wave 2): ordered `DLL_PROCESS_DETACH` teardown (disable hooks → drain in-flight frames → VR/overlay shutdown → MinHook teardown); `ShutdownVR` idempotent, no MinHook calls mid-hook | P1 | 3 |
| Present hook only lands if device created via `D3D11CreateDeviceAndSwapChain` after injection — mitigated (wave 2): `D3D11CreateDevice` + `CreateDXGIFactory1/2` → `IDXGIFactory::CreateSwapChain` hooks + 30 s watchdog that logs and stays inert; no scan of pre-existing swapchains | P2 | 3 |
| Camera matrix written from Present thread, no sync with game writes → jitter/races | P1 | 4/5 |
| Unsafe logging (`vsprintf` into guessed buffers, `LOGFATALF` kills the game) | P2 | 3 |
| Dead generations still compiled: `Vive/Scene/`, `TrackedController`, `Detour/` wrapper, gutted `D3DHooks.hpp`/`HMDSupport.*`, dead shader headers | hygiene | 1 |
| No version-pinned manifest: AOBs hardcoded in `GtaCameraHook.cpp:274-293` (credited to LukeRoss/Enhanced) though `gtavr_camera.ini` allows overrides | P1 — Prime Directive | 1/7 |
| No unit tests, no golden-image, no soak infra | P1 | 9 |
| Whole-process memory sweeps with raw derefs on render thread; test-calling resolved game functions (`GtaCameraFov.cpp:234-246`) | P2 — crash risk | 7 |

### 1.3 Prior-art intelligence already in this repo

- `tmp/gta5-real-mod/README.md` — LukeRoss's complete design playbook (AER, ViewInverse
  fix, per-camera FOV patches, cutscene handling, open problems). **No license file;
  all rights reserved; DMCA'd by Take-Two July 2022.** Learn ideas, copy nothing.
- `GTAV_REAL_mod_Decompiled/` — analysis of R.E.A.L. r7: patch vocabulary
  (`FOV1stCar/FOV3rd/FOVUni/ViewInverse`), 4 HUD/radar shader hashes with fixed HLSL,
  `RealVR.ini` option contract, 3DMigoto-based HUD stereo fixing.
- `Lukrossmod_Decompiled/` — universal `RealVR64.dll`: dxgi-proxy injection, Vulkan
  implicit layer, **network-API hooking (`recv/send/WSARecv/WSASend`) — a proven
  online-session signal source for our Hard Constraint 1 guard**, CUDA optical-flow
  reprojection, `GTAVEPatchCamera` (confirms GTA V Enhanced is tractable).
- `gtavr_camera.ini` — working camera AOB for GTA V Enhanced (`48 8B 41 10 F3 0F 10 00
  F3 0F 10 48 08`, chain +0x10, view matrix +0x1F0) plus alternates incl. the legacy
  GTAForums pattern. **UNVERIFIED which pattern set matches the installed Legacy build.**
- `VRMod previous/GTA5_VR_Mod/` — older scaffolding; its RAGE patterns are self-admitted
  placeholders. Structure reference only; **do not trust its signatures.**

---

## 2. Per-title feasibility

### 2.1 GTA V Legacy (primary target)

- **API:** Direct3D 11 [repo: entire `OVRInject` D3D11 hook chain is built for it; install dir confirms Legacy].
- **Threading model:** RAGE renders from a dedicated render thread; `Present` hook runs on it.
  Camera memory is written by game threads concurrently with our hook → synchronization
  policy required (Phase 5). [repo: defect evidence `GtaCameraHook.cpp:1599-1634`] UNVERIFIED in detail.
- **Anti-tamper / anti-cheat:** **BattlEye** added Sept 2024 — for **GTA Online only**.
  Rockstar ships an official launcher toggle to disable it for Story Mode; GTA Online
  refuses to start with BattlEye disabled. Story-mode modding remains viable and is the
  standard, sanctioned-by-practice posture. [web: videogamer.com/news/gta-5-online-battleye-anti-cheat-update/,
  rockstarintel.com/new-gta-online-update-adds-anti-cheat-for-11th-anniversary-with-battl-eye-anti-cheat-patch-notes/]
  **Our posture:** require Story Mode with BattlEye off; detect BattlEye/online and
  hard-disable. Never touch, patch, or evade BattlEye (Hard Constraints 1–2).
- **Script hook:** ScriptHookV (Alexander Blade) actively maintained, explicitly refuses
  GTA Online itself [web: dev-c.com/gtav/scripthookv/]. Available if native calls are
  needed (game-state detection); not required for the rendering path.
- **Modding entry points (proven in-repo):** dxgi proxy DLL; CreateRemoteThread launcher;
  ScriptHookV `.asi`; 3DMigoto `d3d11.dll` (shader-hash modding).
- **Feasibility:** **HIGH.** Most prior art, mature hooks, D3D11 is the friendliest API,
  and this codebase already injects, hooks, tracks 6DOF, and submits to XR. The missing
  core is true stereo (AER), which LukeRoss proved is achievable on exactly this title.

### 2.2 GTA V Enhanced (free upgrade path)

- **API:** Direct3D 12, ray tracing, TAA mandatory when RT enabled
  [web: pcgamingwiki.com/wiki/Grand_Theft_Auto_V_Enhanced].
- **Anti-cheat:** same BattlEye posture as Legacy [web: same as above].
- **Script hook:** ScriptHookV Enhanced support status **UNVERIFIED** (check dev-c.com at adoption time).
- **Prior art:** LukeRoss universal mod supports GTAVE (`GTAVEPatchCamera`,
  `Lukrossmod_Decompiled/strings/game_patches.txt`; ready-to-drop install in
  `GTAVE_Test_Setup/` with `[GTAVE_QUICK]` config) [repo]. Camera AOB already in
  `gtavr_camera.ini` [repo].
- **Feasibility:** **MEDIUM.** Proven tractable, but D3D12 command-list architecture
  requires a new RHI backend, and TAA/RT temporal accumulation fights AER (history from
  the wrong eye). Defer until Legacy ships.

### 2.3 RDR2

- **API:** D3D12 **and** Vulkan (user-selectable). Two RHI backends or a documented choice.
- **Anti-tamper:** Rockstar launcher/Social Club DRM. Exact current protection stack
  **UNVERIFIED** — must be established before committing (Hard Constraint 2: if it blocks
  legitimate proxy injection, the title is unsupported — document and move on).
- **Script hook:** ScriptHookRDR2 exists but update cadence is slow; currency **UNVERIFIED**.
- **Prior art:** LukeRoss has no RDR2 release in this repo (`RealRepo/` contains no RDR2
  entry) — a signal, not proof, that RDR2 is materially harder.
- **Feasibility:** **LOW-MEDIUM.** Hardest title on the list: newest engine, dual API,
  heaviest GPU load (base framerate headroom for stereo is questionable on mid GPUs).

### 2.4 GTA IV (Complete Edition)

- **API:** Direct3D 9 [web: gtaforums.com/topic/980272]. DXVK (D3D9→Vulkan) is the
  community-standard fix and changes our hook surface entirely.
- **Threading:** notoriously CPU-bound / single-thread-limited; DXVK release notes track
  GTA IV CPU-bound regressions explicitly [web: github.com/doitsujin/dxvk/issues/5631].
- **Script hook:** legacy GTA IV Script Hook exists; maintenance **UNVERIFIED**.
- **Feasibility:** **LOW.** As the mission anticipates: hitting stereo framerate may be
  physically impossible in CPU-bound scenes. Only viable posture is reprojection-heavy
  (Z3D-style) fallback, which violates our comfort bar for a first-class port.
  **Preliminary conclusion: last priority, likely documented as unsupported.**

---

## 3. Prior art — what each solved and how

| Project | What it solved | How | What we take |
|---|---|---|---|
| **LukeRoss R.E.A.L.** (GTA V r7, then universal RealVR64) | Full 6DOF VR in RAGE: AER stereo, camera control, cutscene/HUD strategy, FOV fixes, GTA V Enhanced support | **Alternate-eye rendering** — engine cannot render the same world state twice, so eyes alternate per frame; runtime ATW/ASW reprojects the stale eye. Supplementary **ViewInverse** render-time fix when the camera "resists" writes. Per-camera-type FOV patches. Network-API hooks as online signal. dxgi proxy injection. [repo: decompiled analyses; tmp README] | The stereo strategy (AER default), the ViewInverse lesson (Phase 4), the online-detection signal (Hard Constraint 1), camera AOBs. **License: all rights reserved, DMCA'd 2022 — re-implement, never copy.** |
| **vorpX** | Commercial generic VR injector; defines the G3D/Z3D vocabulary | **Geometry3D** = dual full-scene pass (correct, ~2× GPU); **Z-Buffer3D** = depth reprojection (cheap, fake parallax, breaks on transparency/particles) | Our fallback-mode vocabulary and user-facing tradeoff documentation (Phase 1 stereo-strategy ADR). Current repo's "Reprojection" mode is a Z3D variant. |
| **Praydog UEVR** | Engine-aware VR for Unreal titles | Hooks UE object model (UObject/Engine) for camera, not raw memory scans | Not applicable to RAGE (no public engine bindings) — proves the *value* of engine-aware hooks; our equivalent is AOB manifests + camera-struct RE. |
| **Kiero / MinHook** | Hooking primitives | Kiero: vtable address discovery across D3D9–12/Vulkan. MinHook: inline detours (already vendored in `ThirdParty/minhook` and in active use) [repo] | Keep MinHook; adopt Kiero-style vtable discovery when D3D12 lands. |
| **OpenXR SDK** | Runtime-agnostic XR API | Loader + `openxr_loader.lib` already integrated; D3D11 binding path implemented [repo: `XRGraphicsBinding.cpp`] | OpenXR as primary runtime; OpenVR backend retained for legacy. |
| **3DMigoto** | Shader-hash-based draw-call surgery (HUD fixing) | `d3d11.dll` proxy; identifies HUD shaders by hash; fixes via HLSL rewrites [repo: GTAV_REAL_mod, 4 HUD shader hashes + fixed HLSL] | Phase 6 HUD strategy: identify-by-hash, redirect, composite. |

---

## 4. Recommended target order

1. **GTA V Legacy** — D3D11, installed locally, most prior art, existing code targets it,
   BattlEye posture documented and workable. This is where the vertical slice (Phase 2)
   and all core phases happen.
2. **GTA V Enhanced** — same game, new renderer; D3D12 backend proves the RHI abstraction
   (mission Phase 1 requirement). Only after Legacy is stable.
3. **RDR2** — hardest; requires DRM posture investigation first, dual-API decision, and
   demonstrated performance headroom on Legacy/Enhanced before committing.
4. **GTA IV** — likely unsalvageable for comfort-grade stereo (CPU-bound); keep as
   documented-unsupported unless data says otherwise (Phase 8 will produce that data cheaply
   via a frametime probe without full porting).

Matches the mission default, with GTA V Enhanced inserted as an explicit step because the
user owns it (`GTAVE_Test_Setup/`) and it exercises the RHI abstraction earlier.

---

## 5. Kill criteria (per title — findings that abandon the port)

- **GTA V Legacy:** (a) Rockstar removes the Story-Mode BattlEye toggle or extends
  anti-cheat to story mode; (b) camera/culling AOBs cannot be stabilized across two
  consecutive game builds (manifest churn unbounded); (c) p99 frametime cannot meet the
  target runtime's reprojection threshold on min-spec hardware with all Phase 8 levers
  applied — comfort constraint is non-negotiable.
- **GTA V Enhanced:** D3D12 RHI backend + TAA eye-history partitioning cannot be made
  artifact-free (temporal smearing between eyes = nausea), or launcher/anti-cheat changes
  block legitimate proxy loading.
- **RDR2:** Any DRM/anti-tamper component blocks legitimate injection (Hard Constraint 2 —
  document and walk away); or GPU headroom data shows <45 fps base at lowest viable settings
  on target hardware.
- **GTA IV:** CPU-bound frametime floor > runtime reprojection threshold in representative
  scenes (expected outcome, honestly).

---

## 6. Legal posture (summary; full text in `docs/LEGAL.md` — Phase 1 deliverable)

- Single-player only; online detection is a hard kill-switch, never bypassed.
- No anti-tamper circumvention: BattlEye is left alone; we require the user's own
  Rockstar-provided toggle, we do not touch it.
- No redistribution of game assets or LukeRoss binaries/shaders/code — his work is
  all-rights-reserved and was DMCA'd; this repo's decompiled analyses are kept for
  interoperability study only and must never ship.
- Publisher mod policy can change; use is at the user's own risk.

---

## 7. Immediate next phases

- **Phase 1 (next):** `docs/01-architecture.md` + ADRs. Key ADRs already forced by this
  assessment: ADR-001 salvage-vs-rewrite (decided: salvage); ADR-002 stereo strategy
  (AER default, dual-pass option, Z3D fallback — needs the culling/ViewInverse analysis);
  ADR-003 online-detection design; ADR-004 version-manifest format.
- **Phase 2:** vertical slice on a controlled D3D11 sample — the existing codebase skips
  this; we do not. Dead-code deletion (§1.2 hygiene row) happens here so the slice
  exercises only live code.
