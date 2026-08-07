#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// Mounts the SD card (FATFS over the ESP32 SDMMC peripheral, 1-bit mode) at
// "/sdcard". This is where ROMs and save-states live now that there's no
// appfs/flash filesystem in the picture.
esp_err_t board_sdcard_init(void);

// Fills path_out with the ROM to (re)load: CONFIG_PROBINGEDD_ROM_PATH if
// set and present, otherwise the first *.gg/*.sms file found in the root of
// the SD card. Returns false (path_out untouched) if nothing was found.
bool board_find_rom(char *path_out, size_t path_out_len);
