# SkyPoint

A custom firmware fork for the ESP32-C3-based Xteink X3 and X4 e-ink readers. Based on the [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) project.

## Disclaimer

**Use at your own risk.** Flashing custom firmware to your device can brick it, void any warranty, and is not supported by the device manufacturer. SkyPoint is an unofficial community fork — there is no warranty, express or implied, and no liability is accepted for any damage to your device, loss of data, or other consequences of using this software. If you are not comfortable with the possibility of permanently breaking your reader, do not flash this firmware.

The flasher writes only to the inactive OTA slot and the previous firmware remains bootable on failure, which makes most flashes recoverable, but recovery is not guaranteed. Keep a known-good `firmware.bin` backup before experimenting.

## Install

### Option 1 — flash a prebuilt firmware

1. Grab a `firmware.bin` from a release (or build one — see below).
2. Open [`flasher/index.html`](flasher/index.html) in Chrome or Edge on desktop (WebSerial doesn't work in Firefox/Safari).
3. Plug the X3 or X4 in over USB.
4. Pick the model, select your `firmware.bin`, click flash.

The flasher writes to the inactive OTA slot and only swaps the boot pointer on success — a failed flash leaves the previous firmware bootable. Books on the SD card and saved settings are not touched.

### Option 2 — build from source

```sh
# Linux / macOS
./install.sh

# Windows
install.bat
```

Both scripts install [PlatformIO](https://platformio.org/) via `pipx` (installing `pipx` first if missing) and run a clean build. The resulting `firmware.bin` lands at `.pio/build/default/firmware.bin`. Then use the flasher in step 2 above.

## Differences from CrossPoint

- **Dark mode** as a system-wide toggle (Settings → Display → Dark Mode). Applies to reader, home, and settings. Per-book overrides stored alongside each book's progress file.
- **SkyPoint UI theme** as the default for fresh installs: three recent-book covers across the top, a 2-column tile-grid main menu, "SkyPoint" wordmark in the header.
- **SkyPoint boot splash and sleep screen** — fox logo on boot, full-screen wolf on sleep. Auto-selects between X3 (528×792) and X4 (480×800) panels.
- **Rebranded user-facing text** (`CrossPoint` → `SkyPoint`) without touching the on-disk `/.crosspoint` cache paths or the KOReader sync `DEVICE_ID`, both of which would break existing data if renamed.

The on-disk format and partition layout are unchanged from CrossPoint, so flashing SkyPoint over a stock CrossPoint install (or vice versa) preserves your books, reading positions, settings, and Wi-Fi credentials.

## Credits

- [**CrossPoint Reader**](https://github.com/crosspoint-reader/crosspoint-reader) — the upstream firmware this is built on. All credit for the underlying e-reader engine, board support, and partition layout belongs there.
- [**xteink-flasher**](https://github.com/crosspoint-reader/xteink-flasher) — original WebSerial flash bench that the page in `flasher/` is adapted from.

Both MIT licensed. SkyPoint inherits the same license — see [`LICENSE`](LICENSE).
