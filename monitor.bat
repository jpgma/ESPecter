@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

if not defined IDF_PATH set "IDF_PATH=%USERPROFILE%\esp\esp-idf"
if not exist "%IDF_PATH%\export.bat" (
    echo ESP-IDF export.bat not found at %IDF_PATH%
    echo Install ESP-IDF 5.5 or set IDF_PATH.
    exit /b 1
)

call :tick START
call :tick T
call "%IDF_PATH%\export.bat"
set "ERR=!ERRORLEVEL!"
call :tock "!T!" "esp-idf"
if !ERR! neq 0 (
    echo export.bat failed
    exit /b !ERR!
)

call :tick T
idf.py monitor
set "ERR=!ERRORLEVEL!"
call :tock "!T!" "monitor"
call :tock "!START!" "total"
exit /b !ERR!

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
