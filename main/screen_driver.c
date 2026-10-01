#include "screen_driver.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "driver/spi_common.h"
#include "driver/i2c_master.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst820.h"
#include "esp_lcd_uc8176.h"
#include "ssd1306_oled.h"
#include "servo_driver.h"
#include "freertos/semphr.h"
#include "image.h"

#define USE_EPAPER_SCREEN
// #define USE_SSD1306_OLED
// #define USE_AMOLED

#define PIN_NUM_EPAPER_SCL 38 //褐 CH1
#define PIN_NUM_EPAPER_SDA 39 //白 CH3
#define PIN_NUM_EPAPER_RST 40 //灰 CH5 
#define PIN_NUM_EPAPER_DC 41  //紫 CH7
#define PIN_NUM_EPAPER_CS 42  // 红 CH2
#define PIN_NUM_EPAPER_BUSY 46  // 褐2 

#define PIN_NUM_LCD_PCLK 38
#define PIN_NUM_LCD_DATA0 39
#define PIN_NUM_LCD_DATA1 40
#define PIN_NUM_LCD_DATA2 41
#define PIN_NUM_LCD_DATA3 42
#define PIN_NUM_LCD_CS 46
#define PIN_NUM_LCD_RST -1

#define PIN_NUM_SDA 21
#define PIN_NUM_SCL 47
#define PIN_NUM_LCD_TOUCH_RST -1
#define PIN_NUM_LCD_TOUCH_INT 21

#define I2C_BUS_PORT 0

#define LCD_H_RES 466
#define LCD_HOST 1
#define LCD_BIT_PER_PIXEL 16

static char *TAG = "screen driver";

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t spi_io_handle = NULL;
static esp_lcd_panel_io_handle_t i2c_io_handle = NULL;
static i2c_master_bus_handle_t oled_i2c_bus = NULL;
static i2c_master_dev_handle_t sht40_i2c_dev = NULL;
static ssd1306_oled_handle_t oled_handle = NULL;

static SemaphoreHandle_t touch_mux = NULL;

static uint8_t BW_DATA[15000] = {0};
static uint8_t RED_DATA[15000] = {0};
static void touch_callback(esp_lcd_touch_handle_t tp);

void init_co5300_display(void);

void init_cst820_touch(void);

void init_epd(void);
static esp_err_t sht40_init(void)
{
    if (oled_i2c_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sht40_i2c_dev != NULL) {
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x44,
        .scl_speed_hz = 100000,
    };

    return i2c_master_bus_add_device(oled_i2c_bus, &dev_cfg, &sht40_i2c_dev);
}

static esp_err_t sht40_read_temperature_humidity(float *temperature_c, float *humidity_rh)
{
    if (temperature_c == NULL || humidity_rh == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (sht40_i2c_dev == NULL) {
        esp_err_t ret = sht40_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    uint8_t cmd = 0xFD; // high-precision measurement for SHT40
    uint8_t rx_buf[6] = {0};

    for (int attempt = 0; attempt < 3; attempt++) {
        esp_err_t ret = i2c_master_transmit(sht40_i2c_dev, &cmd, 1, pdMS_TO_TICKS(100));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SHT40 transmit failed, attempt %d/%d: %s", attempt + 1, 3, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(20));

        ret = i2c_master_receive(sht40_i2c_dev, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SHT40 receive failed, attempt %d/%d: %s", attempt + 1, 3, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        if (rx_buf[2] == 0x00 && rx_buf[5] == 0x00) {
            ESP_LOGW(TAG, "SHT40 received invalid data, retrying");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        uint16_t raw_temp = ((uint16_t)rx_buf[0] << 8) | rx_buf[1];
        uint16_t raw_humidity = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];

        *temperature_c = -45.0f + 175.0f * (float)raw_temp / 65535.0f;
        *humidity_rh = -6.0f + 125.0f * (float)raw_humidity / 65535.0f;
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

void init_oled_display(void)
{
    const i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_NUM_1,
        .sda_io_num = PIN_NUM_SDA,
        .scl_io_num = PIN_NUM_SCL,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &oled_i2c_bus));
    ESP_ERROR_CHECK(ssd1306_oled_init(oled_i2c_bus, SSD1306_OLED_I2C_ADDRESS, &oled_handle));

    ssd1306_oled_clear(oled_handle);
    ssd1306_oled_draw_string(oled_handle, 0, 0, "ESP32");
    ssd1306_oled_draw_string(oled_handle, 0, 16, "SSD1306");
    ssd1306_oled_draw_string(oled_handle, 0, 32, "128x64");
    ssd1306_oled_draw_string(oled_handle, 0, 48, "I2C OK");
    ssd1306_oled_refresh(oled_handle);
    ESP_LOGI(TAG, "SSD1306 OLED initialized");
}

static void format_clock_string(char *buf, size_t buf_size)
{
    uint64_t now_ms = esp_timer_get_time() / 1000ULL;
    uint32_t seconds = (uint32_t)(now_ms / 1000ULL);
    uint32_t hour = (seconds / 3600U) % 24U;
    uint32_t minute = (seconds / 60U) % 60U;
    snprintf(buf, buf_size, "%02lu:%02lu", (unsigned long)hour, (unsigned long)minute);
}

static void draw_status_bar(const char *time_text, const char *device_state, const char *wifi_state)
{
    char bar[32] = {0};
    snprintf(bar, sizeof(bar), "%s %s %s", time_text, device_state, wifi_state);
    ssd1306_oled_draw_string(oled_handle, 0, 0, bar);
}

void oled_demo_display(void)
{
    if (oled_handle == NULL) {
        ESP_LOGW(TAG, "OLED handle is NULL, call init_oled_display first");
        return;
    }

    float temperature_c = 0.0f;
    float humidity_rh = 0.0f;
    char time_buf[16] = {0};
    char status_line[32] = {0};
    char line1[32] = {0};
    char line2[32] = {0};
    char line3[32] = {0};
    const char *device_state = "ERR";
    const char *wifi_state = "WIFI";

    format_clock_string(time_buf, sizeof(time_buf));

    if (sht40_read_temperature_humidity(&temperature_c, &humidity_rh) == ESP_OK) {
        device_state = "OK";
        servo_update_temperature(temperature_c);
        snprintf(line1, sizeof(line1), "TEMP: %.1fC", temperature_c);
        snprintf(line2, sizeof(line2), "HUM: %.1f%%", humidity_rh);
        snprintf(line3, sizeof(line3), "SHT40 READY");
    } else {
        snprintf(line1, sizeof(line1), "TEMP: --.-C");
        snprintf(line2, sizeof(line2), "HUM: --.-%%");
        snprintf(line3, sizeof(line3), "SHT40 FAIL");
    }

    snprintf(status_line, sizeof(status_line), "%s %s", device_state, wifi_state);

    ssd1306_oled_clear(oled_handle);
    draw_status_bar(time_buf, device_state, wifi_state);
    ssd1306_oled_draw_string(oled_handle, 0, 16, line1);
    ssd1306_oled_draw_string(oled_handle, 0, 32, line2);
    ssd1306_oled_draw_string(oled_handle, 0, 48, line3);
    ssd1306_oled_refresh(oled_handle);
}

static void sht40_oled_task(void *arg)
{
    (void)arg;

    while (1) {
        oled_demo_display();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void init_screen(void)
{
#ifdef USE_SSD1306_OLED
    // init_oled_display();
    // xTaskCreate(sht40_oled_task, "sht40_oled_task", 4096, NULL, 5, NULL);
    xTaskCreate(servo_demo_task, "servo_demo_task", 4096, NULL, 5, NULL);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#elif defined(USE_EPAPER_SCREEN)
    init_epd();
    vTaskDelay(pdMS_TO_TICKS(1000));
    full_black();
    vTaskDelay(pdMS_TO_TICKS(4000));
    ESP_LOGI(TAG, "vTaskDelay");
    epaper_panel_uc8176_sleep(panel_handle);
#else
    init_co5300_display();

    init_cst820_touch();

    #if 1
    while(1)
    {
        static uint8_t color_data[200] = {0};
        for (int i = 0; i< 200;i++)
        {
            color_data[i] = i;
        }
    esp_lcd_panel_draw_bitmap(panel_handle, 1, 1, 10, 10, color_data);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    #endif

#endif
}

void full_default_image(void)
{
    EPD_WhiteScreen_ALL(panel_handle, gImage_black1, gImage_red1);
}
void full_white(void)
{
    for(int i = 0;i < 15000;i++)
    {
        BW_DATA[i] = 0;
        RED_DATA[i] = 0;
    }
    EPD_WhiteScreen_ALL(panel_handle, BW_DATA, RED_DATA);
    // epaper_panel_uc8176_refresh_screen(panel_handle);
}
void full_black(void)
{
    for(int i = 0;i < 15000;i++)
    {
        BW_DATA[i] = 0xFF;
        RED_DATA[i] = 0;
    }
    EPD_WhiteScreen_ALL(panel_handle, BW_DATA, RED_DATA);
    epaper_panel_uc8176_refresh_screen(panel_handle);
}
void full_red(void)
{
    for(int i = 0;i < 15000;i++)
    {
        BW_DATA[i] = 0;
        RED_DATA[i] = 0xFF;
    }
    EPD_WhiteScreen_ALL(panel_handle, BW_DATA, RED_DATA);
    epaper_panel_uc8176_refresh_screen(panel_handle);
}

void init_epd(void)
{
    ESP_LOGI(TAG, "init_epd");
    // TODO
    const spi_bus_config_t buscfg = UC8176_PANEL_BUS_SPI_CONFIG(PIN_NUM_EPAPER_SCL,
                                                                PIN_NUM_EPAPER_SDA,
                                                                LCD_H_RES * 80 * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");

    esp_lcd_panel_io_spi_config_t io_config = UC8176_PANEL_IO_SPI_CONFIG(PIN_NUM_LCD_CS, PIN_NUM_EPAPER_DC, NULL, NULL);

    io_config.pclk_hz = 1*1000*1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &spi_io_handle));

    esp_lcd_uc8176_config_t epaper_uc8176_config = {
        .busy_gpio_num = PIN_NUM_EPAPER_BUSY,
        .non_copy_mode = false,
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_EPAPER_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, // Implemented by LCD command `36h`
        .bits_per_pixel = LCD_BIT_PER_PIXEL,        // Implemented by LCD command `3Ah` (16/18/24)
        .vendor_config = (void *)&epaper_uc8176_config,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_uc8176(spi_io_handle, &panel_config, &panel_handle));

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_disp_on_off(panel_handle, true);

}

void init_co5300_display(void)
{
    ESP_LOGI(TAG, "Initialize QSPI bus");
    const spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(PIN_NUM_LCD_PCLK,
                                                                 PIN_NUM_LCD_DATA0,
                                                                 PIN_NUM_LCD_DATA1,
                                                                 PIN_NUM_LCD_DATA2,
                                                                 PIN_NUM_LCD_DATA3,
                                                                 LCD_H_RES * 80 * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");

    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(PIN_NUM_LCD_CS, NULL, NULL);
	// set clk 12MHZ
	io_config.pclk_hz = 1*1000*1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &spi_io_handle));

    /**
     * Uncomment these line if use custom initialization commands.
     * The array should be declared as static const and positioned outside the function.
     */
    // static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    // //  {cmd, { data }, data_size, delay_ms}
    //    {0xfe, (uint8_t []){0x00}, 0, 0},
    //    {0xef, (uint8_t []){0x00}, 0, 0},
    //    {0x80, (uint8_t []){0x11}, 1, 0},
    //    {0x81, (uint8_t []){0x70}, 1, 0},
    //     ...
    // };

    ESP_LOGI(TAG, "Install CO5300 panel driver");

    co5300_vendor_config_t vendor_config = {
        // .init_cmds = lcd_init_cmds,         // Only useful when `use_external_init_cmds` is set to 1
        // .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(co5300_lcd_init_cmd_t),
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, // Implemented by LCD command `36h`
        .bits_per_pixel = LCD_BIT_PER_PIXEL,        // Implemented by LCD command `3Ah` (16/18/24)
        .vendor_config = (void *)&vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(spi_io_handle, &panel_config, &panel_handle));

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_disp_on_off(panel_handle, true);
}

void init_cst820_touch(void)
{
    touch_mux = xSemaphoreCreateBinary();

    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = PIN_NUM_SDA,
        .scl_io_num = PIN_NUM_SCL,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &i2c_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_HRES,
        .y_max = LCD_VRES,
        .rst_gpio_num = PIN_NUM_LCD_TOUCH_RST,
        .int_gpio_num = PIN_NUM_LCD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .interrupt_callback = touch_callback,
    };

    esp_lcd_touch_handle_t tp;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst820(i2c_io_handle, &tp_cfg, &tp));
}

void draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    if (panel_handle != NULL)
    {
        esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, color_data);
    }
}

static void touch_callback(esp_lcd_touch_handle_t tp)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(touch_mux, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken)
    {
        portYIELD_FROM_ISR();
    }
}
