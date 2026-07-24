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
   - `manifests/`, `gtavr_settings.ini`, `gtavr_camera.ini`.
   No game files are modified.
3. Configure your OpenXR runtime (the installer/preflight reads the
   `ActiveRuntime` registry key and tells you if none is set).
4. Start the game normally. First launch: the mod initializes on the first
   rendered frame; watch `gtavrInjectLog.txt` (override with
   `GTAVR_LOG_DIR`) for `VR initialized` or a human-readable refusal reason.

## 3. Uninstall

Delete exactly the files you copied in step 2. The game is then fully
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

Details: `docs/01-architecture.md` (design), `docs/known-issues.md` (honest
list of what does not work yet).
