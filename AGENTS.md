# Repository Guidelines

## Mission & Non-negotiables
This repo builds a single-player-only VR injection mod for RAGE-engine games
(GTA V Legacy first). Hard rules, enforced in code and review:
- **Story Mode only.** Online sessions (GTA Online) and BattlEye presence
  hard-disable the mod via `OVRInject/Game/OnlineGuard` (ADR-0003). There is
  no config bypass; never add one; never weaken it "for testing".
- **No DRM/anti-tamper circumvention** and no committed game assets or
  third-party mod material. Reference dirs (`GTAV_REAL_mod*/`, `Lukrossmod*/`,
  `GTAVE_Test_Setup/`, `RealRepo/`, `tmp/`, `VRMod previous/`) are
  gitignored on purpose — do not stage them (see `docs/LEGAL.md`).
- **Version-pin everything:** AOB patterns, offsets, and shader hashes live in
  `manifests/*.ini`, never hardcoded in source (ADR-0004).
- Label `UNVERIFIED` anything not confirmed on real hardware in a real headset.
- Design decisions live in `docs/01-architecture.md` + `docs/ADR/`; update them
  when behavior/conventions change.

## Project Structure & Module Organization
- `GTAVOVR.sln` hosts the Visual Studio solution (projects: GTAVOVR, OVRInject,
  OVRInjectShim; slice + tests projects are being added).
- `GTAVOVR/` is the dev launcher (env setup + CreateRemoteThread injection;
  target process via argv[1] or `GTAVR_TARGET_PROCESS`). `--check` runs
  preflight only (build detection vs `manifests/`, VR runtime, BattlEye
  posture); exit codes 0 ok-to-try, 2 unsupported build, 3 runtime missing,
  4 injection failure.
- `OVRInject/` holds the core VR injection logic, D3D hooks, OpenXR/OpenVR
  backends, overlay UI, and shaders. Subdirs: `D3DHook/` (Present/ResizeBuffers
  hooks, dummy-device late-injection, HudRedirect), `VR/` (backend
  abstraction), `OpenXR/`, `Game/` (title plugin: camera/FOV/state, OnlineGuard,
  BuildManifest — all pattern resolution on a bounded background worker; the
  render thread is O(1)), `Stereo/` (StereoEngine + EyeDelivery AER state
  machine, ComfortRuntime), `Perf/` (PerfStats frametime ring + CSV export),
  `Overlay/`, `Vive/` (HMDRenderer only).
- `OVRInjectShim/` is the shipped loader: a proxy `dxgi.dll` that loads
  `OVRInject.dll` (ADR-0005).
- `tools/` holds `install.bat` / `uninstall.bat` (idempotent game-dir
  installer/reverser; smoke-test against throwaway dirs only),
  `GTAVR-Play.bat` (one-click game launcher), `GTAVR-Panel.bat` +
  `gtavr_panel.py` (control panel: start game, backend switch, inject, live
  log tail), `collect_logs.bat/.ps1` (support bundle to Desktop),
  `scan_patterns.py` + `verify_camera_chain.py` (new-build pattern
  resolution against the running game).
- `samples/D3D11Cube/` is the Phase 2 vertical-slice target + golden-image
  harness (`check_golden.py`).
- `tests/` holds the unit-test runner (21 suites incl. TestEyeDelivery) and
  the injection-lifecycle harness (`injection_lifecycle.py`).
- `manifests/` holds per-title, per-build signature/offset/hash INIs.
- `docs/` holds feasibility, architecture, ADRs, legal, known-issues,
  test-matrix, perf-report, hud-postfx, and `docs/user/` guides.
- `ThirdParty/` is vendored dependencies (OpenVR, OpenXR, ImGui, minhook,
  DirectXMath) — avoid edits unless required.
- Root `.ini` files (`gtavr_camera.ini`, `gtavr_settings.ini`) are templates
  for runtime config; built artifacts go under `x64/` and `build_*/` (ignored).

## Build, Test, and Development Commands
- Visual Studio: open `GTAVOVR.sln`, build `Release|x64` (toolset v145).
- CLI build (Git Bash — use dash switches; `/p:` gets path-mangled):
  `env -u GTAV_INSTALL_DIR "/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" GTAVOVR.sln -p:Configuration=Release -p:Platform=x64 -m -v:m`
  (`env -u GTAV_INSTALL_DIR` is required: the post-build copy into the game
  directory fails the build if that env var points at a non-writable path.)
- Unit tests: build `tests/GTAVRTests.vcxproj` and run the exe
  (`tests/run_tests.bat`).
- Slice golden test: `GTAVR_SLICE_GOLDEN=40 D3D11Cube.exe` twice, then
  `python samples/check_golden.py` (determinism + stereo disparity).
- Prereqs: OpenVR headers in `ThirdParty/openvr/headers`, OpenXR SDK per
  `ThirdParty/openxr/README.md`, ImGui per `ThirdParty/imgui/SETUP.md`
  (`HAS_IMGUI`).

## Coding Style & Naming Conventions
- C++ with `.hpp`/`.cpp` pairs; keep includes and precompiled headers
  consistent with nearby files.
- Follow local formatting; common pattern is 4-space indents and braces on
  the same line.
- Naming: `PascalCase` for classes/functions, `lowerCamelCase` for locals,
  trailing `_` for members (e.g., `device_`), ALL_CAPS for macros.
- The injected DLL must never terminate the host game: no `exit()` paths;
  log at ERR and degrade to pass-through instead.

## Testing Guidelines
- Automated: matrix-math unit tests (`tests/`), slice golden/disparity
  harness (`samples/`), injection-lifecycle and soak harnesses (see
  `docs/test-matrix.md`).
- Manual validation is in-game, per `docs/test-matrix.md` — a scenario may
  not be marked PASS without a capture/trace attached; frametime evidence is
  p99/p99.9, never averages.
- Vendor tests in `ThirdParty/readerwriterqueue/tests` are not part of the
  project test flow.

## Commit & Pull Request Guidelines
- Commit history uses short, sentence-case summaries; keep messages concise
  without prefixes. Phase work commits per phase with evidence (test output,
  capture references) in the message body.
- Never stage gitignored reference/prior-art material (see Mission above).
- PRs should include a summary, affected modules (e.g., `OVRInject/OpenXR`),
  runtime tested (OpenXR/OpenVR), and any config/env var changes.
- Include a screenshot or short clip for overlay/UI changes when possible.

## Configuration & Runtime Tips
- Select runtime with `GTAVR_BACKEND=openxr|openvr` (OpenXR primary,
  ADR-0006); `XR_RUNTIME_JSON` can point to a specific OpenXR runtime.
- Config files `gtavr_settings.ini` and `gtavr_camera.ini` live beside
  `GTA5.exe` or under `GTAVR_SETTINGS_DIR`.
- Override log location with `GTAVR_LOG_PATH` or `GTAVR_LOG_DIR`.
