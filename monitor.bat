@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not defined IDF_PATH set "IDF_PATH=%USERPROFILE%\esp\esp-idf"
if not exist "%IDF_PATH%\export.bat" (
    echo ESP-IDF export.bat not found at %IDF_PATH%
    echo Install ESP-IDF 5.5 or set IDF_PATH.
    exit /b 1
)

call "%IDF_PATH%\export.bat"
if errorlevel 1 (
    echo export.bat failed
    exit /b 1
)
idf.py monitor
exit /b %ERRORLEVEL%
