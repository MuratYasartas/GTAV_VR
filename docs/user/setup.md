# GTAVR — Setup Guide (GTA V Legacy, Story Mode)

> **READ FIRST**
> - **Story Mode only. GTA Online is blocked by design.** The mod detects
>   online sessions and BattlEye and disables itself. There is no setting to
>   change this. Do not attempt to use it online.
> - Single-player modding requires **BattlEye disabled in the Rockstar Games
>   Launcher** (Launcher → Settings → General → uncheck *BattlEye*). This is
>   Rockstar's own option for story-mode modding; the mod never touches
>   BattlEye itself. You cannot enter GTA Online while it is disabled.
> - Motion sickness: read `comfort.md` before your first session.

## 1. Requirements

- GTA V **Legacy** (Story Mode), a supported build (see
  `docs/test-matrix.md` section D — unsupported builds make the mod stay
  inert and say so in the log).
- OpenXR-compatible runtime (recommended) or SteamVR.
- GPU/driver able to hold a high, stable framerate in Story Mode — VR needs
  **p99 frametime at or below your headset's reprojection threshold**, not a
  high average. See `docs/perf-report.md`.
- Windows 10/11 x64.

## 2. Install

1. Disable BattlEye (above). Launch Story Mode once, vanilla, to confirm it
   works and to let the launcher settle.
2. Copy the release files next to `GTA5.exe`:
   - `dxgi.dll` (the OVRInjectShim proxy — this is the mod loader),
   - `OVRInject.dll`, `openvr_api.dll` / `openxr_loader.dll` as shipped,
   - `manifests/`, `gtav_legacy.ini`, `gtavr_settings.ini`, `gtavr_camera.ini`.
   No game files are modified.
   (`tools\install.bat <game dir>` does exactly this — backs up any existing
   `dxgi.dll` to `dxgi.dll.gtavr-backup`, verifies every copy, and
   `tools\uninstall.bat <game dir>` removes exactly those files and restores
   the backup. Both are idempotent.)
3. Configure your OpenXR runtime (the installer/preflight reads the
   `ActiveRuntime` registry key and tells you if none is set).
4. Optional: run `GTAVOVR.exe --check` (preflight only — build detection, VR
   runtime, BattlEye posture; nothing is launched or injected). Exit codes:
   **0** ok-to-try, **2** unsupported build (no manifest section — the mod
   would stay inert), **3** runtime missing, **4** injection failure.
5. Start the game normally. First launch: the mod initializes on the first
   rendered frame; watch `gtavrInjectLog.txt` (override with
   `GTAVR_LOG_DIR`) for `VR initialized` or a human-readable refusal reason.

## 3. Uninstall

Delete exactly the files you copied in step 2 — or run
`tools\uninstall.bat <game dir>`, which removes exactly those files and
restores `dxgi.dll.gtavr-backup` if one exists. The game is then fully
vanilla again — the proxy DLL is the only loader, and nothing else persists.

## 4. Preflight failures (human-readable, on purpose)

| Message | Meaning | Action |
|---|---|---|
| `Unsupported game build <ver>` | No manifest section for your GTA5.exe version | Wait for an updated manifest; do not force it |
| `BattlEye active` | You launched with anti-cheat on | Disable it in the Rockstar launcher (story mode) |
| `No OpenXR runtime` | `ActiveRuntime` not set / runtime broken | Set your headset's OpenXR runtime; or `GTAVR_BACKEND=openvr` |
| `Online session detected` | GTA Online traffic/state seen | The mod stays disabled until you restart into Story Mode |

## 5. Configuration quick reference

- `gtavr_settings.ini` — stereo mode (AER default / Z3D fallback), comfort
  (vignette, snap turn, horizon lock), world scale, performance.
- `gtavr_camera.ini` — per-camera-type FOV overrides (culling mitigation).
- Env: `GTAVR_BACKEND=openxr|openvr`, `GTAVR_LOG_DIR`,
  `GTAVR_SETTINGS_DIR`, `XR_RUNTIME_JSON`.
- In-headset: open the settings overlay (see comfort.md) — no alt-tab needed.

## 6. Reporting a problem

Every refusal or degradation is logged in plain language — the log is the
first thing to check, and the first thing to share.

1. **Turn on verbose logging**: set env `GTAVR_VERBOSE=1` (or add
   `[Debug] verbose=1` to `gtavr_settings.ini`), then reproduce the issue.
   Verbose adds per-decision traces: every camera pattern tried and its
   result, guard detector verdicts, and hook install events.
2. **Collect the bundle**: run `tools\collect_logs.bat`. It gathers the
   inject log, shim log, perf CSV, any crash minidumps, config/manifest
   snapshots, and a `sysinfo.txt` (OS/GPU/driver) into a timestamped
   `GTAVR_LogBundle_*` folder on your Desktop. No game files included.
3. **Read the landmarks** (in `gtavrInjectLog.txt`):
   - `GTAVR session summary` — mod build, game build, manifest match, GPU,
     log path. Answers "what environment" in one block.
   - `GTAVR MOD ACTIVE` / `GTAVR MOD INERT` — the one-line verdict and why.
   - `OnlineGuard: *** MOD HARD-DISABLED *** reason: ...` — the kill-switch
     fired; the reason names the detector.
   - `GtaCameraHook: SUCCESS!` / `All resolution methods failed` — camera
     pattern resolution outcome (with verbose: each attempt).
   - `CrashDump` lines + `*.dmp` — if it crashed, the dump is in the bundle.
4. Share the bundle folder. p99 frametime problems belong in
   `gtavr_perf.csv` (press `F11` in-game to force an export).

Details: `docs/01-architecture.md` (design), `docs/known-issues.md` (honest
list of what does not work yet).
