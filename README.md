# SkyPoint

SkyPoint is a custom firmware fork for the ESP32-C3-based Xteink X3 and X4 e-ink readers. It is based on the open-source [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) project and extends it with reader dark mode, per-book dark-mode overrides, a re-skinned home screen with cover thumbnails, and SkyPoint branding (boot splash, sleep screen, on-device wordmark).

The repository also bundles a self-contained WebSerial flash page in [`flasher/`](flasher/) — adapted from the archived [xteink-flasher](https://github.com/crosspoint-reader/xteink-flasher) — so the firmware can be written to a device directly from Chrome or Edge without any vendor tooling.

## What's in this repo

- **Firmware source** — PlatformIO project at the repo root. Builds with `pio run -e default`. Output is `.pio/build/default/firmware.bin`.
- **`flasher/`** — static HTML/JS flash bench. Open `flasher/index.html` (or serve it locally) in Chrome/Edge, plug an X3/X4 in over USB, point it at a `firmware.bin`, write.
- **`install.sh` / `install.bat`** — one-shot setup scripts that install PlatformIO and run a clean build.

## Differences from upstream CrossPoint

- Reader **dark mode** as a system-wide toggle (Settings → Display → Dark Mode) with per-book overrides stored alongside each book's progress file.
- New **SkyPoint UI theme** as the default for fresh installs: 3 recent-book covers across the top, a 2-column tile-grid main menu, and a "SkyPoint" wordmark in the header.
- New SkyPoint **boot splash and sleep screen** assets (fox logo + wolf sleep art) that auto-select between the X3 (528×792) and X4 (480×800) panels.
- Cosmetic rebrand of user-facing strings (`CrossPoint` → `SkyPoint`) without touching the on-disk `/.crosspoint` cache paths or the KOReader sync `DEVICE_ID`, both of which would break existing data if renamed.

The on-disk format and partition layout are unchanged from CrossPoint, so flashing SkyPoint over a stock CrossPoint install (or vice versa) preserves your books, reading positions, settings, and Wi-Fi credentials.

## Building from source

```sh
# Linux / macOS
./install.sh

# Windows
install.bat
```

Both scripts install [PlatformIO](https://platformio.org/) via `pipx` (installing `pipx` first if missing) and run `pio run -e default`. The resulting `firmware.bin` will be at `.pio/build/default/firmware.bin`.

## Flashing

1. Build the firmware (above) or grab a prebuilt `firmware.bin`.
2. Open `flasher/index.html` in Chrome or Edge on desktop (WebSerial doesn't work in Firefox/Safari).
3. Plug the X3 or X4 in over USB.
4. Pick the model, select your `firmware.bin`, click flash.

The flasher writes to the inactive OTA slot and only swaps the boot pointer on success — a failed flash leaves the previous firmware bootable. Books on the SD card and saved settings are not touched.

## Credits

- **CrossPoint Reader** by the [CrossPoint contributors](https://github.com/crosspoint-reader/crosspoint-reader) — the upstream firmware everything here is built on. MIT licensed.
- **xteink-flasher** by the [crosspoint-reader org](https://github.com/crosspoint-reader/xteink-flasher) — original archived WebSerial flash bench that the page in `flasher/` is adapted from. MIT licensed.
- Other historical/reference forks consulted during this work: [jpirnay/crosspoint-reader (CrossPoint++)](https://github.com/jpirnay/crosspoint-reader) and [franssjz/cpr-vcodex](https://github.com/franssjz/cpr-vcodex).

All credit for the original firmware, board support, e-ink rendering, partition layout, and the WebSerial flashing flow belongs to those projects and their contributors. SkyPoint is a thin layer of customization on top.

## License

MIT — same license as the upstream CrossPoint Reader project. See [`LICENSE`](LICENSE).
