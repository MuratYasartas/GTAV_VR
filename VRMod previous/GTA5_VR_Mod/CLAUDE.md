# CLAUDE.md - GTA 5 VR Mod Project Instructions

## Project Overview
A comprehensive VR injection mod for GTA V with OpenXR support, multiple rendering modes, and professional in-VR UI.

## Tech Stack
- **Language**: C++20
- **Graphics**: DirectX 11 (hooking via vtable/MinHook)
- **VR Runtimes**: OpenXR 1.0+ (primary), OpenVR/SteamVR (fallback)
- **Hooking**: MinHook library
- **UI**: Dear ImGui with VR overlay projection
- **Build System**: CMake 3.20+

## Project Structure
```
src/
├── core/           # Core systems (Logger, Config, VRCore, MemoryManager)
├── runtime/        # VR runtime abstraction (OpenXR, OpenVR, RuntimeDetector)
├── rendering/      # Stereo rendering and render modes
│   └── RenderModes/  # Native Stereo, Synchronized Sequential, AER
├── injection/      # DX11 hooks, shader interception, RAGE engine hooks
├── input/          # VR input, head tracking, motion controllers
├── camera/         # VR camera, cutscene handling, vehicle cameras
└── ui/             # VR overlay, settings menu, HUD projection
```

## Coding Standards

### Naming Conventions
- **Classes**: PascalCase (e.g., `VRCore`, `StereoRenderer`)
- **Functions**: PascalCase (e.g., `Initialize()`, `GetPose()`)
- **Variables**: camelCase (e.g., `frameCount`, `renderTarget`)
- **Member Variables**: m_ prefix (e.g., `m_device`, `m_isInitialized`)
- **Constants**: UPPER_SNAKE_CASE (e.g., `MAX_SWAPCHAIN_IMAGES`)
- **Enums**: PascalCase with k prefix for values (e.g., `RenderMode::kNativeStereo`)

### Code Style
- Use `#pragma once` for include guards
- Include headers in order: system, third-party, project
- Use RAII for resource management
- Prefer smart pointers where appropriate
- Use `nullptr` instead of `NULL` or `0`
- Check all return values and pointers

## Logging System

All major operations must be logged using the Logger macros:

```cpp
LOG_DEBUG("Component", "Detailed debug info: value=%d", value);
LOG_INFO("Component", "Operation completed successfully");
LOG_WARN("Component", "Non-critical issue: %s", message);
LOG_ERROR("Component", "Critical failure: HRESULT=0x%08X", hr);
```

### Component Names
- `VRCore` - Main VR lifecycle
- `DX11Hook` - DirectX hooking
- `OpenXR` - OpenXR runtime
- `OpenVR` - OpenVR/SteamVR runtime
- `Renderer` - Stereo rendering
- `Input` - VR input handling
- `Camera` - VR camera system
- `UI` - Settings and HUD

## Build Instructions

### Prerequisites
- Visual Studio 2022 with C++ workload
- CMake 3.20+
- Windows SDK 10.0.22000.0+
- OpenXR SDK (https://github.com/KhronosGroup/OpenXR-SDK)
- OpenVR SDK (https://github.com/ValveSoftware/openvr)
- MinHook library (https://github.com/TsudaKageyu/minhook)
- Dear ImGui (https://github.com/ocornut/imgui)

### Building
```bash
# Using build scripts
.\build.bat        # Windows batch
.\build.ps1        # PowerShell

# Manual CMake
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### Output
- `bin/GTA5VR.dll` - Main VR mod DLL
- `bin/GTA5VR_Injector.exe` - DLL injector tool
- `bin/GTA5VR.ini` - Configuration file

## Development Rules

1. **Complete Features Only**: Implement complete, working features. No stub implementations.

2. **Error Handling**: Check all HRESULT returns, validate pointers, handle edge cases gracefully.

3. **Thread Safety**: Use proper synchronization for multi-threaded operations (VR runtime calls from game thread).

4. **Resource Management**:
   - Release COM objects with `SafeRelease()` macro
   - Use RAII patterns
   - Clean up in reverse order of initialization

5. **Debug Logging**: Add LOG_DEBUG for detailed tracing, LOG_INFO for important events.

6. **Testing**: Test incrementally. Verify each hook before moving to the next.

## Key Technical Details

### DX11 Hook Points
- `IDXGISwapChain::Present` (vtable index 8) - Frame capture point
- `ID3D11DeviceContext::Draw` (vtable index 13) - Draw call interception
- `ID3D11DeviceContext::DrawIndexed` (vtable index 12) - Indexed draw interception
- `ID3D11DeviceContext::VSSetShader` - Vertex shader capture
- `ID3D11DeviceContext::PSSetShader` - Pixel shader capture

### OpenXR Requirements
- Extensions: `XR_KHR_D3D11_enable`, `XR_KHR_composition_layer_depth`
- Swapchain format: `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`
- View configuration: `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO`
- Reference space: `XR_REFERENCE_SPACE_TYPE_LOCAL` or `STAGE`

### Rendering Modes
1. **Native Stereo**: Single instanced draw, texture array output, best performance
2. **Synchronized Sequential**: Left then right in same frame, good compatibility
3. **Alternating Eye (AER)**: One eye per frame, relies on ASW/reprojection, max compatibility

### GTA V Specifics
- Process: `GTA5.exe`
- Engine: RAGE (Rockstar Advanced Game Engine)
- Camera system: Uses internal camera manager with world matrices
- HUD: Scaleform-based, rendered as overlay

## Hotkeys
- **F12**: Recenter VR view
- **INSERT**: Toggle settings menu
- **HOME**: Toggle performance overlay
- **END**: Toggle debug mode

## Configuration

Settings are stored in `GTA5VR.ini`:
- `[VRRuntime]` - OpenXR vs OpenVR preference
- `[Rendering]` - Render mode, resolution scale
- `[Display]` - World scale, IPD override
- `[Comfort]` - Snap turn, vignette
- `[HUD]` - HUD mode, distance, scale
- `[Camera]` - First person, vehicle cameras
- `[Debug]` - Debug overlay, logging level
