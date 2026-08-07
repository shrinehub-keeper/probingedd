idf_component_register(SRCS "smsplus-main.c" "system-esp32.c"
                        INCLUDE_DIRS "."
                        REQUIRES smsplus gamepad-hal mjpeg-stream board-esp32cam)
