#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board_esp32cam.h"
#include "gamepad_hal.h"
#include "mjpeg_stream.h"
#include "shared.h"
#include "smsplus-main.h"

#define SMS_FPS 60
#define SNDRATE 22050
#define ROM_MAX_BYTES (2 * 1024 * 1024)  // no cartridge paging support, see smsemuRun()
#define AUTOSAVE_INTERVAL_MS 30000

static const char *TAG = "smsplus";

static SemaphoreHandle_t renderSem;
static uint16_t *frameBuf;   // 256x192 worst case, RGB565, PSRAM, 16-byte aligned
static int frameW, frameH;

// Reads the (single) connected Bluetooth controller and updates the
// emulator's input state. Returns true if the system/PS/Home button is
// held, which we use as "soft reset" since there's no on-screen menu.
static bool readJs(void) {
    uint32_t b = gamepad_hal_get_buttons();
    int smsButtons = 0, smsSystem = 0;
    if (b & GAMEPAD_HAL_UP) smsButtons |= INPUT_UP;
    if (b & GAMEPAD_HAL_DOWN) smsButtons |= INPUT_DOWN;
    if (b & GAMEPAD_HAL_LEFT) smsButtons |= INPUT_LEFT;
    if (b & GAMEPAD_HAL_RIGHT) smsButtons |= INPUT_RIGHT;
    if (b & GAMEPAD_HAL_A) smsButtons |= INPUT_BUTTON1;
    if (b & GAMEPAD_HAL_B) smsButtons |= INPUT_BUTTON2;
    if (b & GAMEPAD_HAL_START) smsSystem |= INPUT_START;   /* Game Gear only */
    if (b & GAMEPAD_HAL_SELECT) smsSystem |= INPUT_PAUSE;  /* Master System only */
    input.pad[0] = smsButtons;
    input.system = smsSystem;
    return (b & GAMEPAD_HAL_SYSTEM) != 0;
}

static inline uint16_t to_rgb565(const uint8_t *color) {
    return ((color[0] & 0xf8) << 8) | ((color[1] & 0xfc) << 3) | (color[2] >> 3);
}

// SMS screen is the full 256x192 bitmap, 1:1.
static void lcdWriteSMSFrame(void) {
    const uint8_t *data = bitmap.data;
    uint16_t *p = frameBuf;
    for (int y = 0; y < 192; y++) {
        for (int x = 0; x < 256; x++) {
            *p++ = to_rgb565(bitmap.pal.color[data[x + y * 256] & PIXEL_MASK]);
        }
    }
    frameW = 256;
    frameH = 192;
}

// GG screen is a 160x144 window into the same bitmap, offset by (48, 24).
static void lcdWriteGGFrame(void) {
    const uint8_t *data = bitmap.data;
    uint16_t *p = frameBuf;
    for (int y = 24; y < 24 + 144; y++) {
        for (int x = 48; x < 48 + 160; x++) {
            *p++ = to_rgb565(bitmap.pal.color[data[x + y * 256] & PIXEL_MASK]);
        }
    }
    frameW = 160;
    frameH = 144;
}

static void lcdThread(void *arg) {
    (void)arg;
    int streamEvery = SMS_FPS / CONFIG_PROBINGEDD_STREAM_FPS;
    if (streamEvery < 1) streamEvery = 1;
    int frameCount = 0;

    while (1) {
        xSemaphoreTake(renderSem, portMAX_DELAY);
        if (cart.type == TYPE_SMS) {
            lcdWriteSMSFrame();
        } else {
            lcdWriteGGFrame();
        }
        if ((frameCount++ % streamEvery) == 0) {
            mjpeg_stream_push_frame(frameBuf, frameW, frameH);
        }
    }
}

void sms_system_load_sram(void) {
    // Battery-backed cartridge SRAM save/load was never implemented by this
    // emulator port; save-states (see system-esp32.c) are what persist
    // progress here.
}

// Runs one ROM until a soft reset is requested (system/PS/Home button) or
// it can't be loaded at all.
static int smsemuRun(const char *rom_path, const char *state_path, int loadState) {
    FILE *rf = fopen(rom_path, "rb");
    if (!rf) {
        ESP_LOGE(TAG, "could not open ROM %s", rom_path);
        return EMU_RUN_ERROR;
    }
    fseek(rf, 0, SEEK_END);
    long romSize = ftell(rf);
    fseek(rf, 0, SEEK_SET);
    if (romSize <= 0 || romSize > ROM_MAX_BYTES) {
        ESP_LOGE(TAG, "%s: bad size %ld (max %d bytes, no cart paging support)", rom_path, romSize, ROM_MAX_BYTES);
        fclose(rf);
        return EMU_RUN_ERROR;
    }

    uint8_t *rom = heap_caps_malloc(romSize, MALLOC_CAP_SPIRAM);
    if (!rom) {
        ESP_LOGE(TAG, "%s: out of PSRAM for %ld byte ROM", rom_path, romSize);
        fclose(rf);
        return EMU_RUN_ERROR;
    }
    size_t got = fread(rom, 1, romSize, rf);
    fclose(rf);
    if (got != (size_t)romSize) {
        ESP_LOGE(TAG, "%s: short read (%u of %ld bytes)", rom_path, (unsigned)got, romSize);
        heap_caps_free(rom);
        return EMU_RUN_ERROR;
    }

    sms.use_fm = 0;
    sms.country = TYPE_OVERSEAS;
    bitmap.data = malloc(256 * 192);
    bitmap.width = 256;
    bitmap.height = 192;
    bitmap.pitch = 256;
    bitmap.depth = 8;
    sms.dummy = bitmap.data;  // a normal cart never touches this; point it at VRAM just in case
    sms.sram = malloc(0x8000);

    cart.rom = rom;
    cart.pages = romSize / 0x4000;
    cart.type = TYPE_SMS;
    size_t pathLen = strlen(rom_path);
    if (pathLen >= 3 && strcasecmp(rom_path + pathLen - 3, ".gg") == 0) {
        cart.type = TYPE_GG;
    }
    ESP_LOGI(TAG, "%s: %s, %d pages (%ld bytes)", rom_path, (cart.type == TYPE_GG) ? "Game Gear" : "Master System",
             cart.pages, romSize);

    sms_system_init(SNDRATE);

    if (loadState) {
        FILE *sf = fopen(state_path, "rb");
        if (sf) {
            sms_system_load_state(sf);
            fclose(sf);
            ESP_LOGI(TAG, "resumed from %s", state_path);
        }
    }

    bool resetRequested = false;
    TickType_t lastSave = xTaskGetTickCount();
    const TickType_t saveInterval = pdMS_TO_TICKS(AUTOSAVE_INTERVAL_MS);
    TickType_t lastTickCnt = xTaskGetTickCount();

    while (!resetRequested) {
        for (int frameno = 0; frameno < SMS_FPS && !resetRequested; frameno++) {
            if (readJs()) resetRequested = true;
            sms_frame(0);
            xSemaphoreGive(renderSem);
        }

        TickType_t tickCnt = xTaskGetTickCount();
        if (tickCnt == lastTickCnt) tickCnt++;
        ESP_LOGD(TAG, "fps=%lu", (unsigned long)((SMS_FPS * 1000) / (tickCnt - lastTickCnt)));
        lastTickCnt = tickCnt;

        if ((tickCnt - lastSave) >= saveInterval) {
            FILE *sf = fopen(state_path, "wb");
            if (sf) {
                sms_system_save_state(sf);
                fclose(sf);
            }
            lastSave = tickCnt;
        }
    }

    // Persist progress across the soft reset the user just asked for too -
    // there's no separate "power down" event to hang a save off of anymore.
    FILE *sf = fopen(state_path, "wb");
    if (sf) {
        sms_system_save_state(sf);
        fclose(sf);
    }

    sms_system_shutdown();
    free(bitmap.data);
    free(sms.sram);
    heap_caps_free(rom);
    return EMU_RUN_RESET;
}

static void emuThread(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(200));  // let other startup tasks get going first

    char romPath[128];
    char statePath[132];
    int loadState = 1;

    while (1) {
        if (!board_find_rom(romPath, sizeof(romPath))) {
            ESP_LOGW(TAG, "no ROM found on the SD card - place a .gg or .sms file at its root "
                          "(or set PROBINGEDD_ROM_PATH), retrying...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        strlcpy(statePath, romPath, sizeof(statePath));
        char *dot = strrchr(statePath, '.');
        if (!dot) dot = statePath + strlen(statePath);
        strlcpy(dot, ".state", sizeof(statePath) - (dot - statePath));

        int ret = smsemuRun(romPath, statePath, loadState);
        if (ret == EMU_RUN_ERROR) {
            loadState = 1;
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            loadState = 0;  // fresh start after an intentional reset
        }
    }
}

void smsemuStart(void) {
    frameBuf = heap_caps_aligned_alloc(16, 256 * 192 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!frameBuf) {
        ESP_LOGE(TAG, "out of PSRAM for the frame buffer");
        abort();
    }

    renderSem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(emuThread, "emuThread", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(lcdThread, "lcdThread", 4096, NULL, 5, NULL, 1);
}
