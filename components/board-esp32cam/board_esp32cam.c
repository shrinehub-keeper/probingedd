// SD card support for the AI-Thinker ESP32-CAM: mounts FATFS over the
// SDMMC peripheral's slot 1 in 1-bit mode (CLK=14, CMD=15, D0=2), which is
// how these boards wire their SD card slot in hardware - those pins are
// fixed on the original ESP32, not something firmware can reassign.

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "board_esp32cam.h"

static const char *TAG = "board";
#define MOUNT_POINT "/sdcard"

esp_err_t board_sdcard_init(void) {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    // D1-D3 aren't driven in 1-bit mode; D3 doubles as the SD protocol's
    // chip-select and must not float, so pull it (and the others) up.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    sdmmc_card_t *card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "failed to mount the SD card filesystem (unformatted/corrupt card?)");
        } else {
            ESP_LOGE(TAG, "failed to init the SD card: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}

static bool has_rom_extension(const char *name) {
    size_t len = strlen(name);
    if (len >= 3 && strcasecmp(name + len - 3, ".gg") == 0) return true;
    if (len >= 4 && strcasecmp(name + len - 4, ".sms") == 0) return true;
    return false;
}

bool board_find_rom(char *path_out, size_t path_out_len) {
    if (strlen(CONFIG_PROBINGEDD_ROM_PATH) > 0) {
        struct stat st;
        if (stat(CONFIG_PROBINGEDD_ROM_PATH, &st) == 0 && S_ISREG(st.st_mode)) {
            strlcpy(path_out, CONFIG_PROBINGEDD_ROM_PATH, path_out_len);
            return true;
        }
        ESP_LOGW(TAG, "configured ROM \"%s\" not found, falling back to auto-detect", CONFIG_PROBINGEDD_ROM_PATH);
    }

    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) {
        return false;
    }

    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (has_rom_extension(entry->d_name)) {
            snprintf(path_out, path_out_len, "%s/%s", MOUNT_POINT, entry->d_name);
            found = true;
            break;
        }
    }
    closedir(dir);
    return found;
}
