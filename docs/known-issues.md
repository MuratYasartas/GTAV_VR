# Known Issues

Policy: under-promise. If it is not verified on hardware, it is listed here as
UNVERIFIED or broken — never implied to work. Updated per phase; dates mark
the last status change.

Legend: 🔴 broken/P0 · 🟠 degraded · 🟡 limitation/by-design · ⚪ fixed (kept
one release for the record) · ❓ UNVERIFIED

## Framework-wide

- 🔴 **Online guard did not exist before this milestone** — memory-writing
  builds prior to the OnlineGuard integration must never be used. (Being
  fixed: OnlineGuard module + Present-hook wiring; see ADR-0003.) 2026-07-24
- 🟠 **Camera write race** — plugin writes the camera matrix from the Present
  (render) thread while game threads also write it → possible single-frame
  jitter. Mitigation planned: ViewInverse-style correction quirk (LukeRoss
  lesson). UNVERIFIED impact in-headset. 2026-07-24
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

- ❓ Stereo is currently **fake** (2D backbuffer + optional depth displace).
  True AER stereo is the Phase 4 deliverable; everything before its in-headset
  validation is UNVERIFIED. 2026-07-24
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
