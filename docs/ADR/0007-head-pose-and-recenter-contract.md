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
  head-composed orientation. Record the pose written for each eye and retain
  it with that eye's persistent texture; a stale AER texture is submitted with
  its own historical render pose, never relabelled with the current pose.
- A yaw-only OpenXR rebase rotates around the current head position. Repeated
  recenters compose with the existing rebase instead of replacing it.
- A recenter raised after `BeginFrame` is consumed by the game camera only
  after a fresh rebased pose is available (next OpenXR `BeginFrame`; the
  post-submit `WaitGetPoses` on OpenVR). This deliberately trades one request
  frame for a coherent runtime and camera reference.
- GTA camera Euler rotation order 2 is `ROT_ZXY`: DirectX row-vector
  composition `Rz * Rx * Ry`. Production conversion is guarded by an
  independent expanded-matrix oracle test.
- Derive image X/Y alignment from each eye's asymmetric OpenXR projection
  center. User offsets are fine trim only.
- Interpret `worldScale` as apparent world size: tracked meters and runtime
  IPD use `1 / worldScale`; manual camera trim remains in game meters.
- Version the shared-memory bridge ABI. A core DLL and bridge with different
  layouts must refuse to connect rather than interpret shifted fields.

## Consequences

- Head tracking remains full-rate. OpenXR can reproject each fresh/stale eye
  from the pose that actually rendered it; OpenVR obtains the next render pose
  only after submitting the current image.
- Recenter has a bounded one-frame handoff before the game-camera reference is
  rebuilt from a fresh runtime pose.
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
