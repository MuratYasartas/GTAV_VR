# ImGui Setup for GTA VR

## Download ImGui

ImGui is now vendored in this folder for convenience. If you want to update it,
replace the files below with a newer ImGui release.

1. Download ImGui from: https://github.com/ocornut/imgui
2. Copy the following files to this directory (`ThirdParty/imgui/`):

### Core Files (Required)
- `imgui.cpp`
- `imgui.h`
- `imgui_draw.cpp`
- `imgui_tables.cpp`
- `imgui_widgets.cpp`
- `imgui_internal.h`
- `imconfig.h`
- `imstb_rectpack.h`
- `imstb_textedit.h`
- `imstb_truetype.h`

### D3D11 Backend (Required)
- `backends/imgui_impl_dx11.cpp`
- `backends/imgui_impl_dx11.h`
- `backends/imgui_impl_win32.cpp`
- `backends/imgui_impl_win32.h`

## Directory Structure

```
ThirdParty/imgui/
├── SETUP.md (this file)
├── imgui.cpp
├── imgui.h
├── imgui_draw.cpp
├── imgui_tables.cpp
├── imgui_widgets.cpp
├── imgui_internal.h
├── imconfig.h
├── imstb_rectpack.h
├── imstb_textedit.h
├── imstb_truetype.h
└── backends/
    ├── imgui_impl_dx11.cpp
    ├── imgui_impl_dx11.h
    ├── imgui_impl_win32.cpp
    └── imgui_impl_win32.h
```

## Integration

After adding ImGui files:

1. Add the source files to OVRInject.vcxproj
2. Add `ThirdParty/imgui` to include directories
3. Enable XROverlayUI by defining `HAS_IMGUI` in the project

## Visual Studio Project Changes

Add to OVRInject.vcxproj:

```xml
<ClCompile Include="..\ThirdParty\imgui\imgui.cpp" />
<ClCompile Include="..\ThirdParty\imgui\imgui_draw.cpp" />
<ClCompile Include="..\ThirdParty\imgui\imgui_tables.cpp" />
<ClCompile Include="..\ThirdParty\imgui\imgui_widgets.cpp" />
<ClCompile Include="..\ThirdParty\imgui\backends\imgui_impl_dx11.cpp" />
<ClCompile Include="..\ThirdParty\imgui\backends\imgui_impl_win32.cpp" />
```

Add to Include Directories:
```
$(SolutionDir)ThirdParty\imgui;$(SolutionDir)ThirdParty\imgui\backends
```

Add Preprocessor Definition:
```
HAS_IMGUI
```
