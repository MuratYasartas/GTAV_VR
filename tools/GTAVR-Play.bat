@echo off
rem ============================================================
rem  GTAVR Play - one-click launcher
rem  Runs preflight, starts GTA V (Story Mode), injects OVRInject.
rem  Story Mode ONLY. Disable BattlEye in the Rockstar launcher
rem  first (Settings -> uncheck BattlEye). Never use in GTA Online.
rem ============================================================
setlocal EnableDelayedExpansion
set "REPO=C:\Repos\GTA_VR\GTAV_VR"
set "BIN=%REPO%\x64\Release"
set "CFG=%USERPROFILE%\gtavr_play.ini"

rem --- 1) Elevation: the game runs as admin; injection needs to match.
net session >nul 2>&1
if errorlevel 1 (
    if defined GTAVR_SKIP_ELEVATE goto :noelev
    echo Requesting administrator rights ^(needed to inject into the game^)...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '%*' -Verb RunAs"
    exit /b
)
:noelev

rem --- 2) Resolve the game directory: saved config > env var > browse once.
if not defined GTAV_INSTALL_DIR if exist "%CFG%" set /p GTAV_INSTALL_DIR=<"%CFG%"
if not defined GTAV_INSTALL_DIR call :browse
if not defined GTAV_INSTALL_DIR goto :abort
echo Game dir: %GTAV_INSTALL_DIR%

rem --- 3) Make sure the runtime DLLs sit beside GTAVOVR.exe (idempotent).
if not exist "%BIN%\openvr_api.dll" copy /y "%REPO%\ThirdParty\openvr\bin\x64\openvr_api.dll" "%BIN%\" >nul
if not exist "%BIN%\openxr_loader.dll" copy /y "%REPO%\ThirdParty\openxr\bin\x64\openxr_loader.dll" "%BIN%\" >nul

rem --- 4) The injected DLL's imports resolve via PATH inherited by the game.
set "PATH=%BIN%;%PATH%"

rem --- 5) Preflight + launch + inject (GTAVOVR.exe does all three).
"%BIN%\GTAVOVR.exe" %*
set "RC=%errorlevel%"
echo.
if "%RC%"=="0" (
    echo Done. In-game log: gtavrInjectLog.txt beside GTA5.exe ^(or %%TEMP%% if the game dir is not writable^).
) else (
    echo GTAVOVR exited with code %RC% - read the preflight output above.
)
echo.
pause
exit /b %RC%

:browse
echo.
echo First run: point me at your GTA5.exe or PlayGTAV.exe
powershell -NoProfile -Command "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Filter = 'GTA V (GTA5.exe;PlayGTAV.exe)|GTA5.exe;PlayGTAV.exe'; $d.Title = 'Locate GTA V'; if ($d.ShowDialog() -eq 'OK') { [IO.Path]::GetDirectoryName($d.FileName) }" > "%TEMP%\gtavr_dir.txt"
set /p GTAV_INSTALL_DIR=<"%TEMP%\gtavr_dir.txt"
if defined GTAV_INSTALL_DIR echo %GTAV_INSTALL_DIR%>"%CFG%"
goto :eof

:abort
echo No game selected - aborting.
pause
exit /b 1
