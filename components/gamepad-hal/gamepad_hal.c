// Bluepad32 "custom platform" that turns whatever Bluetooth controller is
// connected (DS4, Wiimote, Xbox, Switch Pro, ...) into the generic
// GAMEPAD_HAL_* button bitmask the emulator reads every frame.

#include <string.h>

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <uni.h>

#include "esp_log.h"
#include "sdkconfig.h"

#include "gamepad_hal.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "sdkconfig.defaults must set CONFIG_BLUEPAD32_PLATFORM_CUSTOM=y"
#endif

static const char *TAG = "gamepad_hal";

static volatile uint32_t s_buttons;
static uni_hid_device_t *s_active_device;

static void set_leds(uni_hid_device_t *d) {
    if (d->report_parser.set_player_leds != NULL) {
        d->report_parser.set_player_leds(d, 0x01 /* player 1 */);
    }
    if (d->report_parser.play_dual_rumble != NULL) {
        d->report_parser.play_dual_rumble(d, 0 /* delayed start ms */, 150 /* duration ms */, 128, 40);
    }
}

static void platform_init(int argc, const char **argv) {
    (void)argc;
    (void)argv;
    ESP_LOGI(TAG, "init");
}

static void platform_on_init_complete(void) {
    // Safe to call "unsafe" BT functions here: we're on the BTstack thread.
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
    ESP_LOGI(TAG, "scanning for controllers (DS4, Wiimote, Xbox, Switch Pro, ...)");
}

static void platform_on_device_connected(uni_hid_device_t *d) {
    ESP_LOGI(TAG, "controller connected: %p", (void *)d);
}

static void platform_on_device_disconnected(uni_hid_device_t *d) {
    ESP_LOGI(TAG, "controller disconnected: %p", (void *)d);
    if (s_active_device == d) {
        s_active_device = NULL;
        s_buttons = 0;
    }
}

static uni_error_t platform_on_device_ready(uni_hid_device_t *d) {
    ESP_LOGI(TAG, "controller ready: %p", (void *)d);
    if (s_active_device == NULL) {
        s_active_device = d;
    }
    set_leds(d);
    return UNI_ERROR_SUCCESS;
}

static void platform_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl) {
    // Only one controller drives the emulator; ignore data from any others
    // that happen to be connected at the same time.
    if (d != s_active_device) {
        return;
    }
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;
    }

    const uni_gamepad_t *gp = &ctl->gamepad;
    uint32_t buttons = 0;

    if (gp->dpad & DPAD_UP) buttons |= GAMEPAD_HAL_UP;
    if (gp->dpad & DPAD_DOWN) buttons |= GAMEPAD_HAL_DOWN;
    if (gp->dpad & DPAD_LEFT) buttons |= GAMEPAD_HAL_LEFT;
    if (gp->dpad & DPAD_RIGHT) buttons |= GAMEPAD_HAL_RIGHT;
    if (gp->buttons & BUTTON_A) buttons |= GAMEPAD_HAL_A;
    if (gp->buttons & BUTTON_B) buttons |= GAMEPAD_HAL_B;
    if (gp->misc_buttons & MISC_BUTTON_START) buttons |= GAMEPAD_HAL_START;
    if (gp->misc_buttons & MISC_BUTTON_SELECT) buttons |= GAMEPAD_HAL_SELECT;
    if (gp->misc_buttons & MISC_BUTTON_SYSTEM) buttons |= GAMEPAD_HAL_SYSTEM;

    s_buttons = buttons;
}

static const uni_property_t *platform_get_property(uni_property_idx_t idx) {
    (void)idx;
    return NULL;
}

static void platform_on_oob_event(uni_platform_oob_event_t event, void *data) {
    (void)data;
    ESP_LOGD(TAG, "oob event: %d", event);
}

static struct uni_platform *get_platform(void) {
    static struct uni_platform plat = {
        .name = "probingedd-gamepad-hal",
        .init = platform_init,
        .on_init_complete = platform_on_init_complete,
        .on_device_connected = platform_on_device_connected,
        .on_device_disconnected = platform_on_device_disconnected,
        .on_device_ready = platform_on_device_ready,
        .on_controller_data = platform_on_controller_data,
        .get_property = platform_get_property,
        .on_oob_event = platform_on_oob_event,
    };
    return &plat;
}

void gamepad_hal_start(void) {
#ifdef CONFIG_ESP_CONSOLE_UART
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif
#endif

    btstack_init();
    uni_platform_set_custom(get_platform());
    uni_init(0, NULL);

    // Does not return.
    btstack_run_loop_execute();

    // Unreachable, but keeps the compiler happy about the noreturn attribute.
    for (;;) {
    }
}

uint32_t gamepad_hal_get_buttons(void) {
    return s_buttons;
}

bool gamepad_hal_is_connected(void) {
    return s_active_device != NULL;
}
