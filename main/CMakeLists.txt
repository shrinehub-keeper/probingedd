idf_component_register(SRCS "app_main.c"
                        INCLUDE_DIRS "."
                        REQUIRES nvs_flash esp_wifi gamepad-hal mjpeg-stream board-esp32cam smsplus-esp32)
