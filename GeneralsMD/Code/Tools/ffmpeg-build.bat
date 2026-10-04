@echo off
rem Builds FFmpeg for the game: see ffmpeg-build.sh for what goes in it and why.
rem
rem Needs MSYS2 with make and nasm:
rem   winget install MSYS2.MSYS2
rem   C:\msys64\usr\bin\bash -lc "pacman -S --noconfirm --needed make nasm diffutils"
rem
rem MSVC comes from vcvars64 below and is inherited into the MSYS2 shell, which is what
rem --toolchain=msvc needs; edit the edition here if this machine has a different one.
rem
rem "ffmpeg-build.bat arm64" builds Windows on ARM64 into dist-arm64, with vcvarsarm64 (the native
rem ARM64 toolset, so run it on an ARM64 machine).

setlocal
set "VCVARS_NAME=vcvars64.bat"
set "ZH_FFMPEG_ARCH=x86_64"
if /i "%~1"=="arm64" (
    set "VCVARS_NAME=vcvarsarm64.bat"
    set "ZH_FFMPEG_ARCH=aarch64"
)
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\%VCVARS_NAME%"

rem Any other edition (Build Tools, Professional) is found through vswhere.
if exist "%VCVARS%" goto have_vcvars
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto have_vcvars
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\%VCVARS_NAME%"
:have_vcvars
set "MSYS_BASH=C:\msys64\usr\bin\bash.exe"

rem Not a parenthesised block: a "(x86)" in the expanded path would close it early.
if exist "%VCVARS%" goto vcvars_found
echo [ffmpeg] ERROR: %VCVARS% not found. Edit this file for your Visual Studio edition.
exit /b 1
:vcvars_found
if not exist "%MSYS_BASH%" (
    echo [ffmpeg] ERROR: %MSYS_BASH% not found. Install MSYS2 - see the comment at the top.
    exit /b 1
)

call "%VCVARS%" >nul 2>&1
set "MSYS2_PATH_TYPE=inherit"
set "CHERE_INVOKING=1"

rem The script's own path, as MSYS2 spells it.
set "SCRIPT=%~dp0ffmpeg-build.sh"
set "SCRIPT=%SCRIPT:\=/%"
set "SCRIPT=/%SCRIPT::=%"

"%MSYS_BASH%" -lc "'%SCRIPT%'"
