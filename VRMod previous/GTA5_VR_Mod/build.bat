@echo off
setlocal enabledelayedexpansion

:: GTA5VR Build Script
:: Usage: build.bat [Debug|Release] [clean]

echo.
echo ========================================
echo   GTA5VR Build Script
echo ========================================
echo.

:: Set defaults
set BUILD_TYPE=Release
set CLEAN=0

:: Parse arguments
if "%1"=="Debug" set BUILD_TYPE=Debug
if "%1"=="debug" set BUILD_TYPE=Debug
if "%1"=="Release" set BUILD_TYPE=Release
if "%1"=="release" set BUILD_TYPE=Release
if "%1"=="clean" set CLEAN=1
if "%2"=="clean" set CLEAN=1

echo Build Type: %BUILD_TYPE%
echo.

:: Check for Visual Studio
where cl >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Visual Studio compiler not found!
    echo.
    echo Please run this script from a Visual Studio Developer Command Prompt
    echo or run vcvarsall.bat first.
    echo.
    echo Example:
    echo   "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
    echo.
    pause
    exit /b 1
)

:: Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake not found!
    echo Please install CMake and add it to your PATH.
    pause
    exit /b 1
)

:: Clean build directory if requested
if %CLEAN%==1 (
    echo Cleaning build directory...
    if exist build rmdir /s /q build
)

:: Create build directory
if not exist build mkdir build
cd build

:: Configure with CMake
echo.
echo Configuring with CMake...
echo.
cmake .. -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed!
    cd ..
    pause
    exit /b 1
)

:: Build
echo.
echo Building %BUILD_TYPE%...
echo.
cmake --build . --config %BUILD_TYPE% --parallel
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed!
    cd ..
    pause
    exit /b 1
)

cd ..

:: Check output
echo.
echo ========================================
echo   Build Complete!
echo ========================================
echo.

if exist "build\bin\%BUILD_TYPE%\GTA5VR.dll" (
    echo [OK] GTA5VR.dll
    for %%I in ("build\bin\%BUILD_TYPE%\GTA5VR.dll") do echo      Size: %%~zI bytes
) else (
    echo [MISSING] GTA5VR.dll
)

if exist "build\bin\%BUILD_TYPE%\GTA5VR_Injector.exe" (
    echo [OK] GTA5VR_Injector.exe
    for %%I in ("build\bin\%BUILD_TYPE%\GTA5VR_Injector.exe") do echo      Size: %%~zI bytes
) else (
    echo [MISSING] GTA5VR_Injector.exe
)

echo.
echo Output directory: build\bin\%BUILD_TYPE%\
echo.

:: Copy config file to output
if exist "config\GTA5VR.ini" (
    copy /y "config\GTA5VR.ini" "build\bin\%BUILD_TYPE%\" >nul
    echo [OK] Copied GTA5VR.ini to output
)

echo.
echo Done!
echo.

pause
