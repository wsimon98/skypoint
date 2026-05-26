#!/usr/bin/env bash
# SkyPoint build setup for Linux / macOS.
# Installs PlatformIO via pipx (installing pipx first if missing) and runs a
# clean build of the default environment. Resulting firmware lands at
# .pio/build/default/firmware.bin.
set -euo pipefail

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERROR: python3 is required. Install it from your package manager or python.org and re-run." >&2
  exit 1
fi

if ! command -v pipx >/dev/null 2>&1; then
  echo "Installing pipx via python3 -m pip ..."
  python3 -m pip install --user pipx
  python3 -m pipx ensurepath
  # ensurepath modifies the shell rc but doesn't update the current shell;
  # add ~/.local/bin to PATH for this run so the rest of the script works.
  export PATH="$HOME/.local/bin:$PATH"
fi

if ! command -v pio >/dev/null 2>&1; then
  echo "Installing PlatformIO via pipx ..."
  pipx install platformio
  export PATH="$HOME/.local/bin:$PATH"
fi

echo "PlatformIO version: $(pio --version)"
echo "Building SkyPoint firmware (env=default) — first run downloads the ESP32 toolchain and can take 5-15 minutes."
pio run -e default

echo
echo "Build complete. Firmware: $(pwd)/.pio/build/default/firmware.bin"
echo "Open flasher/index.html in Chrome or Edge to write it to a device."
