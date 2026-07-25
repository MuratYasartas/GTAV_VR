@echo off
rem GTAVR Control Panel - start game, pick backend, inject, watch the log.
rem Elevated so injection matches the game's privileges.
cd /d C:\Repos\GTA_VR\GTAV_VR
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
start "GTAVR Panel" pythonw tools\gtavr_panel.py
