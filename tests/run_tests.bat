@echo off
REM Builds and runs the GTAVR unit tests (Release|x64).
REM Usage: tests\run_tests.bat [Debug]
setlocal

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set MSBUILD="C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

%MSBUILD% "%~dp0GTAVRTests.vcxproj" /p:Configuration=%CONFIG% /p:Platform=x64 /m /v:m
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

"%~dp0x64\%CONFIG%\GTAVRTests.exe"
exit /b %errorlevel%
