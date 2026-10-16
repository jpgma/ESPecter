@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

if not defined IDF_PATH (
    if exist "%USERPROFILE%\esp\esp-idf\export.bat" (
        set "IDF_PATH=%USERPROFILE%\esp\esp-idf"
    ) else if exist "D:\esp\esp-idf\export.bat" (
        set "IDF_PATH=D:\esp\esp-idf"
    ) else (
        set "IDF_PATH=%USERPROFILE%\esp\esp-idf"
    )
)
if not defined IDF_TOOLS_PATH if exist "D:\esp\.espressif\idf-env.json" set "IDF_TOOLS_PATH=D:\esp\.espressif"
if not exist "%IDF_PATH%\export.bat" (
    echo ESP-IDF export.bat not found at %IDF_PATH%
    echo Install ESP-IDF 5.5 or set IDF_PATH.
    exit /b 1
)

set "PORT="
:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--help" goto help
if /I "%~1"=="--port" (
    shift
    if "%~1"=="" (
        echo --port needs a COM port
        exit /b 1
    )
    set "PORT=%~1"
    shift
    goto parse
)
echo Unknown flag: %~1
exit /b 1

:help
echo Rebuild when sources changed, flash that image, then open the serial log.
echo An unchanged tree only opens the log.
echo.
echo   monitor.bat
echo   monitor.bat --port COM5
echo.
echo ESP-IDF is %%USERPROFILE%%\esp\esp-idf, D:\esp\esp-idf, or IDF_PATH if that is already set.
echo PixelRoot32 sources live in third_party. A missing Engine.h or ApuCore.h stops the script.
echo A build directory without CMakeCache.txt is deleted before set-target.
echo Each step prints how long it took.
exit /b 0

:parsed
call :tick START

call :tick T
set "ERR=0"
if not exist "third_party\pixelroot32\include\core\Engine.h" set "ERR=1"
if not exist "third_party\pixelroot32-apu\include\pixelroot32\apu\ApuCore.h" set "ERR=1"
call :tock "!T!" "engine"
if !ERR! neq 0 (
    echo PixelRoot32 sources are missing from third_party.
    exit /b !ERR!
)

call :tick T
set "ERR=0"
if exist build if not exist "build\CMakeCache.txt" (
    echo Removing incomplete build directory
    rmdir /s /q build
    if exist build set "ERR=1"
)
call :tock "!T!" "build directory"
if !ERR! neq 0 (
    echo Could not remove the build directory.
    exit /b !ERR!
)

call :tick T
call "%IDF_PATH%\export.bat"
set "ERR=!ERRORLEVEL!"
call :tock "!T!" "esp-idf"
if !ERR! neq 0 (
    echo export.bat failed
    exit /b !ERR!
)

if not exist sdkconfig (
    call :tick T
    idf.py set-target esp32s3
    set "ERR=!ERRORLEVEL!"
    call :tock "!T!" "set-target"
    if !ERR! neq 0 exit /b !ERR!
)

echo Using ESP-IDF: %IDF_PATH%

call :tick T
idf.py build
set "ERR=!ERRORLEVEL!"
call :tock "!T!" "build"
if !ERR! neq 0 exit /b !ERR!

call :tick T
set "NEED_FLASH=0"
if not exist "build\.monitor_flash" set "NEED_FLASH=1"
if "!NEED_FLASH!"=="0" (
    powershell -NoProfile -Command "if ((Get-Item 'build\especter.bin').LastWriteTimeUtc -gt (Get-Item 'build\.monitor_flash').LastWriteTimeUtc) { exit 2 } else { exit 0 }"
    if !ERRORLEVEL! equ 2 set "NEED_FLASH=1"
)
call :tock "!T!" "image"
if "!NEED_FLASH!"=="1" (
    call :tick T
    if not defined PORT (
        for /f "usebackq delims=" %%p in (`powershell -NoProfile -Command "$p = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.DeviceID -match 'VID_303A' -and $_.Name -match '\(COM\d+\)' }); if ($p.Count -eq 0) { exit 2 }; if ($p.Count -gt 1) { $p | ForEach-Object { [Console]::Error.WriteLine($_.Name) }; exit 3 }; if ($p[0].Name -match '\((COM\d+)\)') { $Matches[1] }"`) do set "PORT=%%p"
    )
    set "ERR=0"
    if not defined PORT set "ERR=1"
    call :tock "!T!" "port"
    if !ERR! neq 0 (
        echo No Espressif USB serial port found. Plug in the board, or pass --port COMx.
        exit /b 1
    )
    echo Flashing on !PORT!
    call :tick T
    idf.py -p !PORT! flash
    set "ERR=!ERRORLEVEL!"
    call :tock "!T!" "flash"
    if !ERR! neq 0 exit /b !ERR!
    copy /y nul "build\.monitor_flash" >nul
)

call :tick T
if defined PORT (
    idf.py -p !PORT! monitor
) else (
    idf.py monitor
)
set "ERR=!ERRORLEVEL!"
call :tock "!T!" "monitor"
call :tock "!START!" "total"
exit /b !ERR!

:tick
set "NOW=!TIME: =0!"
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
