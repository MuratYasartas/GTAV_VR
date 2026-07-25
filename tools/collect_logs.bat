@echo off
rem ============================================================
rem  GTAVR - collect diagnostic logs into one bundle folder.
rem  Run this after a problem (crash, inert mod, wrong camera).
rem  For verbose logs first set GTAVR_VERBOSE=1 (or [Debug] verbose=1
rem  in gtavr_settings.ini) and reproduce the issue.
rem ============================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect_logs.ps1"
echo.
pause
