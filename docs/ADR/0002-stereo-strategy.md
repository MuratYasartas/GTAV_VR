# ADR-0002: Stereo strategy — Alternate Eye Rendering default, dual-pass option, Z3D fallback

Status: accepted (2026-07-24)

## Context

True stereo needs two views of the *same world state*. RAGE renders once per
frame and does not expose a way to re-render an identical scene state a second
time within the frame. Options:

- **AER (alternate eye rendering)**: eyes alternate per frame (L,R,L,R…); the
  runtime's ATW/ASW reprojects the stale eye. Proven on exactly this title by
  LukeRoss R.E.A.L. (decompiled analysis + README in-repo). Per-eye update is
  half the base rate; temporal effects see alternating viewpoints.
- **Dual full-scene pass**: correct stereo every frame, ~2× GPU cost, and —
  decisively — requires forcing the engine to render twice from one world
  state, which prior art judged impractical on RAGE.
- **Z3D depth reprojection**: one render + depth-displaced second view (vorpX
  vocabulary). Cheapest, fake parallax, breaks on transparency/particles.
  A Z3D-variant already exists in the codebase ("Reprojection" mode).

Comfort constraint: AER demands a high, stable base framerate (≥80–90 fps so
each eye sees ≥40–45 Hz with runtime reprojection covering the gaps). This is
a *measurement gate* for Phase 8, not an assumption.

## Decision

- **Default: AER**, with per-frame IPD camera offset and late-latched pose,
  runtime reprojection covering the off-eye. This matches proven prior art on
  this engine.
- **Dual-pass: retained as an experimental mode** behind config, explicitly
  marked UNVERIFIED until a working second-scene-render mechanism exists.
- **Z3D: retained as fallback** for hardware that cannot hold AER base rates,
  with its artifacts (no occlusion disparity, transparency breakage) documented
  to the user in-product, not just in code.

The in-product settings UI must name the tradeoff per mode (update rate vs.
correctness vs. cost); silently picking for the user is not acceptable.

## Consequences

- Temporal effects (TAA, motion blur, SSR) accumulate from alternating
  viewpoints → Phase 6 effect-audit must disable or eye-partition them.
- Frame pacing and ASW interaction become first-class; p99 frametime gates
  the feature (Phase 8).
- Culling/LOD tuned for the game's frustum will misbehave for per-eye views;
  mitigated via FOV override (see `docs/01-architecture.md` §5), full fix is
  per-title RE tracked in known-issues.

## Alternatives considered

- Z3D as default: rejected — fake parallax is a comfort/quality regression
  unworthy of the primary mode.
- Dual-pass as default: rejected — no known mechanism on RAGE; ~2× cost even
  if one appears.
