@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not defined IDF_PATH set "IDF_PATH=%USERPROFILE%\esp\esp-idf"
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
echo Build and flash ESPecter to the Waveshare board.
echo.
echo   flash.bat
echo   flash.bat --port COM5
echo.
echo ESP-IDF is %%USERPROFILE%%\esp\esp-idf, or IDF_PATH if that is already set.
exit /b 0

:parsed
if not defined PORT (
    for /f "usebackq delims=" %%p in (`powershell -NoProfile -Command "$p = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.DeviceID -match 'VID_303A' -and $_.Name -match '\(COM\d+\)' }); if ($p.Count -eq 0) { exit 2 }; if ($p.Count -gt 1) { $p | ForEach-Object { [Console]::Error.WriteLine($_.Name) }; exit 3 }; if ($p[0].Name -match '\((COM\d+)\)') { $Matches[1] }"`) do set "PORT=%%p"
    if not defined PORT (
        echo No Espressif USB serial port found. Plug in the board, or pass --port COMx.
        exit /b 1
    )
)

echo Using ESP-IDF: %IDF_PATH%
echo Flashing on %PORT%
call "%IDF_PATH%\export.bat"
if errorlevel 1 (
    echo export.bat failed
    exit /b 1
)
if not exist sdkconfig (
    idf.py set-target esp32s3
    if errorlevel 1 exit /b 1
)
idf.py -p %PORT% build flash
exit /b %ERRORLEVEL%
