#pragma once

#include <stdbool.h>
#include <stdint.h>

// Generic button bitmask, decoupled from any specific controller (DS4,
// Wiimote, Xbox, Switch Pro, generic HID gamepad, ...). Bluepad32 already
// normalizes all of those into a single virtual gamepad shape (d-pad +
// A/B/X/Y + start/select/system), so the same mapping works unmodified for
// every controller it supports - no per-controller-type code needed here.
#define GAMEPAD_HAL_UP     (1u << 0)
#define GAMEPAD_HAL_DOWN   (1u << 1)
#define GAMEPAD_HAL_LEFT   (1u << 2)
#define GAMEPAD_HAL_RIGHT  (1u << 3)
#define GAMEPAD_HAL_A      (1u << 4)  // South face button: DS4 Cross, Wiimote A, Xbox A
#define GAMEPAD_HAL_B      (1u << 5)  // East face button: DS4 Circle, Wiimote B, Xbox B
#define GAMEPAD_HAL_START  (1u << 6)  // DS4 Options, Wiimote +, Xbox Menu
#define GAMEPAD_HAL_SELECT (1u << 7)  // DS4 Share, Wiimote -, Xbox View
#define GAMEPAD_HAL_SYSTEM (1u << 8)  // DS4 PS button, Wiimote Home button

// Brings up BTstack + Bluepad32, starts scanning/auto-connecting, and then
// runs the BTstack event loop on the calling task for the remaining
// lifetime of the program - it never returns. Must be called exactly once,
// after every other task has already been started.
void gamepad_hal_start(void) __attribute__((noreturn));

// Latest button state as a GAMEPAD_HAL_* bitmask. Safe to call from any
// task/thread; returns 0 if nothing is connected.
uint32_t gamepad_hal_get_buttons(void);

// True if a controller is currently connected and driving input.
bool gamepad_hal_is_connected(void);
