# ADR-0006: OpenXR primary runtime, OpenVR legacy fallback

Status: accepted (2026-07-24)

## Context

The codebase ships both an OpenXR stack (session/frame loop/swapchains/views/
actions — ~90% complete after wave-1 fixes) and an OpenVR backend behind the
same `IVRBackend`. One must be the default.

- OpenXR is the vendor-supported standard on Quest (Link), WMR, and modern
  SteamVR; gives us `xrLocateViews` predicted poses, standardized FOV for
  asymmetric projection, and composition-layer overlays.
- OpenVR is mature and is what LukeRoss used, but Valve's own direction is
  OpenXR-on-SteamVR, and maintaining two first-class paths doubles validation
  surface (Phase 9 matrix already multiplies title×build×GPU×runtime).

## Decision

- **Primary and default: OpenXR.** `GTAVR_BACKEND=openvr` remains as an
  explicit fallback for runtimes with broken D3D11 OpenXR interop.
- Validation matrix runs OpenXR first; OpenVR rows are run on a
  best-effort basis and marked UNVERIFIED where not covered.
- New features (foveation, depth layers, hand tracking) are OpenXR-only.

## Consequences

- SteamVR users go through SteamVR's OpenXR — one more moving part; preflight
  must detect the active runtime (`ActiveRuntime` registry key) and produce a
  human-readable error when misconfigured.
- OpenVR-specific overlay code (`OpenVROverlaySurface`) stays but is not the
  reference path; the OpenXR quad-layer overlay is.

## Alternatives considered

- OpenVR primary: rejected — freezes us on a legacy API and loses
  standardized pose prediction and overlays.
- Both first-class: rejected — validation cost; revisited if user telemetry
  demands it.
