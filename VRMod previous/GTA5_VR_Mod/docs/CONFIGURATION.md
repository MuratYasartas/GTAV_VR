# GTA 5 VR Mod - Configuration Guide

Complete reference for all settings in `GTA5VR.ini`.

---

## Table of Contents

1. [General Settings](#general-settings)
2. [VR Runtime Settings](#vr-runtime-settings)
3. [Rendering Settings](#rendering-settings)
4. [Display Settings](#display-settings)
5. [Comfort Settings](#comfort-settings)
6. [HUD Settings](#hud-settings)
7. [Camera Settings](#camera-settings)
8. [Input Settings](#input-settings)
9. [Performance Settings](#performance-settings)
10. [Debug Settings](#debug-settings)
11. [Advanced Settings](#advanced-settings)
12. [Recommended Configurations](#recommended-configurations)

---

## General Settings

```ini
[General]
ModEnabled=true
GameVersion=auto
FirstRun=true
```

| Setting | Values | Description |
|---------|--------|-------------|
| `ModEnabled` | true/false | Enable/disable the entire mod |
| `GameVersion` | auto/steam/epic/rgl | Game version detection |
| `FirstRun` | true/false | Shows tutorial on first run |

---

## VR Runtime Settings

```ini
[VRRuntime]
PreferredRuntime=auto
OpenXRRuntime=
VRAPI=0
```

| Setting | Values | Description |
|---------|--------|-------------|
| `PreferredRuntime` | auto/openxr/openvr | Which VR runtime to use |
| `OpenXRRuntime` | (path) | Custom OpenXR runtime path |
| `VRAPI` | 0-3 | 0=auto, 1=Oculus, 2=SteamVR, 3=WMR |

### Runtime Selection Guide

| Headset | Recommended Setting |
|---------|---------------------|
| Quest 2/3/Pro | `PreferredRuntime=openxr`, `VRAPI=1` |
| Valve Index | `PreferredRuntime=openvr` |
| Vive/Vive Pro | `PreferredRuntime=openvr` |
| WMR Headsets | `PreferredRuntime=openxr`, `VRAPI=3` |
| Pimax | `PreferredRuntime=openxr` |

---

## Rendering Settings

```ini
[Rendering]
RenderingMode=synchronized_sequential
ResolutionScale=100
EnableDepthBuffer=true
DepthFormat=D32_FLOAT
TargetFramerate=90
AllowReprojection=true
```

### RenderingMode

| Mode | Performance | Compatibility | Description |
|------|-------------|---------------|-------------|
| `native_stereo` | Best | Low | Single draw call for both eyes |
| `synchronized_sequential` | Good | High | Renders each eye sequentially |
| `alternating_eye` | Best | Highest | One eye per frame (like Luke Ross) |

**Recommendation**: Start with `synchronized_sequential`, try `alternating_eye` if performance is poor.

### Resolution Scale

| Value | Effect | When to Use |
|-------|--------|-------------|
| 50-75 | Lower quality, better FPS | Low-end GPU |
| 100 | Native resolution | Recommended |
| 125-150 | Supersampling | High-end GPU |
| 200 | Maximum quality | RTX 4090 class |

### Other Rendering Options

| Setting | Values | Description |
|---------|--------|-------------|
| `EnableDepthBuffer` | true/false | Enables ASW 2.0 depth data |
| `DepthFormat` | D32_FLOAT/D24_UNORM_S8 | Depth buffer precision |
| `TargetFramerate` | 72/80/90/120/144 | Target FPS (match headset) |
| `AllowReprojection` | true/false | Allow ASW/Motion Smoothing |

---

## Display Settings

```ini
[Display]
WorldScale=1.0
IPDOverride=0
IPDValue=63.0
NearClip=0.1
FarClip=10000.0
```

### World Scale

Controls how big the world feels:

| Value | Effect |
|-------|--------|
| 0.5 | World feels giant (you're tiny) |
| 0.8 | Slightly larger world |
| 1.0 | Realistic scale (default) |
| 1.2 | Slightly smaller world |
| 2.0 | World feels miniature |

### IPD (Inter-Pupillary Distance)

| Setting | Values | Description |
|---------|--------|-------------|
| `IPDOverride` | 0/1 | 0=use headset IPD, 1=use custom |
| `IPDValue` | 50-80 | IPD in millimeters |

**Note**: Most headsets report IPD automatically. Only override if stereo feels wrong.

### Clipping Planes

| Setting | Values | Description |
|---------|--------|-------------|
| `NearClip` | 0.01-1.0 | Near clipping distance (meters) |
| `FarClip` | 1000-100000 | Far clipping distance (meters) |

---

## Comfort Settings

```ini
[Comfort]
EnableSnapTurn=true
SnapTurnDegrees=45
EnableComfortVignette=true
VignetteIntensity=0.5
SeatedMode=false
StandingHeight=0
```

### Snap Turn

| Setting | Values | Description |
|---------|--------|-------------|
| `EnableSnapTurn` | true/false | Instant rotation instead of smooth |
| `SnapTurnDegrees` | 15/30/45/60/90 | Degrees per snap |

### Comfort Vignette

Darkens screen edges during movement to reduce motion sickness:

| VignetteIntensity | Effect |
|-------------------|--------|
| 0.0 | Disabled |
| 0.3 | Subtle |
| 0.5 | Moderate (default) |
| 0.7 | Strong |
| 1.0 | Maximum |

### Play Mode

| Setting | Values | Description |
|---------|--------|-------------|
| `SeatedMode` | true/false | Optimized for seated play |
| `StandingHeight` | -50 to 50 | Height offset in cm |

---

## HUD Settings

```ini
[HUD]
HUDMode=floating
HUDDistance=2.0
HUDScale=1.0
HUDOpacity=1.0
HUDCurvature=0.0
RadarScale=1.0
SubtitleSize=1.0
```

### HUD Modes

| Mode | Description |
|------|-------------|
| `floating` | HUD floats in world space (recommended) |
| `head_locked` | HUD follows your head |
| `wrist` | HUD appears on left wrist |
| `hidden` | HUD completely hidden |

### HUD Positioning

| Setting | Values | Description |
|---------|--------|-------------|
| `HUDDistance` | 0.5-10.0 | Distance from you in meters |
| `HUDScale` | 0.5-3.0 | Size multiplier |
| `HUDOpacity` | 0.0-1.0 | Transparency |
| `HUDCurvature` | 0.0-1.0 | Curve the HUD like a screen |

### UI Elements

| Setting | Values | Description |
|---------|--------|-------------|
| `RadarScale` | 0.5-2.0 | Minimap size |
| `SubtitleSize` | 0.5-2.0 | Subtitle text size |

---

## Camera Settings

```ini
[Camera]
ForceFirstPerson=true
VehicleCamera=dashboard
CutsceneMode=vr_adapted
EnablePositionalTracking=true
PositionalScale=1.0
RecenterMethod=head_shake
RecenterButton=7B
```

### First Person

| Setting | Values | Description |
|---------|--------|-------------|
| `ForceFirstPerson` | true/false | Lock to first-person view |

### Vehicle Camera

| Mode | Description |
|------|-------------|
| `dashboard` | View from driver's seat |
| `hood` | View from hood |
| `third_person` | Third-person (less immersive, less sickness) |

### Cutscene Handling

| Mode | Description |
|------|-------------|
| `vr_adapted` | Camera stabilized for VR (recommended) |
| `theater` | Watch on floating screen |
| `original` | Unmodified (may cause sickness) |
| `skip` | Skip all cutscenes |

### Positional Tracking

| Setting | Values | Description |
|---------|--------|-------------|
| `EnablePositionalTracking` | true/false | Room-scale movement |
| `PositionalScale` | 0.5-2.0 | Movement sensitivity |

### Recentering

| Setting | Values | Description |
|---------|--------|-------------|
| `RecenterMethod` | head_shake/button/both | How to recenter |
| `RecenterButton` | (hex keycode) | Key for recentering (7B=F12) |

---

## Input Settings

```ini
[Input]
InputMode=gamepad
SwapSticks=false
EnableHaptics=true
HapticIntensity=0.7
```

### Input Modes

| Mode | Description |
|------|-------------|
| `gamepad` | VR controllers emulate Xbox gamepad |
| `motion_controllers` | Full motion control (when supported) |
| `hybrid` | Motion aiming + gamepad movement |

### Controller Settings

| Setting | Values | Description |
|---------|--------|-------------|
| `SwapSticks` | true/false | Swap thumbstick functions |
| `EnableHaptics` | true/false | Controller vibration |
| `HapticIntensity` | 0.0-1.0 | Vibration strength |

---

## Performance Settings

```ini
[Performance]
ShowPerformanceOverlay=false
ShowReprojectionIndicator=false
LimitFramerate=false
FramerateLimit=90
```

| Setting | Values | Description |
|---------|--------|-------------|
| `ShowPerformanceOverlay` | true/false | Show FPS/timing info |
| `ShowReprojectionIndicator` | true/false | Show when reprojection active |
| `LimitFramerate` | true/false | Cap framerate |
| `FramerateLimit` | 30-144 | Max FPS |

---

## Debug Settings

```ini
[Debug]
EnableDebugMode=false
LogLevel=verbose
LogToFile=true
LogFilePath=GTA5VR.log
ShowDebugOverlay=false
```

| Setting | Values | Description |
|---------|--------|-------------|
| `EnableDebugMode` | true/false | Enable debug features |
| `LogLevel` | minimal/verbose/debug | Log detail level |
| `LogToFile` | true/false | Write log to file |
| `LogFilePath` | (path) | Log file location |
| `ShowDebugOverlay` | true/false | Show debug info in VR |

---

## Advanced Settings

```ini
[Advanced]
DisablePostProcessing=false
DisableDOF=true
DisableMotionBlur=true
DisableBloom=false
UseAsyncCompute=true
PreferDedicatedGPU=true
```

### Post-Processing

| Setting | Recommended | Description |
|---------|-------------|-------------|
| `DisablePostProcessing` | false | Disable all post effects |
| `DisableDOF` | **true** | Disable depth of field |
| `DisableMotionBlur` | **true** | Disable motion blur |
| `DisableBloom` | false | Disable bloom effect |

**Note**: DoF and motion blur can cause discomfort in VR. Keep them disabled.

### GPU Settings

| Setting | Values | Description |
|---------|--------|-------------|
| `UseAsyncCompute` | true/false | GPU async compute |
| `PreferDedicatedGPU` | true/false | Force dedicated GPU |

---

## Recommended Configurations

### Low-End PC (GTX 1060 / RX 580)

```ini
[Rendering]
RenderingMode=alternating_eye
ResolutionScale=80
EnableDepthBuffer=false
TargetFramerate=72
AllowReprojection=true

[Comfort]
EnableComfortVignette=true
VignetteIntensity=0.5
```

### Mid-Range PC (RTX 3060 / RX 6700 XT)

```ini
[Rendering]
RenderingMode=synchronized_sequential
ResolutionScale=100
EnableDepthBuffer=true
TargetFramerate=90
AllowReprojection=true
```

### High-End PC (RTX 4080 / RX 7900 XTX)

```ini
[Rendering]
RenderingMode=synchronized_sequential
ResolutionScale=150
EnableDepthBuffer=true
TargetFramerate=120
AllowReprojection=false
```

### Motion Sickness Prone

```ini
[Comfort]
EnableSnapTurn=true
SnapTurnDegrees=45
EnableComfortVignette=true
VignetteIntensity=0.7

[Camera]
VehicleCamera=third_person
CutsceneMode=theater

[Advanced]
DisableMotionBlur=true
DisableDOF=true
```

---

## In-Game Settings Sync

Some settings can be changed in-game via the VR menu (INSERT key):
- Rendering Mode
- World Scale
- Snap Turn
- HUD Settings
- Comfort Options

Changes made in-game are saved to `GTA5VR.ini` automatically.
