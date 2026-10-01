idf_component_register(SRCS "lv_display.c" "screen_driver.c" "servo_driver.c" "softap_sta.c" "camera.c" "udp_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES "esp_driver_spi" "esp_driver_i2c" "esp_driver_gpio" "esp_lcd" "esp_wifi" "esp_timer" "nvs_flash" "esp_driver_ledc" "esp_lcd_uc8176" "ssd1306_oled")
