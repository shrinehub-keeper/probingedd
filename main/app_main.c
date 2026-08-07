#include "esp_log.h"
#include "nvs_flash.h"

#include "board_esp32cam.h"
#include "mjpeg_stream.h"
#include "gamepad_hal.h"
#include "smsplus-main.h"

static const char *TAG = "app_main";

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (board_sdcard_init() != ESP_OK) {
        // Keep booting even without a working SD card: WiFi/BT still come
        // up, and the emulator task just keeps retrying (see emuThread) -
        // better than crash-looping the whole board over a missing ROM
        // source.
        ESP_LOGE(TAG, "SD card init failed, no ROMs will be found until this is fixed");
    }

    mjpeg_stream_init();

    // Starts the emulator + render tasks; the render task feeds frames to
    // mjpeg_stream_push_frame() and the input side polls gamepad_hal.
    smsemuStart();

    ESP_LOGI(TAG, "Handing control to the Bluepad32/BTstack run loop");
    // Brings up BTstack + Bluepad32 and never returns: it runs the BTstack
    // event loop on this task for the lifetime of the program, which is why
    // it must be the last thing app_main() does.
    gamepad_hal_start();
}
