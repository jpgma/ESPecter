@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo cmake was not found on PATH, and vswhere.exe is missing.
        exit /b 1
    )
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -property installationPath`) do set "VSINSTALL=%%i"
    if not defined VSINSTALL (
        echo Visual Studio was not found.
        exit /b 1
    )
    if not exist "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" (
        echo vcvars64.bat was not found under !VSINSTALL!
        exit /b 1
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
if errorlevel 1 (
    echo cmake was not found on PATH.
    exit /b 1
)

cmake -S pc -B build-pc -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo Ninja configure failed. Trying the default generator.
    cmake -S pc -B build-pc -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 exit /b 1
)

cmake --build build-pc --config Release
if errorlevel 1 exit /b 1

if exist "build-pc\especter_pc.exe" (
    "build-pc\especter_pc.exe"
    exit /b !ERRORLEVEL!
)
if exist "build-pc\Release\especter_pc.exe" (
    "build-pc\Release\especter_pc.exe"
    exit /b !ERRORLEVEL!
)

echo Built, but especter_pc.exe was not found under build-pc.
exit /b 1
