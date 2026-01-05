cmake_minimum_required(VERSION 3.20)
project(GTA5VR VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Configuration options
option(GTA5VR_BUILD_INJECTOR "Build the DLL injector tool" ON)
option(GTA5VR_USE_OPENXR "Enable OpenXR support" ON)
option(GTA5VR_USE_OPENVR "Enable OpenVR support" ON)

# Find Windows SDK
if(WIN32)
    set(CMAKE_SYSTEM_VERSION 10.0.22000.0)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs/openxr/include
    ${CMAKE_SOURCE_DIR}/libs/openvr/include
    ${CMAKE_SOURCE_DIR}/libs/minhook/include
    ${CMAKE_SOURCE_DIR}/libs/imgui
)

# Source files - Core
set(CORE_SOURCES
    src/core/VRCore.cpp
    src/core/VRConfig.cpp
    src/core/Logger.cpp
    src/core/MemoryManager.cpp
)

# Source files - Runtime
set(RUNTIME_SOURCES
    src/runtime/RuntimeInterface.cpp
    src/runtime/OpenXRRuntime.cpp
    src/runtime/OpenVRRuntime.cpp
    src/runtime/RuntimeDetector.cpp
)

# Source files - Rendering
set(RENDERING_SOURCES
    src/rendering/StereoRenderer.cpp
    src/rendering/SwapchainManager.cpp
    src/rendering/RenderModes/NativeStereo.cpp
    src/rendering/RenderModes/SynchronizedSequential.cpp
    src/rendering/RenderModes/AlternatingEyeRendering.cpp
)

# Source files - Injection
set(INJECTION_SOURCES
    src/injection/DX11Hook.cpp
    src/injection/ShaderInterceptor.cpp
    src/injection/RAGEEngineHook.cpp
)

# Source files - Input
set(INPUT_SOURCES
    src/input/VRInputManager.cpp
    src/input/HeadTracking.cpp
    src/input/MotionController.cpp
)

# Source files - Camera
set(CAMERA_SOURCES
    src/camera/VRCamera.cpp
    src/camera/CutsceneHandler.cpp
    src/camera/VehicleCamera.cpp
)

# Source files - UI
set(UI_SOURCES
    src/ui/VROverlay.cpp
    src/ui/SettingsMenu.cpp
    src/ui/HUDProjection.cpp
)

# DLL Entry Point
set(ENTRY_SOURCES
    src/DllMain.cpp
)

# All sources
set(ALL_SOURCES
    ${ENTRY_SOURCES}
    ${CORE_SOURCES}
    ${RUNTIME_SOURCES}
    ${RENDERING_SOURCES}
    ${INJECTION_SOURCES}
    ${INPUT_SOURCES}
    ${CAMERA_SOURCES}
    ${UI_SOURCES}
)

# Main DLL
add_library(GTA5VR SHARED ${ALL_SOURCES})

# Preprocessor definitions
target_compile_definitions(GTA5VR PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    GTA5VR_EXPORTS
)

# Link libraries
target_link_libraries(GTA5VR PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    Psapi.lib
)

# OpenXR
if(GTA5VR_USE_OPENXR)
    target_compile_definitions(GTA5VR PRIVATE GTA5VR_USE_OPENXR)
    target_link_libraries(GTA5VR PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/openxr/lib/openxr_loader.lib
    )
endif()

# OpenVR
if(GTA5VR_USE_OPENVR)
    target_compile_definitions(GTA5VR PRIVATE GTA5VR_USE_OPENVR)
    target_link_libraries(GTA5VR PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/openvr/lib/win64/openvr_api.lib
    )
endif()

# MinHook
target_link_libraries(GTA5VR PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/minhook/lib/libMinHook.x64.lib
)

# Compiler flags
if(MSVC)
    target_compile_options(GTA5VR PRIVATE
        /W4
        /WX-
        /permissive-
        /MP
        /GS
        /Gy
        /Zc:wchar_t
        /Zc:forScope
        /Zc:inline
    )

    # Release optimizations
    target_compile_options(GTA5VR PRIVATE "$<$<CONFIG:Release>:/O2;/Ob2;/Oi;/Ot>")

    # Debug settings
    target_compile_options(GTA5VR PRIVATE "$<$<CONFIG:Debug>:/Od;/Zi>")
endif()

# Injector tool
if(GTA5VR_BUILD_INJECTOR)
    add_executable(GTA5VR_Injector
        tools/Injector/main.cpp
    )

    target_compile_definitions(GTA5VR_Injector PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    target_link_libraries(GTA5VR_Injector PRIVATE
        Psapi.lib
    )
endif()

# Install rules
install(TARGETS GTA5VR
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)

if(GTA5VR_BUILD_INJECTOR)
    install(TARGETS GTA5VR_Injector
        RUNTIME DESTINATION bin
    )
endif()

install(FILES
    config/GTA5VR.ini
    DESTINATION config
)

install(FILES
    README.md
    docs/INSTALLATION.md
    docs/CONFIGURATION.md
    docs/TROUBLESHOOTING.md
    DESTINATION docs
)

install(DIRECTORY shaders/
    DESTINATION shaders
)

# Copy config to output directory
add_custom_command(TARGET GTA5VR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/config/GTA5VR.ini
    $<TARGET_FILE_DIR:GTA5VR>/GTA5VR.ini
)

# Copy OpenXR loader DLL if available
if(GTA5VR_USE_OPENXR AND EXISTS ${CMAKE_SOURCE_DIR}/libs/openxr/bin/openxr_loader.dll)
    add_custom_command(TARGET GTA5VR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/libs/openxr/bin/openxr_loader.dll
        $<TARGET_FILE_DIR:GTA5VR>/openxr_loader.dll
    )
endif()

# Copy OpenVR API DLL if available
if(GTA5VR_USE_OPENVR AND EXISTS ${CMAKE_SOURCE_DIR}/libs/openvr/bin/win64/openvr_api.dll)
    add_custom_command(TARGET GTA5VR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/libs/openvr/bin/win64/openvr_api.dll
        $<TARGET_FILE_DIR:GTA5VR>/openvr_api.dll
    )
endif()
