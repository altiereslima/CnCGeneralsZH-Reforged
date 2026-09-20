@echo off
setlocal enabledelayedexpansion

rem Build C&C Generals Zero Hour, x64. Double-click it, or:
rem
rem   build.bat                    configure (if needed) + build Release
rem   build.bat Debug              build another config (Release|RelWithDebInfo|Debug)
rem   build.bat Release test       build, then run ctest
rem   build.bat Release generals   build a single target
rem   build.bat clean              delete the build tree and configure from scratch
rem
rem Nothing to edit before the first run: it finds cmake itself, fetches the third-party
rem sources EA stripped and the fork's own art, builds, and copies the exe and everything
rem beside it into GeneralsMD\Run. What it cannot fetch is the game: a Zero Hour install's
rem *.big go next to generals.exe in GeneralsMD\Run, and the base game's in Run\ZH_Generals.
rem
rem The four knobs below are overrides. Every one of them empty is the supported path.

set "CMAKE="
set "VS_EDITIONS=Community Professional Enterprise BuildTools"
set "GENERATOR=Visual Studio 17 2022"
set "DEFAULT_CONFIG=Release"
set "BUILD="

rem Double-clicked from Explorer, the window closes on the last line and nobody reads it.
rem A double-click passes no arguments and puts this file's own name in the parent command line.
set "PAUSE_AT_END="
if "%~1"=="" echo %cmdcmdline% | find /i "%~nx0" >nul && set "PAUSE_AT_END=1"

set "ROOT=%~dp0"
set "SRC=%ROOT%GeneralsMD\Code"
if not defined BUILD set "BUILD=%ROOT%build64"
set "CONFIG=%~1"
set "ARG2=%~2"

if /i "%CONFIG%"=="clean" (
    echo [build] removing %BUILD%
    if exist "%BUILD%" rmdir /s /q "%BUILD%"
    set "CONFIG=%ARG2%"
    set "ARG2="
)
if "%CONFIG%"=="" set "CONFIG=%DEFAULT_CONFIG%"

set "RUNTESTS="
set "TARGET="
if /i "%ARG2%"=="test" (set "RUNTESTS=1") else (if not "%ARG2%"=="" set "TARGET=%ARG2%")

rem --- locate cmake: the override, then PATH, then the copy shipped with VS2022 ---
if defined CMAKE if not exist "%CMAKE%" (
    echo [build] ERROR: CMAKE is set to "%CMAKE%" at the top of this file, but that file does not exist.
    goto :fail
)
for /f "delims=" %%C in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%C"
if not defined CMAKE (
    for %%E in (%VS_EDITIONS%) do (
        set "TRY=C:\Program Files\Microsoft Visual Studio\2022\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if not defined CMAKE if exist "!TRY!" set "CMAKE=!TRY!"
    )
)
if not defined CMAKE (
    echo [build] ERROR: cmake.exe not found on PATH or under Visual Studio 2022.
    echo [build] Install Visual Studio 2022 with the C++ desktop workload and run this again.
    goto :fail
)
echo [build] cmake:  %CMAKE%
echo [build] config: %CONFIG%

rem --- third-party sources the repository does not carry (zlib, LZH-Light, the DirectX 8 headers,
rem the GameSpy SDK) and the upscaled art. Fetches whatever is missing and is a no-op once it is
rem there, so one build.bat on a fresh clone is enough. ---
powershell -NoProfile -ExecutionPolicy Bypass -File "%SRC%\Tools\vendor.ps1"
if !errorlevel! neq 0 (
    echo [build] ERROR: fetching the third-party sources failed.
    goto :fail
)

rem --- configure (only when the cache is missing) ---
if not exist "%BUILD%\CMakeCache.txt" (
    echo [build] configuring %SRC% -^> %BUILD%
    "%CMAKE%" -S "%SRC%" -B "%BUILD%" -G "%GENERATOR%" -A x64
    if !errorlevel! neq 0 (
        echo [build] ERROR: configure failed.
        goto :fail
    )
)

rem --- build ---
if defined TARGET (
    echo [build] building target %TARGET%
    "%CMAKE%" --build "%BUILD%" --config %CONFIG% --target %TARGET%
) else (
    "%CMAKE%" --build "%BUILD%" --config %CONFIG%
)
if !errorlevel! neq 0 (
    echo [build] ERROR: build failed.
    goto :fail
)

rem --- tests ---
if defined RUNTESTS (
    rem ctest lives beside cmake. It is not on PATH when cmake came from the VS2022 install,
    rem and "cmake -E chdir ctest" then fails with a bare "no such file or directory".
    for %%I in ("%CMAKE%") do set "CTEST=%%~dpIctest.exe"
    echo [build] running !CTEST!
    "!CTEST!" --test-dir "%BUILD%" -C %CONFIG% --output-on-failure
    if !errorlevel! neq 0 (
        echo [build] ERROR: tests failed.
        goto :fail
    )
)

echo [build] done. %ROOT%GeneralsMD\Run\generals.exe is the one to run.
if defined PAUSE_AT_END pause
endlocal
exit /b 0

:fail
if defined PAUSE_AT_END pause
endlocal
exit /b 1
