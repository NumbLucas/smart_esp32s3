#include "screen_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/spi_common.h"
#include "driver/i2c_master.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst820.h"
#include "esp_lcd_uc8176.h"
#include "freertos/semphr.h"
#include "image.h"

#define USE_EPAPER_SCREEN
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

static SemaphoreHandle_t touch_mux = NULL;

static uint8_t BW_DATA[15000] = {0};
static uint8_t RED_DATA[15000] = {0};
static void touch_callback(esp_lcd_touch_handle_t tp);

void init_co5300_display(void);

void init_cst820_touch(void);

void init_epd(void);

void init_screen(void)
{
#ifdef USE_EPAPER_SCREEN
    init_epd();
    vTaskDelay(pdMS_TO_TICKS(1000));
    full_default_image();
    vTaskDelay(pdMS_TO_TICKS(40000));
    epaper_panel_uc8176_sleep(panel_handle);
    // while(1)
    // {
    //     full_black();
    //     vTaskDelay(pdMS_TO_TICKS(50000));
    //     full_white();
    //     vTaskDelay(pdMS_TO_TICKS(50000));
    //     full_red();
    //     vTaskDelay(pdMS_TO_TICKS(50000));
    // }

    
    // full_black();
    // epaper_panel_uc8176_sleep(panel_handle);
    // vTaskDelay(pdMS_TO_TICKS(20000));

    // full_red();
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
