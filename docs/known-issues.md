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
- ❓ HUD stereo: r7-era HUD shader hashes exist from prior-art analysis;
  current-build hashes must be re-derived. HUD redirect infrastructure only. 2026-07-24
- 🟡 Cutscene handling relies on camera-hash heuristics + VirtualScreen theater
  mode; cutscene mis-detection = forced camera motion = P0 comfort defect.
  In-headset validation pending. 2026-07-24

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
