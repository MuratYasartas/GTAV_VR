@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ============================================================================
rem GTAVR installer - GTA V Legacy, STORY MODE ONLY.
rem
rem What:  copies the GTAVR release set into a GTA V Legacy game directory:
rem          - OVRInjectShim.dll installed AS dxgi.dll (the mod loader proxy,
rem            ADR-0005; loaded by the game at startup, loads OVRInject.dll)
rem          - OVRInject.dll, openvr_api.dll, openxr_loader.dll (if present)
rem          - manifests\ + a root copy of gtav_legacy.ini (the injected mod's
rem            BuildManifest searches <game dir>\gtav_legacy.ini first)
rem          - gtavr_settings.ini, gtavr_camera.ini
rem        An existing dxgi.dll is never overwritten without being backed up
rem        to dxgi.dll.gtavr-backup first (unless it is already our shim).
rem
rem Why:   the mod loads through the dxgi proxy; no game files are modified.
rem
rem Undo:  run tools\uninstall.bat with the same game directory. It restores
rem        dxgi.dll.gtavr-backup and removes exactly the files listed above.
rem
rem Usage: tools\install.bat [GTA_V_Legacy_dir]
rem        (falls back to %GTAV_INSTALL_DIR% when no argument is given;
rem         GTAVR_DIST_DIR overrides where the release DLLs are taken from)
rem
rem Idempotent: safe to run repeatedly. Every action is logged to the console
rem and to <game dir>\gtavr_install.log. Exit code 0 on success, 1 on error.
rem
rem NOTE: inside parenthesized blocks only !delayed! expansion is used, so
rem install paths containing parentheses cannot break the parser.
rem ============================================================================

set "P=[GTAVR-INSTALL]"

rem --- target game directory -------------------------------------------------
set "GAME_DIR=%~1"
if not defined GAME_DIR set "GAME_DIR=%GTAV_INSTALL_DIR%"
if not defined GAME_DIR (
	echo %P% ERROR: no game directory given. Usage: install.bat [GTA_V_Legacy_dir] or set GTAV_INSTALL_DIR.
	exit /b 1
)
if "%GAME_DIR:~-1%"=="\" set "GAME_DIR=%GAME_DIR:~0,-1%"
if not exist "%GAME_DIR%\" (
	echo %P% ERROR: game directory not found: %GAME_DIR%
	exit /b 1
)

set "LOG=%GAME_DIR%\gtavr_install.log"
echo %P% installing GTAVR into %GAME_DIR%
echo %P% %DATE% %TIME% installing into %GAME_DIR%>>"%LOG%"

if not exist "%GAME_DIR%\GTA5.exe" (
	echo %P% WARNING: !GAME_DIR!\GTA5.exe not found - is this really the GTA V Legacy directory?
	echo %P% WARNING: GTA5.exe not found>>"!LOG!"
)

rem --- source release set ------------------------------------------------------
set "REPO=%~dp0.."
if defined GTAVR_DIST_DIR (
	set "DIST=%GTAVR_DIST_DIR%"
) else if exist "%REPO%\build_solution_out\OVRInject.dll" (
	set "DIST=%REPO%\build_solution_out"
) else (
	set "DIST=%REPO%\x64\Release"
)
echo %P% source dir: %DIST%>>"%LOG%"

set "SHIM=%DIST%\OVRInjectShim.dll"
set "CORE=%DIST%\OVRInject.dll"
set "OPENVR=%DIST%\openvr_api.dll"
if not exist "%OPENVR%" set "OPENVR=%REPO%\ThirdParty\openvr\bin\x64\openvr_api.dll"
set "OPENXR=%DIST%\openxr_loader.dll"
if not exist "%OPENXR%" set "OPENXR=%REPO%\ThirdParty\openxr\bin\x64\openxr_loader.dll"

if not exist "%SHIM%" (
	echo %P% ERROR: !SHIM! not found - build Release^|x64 first.
	echo %P% ERROR: OVRInjectShim.dll missing>>"!LOG!"
	exit /b 1
)
if not exist "%CORE%" (
	echo %P% ERROR: !CORE! not found - build Release^|x64 first.
	echo %P% ERROR: OVRInject.dll missing>>"!LOG!"
	exit /b 1
)

rem --- dxgi.dll: back up whatever is there before installing the proxy -------
set "DXGI=%GAME_DIR%\dxgi.dll"
set "BACKUP=%GAME_DIR%\dxgi.dll.gtavr-backup"
if exist "%DXGI%" (
	fc /b "%DXGI%" "%SHIM%" >nul 2>&1
	if !errorlevel!==0 (
		echo %P% dxgi.dll is already the GTAVR shim - no backup needed.
		echo %P% dxgi.dll already ours - no backup>>"!LOG!"
	) else (
		if exist "%BACKUP%" (
			echo %P% backup dxgi.dll.gtavr-backup already exists - leaving it untouched.
			echo %P% backup already exists - untouched>>"!LOG!"
		) else (
			copy /y "%DXGI%" "%BACKUP%" >nul
			if errorlevel 1 (
				echo %P% ERROR: could not back up existing dxgi.dll - aborting before overwrite.
				echo %P% ERROR: backup of dxgi.dll failed>>"!LOG!"
				exit /b 1
			)
			fc /b "%DXGI%" "%BACKUP%" >nul 2>&1
			if errorlevel 1 (
				echo %P% ERROR: backup verify failed - dxgi.dll.gtavr-backup differs; aborting before overwrite.
				echo %P% ERROR: backup verify failed>>"!LOG!"
				exit /b 1
			)
			echo %P% backed up existing dxgi.dll to dxgi.dll.gtavr-backup
			echo %P% backed up dxgi.dll to dxgi.dll.gtavr-backup>>"!LOG!"
		)
	)
)

rem --- copy + verify each file ------------------------------------------------
call :CopyVerify "%SHIM%" "%DXGI%" "dxgi.dll - OVRInjectShim proxy" || exit /b 1
call :CopyVerify "%CORE%" "%GAME_DIR%\OVRInject.dll" "OVRInject.dll" || exit /b 1
call :CopyVerify "%OPENVR%" "%GAME_DIR%\openvr_api.dll" "openvr_api.dll" || exit /b 1
if exist "%OPENXR%" (
	call :CopyVerify "%OPENXR%" "%GAME_DIR%\openxr_loader.dll" "openxr_loader.dll" || exit /b 1
) else (
	echo %P% openxr_loader.dll not present in this build - skipped.
	echo %P% openxr_loader.dll skipped>>"!LOG!"
)

rem manifests\ folder (shipped layout) + root copy of gtav_legacy.ini (the
rem injected BuildManifest resolves GTAVR_SETTINGS_DIR/game dir + gtav_legacy.ini).
if not exist "%GAME_DIR%\manifests\" mkdir "%GAME_DIR%\manifests%"
call :CopyVerify "%REPO%\manifests\gtav_legacy.ini" "%GAME_DIR%\manifests\gtav_legacy.ini" "manifests\gtav_legacy.ini" || exit /b 1
call :CopyVerify "%REPO%\manifests\gtav_legacy.ini" "%GAME_DIR%\gtav_legacy.ini" "gtav_legacy.ini - root copy for BuildManifest" || exit /b 1

call :CopyVerify "%REPO%\gtavr_settings.ini" "%GAME_DIR%\gtavr_settings.ini" "gtavr_settings.ini" || exit /b 1
call :CopyVerify "%REPO%\gtavr_camera.ini" "%GAME_DIR%\gtavr_camera.ini" "gtavr_camera.ini" || exit /b 1

echo %P% DONE. Start the game normally; watch gtavrInjectLog.txt in the game dir.
echo %P% Reminder: STORY MODE ONLY. BattlEye must be OFF (Rockstar launcher setting).
echo %P% DONE>>"%LOG%"
exit /b 0

rem --- helper: copy one file and verify the bytes -----------------------------
:CopyVerify
set "SRC=%~1"
set "DST=%~2"
set "WHAT=%~3"
if not exist "%SRC%" (
	echo %P% ERROR: source missing for !WHAT!: !SRC!
	echo %P% ERROR: source missing !SRC!>>"!LOG!"
	exit /b 1
)
copy /y "%SRC%" "%DST%" >nul
if errorlevel 1 (
	echo %P% ERROR: copy failed: !WHAT! -^> !DST!
	echo %P% ERROR: copy failed !WHAT!>>"!LOG!"
	exit /b 1
)
fc /b "%SRC%" "%DST%" >nul 2>&1
if errorlevel 1 (
	echo %P% ERROR: verify failed - bytes differ: !DST!
	echo %P% ERROR: verify failed !DST!>>"!LOG!"
	exit /b 1
)
echo %P% installed !WHAT!
echo %P% installed !WHAT! -^> !DST!>>"!LOG!"
exit /b 0
