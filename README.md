# Probing EDD

This is a port of SMSPlus, a Sega Master System / Game Gear emulator, originally built for the
8bkc handheld, specifically of SMSPlus version 1.3, by Charles MacDonald. SMSPlus itself (both
the original as well as this port) is licensed under the GPLv2, as detailed in the 'LICENSE' file.

The Z80 emulator core Z80 normally uses has been replaced by the one from the EightyOne Z81
emulator (which itself is borrowed from Fuse, the Free Unix Spectrum Emulator) because the
original version of the emulator core had an incompatible license; the EightyOne core is
GPL licensed.

---
## You fool

This is actually for an ESP32-CAM: its camera isn't working, so instead of a display it connects
a Bluetooth controller (DS4, Wiimote, or anything else [Bluepad32](https://github.com/ricardoquesada/bluepad32)
supports) and streams gameplay as MJPEG over WiFi so you can watch it in a browser. No 320x240
displays were harmed (or hunted for) making this.

### How it works

* **Input** - `components/gamepad-hal` implements a [Bluepad32](https://github.com/ricardoquesada/bluepad32)
  "custom platform". Bluepad32 already normalizes DS4, Wiimote, Xbox, Switch Pro and generic HID
  gamepads into one virtual gamepad shape (d-pad + A/B + start/select/system), so the same code
  path drives the emulator no matter which controller connects - there's no per-controller special
  casing.
* **Video** - the emulator renders into a plain RGB565 buffer, which `components/mjpeg-stream`
  JPEG-encodes (via `esp_new_jpeg`) and serves over `esp_http_server` as a
  `multipart/x-mixed-replace` MJPEG stream, decoupled from the emulator's own 50/60Hz frame rate
  (see `PROBINGEDD_STREAM_FPS`). The ESP32-CAM hosts its own WiFi access point - connect to it and
  open `http://192.168.4.1/` in a browser to watch. Sound is not wired up to any output yet; the
  emulator still runs its internal sound emulation, it's just not played anywhere.
* **ROMs** - there's no display or buttons for an on-device file picker anymore, so ROMs
  (`.gg`/`.sms`) and save-states now live on an SD card (`components/board-esp32cam`) instead of
  the old appfs-on-flash setup. On boot it loads `PROBINGEDD_ROM_PATH` if set, otherwise the first
  `.gg`/`.sms` file it finds at the root of the card. Progress autosaves every 30s and on every
  soft reset (hold the controller's system/PS/Home button).

The core Z80/VDP/PSG emulation in `components/smsplus` is untouched and hardware-agnostic; only
the ESP32-CAM-specific glue in `components/smsplus-esp32`, `components/gamepad-hal`,
`components/mjpeg-stream`, `components/board-esp32cam` and `main/` changed.

### Hardware

Targets the AI-Thinker ESP32-CAM module: a plain ESP32 (classic BT + WiFi, needed for Bluepad32)
with PSRAM (used for the ROM buffer, frame buffer and JPEG scratch space) and a micro SD slot
wired to the SDMMC peripheral's fixed slot-1 pins (CLK=14, CMD=15, D0=2) - the same pins these
boards already use for the camera's SD slot.

### Building

1. Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) v5.3.
2. Fetch submodules (this pulls in Bluepad32 and, nested inside it, BTstack). The submodule is
   configured `shallow = true`, so this only fetches the pinned commit's history, not the full
   repos:
   ```sh
   git submodule update --init --recursive --jobs 8
   ```
   `--jobs 8` fetches nested submodules in parallel instead of one at a time; drop it (or lower
   the number) if that saturates your connection.
3. One-time step: install BTstack as a component inside the Bluepad32 checkout (this is
   Bluepad32's own integration script, not something we can vendor statically):
   ```sh
   cd third_party/bluepad32/external/btstack/port/esp32
   IDF_PATH=../../../../src ./integrate_btstack.py
   cd -
   ```
4. Set the target and build:
   ```sh
   idf.py set-target esp32
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

`idf.py` will also pull down `esp_new_jpeg` automatically via the ESP-IDF Component Manager the
first time you build (see `components/mjpeg-stream/idf_component.yml`).

### Configuring

`idf.py menuconfig` -> "Probing EDD (ESP32-CAM Game Gear/SMS emulator)" has the settings you're
most likely to want to change: the WiFi AP SSID/password, MJPEG quality/target frame rate, and a
fixed ROM path if you don't want auto-detection off the SD card.

### Using it

1. Put one or more `.gg`/`.sms` ROMs on a FAT-formatted SD card and insert it.
2. Power up the board, then pair a DS4 or Wiimote with it (Bluepad32 starts scanning and
   auto-connecting on boot; put the controller in pairing mode as usual).
3. Connect to the `probingedd` WiFi access point it creates (SSID/password configurable, see
   above) and open `http://192.168.4.1/` in a browser to watch.
4. Hold the controller's system/PS/Home button for a soft reset.
