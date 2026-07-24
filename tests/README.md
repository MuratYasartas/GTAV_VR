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
