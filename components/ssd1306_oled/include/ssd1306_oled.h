#ifndef SSD1306_OLED_H_
#define SSD1306_OLED_H_

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_OLED_WIDTH 128
#define SSD1306_OLED_HEIGHT 64
#define SSD1306_OLED_PAGE_HEIGHT 8
#define SSD1306_OLED_I2C_ADDRESS 0x3C

typedef struct ssd1306_oled_t ssd1306_oled_t;
typedef ssd1306_oled_t *ssd1306_oled_handle_t;

esp_err_t ssd1306_oled_init(i2c_master_bus_handle_t bus_handle, uint8_t slave_address, ssd1306_oled_handle_t *ret_handle);
esp_err_t ssd1306_oled_deinit(ssd1306_oled_handle_t handle);
esp_err_t ssd1306_oled_clear(ssd1306_oled_handle_t handle);
esp_err_t ssd1306_oled_fill(ssd1306_oled_handle_t handle, uint8_t pattern);
esp_err_t ssd1306_oled_set_pixel(ssd1306_oled_handle_t handle, int x, int y, bool on);
esp_err_t ssd1306_oled_draw_char(ssd1306_oled_handle_t handle, int x, int y, char ch);
esp_err_t ssd1306_oled_draw_string(ssd1306_oled_handle_t handle, int x, int y, const char *text);
esp_err_t ssd1306_oled_refresh(ssd1306_oled_handle_t handle);

esp_err_t ssd1306_oled_write_cmd(ssd1306_oled_handle_t handle, uint8_t cmd);
esp_err_t ssd1306_oled_write_data(ssd1306_oled_handle_t handle, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
