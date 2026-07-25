@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ============================================================================
rem GTAVR uninstaller - GTA V Legacy, STORY MODE ONLY.
rem
rem What:  removes exactly the files tools\install.bat placed in the game
rem        directory:
rem          dxgi.dll (only if it is our OVRInjectShim proxy - see below),
rem          OVRInject.dll, openvr_api.dll, openxr_loader.dll,
rem          manifests\gtav_legacy.ini (+ manifests\ dir if left empty),
rem          gtav_legacy.ini, gtavr_settings.ini, gtavr_camera.ini,
rem          gtavr_install.log.
rem        If dxgi.dll.gtavr-backup exists, the original dxgi.dll is restored
rem        from it. A dxgi.dll that is neither our shim nor backed up is left
rem        alone with a warning - uninstall never touches foreign files.
rem
rem Why:   the dxgi proxy is the only loader; removing it plus the payload
rem        files returns the game directory to fully vanilla.
rem
rem Undo:  not needed (this IS the undo). To reinstall: tools\install.bat.
rem
rem Usage: tools\uninstall.bat [GTA_V_Legacy_dir]
rem        (falls back to %GTAV_INSTALL_DIR% when no argument is given)
rem
rem Idempotent: missing files are reported as already-absent, not errors.
rem Exit code 0 when the directory is verified clean, 1 otherwise.
rem NOTE: inside parenthesized blocks only !delayed! expansion is used, so
rem game paths containing parentheses cannot break the parser.
rem ============================================================================

set "P=[GTAVR-UNINSTALL]"

set "GAME_DIR=%~1"
if not defined GAME_DIR set "GAME_DIR=%GTAV_INSTALL_DIR%"
if not defined GAME_DIR (
	echo %P% ERROR: no game directory given. Usage: uninstall.bat [GTA_V_Legacy_dir] or set GTAV_INSTALL_DIR.
	exit /b 1
)
if "%GAME_DIR:~-1%"=="\" set "GAME_DIR=%GAME_DIR:~0,-1%"
if not exist "%GAME_DIR%\" (
	echo %P% ERROR: game directory not found: %GAME_DIR%
	exit /b 1
)

set "REPO=%~dp0.."
if defined GTAVR_DIST_DIR (
	set "DIST=%GTAVR_DIST_DIR%"
) else if exist "%REPO%\build_solution_out\OVRInjectShim.dll" (
	set "DIST=%REPO%\build_solution_out"
) else (
	set "DIST=%REPO%\x64\Release"
)
set "SHIM=%DIST%\OVRInjectShim.dll"

set "DXGI=%GAME_DIR%\dxgi.dll"
set "BACKUP=%GAME_DIR%\dxgi.dll.gtavr-backup"
set "DIRTY=0"

rem --- dxgi.dll: restore backup, or delete only if it is our shim -------------
if exist "%BACKUP%" (
	if exist "%DXGI%" del /f /q "%DXGI%" >nul 2>&1
	move /y "%BACKUP%" "%DXGI%" >nul
	if errorlevel 1 (
		echo %P% ERROR: could not restore dxgi.dll.gtavr-backup to dxgi.dll
		set "DIRTY=1"
	) else (
		echo %P% restored original dxgi.dll from dxgi.dll.gtavr-backup
	)
) else if exist "%DXGI%" (
	set "OURS=0"
	if exist "%SHIM%" (
		fc /b "%DXGI%" "%SHIM%" >nul 2>&1
		if !errorlevel!==0 set "OURS=1"
	)
	if "!OURS!"=="1" (
		del /f /q "%DXGI%" >nul 2>&1
		echo %P% removed dxgi.dll - GTAVR shim
	) else (
		echo %P% WARNING: dxgi.dll is not the GTAVR shim and no backup exists - leaving it untouched.
	)
) else (
	echo %P% dxgi.dll already absent.
)

rem --- payload + config files --------------------------------------------------
for %%F in (OVRInject.dll openvr_api.dll openxr_loader.dll gtav_legacy.ini gtavr_settings.ini gtavr_camera.ini) do (
	if exist "!GAME_DIR!\%%F" (
		del /f /q "!GAME_DIR!\%%F" >nul 2>&1
		if exist "!GAME_DIR!\%%F" (
			echo %P% ERROR: could not remove %%F
			set "DIRTY=1"
		) else (
			echo %P% removed %%F
		)
	) else (
		echo %P% %%F already absent.
	)
)

rem --- manifests folder (only what we put there) -------------------------------
if exist "%GAME_DIR%\manifests\gtav_legacy.ini" (
	del /f /q "%GAME_DIR%\manifests\gtav_legacy.ini" >nul 2>&1
	echo %P% removed manifests\gtav_legacy.ini
) else (
	echo %P% manifests\gtav_legacy.ini already absent.
)
if exist "%GAME_DIR%\manifests\" (
	rd "%GAME_DIR%\manifests" >nul 2>&1
	if exist "!GAME_DIR!\manifests\" (
		echo %P% manifests\ not empty after removing our file - leaving the folder in place.
	) else (
		echo %P% removed empty manifests\ folder
	)
)

rem --- our install log (removed last; console output is the uninstall record) --
if exist "%GAME_DIR%\gtavr_install.log" (
	del /f /q "%GAME_DIR%\gtavr_install.log" >nul 2>&1
	echo %P% removed gtavr_install.log
)

rem --- verify the directory is clean -------------------------------------------
for %%F in (dxgi.dll.gtavr-backup OVRInject.dll openvr_api.dll openxr_loader.dll gtav_legacy.ini gtavr_settings.ini gtavr_camera.ini gtavr_install.log manifests\gtav_legacy.ini) do (
	if exist "!GAME_DIR!\%%F" (
		echo %P% VERIFY-FAIL: %%F still present
		set "DIRTY=1"
	)
)

if "%DIRTY%"=="0" (
	echo %P% DONE. Game directory verified clean - vanilla.
	exit /b 0
) else (
	echo %P% DONE WITH ERRORS - see above.
	exit /b 1
)
