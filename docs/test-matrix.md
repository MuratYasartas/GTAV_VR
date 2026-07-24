# Phase 9 — Test Matrix & Validation Plan

Rule: **a scenario may not be marked PASS without a capture or trace attached.**
Frametime evidence = p99/p99.9 percentiles, dropped-frame count, reprojection
ratio. Averages are not accepted.

## A. Automated suite (CI-runnable, no headset)

| ID | Test | Mechanism | Status |
|---|---|---|---|
| A1 | Matrix math unit tests (projection oracle, eye-view composition, IPD, layout, handedness) | `tests/GTAVRTests` exe | in progress (wave 1) |
| A2 | Golden-image determinism (slice, fixed seed) | `samples/D3D11Cube` golden mode + `check_golden.py` | in progress (wave 1) |
| A3 | Stereo-correctness: parallax sign & magnitude vs known depths | disparity analysis in `check_golden.py` (near cube z=1m shifts > far z=10m, correct direction for IPD sign) | in progress (wave 1) |
| A4 | Injection lifecycle ×100 (inject → hook → uninject, no leaks/crashes) | `tests/injection_lifecycle` harness vs slice app | pending (wave 4) |
| A5 | OpenXR API validation + api-dump layers | loader validation layers env on slice run | pending; headsetless — session creation may be unavailable ❓ |
| A6 | Soak ≥2 h per title | scripted session + frametime CSV | harness pending; **2 h run UNVERIFIED this milestone** |
| A7 | Build gate | `Release|x64` 0 errors | active |

## B. Manual matrix — per title × build × GPU × runtime

Dimensions: title/build (manifest `verified=` rows) × GPU vendor
(NVIDIA/AMD/Intel) × runtime (OpenXR primary; OpenVR fallback) ×
{seated, standing} × scenarios below.

| # | Scenario | Expected | Actual | p99 ms | Pass/Fail | Capture |
|---|---|---|---|---|---|---|
| B1 | On-foot free roam | stable stereo, correct scale/IPD | — | — | — | — |
| B2 | Vehicle driving (city, highway) | horizon lock active, no judder above reprojection threshold | — | — | — | — |
| B3 | Aircraft flight | as B2 + roll handling per comfort profile | — | — | — | — |
| B4 | Cutscene | theater mode engages ≤1 frame after camera takeover; zero forced head motion | — | — | — | — |
| B5 | Menus / pause / map | theater mode, legible | — | — | — | — |
| B6 | Save → load cycle | hooks survive, no state corruption | — | — | — | — |
| B7 | Alt-tab out/in | no crash, XR session resumes | — | — | — | — |
| B8 | Resolution change in game settings | eye targets recreated, no stale-swapchain crash | — | — | — | — |
| B9 | Online-session attempt | **mod hard-disables before any online interaction; reason logged** | — | — | — | — |
| B10 | BattlEye-enabled launch | mod refuses to activate, human-readable log | — | — | — | — |
| B11 | Unsupported game build | clean "unsupported build" diagnostic, mod inert | — | — | — | — |
| B12 | DLL unload / game exit | no crash, no leaked hooks (lifecycle log clean) | — | — | — | — |

All B rows: **UNVERIFIED — no in-headset run has happened yet.** Headset, GPU
and runtime for the first matrix pass are TBD (user environment).

## C. Comfort validation (separate session, fresh eyes)

Explicit checklist per run: double vision (IPD/scale wrong) · world scale ·
judder (p99 vs refresh) · uncommanded motion (cutscene/vehicle) · vergence
stress (HUD depth) · simulator sickness onset time. Findings logged per
scenario; any uncommanded-motion hit = P0 defect, blocks release.

## D. Supported-build matrix (generated from `manifests/`)

| Title | Build | Patterns | Source | Verified |
|---|---|---|---|---|
| GTA V Legacy | (from `manifests/gtav_legacy.ini` `[b*]` sections) | camera/FOV sets | per `source=` comments | ❓ none yet |

Populated when manifest resolution runs against a live game build.
