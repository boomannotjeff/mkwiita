@echo off
rem Windows: double-click, then drag your Mario Kart Wii (USA) ISO or WBFS into this window.
rem Builds only the Retro Rewind game files (build\vita\data\mkwrr), for mkw-retro-rewind.vpk from the GitHub releases.
rem Use a separate copy of the source from the one that builds the normal edition.
rem Builds only the game files (build\vita\data\mkw), for the VPK from the GitHub releases.
rem Needs Python 3.11+, Node.js LTS and Git (no VitaSDK); see BUILDING.md.
setlocal
cd /d "%~dp0"

set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY goto :nopython
%PY% -c "import sys; sys.exit(sys.version_info < (3, 11))" || goto :nopython
where git >nul 2>nul || (echo Git is needed: https://git-scm.com/download/win & goto :end)
where node >nul 2>nul || (echo Node.js LTS is needed: https://nodejs.org & goto :end)

set "disc="
set /p "disc=Drag your Mario Kart Wii (USA) disc image here and press Enter (or just Enter if already built): "
if defined disc set "disc=%disc:"=%"
if defined disc if not exist "%disc%" goto :nodisc

set "rr="
set /p "rr=Drag your Retro Rewind folder (RetroRewind6) here and press Enter (or just Enter for Dolphin's): "
if defined rr set "rr=%rr:"=%"
if not defined rr if exist "%APPDATA%\Dolphin Emulator\Load\Riivolution\WheelWizard\RetroRewind6\Binaries\ConfigRT.pul" set "rr=%APPDATA%\Dolphin Emulator\Load\Riivolution\WheelWizard\RetroRewind6"
if not defined rr if exist "%APPDATA%\Dolphin Emulator\Load\Riivolution\RetroRewind6\Binaries\ConfigRT.pul" set "rr=%APPDATA%\Dolphin Emulator\Load\Riivolution\RetroRewind6"
if not defined rr goto :norr
if not exist "%rr%\Binaries\ConfigRT.pul" goto :norr

if not exist "vendor\gclib" (%PY% tools\bootstrap.py || goto :end)
if not exist ".venv\Scripts\python.exe" (
    %PY% -m venv .venv || goto :end
    ".venv\Scripts\python.exe" -m pip install -r requirements.txt || goto :end
)
rem Show the build and keep a copy in build-rr-assets-log.txt, as UTF-8.
set "PYTHONUTF8=1"
set "PYTHONUNBUFFERED=1"
set "MKW_DISC=%disc%"
set "MKW_RR_FOLDER=%rr%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Content -Path 'build-rr-assets-log.txt' -Value $null -Encoding UTF8; $a=@('tools\vita\build.py','--assets-only'); if ($env:MKW_DISC) { $a+=$env:MKW_DISC }; $a+=@('--rr',$env:MKW_RR_FOLDER); & '.venv\Scripts\python.exe' @a 2>&1 | ForEach-Object { $line=$_.ToString(); $line; Add-Content -Path 'build-rr-assets-log.txt' -Value $line -Encoding UTF8 }"
echo Log: "%CD%\build-rr-assets-log.txt"
goto :end

:nopython
echo Python 3.11 or newer is needed: https://www.python.org/downloads/ (tick "Add python.exe to PATH")
goto :end
:norr
echo No Retro Rewind folder (with Binaries\ConfigRT.pul) at: "%rr%"
goto :end
:nodisc
echo No disc image at: "%disc%"
:end
pause
