@echo off
rem GTAVR Control Panel - start game, pick backend, inject, watch the log.
rem Elevated so injection matches the game's privileges.
cd /d C:\Repos\GTA_VR\GTAV_VR

rem --- Admin check via fltmc (works even when the Server service is off).
fltmc >nul 2>&1
if errorlevel 1 (
    if defined GTAVR_ELEVATED_RETRY goto :noelev_fail
    set GTAVR_ELEVATED_RETRY=1
    echo Requesting administrator rights ^(needed to inject into the game^)...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
goto :start
:noelev_fail
echo Could not obtain administrator rights - running without elevation.
echo Injection may fail if the game runs as admin.
:start
start "GTAVR Panel" pythonw tools\gtavr_panel.py
