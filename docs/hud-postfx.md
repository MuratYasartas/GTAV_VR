# Phase 6 — HUD, UI & Post-Processing Decisions (GTA V Legacy)

Every screen-space effect gets an explicit decision and reasoning. "AER" =
alternate-eye rendering (ADR-0002): each frame is rendered from one eye's
viewpoint, alternating — so any *view-dependent temporal* effect sees a
ping-ponging history unless handled.

## Post-processing compatibility table

| Effect | Decision | Reasoning | How actioned |
|---|---|---|---|
| TAA / TXAA | **Disable** | Temporal history alternates eyes under AER → smearing/ghosting/eye-rivalry. Nausea risk (P0 comfort class). | In-game setting off (preflight instructs; runtime check UNVERIFIED) |
| Motion blur | **Disable** | Simulated camera blur is wrong in a headset and amplifies judder perception. | In-game setting off |
| Depth of field | **Disable** | Fake focal plane is a camera artifact; in stereo it reads as eye-focus malfunction. | In-game setting off |
| Lens flare / chromatic aberration / film grain | **Disable** | Lens artifacts read as eye damage in HMD. | In-game settings off |
| SSAO | **Disable recommended** | View-dependent; alternates per eye-frame → shimmer. Revisit if per-eye SSAO proves stable. ❓UNVERIFIED in-headset | Setting: low/off |
| SSR (screen-space reflections) | **Disable** | View-dependent and wrong-eye half the time; GTA V SSR is subtle anyway. | Setting off |
| MSAA | **Keep** | Geometric AA, per-frame, no temporal state. Note: MSAA depth currently blocks Z3D depth grab (known-issues). | User choice |
| FXAA | **Keep** | Screen-space but per-frame, symmetric. | User choice |
| Bloom | **Keep** | Effectively view-symmetric at these scales. ❓verify in-headset | Default |
| Shadows / cascades | **Keep, expect artifacts** | Cascades tuned to original FOV; visible ends at VR FOV (culling class, see known-issues). | FOV override mitigation |
| Anisotropic filtering | **Keep** | View-independent. | Default |
| Frame Scaling Mode | **Configure** | R.E.A.L. used 5/2 to render world at 2700×2700 for HMD supersampling. Ours: per-eye resolution from runtime recommended size. | `gtavr_settings.ini` |

## HUD / UI

| Element | Decision | Reasoning |
|---|---|---|
| HUD, minimap, subtitles | **Redirect → world-locked quad** | Screen-space 2D is at wrong depth for both eyes (permanent vergence conflict). Identify draw calls by shader hash (manifest-driven), render to offscreen target, composite as quad at configurable distance/scale. R.E.A.L. r7 proves the technique (4 HUD/radar hashes analyzed in-repo); current-build hashes must be re-derived — ❓UNVERIFIED. |
| Menus / loading / pause | **Theater mode** | No 3D scene to stereoscopize; render on `VirtualScreen` cinema quad. |
| Cutscenes | **Theater mode (mandatory)** | Forced camera motion in VR is the single most nausea-inducing case (mission P0). Detection via plugin game-state (camera-hash heuristic today); on detect: suspend AER, show stream on VirtualScreen, head-locked comfort reference. Mis-detection is a P0 defect class. |
| In-headset settings UI | **OpenXR quad layer + ImGui** (exists) | Users cannot alt-tab in a headset; all comfort/stereo settings must be reachable in-VR. |

## Notes

- Action mechanism today = documented user settings (preflight checklist) +
  FOV override + theater/quad infrastructure. Automatic in-memory effect
  disabling via natives/patches is possible in principle; any such mechanism
  ships only after the same effect has been validated manually — ❓UNVERIFIED,
  tracked in `docs/known-issues.md`.
- Shader-hash lists live in `manifests/gtav_legacy.ini` per build (ADR-0004),
  never in code.
