@echo off
rem Builds FFmpeg for the game: see ffmpeg-build.sh for what goes in it and why.
rem
rem Needs MSYS2 with make and nasm:
rem   winget install MSYS2.MSYS2
rem   C:\msys64\usr\bin\bash -lc "pacman -S --noconfirm --needed make nasm diffutils"
rem
rem MSVC comes from vcvars64 below and is inherited into the MSYS2 shell, which is what
rem --toolchain=msvc needs; edit the edition here if this machine has a different one.

setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "MSYS_BASH=C:\msys64\usr\bin\bash.exe"

if not exist "%VCVARS%" (
    echo [ffmpeg] ERROR: %VCVARS% not found. Edit this file for your Visual Studio edition.
    exit /b 1
)
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
