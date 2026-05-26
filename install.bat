@echo off
REM SkyPoint build setup for Windows.
REM Installs PlatformIO via pipx (installing pipx first if missing) and runs a
REM clean build of the default environment. Resulting firmware lands at
REM .pio\build\default\firmware.bin.

setlocal EnableDelayedExpansion

where python >nul 2>&1
if errorlevel 1 (
    echo ERROR: python is required. Install Python 3 from https://www.python.org/ or the Microsoft Store and re-run.
    exit /b 1
)

where pipx >nul 2>&1
if errorlevel 1 (
    echo Installing pipx via python -m pip ...
    python -m pip install --user pipx
    if errorlevel 1 exit /b 1
    python -m pipx ensurepath
    REM ensurepath modifies the persistent PATH but not this shell. Add the
    REM user scripts dir to PATH for this run so the rest of the script works.
    for /f "delims=" %%i in ('python -c "import site,os;print(os.path.join(site.USER_BASE,'Scripts'))"') do set "USERSCRIPTS=%%i"
    set "PATH=!USERSCRIPTS!;%PATH%"
)

where pio >nul 2>&1
if errorlevel 1 (
    echo Installing PlatformIO via pipx ...
    pipx install platformio
    if errorlevel 1 exit /b 1
    REM pipx puts shims in %USERPROFILE%\.local\bin
    set "PATH=%USERPROFILE%\.local\bin;%PATH%"
)

pio --version
if errorlevel 1 (
    echo ERROR: pio is installed but not on PATH. Open a new terminal and re-run, or run "pipx ensurepath".
    exit /b 1
)

echo Building SkyPoint firmware (env=default) -- first run downloads the ESP32 toolchain and can take 5-15 minutes.
pio run -e default
if errorlevel 1 exit /b 1

echo.
echo Build complete. Firmware: %CD%\.pio\build\default\firmware.bin
echo Open flasher\index.html in Chrome or Edge to write it to a device.

endlocal
