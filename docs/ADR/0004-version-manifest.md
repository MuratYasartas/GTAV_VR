# ADR-0004: Version-pinned manifest format

Status: accepted (2026-07-24)

## Context

Prime Directive: game builds change offsets and shaders; every signature,
offset, and shader hash must live in a per-version manifest, never hardcoded.
The salvaged code hardcodes AOBs in `GtaCameraHook.cpp`/`GtaCameraFov.cpp`
(with a partial `gtavr_camera.ini` override) and retries heuristics forever
when patterns miss.

## Decision

- **Format: INI** (`manifests/<title>_<edition>.ini`). No new dependencies,
  hand-editable by users for community build support, consistent with the
  existing config system.
- **Build detection**: `GetFileVersionInfo` on the game exe + module size as
  the primary key (`[detect]` section); fallback key = exe SHA-256 (cheap
  enough, done once at resolve).
- **Layout**: `[detect]` then one `[bXXXX]` section per supported build with
  `cameraPattern`, `cameraPatternOffset`, `pointerOffsets`, `matrixOffset`,
  `fovPatterns`, `nearPlane/farPlane`, shader-hash lists, and per-value
  `source=` attribution comments (provenance matters for maintenance).
- **Resolution order**: user override INI > manifest for detected build >
  *nothing*. Unknown build → the mod reports "unsupported build <version>" and
  stays inert. **No silent heuristic fallback** — the old "scan forever"
  behavior produced silent wrong-camera bugs.
- **Verification state**: each build section carries `verified=` (date,
  hardware) or `UNVERIFIED`; the supported-build matrix in
  `docs/test-matrix.md` is generated from these.

## Consequences

- New game patch = new INI section; no recompile needed for pattern drift.
- Community can contribute build sections; `source=` attribution keeps
  provenance auditable.
- Slight duplication across build sections accepted (INI has no inheritance;
  keep it stupid).

## Alternatives considered

- JSON: rejected — needs a vendored parser for zero practical gain.
- In-code pattern tables with INI override: rejected — violates the directive;
  code must not *contain* patterns.
