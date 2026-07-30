@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem Update only the two binaries used by the development-panel workflow.
rem This does not install the dxgi proxy and does not touch user settings.
rem GTA must be closed. Usage: update_runtime.bat [GTA V Legacy directory]

set "P=[GTAVR-UPDATE]"
set "GAME_DIR=%~1"
if not defined GAME_DIR set "GAME_DIR=%GTAV_INSTALL_DIR%"
if not defined GAME_DIR (
    echo %P% ERROR: no game directory supplied.
    exit /b 1
)
if not exist "%GAME_DIR%\GTA5.exe" (
    echo %P% ERROR: GTA5.exe not found in %GAME_DIR%
    exit /b 1
)

tasklist /FI "IMAGENAME eq GTA5.exe" 2>nul | find /I "GTA5.exe" >nul
if not errorlevel 1 (
    echo %P% ERROR: close GTA5.exe before updating loaded binaries.
    exit /b 1
)

set "REPO=%~dp0.."
set "CORE=%REPO%\x64\Release\OVRInject.dll"
set "BRIDGE=%REPO%\GTAVRBridge\x64\Release\GTAVRBridge.asi"
if not exist "%CORE%" (
    echo %P% ERROR: build missing: %CORE%
    exit /b 1
)
if not exist "%BRIDGE%" (
    echo %P% ERROR: build missing: %BRIDGE%
    exit /b 1
)

call :CopyVerify "%CORE%" "%GAME_DIR%\OVRInject.dll" || exit /b 1
call :CopyVerify "%BRIDGE%" "%GAME_DIR%\GTAVRBridge.asi" || exit /b 1
echo %P% DONE. DLL and bridge are a matching build; settings were preserved.
pause
exit /b 0

:CopyVerify
copy /y "%~1" "%~2" >nul
if errorlevel 1 (
    echo %P% ERROR: copy failed: %~2
    exit /b 1
)
fc /b "%~1" "%~2" >nul 2>&1
if errorlevel 1 (
    echo %P% ERROR: verification failed: %~2
    exit /b 1
)
echo %P% updated %~2
exit /b 0
