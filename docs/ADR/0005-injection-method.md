# ADR-0005: Injection method — dxgi proxy shim for users, CreateRemoteThread launcher for dev

Status: accepted (2026-07-24)

## Context

Options for getting `OVRInject.dll` into the game process:

- **Proxy DLL** (`dxgi.dll` beside the exe): loads early (before/while the
  render device is created) — the right vehicle for guaranteed D3D hook
  coverage. Zero process-manipulation surface. Already implemented
  (`OVRInjectShim`). Needs the export set to match what the game imports.
- **CreateRemoteThread** from a launcher: no file deployment, easy dev
  iteration, easy uninject. Already implemented (`GTAVOVR.exe`). Can arrive
  after device creation → needs the existing-swapchain scan fallback (wave 2).
- **Manual mapping**: avoids LoadLibrary traces. Rejected — significantly more
  complex (relocations, TLS, imports), and its stealth properties read as
  anti-cheat evasion, which is the opposite of our posture (Hard Constraint 2).
- **AppInit_DLLs / SetWindowsHookEx**: fragile, deprecated, global scope.
  Rejected (legacy AppInit code was deleted in wave 1).

## Decision

- **Primary (shipped): dxgi proxy shim** — early, deterministic, no remote
  process APIs. Installer copies it; uninstaller removes exactly our files.
- **Dev/secondary: launcher with CreateRemoteThread** — kept for iteration;
  gains a swapchain-scan fallback so late injection still hooks.
- Per-title choice documented in the title's manifest section; GTA V Legacy
  and Enhanced both use the shim.

## Consequences

- Shim must gate on exact exe name (already does) and refuse other processes.
- Proxy export completeness is a launch-blocker class of bug; the preflight
  (Phase 10) must validate that the game loads with the shim present.
- Uninstall must be total (no leftover proxy DLL → game must run vanilla).

## Alternatives considered

- Launcher-only: rejected — late injection misses device creation; fragile
  timing vs. splash/launcher transitions.
- Shim-only (drop launcher): rejected — dev loop too slow, no easy uninject
  for testing the unload path (Phase 9 lifecycle tests need it).
