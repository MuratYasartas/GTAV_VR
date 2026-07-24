## GTA: Vive

VR support for Grand Theft Auto: V using OpenVR, some reverse engineering of the game's renderer, and Script Hook V. Still somewhat work in progress.

> **Status (rebuild, 2026-07):** this project is being re-engineered into a
> **single-player-only** VR framework (GTA V Legacy first). Online sessions
> and BattlEye hard-disable the mod by design — there is no bypass.
> Start here:
> - `docs/00-feasibility.md` — target assessment, prior art, kill criteria
> - `docs/01-architecture.md` + `docs/ADR/` — design and decisions
> - `docs/LEGAL.md` — legal/safety posture (read before use)
> - `docs/known-issues.md` — honest list of what does not work yet
> - `docs/user/setup.md`, `docs/user/comfort.md` — install and comfort guide

Watch a short clip:

[![Driving Thumbnail](https://user-images.githubusercontent.com/11465187/46969860-66e03e80-d085-11e8-9428-6dc3478dcb8a.png)](https://gfycat.com/AncientDeepAcouchi)


[![Helicopter Thumbnail](https://user-images.githubusercontent.com/11465187/46969983-c50d2180-d085-11e8-9b6d-a6ec5a42f5c4.png)](https://gfycat.com/RecklessSevereHind)

## Status

- OpenVR and OpenXR backends are wired through a unified VRManager.
- The current render path supports depth-based reprojection and alternate-eye stereo (see camera hook below).
- Overlay UI is available in both OpenXR and OpenVR when ImGui is installed.

## Alternate-eye stereo (camera hook)

True stereo requires driving the game camera per-eye. This build uses a lightweight
camera matrix hook configured via `gtavr_camera.ini`. Place it next to `GTA5.exe` or set
`GTAVR_SETTINGS_DIR` to the folder that contains it (the injector sets this to the GTA
install directory). See `gtavr_camera.ini` in this repo for a template and notes.

## Build prerequisites

1. OpenVR headers: `ThirdParty/openvr/headers`
2. OpenXR SDK (headers + loader): see `ThirdParty/openxr/README.md`
3. ImGui for the overlay UI: see `ThirdParty/imgui/SETUP.md` and define `HAS_IMGUI`

## Runtime selection

- Auto-detects available runtimes.
- Force a backend with `GTAVR_BACKEND=openxr` or `GTAVR_BACKEND=openvr`.
- OpenXR can also be pointed to a specific runtime via `XR_RUNTIME_JSON`.

## Overlay controls

- Toggle the settings overlay with the controller menu button.
- Settings persist to `gtavr_settings.ini` in the working directory or `GTAVR_SETTINGS_DIR` if set.
