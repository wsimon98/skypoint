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
- **Per-folder reading profiles** — drop a `.skypoint-folder.bin` sidecar in any folder on the SD card and SkyPoint applies that folder's settings overrides (font family/size, line spacing, paragraph alignment, screen margin, dark mode, hyphenation, focus reading) for every book in or under it. The nearest sidecar wins. Configure from the file browser: long-press a folder → **Save Folder Profile** captures your current reader settings, **Clear Folder Profile** removes the sidecar. See [Per-folder profiles](#per-folder-profiles) below.
- **SkyPoint UI theme** as the default for fresh installs: three recent-book covers across the top, a 2-column tile-grid main menu, "SkyPoint" wordmark in the header.
- **SkyPoint boot splash and sleep screen** — fox logo on boot, full-screen wolf on sleep. Auto-selects between X3 (528×792) and X4 (480×800) panels.
- **Rebranded user-facing text** (`CrossPoint` → `SkyPoint`) without touching the on-disk `/.crosspoint` cache paths or the KOReader sync `DEVICE_ID`, both of which would break existing data if renamed.

The on-disk format and partition layout are unchanged from CrossPoint, so flashing SkyPoint over a stock CrossPoint install (or vice versa) preserves your books, reading positions, settings, and Wi-Fi credentials.

## Per-folder profiles

A folder profile is a tiny binary file named `.skypoint-folder.bin` that lives inside a folder on the SD card. When you open a book, SkyPoint walks up from that book's folder looking for the nearest sidecar. If it finds one, the profile's overrides are applied to your reader settings for the duration of the reading session, then reverted on exit.

**Typical use:** create a `Bibles/` folder for scripture (large serif, generous line spacing, dark mode), a `Novels/` folder for fiction (medium sans, justified, normal spacing), a `Manuals/` folder for technical reading (small font, narrow margins). Each book inside those folders automatically opens with the reading style you set for that folder. You decide the folders and the profiles — SkyPoint ships none.

**How to set one:**
1. Configure the reader's font / size / margins / dark mode / etc. exactly how you want them for that folder.
2. Open the file browser, navigate to the folder, and long-press it.
3. Pick **Save Folder Profile**. The current reader settings are captured into the sidecar.

**How to clear one:**
Long-press the folder → **Clear Folder Profile**.

**Overridden fields:** font family, font size, line spacing, paragraph alignment, screen margin, dark mode, hyphenation, focus reading.

**Inheritance:** the nearest sidecar wins. A profile in `/Bibles/` applies to `/Bibles/Tanakh.epub` and `/Bibles/NT/John.epub`. If `/Bibles/NT/` also has its own sidecar, that one wins for books underneath it.

**Caveat:** settings changes made *during* a reading session are reverted when you close the book. The folder profile owns the session. To update a profile, close the book first, change the settings you want, then re-save the profile from the file browser.

## Credits

- [**CrossPoint Reader**](https://github.com/crosspoint-reader/crosspoint-reader) — the upstream firmware this is built on. All credit for the underlying e-reader engine, board support, and partition layout belongs there.
- [**xteink-flasher**](https://github.com/crosspoint-reader/xteink-flasher) — original WebSerial flash bench that the page in `flasher/` is adapted from.

Both MIT licensed. SkyPoint inherits the same license — see [`LICENSE`](LICENSE).
