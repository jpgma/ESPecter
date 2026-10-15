@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

call :tick START
call :tick T
where cmake >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo cmake was not found on PATH, and vswhere.exe is missing.
        set "RC=1"
        goto toolchain_done
    )
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -property installationPath`) do set "VSINSTALL=%%i"
    if not defined VSINSTALL (
        echo Visual Studio was not found.
        set "RC=1"
        goto toolchain_done
    )
    if not exist "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" (
        echo vcvars64.bat was not found under !VSINSTALL!
        set "RC=1"
        goto toolchain_done
    )
    call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" >nul
    if exist "!VSINSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "PATH=!VSINSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;!PATH!"
    )
    if exist "!VSINSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" (
        set "PATH=!VSINSTALL!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;!PATH!"
    )
)

where cmake >nul 2>&1
set "RC=!ERRORLEVEL!"
if !RC! neq 0 echo cmake was not found on PATH.
:toolchain_done
call :tock "!T!" "toolchain"
if !RC! neq 0 exit /b !RC!

call :tick T
cmake -S pc -B build-pc -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo Ninja configure failed. Trying the default generator.
    cmake -S pc -B build-pc -DCMAKE_BUILD_TYPE=Release
)
set "RC=!ERRORLEVEL!"
call :tock "!T!" "configure"
if !RC! neq 0 exit /b !RC!

call :tick T
cmake --build build-pc --config Release
set "RC=!ERRORLEVEL!"
call :tock "!T!" "build"
if !RC! neq 0 exit /b !RC!

call :tick T
set "RC=0"
if exist "build-pc\especter_pc.exe" (
    "build-pc\especter_pc.exe"
    set "RC=!ERRORLEVEL!"
) else if exist "build-pc\Release\especter_pc.exe" (
    "build-pc\Release\especter_pc.exe"
    set "RC=!ERRORLEVEL!"
) else (
    echo Built, but especter_pc.exe was not found under build-pc.
    set "RC=1"
)
call :tock "!T!" "run"
call :tock "!START!" "total"
exit /b !RC!

:tick
set "NOW=!TIME: =0!"
set "NOW=!NOW:,=.!"
for /f "tokens=1-4 delims=:." %%a in ("!NOW!") do set /a "%~1=((1%%a-100)*360000)+((1%%b-100)*6000)+((1%%c-100)*100)+(1%%d-100)"
exit /b 0

:tock
set "NOW=!TIME: =0!"
set "NOW=!NOW:,=.!"
for /f "tokens=1-4 delims=:." %%a in ("!NOW!") do set /a "END=((1%%a-100)*360000)+((1%%b-100)*6000)+((1%%c-100)*100)+(1%%d-100)"
set /a "DELTA=END-%~1"
if !DELTA! lss 0 set /a "DELTA+=8640000"
set /a "SEC=DELTA/100"
set /a "FRAC=DELTA%%100"
if !FRAC! lss 10 (
    echo [!SEC!.0!FRAC!s] %~2
) else (
    echo [!SEC!.!FRAC!s] %~2
)
exit /b 0
