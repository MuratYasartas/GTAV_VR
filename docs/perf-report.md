# Phase 8 — Performance Report

Status: **methodology + instrumentation landed; all measurements UNVERIFIED
(no headset/GPU matrix run yet).** This file is the template that real
captures fill in; rows without attached traces are never marked PASS.

## 1. Budget model (work backwards from refresh)

| Headset class | Refresh | Frame budget | p99 target (with reprojection) | Per-eye update (AER) |
|---|---|---|---|---|
| Typical PCVR (Quest 3 Link / Index class) | 90 Hz | 11.1 ms | ≤ 11.1 ms with ≤ 20% reprojected frames | 45 Hz + ATW/ASW |
| Fallback target | 72 Hz | 13.9 ms | ≤ 13.9 ms with ≤ 20% reprojected | 36 Hz + ATW/ASW |

CPU budget inside the hook per frame: camera write + blit/copy + XR submit
≤ 2.0 ms on the render thread (measured by PerfStats; see §3). GPU budget:
base game frame + ≤ 1.0 ms stereo overhead (blit + vignette + overlay).

**Comfort rule:** a configuration whose p99 exceeds the threshold sustained
over 60 s is a P0 defect, not a "settings suggestion".

## 2. Measurement methodology

- **Frametimes:** in-mod PerfStats ring buffer (Present-to-Present on the
  render thread), p50/p99/p99.9, dropped-frame count, written to CSV
  (`gtavr_perf.csv`) on unload / hotkey.
- **XR-side:** runtime-reported reprojection ratio (OpenXR via runtime tools;
  SteamVR frame timing for the OpenVR path).
- **GPU:** PIX/RenderDoc captures per scenario; NSight for bottleneck
  classification (CPU- vs GPU-bound per title).
- **Scenarios:** the B-matrix rows in `docs/test-matrix.md` (roam, drive, fly,
  cutscene, menu) on a fixed route/seed for comparability.
- **Machine matrix:** filled from the test matrix GPU×runtime rows.

## 3. Instrumentation landed

| Item | Where | Status |
|---|---|---|
| Frametime ring buffer + percentiles | `OVRInject/Perf/PerfStats` — 4096-sample Present-to-Present ring on the render thread, p50/p99/p99.9 + mean, `Record()` called from every `hookedPresent` | done |
| Dropped-frame estimate | `PerfStats` — ring frames > 1.5× p50, per-second and window totals | done |
| Engine section timers | `OVRInject/Stereo/StereoEngine` — QPC-timed camera-write / blit / submit accumulators fed into `PerfStats` | done |
| CSV export | `PerfStats::ExportCsv` → `gtavr_perf.csv` (one row per second: timestamp, frames, p50, p99, p99.9, drops), on `ShutdownVR`/unload and on demand via F11 (polled on the render thread; the mod has no WndProc hook) | done |
| Cross-thread perf mirrors | `VR::RuntimeStats` atomics (`fps`, `frametimeP50Ms/P99Ms/P999Ms`) updated once per second | done |
| Overlay frametime/p99 display | read API landed: `PerfStats::GetSnapshot()` + RuntimeStats mirrors; overlay rendering itself is next wave | pending wave 4 |
| Slice-app baseline (framework overhead only) | `samples/D3D11Cube` + PerfStats | pending wave 3 |

## 4. Results (UNVERIFIED — no hardware run yet)

| Config | Scenario | p50 | p99 | p99.9 | Dropped | Reproj % | Verdict |
|---|---|---|---|---|---|---|---|
| _pending headset matrix_ | | | | | | | |

## 5. Levers (in order of preference)

1. Per-eye resolution scaling (runtime render scale).
2. AER (default) vs Z3D fallback mode.
3. Effect disables per `docs/hud-postfx.md` (TAA/SSR/SSAO class).
4. Foveated rendering — OpenXR eye-tracked foveation only if the runtime
   exposes it; ❓UNVERIFIED on target headsets.
5. Runtime reprojection/ASW interaction — documented behavior, **never relied
   on as a crutch** for missing base performance.
6. Culling/LOD tuning via camera FOV override (mitigates pop-in cost/quality).

## 6. Title outlook (from feasibility, to be confirmed by data)

- GTA V Legacy: expected viable at 90 Hz class on modern mid GPUs. ❓
- GTA V Enhanced: heavier; RT must be off for VR budget. ❓
- RDR2: questionable headroom; gate on measurements before porting. ❓
- GTA IV: expected CPU-bound below comfort floor → unsupported. ❓
