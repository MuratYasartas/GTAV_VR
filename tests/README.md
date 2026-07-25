# GTAVR Unit Tests

Unit tests for the GTAV VR injection mod, focused on matrix math (Phase 9:
most "VR looks wrong" bugs are matrix bugs).

## Layout

| File | Purpose |
|------|---------|
| `GTAVRTests.vcxproj` | Standalone console test exe (v145, x64, Debug+Release). Links against **nothing** from OVRInject; compiles the production code under test (`../OVRInject/OpenXR/XRCore.cpp`) directly into the exe. |
| `TestFramework.hpp` | Tiny zero-dependency framework: `TEST(name)` auto-registration, `CHECK(cond)`, `CHECK_NEAR(a,b,eps)`, `SKIP(reason)`, per-test `[PASS]/[FAIL]/[SKIP]`, exit code 1 on any failure. |
| `TestMain.cpp` | `main()` -> `RunAllTests()`. |
| `TestProjection.cpp` | Projection oracle tests for `OVRInject::XR::XrFovToProjectionMatrixD3D` (D3D clip z in [0,1], OpenXR RH view space, forward -Z, +Y up). |
| `TestEyeView.cpp` | Eye-view composition (`V = inverse(P_eye * P_head)`), IPD, handedness, and row-major `float[16]` shared-memory layout tests. Self-contained (only `<DirectXMath.h>`); must always pass standalone. |
| `TestEyeDelivery.cpp` | AER eye-delivery state machine (`../OVRInject/Stereo/EyeDelivery.hpp`, header-only, dependency-free): per-frame layer→texture mapping on F/F+1/F+2, the no-cross-eye invariant (layer i always backed by eye texture i; single warmup exception after reset), missed-frame parity stability, and camera-write eye = next render eye. |
| `Stubs.cpp` | Link stubs for symbols `XRCore.cpp` drags in: mod logging (`LOGSTRF` & friends, no-ops) and the two OpenXR loader entry points (`xrResultToString`, `xrEnumerateInstanceExtensionProperties`). The math under test never calls them. |
| `run_tests.bat` | Build + run (see below). |

## Build & Run

From a Visual Studio developer prompt (or any cmd with MSBuild on PATH):

```
tests\run_tests.bat          :: Release
tests\run_tests.bat Debug
```

Or manually:

```
msbuild tests\GTAVRTests.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:m
tests\x64\Release\GTAVRTests.exe
```

From Git Bash (dash switches, and clear `GTAV_INSTALL_DIR` so it cannot leak
into the build):

```
env -u GTAV_INSTALL_DIR "/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" \
    tests/GTAVRTests.vcxproj -p:Configuration=Release -p:Platform=x64 -m -v:m
./tests/x64/Release/GTAVRTests.exe
```

Exit code is 0 when all tests pass (skips do not fail the run), 1 otherwise.
Build artifacts go to `tests/x64/<Config>/`.

## Injection-lifecycle harness (`injection_lifecycle.py`)

Phase 9 harness for test-matrix row A4: inject -> hook/inert -> terminate,
looped N times against the D3D11Cube slice. Pure Python 3 standard library.

```
python tests/injection_lifecycle.py [--count N] [--mode MODE] [--keep-logs]
```

Per iteration it starts `D3D11Cube.exe` with `GTAVR_LOG_DIR=<fresh temp dir>`,
runs `GTAVOVR.exe` with `GTAVR_TARGET_PROCESS=D3D11Cube.exe` (OVRInject.dll is
staged into a temp `GTAV_INSTALL_DIR` together with `openvr_api.dll` /
`openxr_loader.dll`), watches the mod log stream for a terminal marker, then
kills the cube and confirms it dies within 10 s. Results (per-run status +
summary) go to `tests/lifecycle_report.txt`; exit code is non-zero unless
every run is hook-success or clean-inert with zero crashes/hangs/timeouts.

Modes (`--mode`):

| Mode | Flow | Typical outcome on the slice |
|------|------|------------------------------|
| `cube-first` (default) | harness starts cube, launcher injects into the running process | `inert-clean` (late injection) |
| `launcher-first` | launcher starts the cube from a staged dir, then injects | `inert-clean` (race) |
| `shim` | dxgi-proxy load path (cube copy renamed `GTA5.exe`; the shim's MessageBox is auto-dismissed) | `inert-clean` (race) |
| `mixed` | cycles the three | mixed |

Terminal markers (from `OVRInject/D3DHook/D3DHooks_VRManager.hpp`): `Hooked
Present at vtable index` (+ `Failed to initialize VR` / `VR initialized with`)
= hook-success; `No D3D11 swapchain hooked within 30 s` = clean inert
(late-injection watchdog); a CrashDump line or `gtavr_crash_*.dmp` = crash.

Why inert-clean dominates on the slice: the cube creates its D3D11 device
well under a second after process start, while any async mod load
(CreateRemoteThread `LoadLibrary` of OVRInject + imports, or the shim's
deferred load) finishes later - the creation hooks therefore land after the
swapchain exists and the 30 s watchdog parks the mod inert, exactly as
designed. Against a real game (minutes of startup before D3D init) the same
flow ends hooked with inert-VR pass-through when no headset is active.

Notes:
- Live log capture uses `OutputDebugString` via the Win32 DBWIN protocol
  (the mod mirrors every log line there; `gtavrInjectLog.txt` itself is held
  open read-denied by the game and cannot be tailed). Only one DBWIN consumer
  can exist per Windows session - do not run DebugView or a second harness
  instance concurrently.
- The launcher refuses to inject without a VR runtime (OpenXR ActiveRuntime
  or SteamVR); on a machine with none, every run fails as `inject-failure`.
- Leftover `D3D11Cube.exe` processes are killed by image name between runs;
  shim mode instead cleans up strictly by PID because the cube runs renamed
  as `GTA5.exe`.

## Conventions for adding tests

- Put suites in `tests/Test<Something>.cpp`, add the file to the
  `ClCompile` item group in `GTAVRTests.vcxproj`.
- Prefer testing header-inline/self-contained production functions. If a
  production `.cpp` must be compiled in and drags in externals (XR loader,
  logging), stub them in `Stubs.cpp` rather than linking OVRInject code.
- DirectXMath is header-only; `#include <DirectXMath.h>` anywhere. Note:
  `XMMATRIX::operator()` and `.m` only exist with `_XM_NO_INTRINSICS_` -- use
  `XMStoreFloat4x4` into an `XMFLOAT4X4` for element access.
- Shared-memory matrices are **row-major `float[16]`**, rows ordered
  right / forward / up / position (see `GtaCameraHook.hpp`'s
  `GtaCameraMatrix`). `MatrixLayout_*` tests pin this contract.

## EXPECTED-PENDING mechanism

`TestProjection.cpp` tests `XrFovToProjectionMatrixD3D`, which was added to
`OVRInject/OpenXR/XRCore.hpp` by a concurrent change. If you build these
tests against a tree where that function does **not** exist yet, do not
delete the tests:

1. Add `GTAVR_XR_FOV_D3D_PENDING` to `PreprocessorDefinitions` in
   `GTAVRTests.vcxproj` (both configs).
2. The projection suite then compiles with every test reporting
   `[SKIP] ... EXPECTED-PENDING` instead of failing the build, and the
   eye-view/layout suites still run for real.
3. Remove the define once `XrFovToProjectionMatrixD3D` lands in
   `XRCore.hpp` (status as of this writing: **landed** -- the define is
   intentionally NOT set, and the suite runs for real).

## Notes

- Compiling `../OVRInject/OpenXR/XRCore.cpp` emits a pre-existing
  production warning C4996 (`strncpy` is unsafe) at `XRCore.cpp:81`.
  Left as-is (test project does not treat warnings as errors).
