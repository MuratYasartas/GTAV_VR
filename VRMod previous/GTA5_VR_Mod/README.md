# GTA 5 VR Mod

A comprehensive VR support system for Grand Theft Auto V that transforms the flat-screen game into a full stereoscopic VR experience.

## Features

- **OpenXR Support** - Native OpenXR 1.0+ with broad headset compatibility
- **OpenVR/SteamVR Fallback** - Works with SteamVR-compatible headsets
- **Multiple Rendering Modes**:
  - Native Stereo (best performance)
  - Synchronized Sequential (balanced)
  - Alternating Eye Rendering (maximum compatibility)
- **Full Motion Controller Support** - Track controllers, haptic feedback
- **Comfort Options** - Snap turning, comfort vignette, world scale
- **In-VR Settings Menu** - Adjust all settings without leaving VR
- **HUD Projection** - Game UI projected into VR space

## Quick Start

### Prerequisites

1. **GTA V** (Steam, Epic, or Rockstar Launcher version)
2. **VR Headset** with OpenXR or SteamVR support
3. **Visual Studio 2022** (for building)
4. **Windows 10/11** with latest updates

### Installation

1. **Download** the latest release or build from source
2. **Copy files** to your GTA V installation folder:
   ```
   GTA V/
   ├── GTA5VR.dll
   ├── GTA5VR.ini
   ├── openxr_loader.dll (if using OpenXR)
   └── openvr_api.dll (if using SteamVR)
   ```
3. **Run the injector** before or after starting GTA V:
   ```
   GTA5VR_Injector.exe
   ```

### First Launch

1. Start **SteamVR** or your OpenXR runtime
2. Launch **GTA V** (Story Mode only - DO NOT use online!)
3. Run **GTA5VR_Injector.exe**
4. Put on your headset - you should see the game in VR!
5. Press **F12** to recenter your view
6. Press **INSERT** to open the VR settings menu

## Building from Source

### Requirements

- Visual Studio 2022 with C++ workload
- CMake 3.20+
- Windows 10/11 SDK (10.0.22000.0+)

### Build Steps

```batch
# Clone the repository
git clone https://github.com/YourRepo/GTA5_VR_Mod.git
cd GTA5_VR_Mod

# Run the build script
build.bat

# Or use CMake directly
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Output Files

After building, you'll find:
- `build/bin/Release/GTA5VR.dll` - Main mod DLL
- `build/bin/Release/GTA5VR_Injector.exe` - DLL injector

## Controls

### Default VR Controller Bindings

| Button | Action |
|--------|--------|
| Right Trigger | Shoot |
| Right Grip | Aim/Zoom |
| Right Thumbstick | Movement |
| Left Trigger | Sprint |
| Left Grip | Take Cover |
| Left Thumbstick | Camera (when enabled) |
| A/X Button | Jump/Climb |
| B/Y Button | Reload |
| Menu Button | Open VR Settings |

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| F12 | Recenter View |
| INSERT | Toggle Settings Menu |
| HOME | Toggle Performance Overlay |
| END | Toggle Debug Mode |

## Configuration

Edit `GTA5VR.ini` to customize settings. See [CONFIGURATION.md](docs/CONFIGURATION.md) for details.

### Key Settings

```ini
[Rendering]
RenderingMode=synchronized_sequential  ; native_stereo, synchronized_sequential, alternating_eye
ResolutionScale=100                     ; 50-200%

[Comfort]
EnableSnapTurn=true
SnapTurnDegrees=45
EnableComfortVignette=true

[Display]
WorldScale=1.0                          ; 0.5-2.0
```

## Troubleshooting

See [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) for common issues.

### Quick Fixes

- **No VR display**: Ensure SteamVR/OpenXR is running before injection
- **Crash on inject**: Run as Administrator
- **Low FPS**: Reduce ResolutionScale, try AlternatingEye mode
- **Controls not working**: Check InputMode setting in config

## Important Warnings

- **SINGLE PLAYER ONLY** - Using this mod in GTA Online will result in a ban!
- **Backup your saves** before using any mods
- **Disable antivirus** temporarily if injection fails (false positive)

## Compatibility

### Tested Headsets
- Meta Quest 2/3/Pro (via Link/Air Link)
- Valve Index
- HTC Vive/Vive Pro
- Windows Mixed Reality headsets
- Pimax headsets

### Game Versions
- Steam version: Supported
- Epic Games version: Supported
- Rockstar Launcher version: Supported

## Credits

This project was inspired by:
- [Luke Ross R.E.A.L. VR Mods](https://www.patreon.com/realvr)
- [UEVR by Praydog](https://github.com/praydog/UEVR)
- [3Dmigoto](https://github.com/bo3b/3Dmigoto)

## License

This project is for educational purposes. GTA V is property of Rockstar Games.

## Support

- [GitHub Issues](https://github.com/YourRepo/GTA5_VR_Mod/issues)
- [Discord Server](https://discord.gg/example)
