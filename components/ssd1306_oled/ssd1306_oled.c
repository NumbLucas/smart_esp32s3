#include "ssd1306_oled.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define SSD1306_BUFFER_SIZE (SSD1306_OLED_WIDTH * SSD1306_OLED_HEIGHT / 8)

typedef struct ssd1306_oled_t {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t slave_address;
    uint8_t *buffer;
} ssd1306_oled_t;

static const char *TAG = "ssd1306_oled";

esp_err_t ssd1306_oled_write_cmd(ssd1306_oled_handle_t handle, uint8_t cmd)
{
    uint8_t write_buffer[2] = {0x00, cmd};
    return i2c_master_transmit(handle->i2c_dev, write_buffer, sizeof(write_buffer), pdMS_TO_TICKS(100));
}

esp_err_t ssd1306_oled_write_data(ssd1306_oled_handle_t handle, const uint8_t *data, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }

    uint8_t *tx_buf = malloc(len + 1);
    if (tx_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    tx_buf[0] = 0x40;
    memcpy(&tx_buf[1], data, len);

    esp_err_t ret = i2c_master_transmit(handle->i2c_dev, tx_buf, len + 1, pdMS_TO_TICKS(100));
    free(tx_buf);
    return ret;
}

static const uint8_t *ssd1306_get_char_pattern(char ch)
{
    static const uint8_t font_space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t font_A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    static const uint8_t font_B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t font_C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
    static const uint8_t font_D[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
    static const uint8_t font_E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
    static const uint8_t font_F[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
    static const uint8_t font_G[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
    static const uint8_t font_H[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t font_I[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
    static const uint8_t font_J[5] = {0x20, 0x40, 0x41, 0x3F, 0x01};
    static const uint8_t font_L[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
    static const uint8_t font_M[5] = {0x7F, 0x02, 0x04, 0x02, 0x7F};
    static const uint8_t font_N[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
    static const uint8_t font_O[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t font_P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
    static const uint8_t font_R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
    static const uint8_t font_S[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
    static const uint8_t font_T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t font_U[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
    static const uint8_t font_Y[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
    static const uint8_t font_Z[5] = {0x71, 0x49, 0x45, 0x43, 0x41};
    static const uint8_t font_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t font_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
    static const uint8_t font_2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t font_3[5] = {0x21, 0x41, 0x49, 0x4D, 0x33};
    static const uint8_t font_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
    static const uint8_t font_5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
    static const uint8_t font_6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    static const uint8_t font_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
    static const uint8_t font_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t font_9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};
    static const uint8_t font_colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t font_minus[5] = {0x00, 0x08, 0x08, 0x08, 0x00};
    static const uint8_t font_dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};

    ch = (char)toupper((unsigned char)ch);

    switch (ch) {
        case ' ': return font_space;
        case 'A': return font_A;
        case 'B': return font_B;
        case 'C': return font_C;
        case 'D': return font_D;
        case 'E': return font_E;
        case 'F': return font_F;
        case 'G': return font_G;
        case 'H': return font_H;
        case 'I': return font_I;
        case 'J': return font_J;
        case 'L': return font_L;
        case 'M': return font_M;
        case 'N': return font_N;
        case 'O': return font_O;
        case 'P': return font_P;
        case 'R': return font_R;
        case 'S': return font_S;
        case 'T': return font_T;
        case 'U': return font_U;
        case 'Y': return font_Y;
        case 'Z': return font_Z;
        case '0': return font_0;
        case '1': return font_1;
        case '2': return font_2;
        case '3': return font_3;
        case '4': return font_4;
        case '5': return font_5;
        case '6': return font_6;
        case '7': return font_7;
        case '8': return font_8;
        case '9': return font_9;
        case ':': return font_colon;
        case '-': return font_minus;
        case '.': return font_dot;
        default: return font_space;
    }
}

esp_err_t ssd1306_oled_init(i2c_master_bus_handle_t bus_handle, uint8_t slave_address, ssd1306_oled_handle_t *ret_handle)
{
    if (bus_handle == NULL || ret_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ssd1306_oled_t *handle = calloc(1, sizeof(ssd1306_oled_t));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    handle->slave_address = slave_address;
    handle->buffer = calloc(1, SSD1306_BUFFER_SIZE);
    if (handle->buffer == NULL) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = slave_address,
        .scl_speed_hz = 400000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &handle->i2c_dev);
    if (ret != ESP_OK) {
        free(handle->buffer);
        free(handle);
        return ret;
    }

    static const uint8_t init_cmds[] = {
        0xAE, // display off
        0xD5, 0x80, // clock divide ratio / oscillator frequency
        0xA8, 0x3F, // multiplex ratio 64
        0xD3, 0x00, // display offset
        0x40, // start line
        0x8D, 0x14, // charge pump enabled
        0x20, 0x00, // memory mode horizontal
        0xA1, // segment remap
        0xC8, // COM scan direction
        0xDA, 0x12, // COM pins hardware config
        0x81, 0x7F, // contrast
        0xD9, 0xF1, // pre-charge period
        0xDB, 0x40, // VCOMH deselect
        0xA4, // display from RAM
        0xA6, // normal display
        0xAF // display on
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(ssd1306_oled_write_cmd(handle, init_cmds[i]));
    }

    ret = ssd1306_oled_clear(handle);
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(handle->i2c_dev);
        free(handle->buffer);
        free(handle);
        return ret;
    }

    *ret_handle = handle;
    return ESP_OK;
}

esp_err_t ssd1306_oled_deinit(ssd1306_oled_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_rm_device(handle->i2c_dev);
    free(handle->buffer);
    free(handle);
    return ESP_OK;
}

esp_err_t ssd1306_oled_clear(ssd1306_oled_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle->buffer, 0x00, SSD1306_BUFFER_SIZE);
    return ssd1306_oled_refresh(handle);
}

esp_err_t ssd1306_oled_fill(ssd1306_oled_handle_t handle, uint8_t pattern)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle->buffer, pattern, SSD1306_BUFFER_SIZE);
    return ssd1306_oled_refresh(handle);
}

esp_err_t ssd1306_oled_set_pixel(ssd1306_oled_handle_t handle, int x, int y, bool on)
{
    if (handle == NULL || x < 0 || x >= SSD1306_OLED_WIDTH || y < 0 || y >= SSD1306_OLED_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    int page = y / SSD1306_OLED_PAGE_HEIGHT;
    int bit = y % SSD1306_OLED_PAGE_HEIGHT;
    int index = page * SSD1306_OLED_WIDTH + x;

    if (on) {
        handle->buffer[index] |= (1 << bit);
    } else {
        handle->buffer[index] &= ~(1 << bit);
    }

    return ESP_OK;
}

esp_err_t ssd1306_oled_draw_char(ssd1306_oled_handle_t handle, int x, int y, char ch)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (x < 0 || y < 0 || x >= SSD1306_OLED_WIDTH || y >= SSD1306_OLED_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *font = ssd1306_get_char_pattern(ch);
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 8; col++) {
            if (font[row] & (1 << col)) {
                ssd1306_oled_set_pixel(handle, x + row, y + col, true);
            }
        }
    }
    return ESP_OK;
}

esp_err_t ssd1306_oled_draw_string(ssd1306_oled_handle_t handle, int x, int y, const char *text)
{
    if (handle == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int cursor_x = x;
    int cursor_y = y;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            cursor_y += 10;
            cursor_x = x;
            continue;
        }

        if (cursor_x >= SSD1306_OLED_WIDTH - 6) {
            break;
        }

        ssd1306_oled_draw_char(handle, cursor_x, cursor_y, *p);
        cursor_x += 6;
    }

    return ESP_OK;
}

esp_err_t ssd1306_oled_refresh(ssd1306_oled_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int page = 0; page < 8; page++) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(ssd1306_oled_write_cmd(handle, 0xB0 + page));
        ESP_ERROR_CHECK_WITHOUT_ABORT(ssd1306_oled_write_cmd(handle, 0x00));
        ESP_ERROR_CHECK_WITHOUT_ABORT(ssd1306_oled_write_cmd(handle, 0x10));

        uint8_t *page_data = &handle->buffer[page * SSD1306_OLED_WIDTH];
        esp_err_t ret = ssd1306_oled_write_data(handle, page_data, SSD1306_OLED_WIDTH);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return ESP_OK;
}
