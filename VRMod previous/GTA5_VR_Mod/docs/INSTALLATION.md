# GTA 5 VR Mod - Installation Guide

## Table of Contents

1. [System Requirements](#system-requirements)
2. [Prerequisites](#prerequisites)
3. [Download Options](#download-options)
4. [Installation Steps](#installation-steps)
5. [First Run Setup](#first-run-setup)
6. [VR Runtime Configuration](#vr-runtime-configuration)
7. [Verification](#verification)
8. [Updating](#updating)
9. [Uninstallation](#uninstallation)

---

## System Requirements

### Minimum Requirements

| Component | Requirement |
|-----------|-------------|
| OS | Windows 10 64-bit (version 1903+) |
| CPU | Intel Core i5-4590 / AMD Ryzen 3 1200 |
| RAM | 8 GB |
| GPU | NVIDIA GTX 1060 / AMD RX 580 (6GB VRAM) |
| Storage | 100 GB (for GTA V) + 500 MB (for mod) |
| VR Headset | Any OpenXR or SteamVR compatible headset |

### Recommended Requirements

| Component | Requirement |
|-----------|-------------|
| OS | Windows 11 64-bit |
| CPU | Intel Core i7-9700K / AMD Ryzen 7 3700X |
| RAM | 16 GB |
| GPU | NVIDIA RTX 3070 / AMD RX 6800 (8GB+ VRAM) |
| Storage | NVMe SSD |
| VR Headset | Quest 3, Valve Index, or Pimax Crystal |

---

## Prerequisites

### 1. GTA V Installation

Ensure GTA V is installed and working:

```
Default Locations:
- Steam: C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto V
- Epic: C:\Program Files\Epic Games\GTAV
- Rockstar: C:\Program Files\Rockstar Games\Grand Theft Auto V
```

**Important**: Launch GTA V at least once before installing the mod to ensure all game files are present.

### 2. VR Software

Install ONE of the following:

#### Option A: OpenXR (Recommended)

1. Install your headset's native runtime:
   - **Meta Quest**: Install Oculus PC app
   - **Valve Index**: Install SteamVR
   - **WMR**: Windows Mixed Reality Portal (pre-installed)

2. Set as active OpenXR runtime:
   - **Oculus**: Oculus app → Settings → General → OpenXR Runtime → Set as Active
   - **SteamVR**: SteamVR → Settings → Developer → Set SteamVR as OpenXR Runtime

#### Option B: SteamVR/OpenVR

1. Install Steam
2. Install SteamVR from Steam Library
3. Connect and set up your headset

### 3. Visual C++ Redistributable

Download and install: [VC++ 2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)

### 4. DirectX Runtime

Download and install: [DirectX End-User Runtime](https://www.microsoft.com/en-us/download/details.aspx?id=35)

---

## Download Options

### Option 1: Pre-built Release (Easiest)

1. Go to [Releases Page](https://github.com/YourRepo/GTA5_VR_Mod/releases)
2. Download `GTA5VR_v1.0.0.zip`
3. Extract to a temporary folder

### Option 2: Build from Source

See [Building from Source](#building-from-source) section below.

---

## Installation Steps

### Step 1: Locate GTA V Folder

Find your GTA V installation:

**Steam:**
```
Right-click GTA V in Steam → Manage → Browse Local Files
```

**Epic Games:**
```
Open Epic Launcher → Library → GTA V → ⋮ → Manage → Open Install Location
```

**Rockstar:**
```
Open Rockstar Launcher → Settings → GTA V → View Installation Folder
```

### Step 2: Copy Mod Files

Copy these files from the release to your GTA V folder:

```
GTA V Installation Folder/
├── GTA5VR.dll              ← Main mod file
├── GTA5VR.ini              ← Configuration file
├── GTA5VR_Injector.exe     ← DLL injector
├── openxr_loader.dll       ← OpenXR runtime (required)
└── openvr_api.dll          ← OpenVR fallback (optional)
```

### Step 3: Configure Antivirus Exceptions

Add exceptions for:
- `GTA V folder\GTA5VR.dll`
- `GTA V folder\GTA5VR_Injector.exe`

**Windows Defender:**
1. Windows Security → Virus & threat protection
2. Manage settings → Exclusions → Add exclusion
3. Add folder: Your GTA V installation folder

### Step 4: Verify File Placement

Your GTA V folder should now contain:

```
Grand Theft Auto V/
├── GTA5.exe                 ← Original game
├── GTA5VR.dll               ← ✓ Mod DLL
├── GTA5VR.ini               ← ✓ Config file
├── GTA5VR_Injector.exe      ← ✓ Injector
├── openxr_loader.dll        ← ✓ OpenXR loader
├── openvr_api.dll           ← ✓ OpenVR (optional)
├── PlayGTAV.exe
├── GTAVLauncher.exe
└── ... (other game files)
```

---

## First Run Setup

### Step 1: Start VR Runtime

1. Put on your VR headset
2. Ensure controllers are connected
3. Start SteamVR or your OpenXR runtime
4. Verify headset tracking is working

### Step 2: Launch GTA V

1. Start GTA V normally (Steam/Epic/Rockstar)
2. Wait for the game to fully load to the main menu
3. **IMPORTANT**: Select Story Mode (NOT Online!)

### Step 3: Inject the Mod

**Method A: Manual Injection**
1. Run `GTA5VR_Injector.exe` as Administrator
2. The injector will find GTA5.exe automatically
3. Click "Inject" or press Enter
4. You should see "Injection successful!"

**Method B: Auto-Injection**
1. Create a shortcut to `GTA5VR_Injector.exe`
2. Right-click → Properties → Add `--auto` to target
3. Run this shortcut after starting GTA V

### Step 4: Verify VR is Active

Signs that VR is working:
- Game renders in your headset (both eyes)
- Head tracking moves the camera
- "GTA5VR Loaded" appears in-game

### Step 5: Initial Calibration

1. Press **F12** to recenter your view
2. Press **INSERT** to open settings menu
3. Adjust **World Scale** if you feel too big/small
4. Configure comfort options as needed

---

## VR Runtime Configuration

### OpenXR Configuration

Edit `GTA5VR.ini`:

```ini
[VRRuntime]
PreferredRuntime=openxr
VRAPI=0  ; 0=auto, 1=oculus, 2=steamvr, 3=wmr
```

### OpenVR/SteamVR Configuration

```ini
[VRRuntime]
PreferredRuntime=openvr
```

### Runtime Priority

The mod checks runtimes in this order:
1. OpenXR (if available and configured)
2. OpenVR/SteamVR (fallback)

---

## Verification

### Test Checklist

Run through this checklist to verify installation:

- [ ] GTA V launches normally
- [ ] Injector runs without errors
- [ ] VR headset shows game image
- [ ] Both eyes render correctly
- [ ] Head tracking moves camera
- [ ] F12 recenters view
- [ ] INSERT opens settings menu
- [ ] Controllers are detected
- [ ] No crashes after 5 minutes

### Verification Commands

Run the injector with verification:

```batch
GTA5VR_Injector.exe --verify
```

This will check:
- DLL file integrity
- OpenXR/OpenVR availability
- GTA V process status
- Hook installation

---

## Building from Source

### Install Build Tools

1. **Visual Studio 2022**
   - Download: https://visualstudio.microsoft.com/
   - Select "Desktop development with C++"
   - Include Windows 10/11 SDK

2. **CMake 3.20+**
   - Download: https://cmake.org/download/
   - Add to PATH during installation

3. **Git**
   - Download: https://git-scm.com/

### Clone and Build

```batch
# Clone repository
git clone https://github.com/YourRepo/GTA5_VR_Mod.git
cd GTA5_VR_Mod

# Initialize submodules (for dependencies)
git submodule update --init --recursive

# Build using script
build.bat Release

# Or build manually
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Build Output

```
build/
├── bin/
│   └── Release/
│       ├── GTA5VR.dll
│       ├── GTA5VR_Injector.exe
│       └── GTA5VR.ini
└── lib/
    └── Release/
        └── GTA5VR.lib
```

---

## Updating

### Automatic Update Check

The mod checks for updates on startup. To disable:

```ini
[General]
CheckForUpdates=false
```

### Manual Update

1. Download new release
2. Close GTA V completely
3. Replace files in GTA V folder
4. Keep your `GTA5VR.ini` (or merge settings)

### Preserving Settings

Your settings are stored in `GTA5VR.ini`. When updating:
1. Backup `GTA5VR.ini`
2. Install new version
3. Compare and merge settings

---

## Uninstallation

### Complete Removal

1. Close GTA V and any VR software
2. Delete these files from GTA V folder:
   - `GTA5VR.dll`
   - `GTA5VR.ini`
   - `GTA5VR_Injector.exe`
   - `openxr_loader.dll`
   - `openvr_api.dll`
   - `GTA5VR.log` (if exists)

3. Remove antivirus exceptions (optional)

### Verify Removal

Launch GTA V normally - it should work without VR.

---

## Next Steps

- Read [CONFIGURATION.md](CONFIGURATION.md) for all settings
- See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) if you have issues
- Join our Discord for support
