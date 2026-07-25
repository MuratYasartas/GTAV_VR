# OpenXR SDK

This directory should contain the OpenXR SDK.

## Setup Instructions

1. Download the OpenXR SDK from: https://github.com/KhronosGroup/OpenXR-SDK/releases
2. Extract the contents so the structure looks like:
   ```
   ThirdParty/openxr/
   ├── include/
   │   └── openxr/
   │       ├── openxr.h
   │       ├── openxr_platform.h
   │       └── openxr_reflection.h
   └── lib/
       ├── openxr_loader.lib (x64)
       └── openxr_loader.lib (Win32)
   ```

3. Alternatively, use vcpkg:
   ```
   vcpkg install openxr-loader:x64-windows
   ```

## Required Version

OpenXR 1.0 or later
