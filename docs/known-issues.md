# Known Issues

Policy: under-promise. If it is not verified on hardware, it is listed here as
UNVERIFIED or broken — never implied to work. Updated per phase; dates mark
the last status change.

Legend: 🔴 broken/P0 · 🟠 degraded · 🟡 limitation/by-design · ⚪ fixed (kept
one release for the record) · ❓ UNVERIFIED

## Framework-wide

- ⚪ **Render-thread freeze (observed live) — FIXED** — root cause: whole-process
  memory sweeps ran synchronously on the render thread and repeated forever
  (~0.1 fps = looked frozen). All camera/FOV resolution now runs on a bounded
  background worker (30 s budget, clean give-up + mono fallback, cheap 60 s
  retries); render thread is O(1) ~10–30 µs/frame. In-game confirmation pending. 2026-07-25
- ⚪ **OpenVR submission hardened** — eye formats normalized away from TYPELESS
  (silent-rejection risk, agent audit), Submit return codes logged rate-limited. 2026-07-25
- ⚪ **XR initial swapchain clamp** — no more 5424×5356-vs-4096×4045 mismatch
  strip / ~117 MB/eye waste on Crystal Super at init. 2026-07-25
- 🟠 **Double frame pacer** — xrWaitFrame + desktop vsync share the render
  thread (p99 risk). Mitigation ON by default: `desktopMirrorSyncOverride=1`.
  Desktop mirror may tear (cosmetic only). 2026-07-25
- 🔴 **Online guard did not exist before this milestone** — memory-writing
  builds prior to the OnlineGuard integration must never be used. (Being
  fixed: OnlineGuard module + Present-hook wiring; see ADR-0003.) 2026-07-24
- 🟠 **Camera write race** — plugin writes the camera matrix from the Present
  (render) thread while game threads also write it → possible single-frame
  jitter. Mitigation implemented 2026-07-25: the write now lands immediately
  after the original Present returns (latest point strictly before the next
  game render on the same thread; smallest overwrite window against the
  game's own camera refresh — contract documented at the write site in
  `OVRInject/Stereo/StereoEngine.cpp`). A stronger ViewInverse-style
  correction quirk (LukeRoss lesson) remains unimplemented. UNVERIFIED
  impact in-headset. 2026-07-25
- 🟠 **Culling/LOD/shadow pop-in at VR FOV** — game culls against its original
  frustum. v1 mitigation: per-camera-type FOV override. Full culling-frustum
  patch is per-title RE work, not yet scheduled. 2026-07-24
- 🟠 **Temporal effects under AER** — TAA/motion-blur/SSR accumulate from
  alternating viewpoints → smearing/eye-rivalry. Decision table in
  `docs/hud-postfx.md`; disable-list recommended settings required. ❓ exact
  per-effect severity UNVERIFIED in-headset. 2026-07-24
- 🟡 **xrWaitFrame throttles the game loop** — accepted v1 design (it is the
  frame-pacing marriage); XR-thread split is a Phase 8 decision. 2026-07-24
- 🟡 **First Present frame swallowed** by the hook; desktop mirror forced to
  SyncInterval 0. Scheduled in wave 2. 2026-07-24
- ❓ **Depth-buffer grab for Z3D reprojection likely never engages** (DSV read
  at Present time after unbind; MSAA depth rejected) → Z3D silently degrades
  to plain blit. Needs mid-frame depth capture. 2026-07-24
- ❓ **BattlEye posture detection** — guard assumes "BE active ⇒ not the
  sanctioned story-mode posture". Edge cases (BE installed but story mode)
  intentionally fail closed; see ADR-0003. 2026-07-24

## GTA V Legacy (primary)

- ⚪ **OpenXR session stuck at READY, nothing displayed (observed live) —
  FIXED** — root cause: the visibility gate skipped `xrWaitFrame` until the
  session became visible, but the runtime's session state machine
  (READY→SYNCHRONIZED→VISIBLE) advances **from frame-loop activity** →
  chicken-and-egg, session wedged at READY (the Pimax "void" the user saw).
  The gate is now `IsBegun()`: after `xrBeginSession` the frame loop runs
  every frame, with `shouldRender=false` driving the engine's existing
  EndFrameEmpty path until the runtime grants visibility. Also fixed the
  heartbeat flood (time-based, 5 s). In-headset confirmation pending. 2026-07-26
- ⚪ **Backend/verbose settings not reaching an already-running game** —
  env vars (`GTAVR_BACKEND`, `GTAVR_VERBOSE`) only propagate to processes we
  spawn. Backend is now also read from `gtavr_settings.ini [Runtime] backend=`
  (the panel writes it before injecting). The settings fallback now resolves
  beside the loaded `OVRInject.dll`, not from GTA's process working directory.
  2026-07-30
- ❓ **OpenXR session can sit pre-READY forever** (observed live with the
  Pimax runtime: session created, overlay initialized, zero
  "OpenXR session state" lines → `xrBeginSession` never runs → controllers
  and overlay never come alive). `XRSession::PollEvents` now logs a
  "waiting for session READY" heartbeat (~every 5 s) so the log proves
  event pumping is alive; if heartbeats appear, the runtime (HMD idle,
  focus held by another app, Pimax quirk) is what blocks READY — outside
  mod code. Root cause UNVERIFIED pending HMD retest. 2026-07-25
- ⚪ **Overlay keyboard parity on OpenXR** — `Delete`/`Insert`/`F10` now
  toggle the OpenXR overlay exactly like the OpenVR path, polled every
  frame even while the session is pre-READY or controllers are dead
  (previously F10/Insert were polled only inside the renderable+located
  frame path, so the menu could not be opened at all before visibility).
  Overlay remains hidden by default (`GTAVR_OVERLAY=1` to force-show).
  2026-07-25
- ⚪ **Overlay usable without controllers** — physical mouse drives the
  ImGui cursor (visible in-headset) and arrows+Enter drive ImGui keyboard
  nav. Limit: nav keys also reach the game (no WndProc hook); text entry
  unsupported. In-headset feel UNVERIFIED. 2026-07-25

- ❓ **AER stereo implemented in code, UNVERIFIED in-headset.** The camera is
  rewritten per frame with the next eye's pose (one-frame delay), each eye
  texture holds that eye's own render, and both eye layers are submitted
  every frame — fresh eye + the stale eye's own previous frame, never the
  other eye's (`OVRInject/Stereo/EyeDelivery.hpp`, driven by
  `StereoEngine::OnPresent`). Each texture now retains the runtime pose that
  actually rendered it, so OpenXR ATW/ASW no longer receives stale content
  labelled as the current pose.
  Until validated in a real headset this remains UNVERIFIED; the mono
  fallback (camera hook not ready) intentionally stays mono. Z3D depth
  reprojection remains the fallback mode. 2026-07-25
- ❓ Camera AOB patterns — manifest-seeded from prior code + `gtavr_camera.ini`
  (Enhanced-derived and GTAForums legacy patterns); which set matches each
  Legacy build is UNVERIFIED until resolved on the live build. 2026-07-24
- ⚪ **Comfort defaults now match `docs/user/comfort.md`** — snap turn ON
  (45°), locomotion vignette ON (0.5), cutscene theater (VirtualScreen) ON,
  vehicle horizon lock ON, quick-recenter bind (right stick click) live.
  Sources: `gtavr_settings.ini` template + `XR::VRSettings` defaults
  (XROverlayUI.hpp); `VR::ComfortSettings` atomic initializers remain
  false/legacy but are overwritten at settings load on both runtimes.
  In-headset validation still pending. 2026-07-24
- ❓ **Vignette is now locomotion-gated** (fades in with movement, off while
  stationary) instead of always-on. Proxy = max(head-translation speed, left
  stick) + a 0.5 floor while in-vehicle — game-speed input is unavailable
  without Game/ changes, so keyboard/gamepad-only walking may under-trigger.
  UNVERIFIED in-headset. 2026-07-24
- ❓ **Vehicle horizon lock** implemented as a post-write pitch/roll filter of
  the composed camera rotation in StereoEngine (yaw passes through; requires
  decoupling, which defaults on; correction skipped while looking
  near-vertical, e.g. aerobatics). In-vehicle detection rides the UNVERIFIED
  camera-hash heuristics; in-headset behavior UNVERIFIED. 2026-07-24
- 🟡 Cutscene handling relies on camera-hash heuristics + VirtualScreen theater
  mode; cutscene mis-detection = forced camera motion = P0 comfort defect.
  **Theater engagement path re-verified in code after the StereoEngine
  extraction**: same-frame engage (game-state update → VirtualScreen enable →
  eye submission from VirtualScreen, camera writes suspended). Detection
  reliability (camera hashes + cutscene-flag patterns) UNVERIFIED; in-headset
  validation pending. 2026-07-24
- ❓ **HUD infrastructure implemented, in-game behavior UNVERIFIED.** The
  mechanism (shader-creation identification hooks, armed-state tracking,
  backbuffer→offscreen-RT substitution, per-eye alpha composite) is built in
  `OVRInject/D3DHook/HudRedirect.hpp`, config-gated by `[hud] enabled=1` in
  `manifests/gtav_legacy.ini`, and validated on a synthetic D3D11 harness
  (identification → substitution → composite → clear all PASS). Open items:
  the 4 seeded hashes are r7-era (old build, `verified=0`) and must be
  re-derived; FNV-1a-64 parity with 3Dmigoto hash naming is UNVERIFIED;
  composite is a screen-space blit (world-locked quad is follow-up); HUD
  during cutscenes is not composited onto the VirtualScreen; MSAA backbuffer
  disables substitution (identification still works); per-call hook overhead
  unmeasured in-game. 2026-07-24

## GTA V Enhanced

- ❓ Untouched by code so far. D3D12 RHI backend designed (architecture §4.3)
  but unimplemented. Camera AOB exists in `gtavr_camera.ini` (attributed to
  LukeRoss analysis). All Enhanced claims UNVERIFIED. 2026-07-24

## RDR2

- 🟡 Not started. DRM posture must be established first (Hard Constraint 2).
  Dual API (D3D12/Vulkan). GPU headroom for stereo questionable. All
  UNVERIFIED. 2026-07-24

## GTA IV

- 🟡 Expected **unsupported**: CPU-bound ceiling likely below comfort
  threshold (see `docs/00-feasibility.md` §2.4). Will be confirmed/refuted by
  a frametime probe, not by porting. 2026-07-24
