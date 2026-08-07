#pragma once

#include <stdint.h>

// Brings up the WiFi access point and the HTTP server: "/" (a minimal
// viewer page), "/stream" (MJPEG, multipart/x-mixed-replace) and
// "/snapshot" (a single JPEG). The ESP32-CAM has no display of its own, so
// this is how gameplay is actually watched.
void mjpeg_stream_init(void);

// Called by the emulator's render task with a freshly rendered RGB565
// (native/little-endian byte order) frame. JPEG-encodes it and publishes it
// to any browser currently watching "/stream" or "/snapshot".
//
// Encoding happens synchronously on the caller's task/core, so callers that
// render faster than they want to stream (e.g. a 50/60Hz emulator streaming
// at 15fps) should only call this every Nth frame rather than throttle
// inside here.
void mjpeg_stream_push_frame(const uint16_t *rgb565, int width, int height);
