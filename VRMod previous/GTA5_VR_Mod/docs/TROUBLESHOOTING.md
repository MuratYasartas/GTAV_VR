# GTA 5 VR Mod - Troubleshooting Guide

Solutions for common issues with the GTA 5 VR mod.

---

## Table of Contents

1. [Installation Issues](#installation-issues)
2. [Injection Problems](#injection-problems)
3. [VR Display Issues](#vr-display-issues)
4. [Performance Problems](#performance-problems)
5. [Controller Issues](#controller-issues)
6. [Crashes and Stability](#crashes-and-stability)
7. [Visual Glitches](#visual-glitches)
8. [Audio Issues](#audio-issues)
9. [Diagnostic Tools](#diagnostic-tools)
10. [Getting Help](#getting-help)

---

## Installation Issues

### Files Not Found After Extraction

**Symptoms**: Antivirus deleted mod files during extraction

**Solution**:
1. Disable real-time antivirus temporarily
2. Re-extract the mod files
3. Add GTA V folder to antivirus exceptions
4. Re-enable antivirus

### Wrong DLL Architecture

**Symptoms**: "Invalid image format" or similar error

**Solution**:
- Ensure you're using the 64-bit (x64) version
- GTA V is 64-bit only
- Download the correct release package

### Missing Dependencies

**Symptoms**: "VCRUNTIME140.dll not found" or similar

**Solution**:
1. Install Visual C++ Redistributable 2022 (x64):
   https://aka.ms/vs/17/release/vc_redist.x64.exe
2. Install DirectX End-User Runtime:
   https://www.microsoft.com/en-us/download/details.aspx?id=35

---

## Injection Problems

### Injector Doesn't Find GTA5.exe

**Symptoms**: "Process not found" error

**Solutions**:
1. Ensure GTA V is running (visible in Task Manager)
2. Run injector as Administrator
3. Wait for game to fully load to main menu
4. Try manual process selection:
   ```
   GTA5VR_Injector.exe --pid 12345
   ```

### Injection Failed - Access Denied

**Symptoms**: "Failed to open process" or "Access denied"

**Solutions**:
1. **Run as Administrator** - Right-click → Run as administrator
2. **Disable Game Bar**:
   - Settings → Gaming → Xbox Game Bar → Off
3. **Disable Overlay Software**:
   - Discord overlay
   - GeForce Experience overlay
   - Steam overlay (Settings → In-Game → Disable)
4. **Check antivirus** - Add exception for injector

### Injection Causes Immediate Crash

**Symptoms**: GTA V closes immediately after injection

**Solutions**:
1. **Verify game files**:
   - Steam: Right-click → Properties → Local Files → Verify
   - Epic: Library → ⋮ → Verify
2. **Remove other mods** temporarily
3. **Update GPU drivers**
4. **Try compatibility mode**:
   - Right-click GTA5.exe → Properties → Compatibility
   - Try Windows 8 compatibility mode

### Injection Hangs

**Symptoms**: Injector freezes during injection

**Solutions**:
1. Wait up to 30 seconds (DLL loading can be slow)
2. Kill and restart both GTA V and injector
3. Try with `--timeout 60000` flag

---

## VR Display Issues

### No Image in Headset

**Symptoms**: Headset shows SteamVR/Oculus home but not game

**Solutions**:
1. **Verify VR runtime is running**:
   - SteamVR: SteamVR window should be open
   - Oculus: Oculus app running in background
2. **Check injection was successful** - Look for "GTA5VR Loaded" message
3. **Try switching runtime**:
   ```ini
   [VRRuntime]
   PreferredRuntime=openvr  ; Try openvr instead of openxr
   ```
4. **Force window focus** - Click on GTA V window before injecting

### Only One Eye Renders

**Symptoms**: See image in only left or right eye

**Solutions**:
1. **Switch rendering mode**:
   ```ini
   [Rendering]
   RenderingMode=synchronized_sequential
   ```
2. **Check GPU driver version** - Update to latest
3. **Disable FXAA** in GTA V graphics settings

### Image is Flat (No 3D Effect)

**Symptoms**: Both eyes show same image (no depth)

**Solutions**:
1. **Verify IPD setting**:
   ```ini
   [Display]
   IPDOverride=0  ; Use headset IPD
   ```
2. **Check rendering mode** - `native_stereo` may not work on all systems
3. **Update VR runtime** - Get latest SteamVR/Oculus software

### Wrong Eye Swap (Cross-Eyed)

**Symptoms**: Left and right eyes are swapped

**Solution**:
```ini
[Display]
SwapEyes=true  ; Add this setting if needed
```

### Image Judder/Jitter

**Symptoms**: Image shakes or stutters

**Solutions**:
1. **Enable reprojection**:
   ```ini
   [Rendering]
   AllowReprojection=true
   ```
2. **Lower resolution**:
   ```ini
   [Rendering]
   ResolutionScale=80
   ```
3. **Try alternating eye mode**:
   ```ini
   [Rendering]
   RenderingMode=alternating_eye
   ```

---

## Performance Problems

### Low Frame Rate (FPS)

**Symptoms**: Choppy, laggy gameplay

**Solutions**:
1. **Lower render scale**:
   ```ini
   [Rendering]
   ResolutionScale=80
   ```

2. **Use alternating eye rendering**:
   ```ini
   [Rendering]
   RenderingMode=alternating_eye
   ```

3. **In-game settings** - Lower these in GTA V:
   - Grass Quality: Normal
   - Shadows: Normal
   - Reflection Quality: Normal
   - Water Quality: Normal
   - Particles Quality: Normal
   - Post FX: Normal
   - Extended Distance Scaling: 0%

4. **Disable VSync** in GTA V settings

5. **Close background applications**:
   - Chrome/Firefox
   - Discord (disable overlay)
   - Recording software

### High GPU/CPU Usage

**Symptoms**: System running hot, fans loud

**Solutions**:
1. Use performance preset (see CONFIGURATION.md)
2. Enable frame rate limit:
   ```ini
   [Performance]
   LimitFramerate=true
   FramerateLimit=72
   ```
3. Reduce population density in GTA V settings

### Memory Issues

**Symptoms**: Game crashes after extended play, "out of memory"

**Solutions**:
1. **Increase virtual memory**:
   - System Properties → Advanced → Performance Settings
   - Virtual Memory → Custom size: 16384 - 32768 MB
2. **Close memory-heavy applications**
3. **Restart game periodically** for long sessions

---

## Controller Issues

### Controllers Not Detected

**Symptoms**: VR controllers don't work in game

**Solutions**:
1. **Verify in VR home** - Controllers work in SteamVR/Oculus home?
2. **Check input mode**:
   ```ini
   [Input]
   InputMode=gamepad
   ```
3. **Restart controller** - Turn off and on
4. **Re-pair controller** through VR software

### Wrong Button Mapping

**Symptoms**: Buttons do unexpected actions

**Solutions**:
1. **Check controller profile** - Using correct interaction profile
2. **Reset to defaults**:
   ```ini
   [Input]
   ; Delete all custom bindings to reset
   ```
3. **Try different input mode**:
   ```ini
   [Input]
   InputMode=motion_controllers
   ```

### Stick Drift

**Symptoms**: Character moves without touching stick

**Solution**:
```ini
[Input]
DeadzoneLeft=0.2
DeadzoneRight=0.2
```

### No Haptic Feedback

**Symptoms**: Controllers don't vibrate

**Solutions**:
1. **Enable haptics**:
   ```ini
   [Input]
   EnableHaptics=true
   HapticIntensity=1.0
   ```
2. **Check controller batteries**
3. **Verify in other VR apps**

---

## Crashes and Stability

### Crash on Startup

**Symptoms**: Game crashes before/during loading

**Solutions**:
1. **Verify game files** through launcher
2. **Remove all mods** except GTA5VR
3. **Update drivers** - GPU, chipset
4. **Run as Administrator**
5. **Disable overlays** - Steam, Discord, GeForce Experience

### Crash During Gameplay

**Symptoms**: Random crashes while playing

**Solutions**:
1. **Enable logging** to find cause:
   ```ini
   [Debug]
   EnableDebugMode=true
   LogLevel=debug
   LogToFile=true
   ```
2. **Check GTA5VR.log** for error messages
3. **Lower graphics settings**
4. **Disable specific features** to isolate issue

### Crash in Cutscenes

**Symptoms**: Crashes when cutscene starts

**Solution**:
```ini
[Camera]
CutsceneMode=skip  ; Skip problematic cutscenes
```

### Freeze/Hang

**Symptoms**: Game freezes, doesn't crash

**Solutions**:
1. **Wait 30 seconds** - May be loading
2. **Check Task Manager** - Is GTA V using CPU?
3. **Alt+Tab** and back may unfreeze
4. **Force close** via Task Manager if needed

---

## Visual Glitches

### UI Elements in Wrong Position

**Symptoms**: HUD in wrong place or 3D depth

**Solutions**:
1. **Reset HUD settings**:
   ```ini
   [HUD]
   HUDMode=floating
   HUDDistance=2.0
   HUDScale=1.0
   ```
2. **Toggle HUD** - Press assigned key to cycle modes

### Objects Pop In/Out

**Symptoms**: Objects appear/disappear suddenly

**Solutions**:
1. **Adjust clipping planes**:
   ```ini
   [Display]
   NearClip=0.05
   FarClip=15000.0
   ```
2. **Increase Extended Distance Scaling** in GTA V

### Shader Artifacts

**Symptoms**: Visual corruption, wrong colors

**Solutions**:
1. **Update GPU drivers**
2. **Try different rendering mode**
3. **Disable shader effects**:
   ```ini
   [Advanced]
   DisablePostProcessing=true
   ```

### Sky/Water Rendering Wrong

**Symptoms**: Sky is flat or water looks broken

**Known issue**: Some effects don't convert perfectly to stereo

**Workaround**:
```ini
[Advanced]
DisableWaterReflections=true
```

---

## Audio Issues

### No Sound in Headset

**Symptoms**: Audio plays through speakers, not headset

**Solutions**:
1. **Set VR headset as default audio** in Windows Sound settings
2. **Configure in SteamVR/Oculus** audio settings
3. **Restart game** after changing audio devices

### Audio Crackling/Popping

**Symptoms**: Audio has pops or crackles

**Solutions**:
1. **Increase audio buffer** in Windows audio settings
2. **Close other audio applications**
3. **Update audio drivers**

---

## Diagnostic Tools

### Log Analysis

Enable detailed logging:
```ini
[Debug]
EnableDebugMode=true
LogLevel=debug
LogToFile=true
LogFilePath=GTA5VR.log
```

Check `GTA5VR.log` for:
- Error messages
- Warning signs
- Initialization failures

### Verification Mode

Run comprehensive check:
```
GTA5VR_Injector.exe --verify --verbose
```

### System Info Collection

Collect system information:
```
GTA5VR_Injector.exe --sysinfo > system_info.txt
```

Include this when reporting issues.

---

## Getting Help

### Before Asking for Help

1. Check this troubleshooting guide
2. Search existing GitHub issues
3. Try latest release version
4. Collect relevant information:
   - `GTA5VR.log` file
   - System specs (CPU, GPU, RAM)
   - VR headset model
   - GTA V version (Steam/Epic/RGL)
   - Steps to reproduce issue

### Where to Get Help

1. **GitHub Issues**: https://github.com/YourRepo/GTA5_VR_Mod/issues
2. **Discord Server**: https://discord.gg/example
3. **Reddit**: r/GTA5VRMod

### Reporting Bugs

Create a GitHub issue with:
- Description of problem
- Steps to reproduce
- Expected vs actual behavior
- Log files (attach GTA5VR.log)
- System information
- Screenshots/videos if applicable
