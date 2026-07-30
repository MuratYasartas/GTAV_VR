# ADR-0007: Head pose, recenter, and automatic alignment contract

Status: accepted (2026-07-30)

## Context

The scripted GTA camera, runtime projection layer, and OpenXR tracking space
all carry parts of the final view. A July 2026 regression low-pass filtered
the already-composed camera at 10 Hz, adding head latency. Yaw recenter also
rotated an off-origin head around the tracking-space origin, and manual image
offsets compensated for asymmetric runtime projections.

## Decision

- Sample the runtime pose as late as possible and never smooth the final
  head-composed orientation.
- Consume a recenter before the next camera write. A yaw-only OpenXR rebase
  rotates around the current head position, while the scripted camera drops
  its rotation and position references in the same frame.
- Derive image X/Y alignment from each eye's asymmetric OpenXR projection
  center. User offsets are fine trim only.
- Interpret `worldScale` as apparent world size: tracked meters and runtime
  IPD use `1 / worldScale`; manual camera trim remains in game meters.
- Version the shared-memory bridge ABI. A core DLL and bridge with different
  layouts must refuse to connect rather than interpret shifted fields.

## Consequences

- Head tracking and recentering respond immediately; game-owned camera motion
  can still step at the engine rate, but it must be addressed separately from
  the live headset pose.
- Settings v2 clears obsolete X/Y compensation once and enables automatic
  projection alignment.
- Runtime binary updates must deploy `OVRInject.dll` and `GTAVRBridge.asi` as
  a matching pair.

## Alternatives considered

- Smooth the final camera: rejected because it necessarily filters physical
  head motion and adds latency.
- Recenter with a pure tracking-origin yaw: rejected because position moves
  on an arc when the user is not at the origin.
- Keep manual per-headset image offsets: rejected as a default because the
  projection matrix already contains the required lens-center data.
