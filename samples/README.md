# GTAVR Vertical Slice — D3D11Cube

Mission Phase 2 vertical slice: a trivial D3D11 target we fully control, used
to prove the OVRInject stereo path end-to-end **before** touching GTA V.

## Contents

- `D3D11Cube/` — minimal Win32 + D3D11 sample app (single `.cpp` + `SliceCam.hpp`
  shared-ABI header + `D3D11Cube.vcxproj`, toolset v145, x64).
  Renders three solid-colour cubes at known depths — red at z=1m, green at
  z=3m, blue at z=10m — plus a grey ground grid. Default camera: origin,
  looking down -Z.
- `check_golden.py` — Python 3 (stdlib only) verifier for the golden frames:
  stereo difference, near/far disparity ordering and direction, determinism.

## Build

From the repo root (Git Bash):

```sh
env -u GTAV_INSTALL_DIR "/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" \
    samples/D3D11Cube/D3D11Cube.vcxproj -p:Configuration=Release -p:Platform=x64 -m -v:m
```

Output: `samples/D3D11Cube/x64/Release/D3D11Cube.exe`
(the project is standalone; it is intentionally not part of `GTAVOVR.sln`).

## Run modes

### 1. Standalone (default camera)

```sh
./samples/D3D11Cube/x64/Release/D3D11Cube.exe
```

Renders the scene with the built-in camera until the window is closed.

### 2. Cooperative-engine harness (`GTAVR_SLICE_CAM`)

The app creates a named shared-memory mapping `GTAVR_SLICE_CAM`
(session-local) holding a versioned struct (see `D3D11Cube/SliceCam.hpp`):

```c
struct SliceCamState {                 // 208 bytes, magic 'GTVC' (v1)
    uint32_t magic;                    // 0x43565447 when valid
    uint32_t frameIndex;               // producer frame counter
    uint32_t flags;                    // bit0: VR_CONTROL
    float    viewL[16];                // row-major, column-vector convention
    float    viewR[16];                // clipPos = proj * view * worldPos
    float    proj[16];
    float    ipd;                      // metres, informational
};
```

Each frame the app snapshots the mapping. If `magic` matches and
`flags & 1` (VR_CONTROL) it renders with `viewL` on even frames and `viewR`
on odd frames (alternate-eye simulation) using `proj`; otherwise it falls
back to the default camera. This is how the injected mod (or any test
harness) drives poses without GTA. The app only reads the mapping; a
producer may open/create it at any time (it is zero-filled until written).

### 3. Golden/test mode (deterministic BMP dump)

```sh
cd samples/D3D11Cube
env GTAVR_SLICE_GOLDEN=40 ./x64/Release/D3D11Cube.exe
```

Renders 40 deterministic frames and exits 0. Even frames are the left eye
(camera x = -0.0315m), odd frames the right eye (x = +0.0315m; IPD 0.063m).
Every backbuffer is written to `golden_out\frame_####_L|R.bmp`
(relative to the working directory; override with
`GTAVR_SLICE_GOLDEN_DIR`). Determinism: animation is driven only by the
frame index — both frames of an L/R pair share the same cube rotation
(step 0.02 rad per pair) — there is no clock dependence and the shared
mapping is ignored, so bytes are identical across runs on the same machine.

### 4. Verifying golden frames

Two runs are needed (stereo checks run on the second one):

```sh
cd samples/D3D11Cube
env GTAVR_SLICE_GOLDEN=40 GTAVR_SLICE_GOLDEN_DIR=golden_run_a ./x64/Release/D3D11Cube.exe
env GTAVR_SLICE_GOLDEN=40 ./x64/Release/D3D11Cube.exe
cd ../..
python samples/check_golden.py
```

The checker verifies, and exits non-zero with a clear message on failure:
- (a) L vs R frames of every pair differ;
- (b) per-cube horizontal disparity (centroid X shift, cubes detected by
  their unique colours): all positive (left eye at negative X shifts the
  scene right in the L image) and `near(z=1m) > mid(z=3m) > far(z=10m)`;
- (c) both runs are byte-identical (determinism).

Explicit dirs: `python samples/check_golden.py <run_a_dir> <run_b_dir>`.

## Injecting the mod into the slice (launcher)

`GTAVOVR.exe` accepts a target process override (see Task 2): `argv[1]`
wins, then `GTAVR_TARGET_PROCESS`, else the original GTA5.exe behaviour.
The target exe must sit next to `OVRInject.dll` under `GTAV_INSTALL_DIR`
when the launcher has to start it; if it is already running, the launcher
just finds and injects it.

```sh
# D3D11Cube.exe already running:
env GTAVR_TARGET_PROCESS=D3D11Cube.exe GTAV_INSTALL_DIR=<dir with OVRInject.dll> ./GTAVOVR.exe
# or
./GTAVOVR.exe D3D11Cube.exe
```

## Known hacks / TODO

- **grcWindow** (RESOLVED, kept harmlessly): the window class was named
  `grcWindow` because the mod's ImGui init used `FindWindowA("grcWindow")`.
  Since the Phase-3 robustness wave the mod resolves the target window from
  the swapchain (`IDXGISwapChain::GetDesc` → `OutputWindow`) and ImGui input
  resolves by process ID, so the class name no longer matters. Left as-is;
  rename only if it ever collides with the real game.
