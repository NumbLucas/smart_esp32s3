idf_component_register(SRCS "lv_display.c" "screen_driver.c" "softap_sta.c" "camera.c" "udp_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES "esp_driver_spi" "esp_lcd" "esp_wifi" "nvs_flash" "esp_driver_ledc" "esp_lcd_uc8176")
