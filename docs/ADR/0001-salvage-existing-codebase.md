# ADR-0001: Salvage the existing OVRInject codebase instead of rewriting

Status: accepted (2026-07-24)

## Context

The repo contained a partially-working mod: 2D-screen-viewer VR, fake stereo,
a mid-refactor camera injection, and several dead generations of code. The
mission allowed starting from scratch if the code "doesn't help".

A six-track audit found:

- The full solution builds clean (`Release|x64`, v145, 0 errors).
- `VR/IVRBackend.hpp` is a genuinely clean runtime abstraction.
- The OpenXR stack (instance/session/frame loop/swapchains/views/actions) is
  ~90% complete with four isolated, fixable bugs (action-attach ordering,
  frame wedge, GL-style projection matrix, getter const_cast).
- The hook chain and 6DOF camera injection work; the gaps (no online guard,
  fake stereo, no unload/resize handling, unsafe logging) are additive work,
  not design flaws.
- Dead code (`Vive/Scene`, `TrackedController`, `Detour` wrapper, gutted
  files, unused shader headers) is deletable without touching live paths.

## Decision

Salvage. Keep the VR layer, hook chain, camera/game layer, loader/shim, and
overlay stack. Delete dead generations. Fix the audited defects in place.
Restructure the Present-hook monolith incrementally (Phase 3–4) rather than
by rewrite.

## Consequences

- We inherit months of embedded fixes (MSAA resolve, depth-format tables,
  overlay wiring) that a rewrite would silently lose.
- We also inherit the monolithic `D3DHooks_VRManager.hpp` and the heuristic
  pile in `GtaCameraHook`; both are quarantined behind the Phase 1 layering
  and the manifest system rather than "cleaned up" opportunistically.
- Rewrite remains available as a fallback if wave-2 robustness work reveals
  structural rot the audit missed.

## Alternatives considered

- Full rewrite: rejected — discards working XR plumbing; highest risk of
  re-introducing solved bugs; no architectural benefit the layering plan
  doesn't already deliver.
- Cherry-pick into a new tree: rejected — same loss risk, plus history/merge
  churn for no gain over in-place restructuring.
