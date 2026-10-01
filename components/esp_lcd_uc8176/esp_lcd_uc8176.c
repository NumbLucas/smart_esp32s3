#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_lcd_uc8176.h"

static const char *TAG = "UC8176 Driver";

// #define DEBUG_BUSY_NO_WAIT
#define UC8176_CMD_SWRST -1
#define UC8176_CMD_REFRESH  0x12
#define UC8176_CMD_TRANSFER_BW_DATA   0x10
#define UC8176_CMD_TRANSFER_RED_DATA  0x13

#define EPD_4IN2_WIDTH  400
#define EPD_4IN2_HEIGHT  300
#define EPD_ARRAY  EPD_4IN2_WIDTH*EPD_4IN2_HEIGHT/8  

esp_err_t epaper_panel_uc8176_refresh_screen(esp_lcd_panel_t *panel);
esp_err_t epaper_panel_uc8176_sleep(esp_lcd_panel_t *panel);

static esp_err_t panel_epaper_wait_busy(esp_lcd_panel_t *panel);

static esp_err_t panel_uc8176_del(esp_lcd_panel_t *panel);
static esp_err_t panel_uc8176_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_uc8176_init(esp_lcd_panel_t *panel);
static esp_err_t panel_uc8176_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_uc8176_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_uc8176_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_uc8176_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_uc8176_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_uc8176_disp_on_off(esp_lcd_panel_t *panel, bool off);
static inline uint8_t byte_reverse(uint8_t data);

typedef struct
{
    esp_lcd_epaper_panel_cb_t callback_ptr;
    void *args;
} epaper_panel_callback_t;

typedef struct
{
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    // --- Normal configurations
    // Configurations from panel_dev_config
    int reset_gpio_num;
    bool reset_level;
    // Configurations from epaper_uc8176_conf
    int busy_gpio_num;
    bool full_refresh;
    // Configurations from interface functions
    int gap_x;
    int gap_y;
    // Configurations from e-Paper specific public functions
    epaper_panel_callback_t epaper_refresh_done_isr_callback;
    esp_lcd_uc8176_bitmap_color_t bitmap_color;
    // --- Associated configurations
    // SHOULD NOT modify directly
    // in order to avoid going into undefined state
    bool _non_copy_mode;
    bool _mirror_y;
    bool _swap_xy;
    // --- Other private fields
    bool _mirror_x;
    uint8_t *_framebuffer;
    bool _invert_color;
} uc8176_panel_t;

static const uc8176_lcd_cmds_sequence_t vendor_specific_init_default[] = {
    //  {cmd, { data }, data_size, delay_ms, wait_busy_sig}
    {0x06, (uint8_t[]){0x17, 0x17, 0x17}, 3, 0, 0},                 // 
    {0x04, (uint8_t[]){0x00}, 0, 0, 1},                   // 
    {0x00, (uint8_t[]){0x0f, 0x0d}, 2, 0, 0},                   // 
};

static const uc8176_lcd_cmds_sequence_t vendor_specific_sleep_default[] = {
    //  {cmd, { data }, data_size, delay_ms, wait_busy_sig}
    {0x50, (uint8_t[]){0xf7}, 1, 0, 0},                 // 
    {0x02, (uint8_t[]){0x00}, 0, 100, 1},                   // 
    {0x07, (uint8_t[]){0xA5}, 1, 0, 0},                   // 
};

static esp_err_t panel_epaper_wait_busy(esp_lcd_panel_t *panel)
{
    uc8176_panel_t *epaper_panel = __containerof(panel, uc8176_panel_t, base);
    ESP_LOGI(TAG, "panel_epaper_wait_busy.......");
    #ifndef DEBUG_BUSY_NO_WAIT
    while (gpio_get_level(epaper_panel->busy_gpio_num) == 0)
    {
        ESP_LOGI(TAG, "panel_epaper_wait_busy.......while");
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    #endif
    ESP_LOGI(TAG, "panel_epaper_wait_busy succeed");
    return ESP_OK;
}

esp_err_t epaper_panel_uc8176_sleep(esp_lcd_panel_t *panel)
{
    uc8176_panel_t *uc8176 = __containerof(panel, uc8176_panel_t, base);
    esp_lcd_panel_io_handle_t io = uc8176->io;
    const uc8176_lcd_cmds_sequence_t *sleep_cmds = NULL;
    uint16_t sleep_cmds_size = 0;

    sleep_cmds = vendor_specific_sleep_default;
    sleep_cmds_size = sizeof(vendor_specific_sleep_default) / sizeof(uc8176_lcd_cmds_sequence_t);

    for (int i = 0; i < sleep_cmds_size; i++)
    {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, sleep_cmds[i].cmd, sleep_cmds[i].data, sleep_cmds[i].data_bytes), TAG, "send command failed");

        if (sleep_cmds[i].delay_ms > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(sleep_cmds[i].delay_ms));
        }
        if (sleep_cmds[i].wait_busy_sig > 0)
        {
            ESP_LOGI(TAG, "epaper_panel_uc8176_sleep panel_epaper_wait_busy");
            panel_epaper_wait_busy(panel);
            ESP_LOGI(TAG, "epaper_panel_uc8176_sleep panel_epaper_wait_busy succeed");
        }
    }
    ESP_LOGI(TAG, "send sleep commands success");
    return ESP_OK;
}

static void epaper_driver_gpio_isr_handler(void *arg)
{
    uc8176_panel_t *epaper_panel = arg;
    // --- Disable ISR handling
    gpio_intr_disable(epaper_panel->busy_gpio_num);

    // --- Call user callback func
    if (epaper_panel->epaper_refresh_done_isr_callback.callback_ptr) {
        (epaper_panel->epaper_refresh_done_isr_callback.callback_ptr)(&(epaper_panel->base), NULL, epaper_panel->epaper_refresh_done_isr_callback.args);
    }
}

static esp_err_t panel_uc8176_del(esp_lcd_panel_t *panel)
{
    uc8176_panel_t *epaper_panel = __containerof(panel, uc8176_panel_t, base);
    // --- Reset used GPIO pins
    if ((epaper_panel->reset_gpio_num) >= 0)
    {
        gpio_reset_pin(epaper_panel->reset_gpio_num);
    }
    gpio_reset_pin(epaper_panel->busy_gpio_num);
    // --- Free allocated RAM
    if ((epaper_panel->_framebuffer) && (!(epaper_panel->_non_copy_mode)))
    {
        // Should not free if buffer is not allocated by driver
        free(epaper_panel->_framebuffer);
    }
    ESP_LOGI(TAG, "del UC8176 epaper panel @%p", epaper_panel);
    free(epaper_panel);
    return ESP_OK;
}
static esp_err_t panel_uc8176_reset(esp_lcd_panel_t *panel)
{
    uc8176_panel_t *epaper_panel = __containerof(panel, uc8176_panel_t, base);
    esp_lcd_panel_io_handle_t io = epaper_panel->io;

    // perform hardware reset
    if (epaper_panel->reset_gpio_num >= 0)
    {
        for (int i = 0; i < 3; i++)
        {
            ESP_RETURN_ON_ERROR(gpio_set_level(epaper_panel->reset_gpio_num, epaper_panel->reset_level), TAG,
                                "gpio_set_level error");
            vTaskDelay(pdMS_TO_TICKS(10));
            ESP_RETURN_ON_ERROR(gpio_set_level(epaper_panel->reset_gpio_num, !epaper_panel->reset_level), TAG,
                                "gpio_set_level error");
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    else
    {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, UC8176_CMD_SWRST, NULL, 0), TAG,
                            "param uc8176_CMD_SWRST err");
    }
    ESP_LOGI(TAG, "panel_uc8176_reset panel_epaper_wait_busy");
    panel_epaper_wait_busy(panel);
    ESP_LOGI(TAG, "panel_uc8176_reset panel_epaper_wait_busy succeed");
    return ESP_OK;
}
static esp_err_t panel_uc8176_init(esp_lcd_panel_t *panel)
{
    uc8176_panel_t *uc8176 = __containerof(panel, uc8176_panel_t, base);
    esp_lcd_panel_io_handle_t io = uc8176->io;
    const uc8176_lcd_cmds_sequence_t *cmds_sequence = NULL;
    uint16_t cmds_size = 0;

    cmds_sequence = vendor_specific_init_default;
    cmds_size = sizeof(vendor_specific_init_default) / sizeof(uc8176_lcd_cmds_sequence_t);

    for (int i = 0; i < cmds_size; i++)
    {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, cmds_sequence[i].cmd, cmds_sequence[i].data, cmds_sequence[i].data_bytes), TAG, "send command failed");

        if (cmds_sequence[i].delay_ms > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(cmds_sequence[i].delay_ms));
        }
        if (cmds_sequence[i].wait_busy_sig > 0)
        {
            ESP_LOGI(TAG, "panel_uc8176_init panel_epaper_wait_busy");
            panel_epaper_wait_busy(panel);
        }
    }
    ESP_LOGI(TAG, "send init commands success");

    return ESP_OK;
}

static esp_err_t panel_uc8176_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    return ESP_OK;
}

static esp_err_t process_bitmap(esp_lcd_panel_t *panel, int len_x, int len_y, int buffer_size, const void *color_data)
{
    uc8176_panel_t *epaper_panel = __containerof(panel, uc8176_panel_t, base);
    // --- Convert image according to configuration
    if ((!(epaper_panel->_mirror_x)) && (!(epaper_panel->_mirror_y))) {
        if (!(epaper_panel->_non_copy_mode)) {
            if (epaper_panel->_swap_xy) {
                memset(epaper_panel->_framebuffer, 0, 200 * 200 / 8);
                for (int i = 0; i < buffer_size * 8; i++) {
                    uint8_t bitmap_byte = ((uint8_t *) (color_data))[i / 8];
                    uint8_t bitmap_pixel = (bitmap_byte & (0x01 << (7 - (i % 8)))) ? 0x01 : 0x00;
                    (epaper_panel->_framebuffer)[((i * len_y / 8) % buffer_size) + (i / 8 / len_x)] |= (bitmap_pixel << (7 - ((i / len_x) % 8)));
                }
            } else {
                for (int i = 0; i < buffer_size; i++) {
                    (epaper_panel->_framebuffer)[i] = ((uint8_t *) (color_data))[i];
                }
            }
        }
    }
    if ((!(epaper_panel->_mirror_x)) && (epaper_panel->_mirror_y)) {
        if (epaper_panel->_swap_xy) {
            memset((epaper_panel->_framebuffer), 0, 200 * 200 / 8);
            for (int i = 0; i < buffer_size * 8; i++) {
                uint8_t bitmap_byte = ((uint8_t *) (color_data))[i / 8];
                uint8_t bitmap_pixel = (bitmap_byte & (0x01 << (7 - (i % 8)))) ? 0x01 : 0x00;
                (epaper_panel->_framebuffer)[buffer_size - (((i * len_y / 8) % buffer_size) + (i / 8 / len_x)) - 1] |= (bitmap_pixel << (((i / len_x) % 8)));
            }
        } else {
            for (int i = 0; i < buffer_size; i++) {
                (epaper_panel->_framebuffer)[buffer_size - i - 1] = byte_reverse(((uint8_t *)(color_data))[i]);
            }
        }
    }
    if (((epaper_panel->_mirror_x)) && (!(epaper_panel->_mirror_y))) {
        if (!(epaper_panel->_non_copy_mode)) {
            if (epaper_panel->_swap_xy) {
                memset((epaper_panel->_framebuffer), 0, 200 * 200 / 8);
                for (int i = 0; i < buffer_size * 8; i++) {
                    uint8_t bitmap_byte = ((uint8_t *) (color_data))[i / 8];
                    uint8_t bitmap_pixel = (bitmap_byte & (0x01 << (7 - (i % 8)))) ? 0x01 : 0x00;
                    (epaper_panel->_framebuffer)[((i * len_y / 8) % buffer_size) + (i / 8 / len_x)] |= (bitmap_pixel << (7 - ((i / len_x) % 8)));
                }
            } else {
                for (int i = 0; i < buffer_size; i++) {
                    (epaper_panel->_framebuffer)[i] = ((uint8_t *) (color_data))[i];
                }
            }
        }
    }
    if (((epaper_panel->_mirror_x)) && (epaper_panel->_mirror_y)) {
        if (epaper_panel->_swap_xy) {
            memset((epaper_panel->_framebuffer), 0, 200 * 200 / 8);
            for (int i = 0; i < buffer_size * 8; i++) {
                uint8_t bitmap_byte = ((uint8_t *) (color_data))[i / 8];
                uint8_t bitmap_pixel = (bitmap_byte & (0x01 << (7 - (i % 8)))) ? 0x01 : 0x00;
                (epaper_panel->_framebuffer)[buffer_size - (((i * len_y / 8) % buffer_size) + (i / 8 / len_x)) - 1] |= (bitmap_pixel << (((i / len_x) % 8)));
            }
        } else {
            for (int i = 0; i < buffer_size; i++) {
                (epaper_panel->_framebuffer)[buffer_size - i - 1] = byte_reverse(((uint8_t *)(color_data))[i]);
            }
        }
    }

    return ESP_OK;
}

static esp_err_t panel_uc8176_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    uc8176_panel_t *epaper_panel = __containerof(panel, uc8176_panel_t, base);
    epaper_panel->_invert_color = invert_color_data;
    return ESP_OK;
}
static esp_err_t panel_uc8176_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    uc8176_panel_t *epaper_panel = __containerof(panel, uc8176_panel_t, base);
    if (mirror_y) {
        if (epaper_panel->_non_copy_mode) {
            ESP_LOGE(TAG, "mirror_y is unavailable when enabling non-copy mode");
            return ESP_ERR_INVALID_ARG;
        }
    }
    epaper_panel->_mirror_x = mirror_x;
    epaper_panel->_mirror_y = mirror_y;

    return ESP_OK;
}
static esp_err_t panel_uc8176_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    uc8176_panel_t *epaper_panel = __containerof(panel, uc8176_panel_t, base);
    if (swap_axes) {
        if (epaper_panel->_non_copy_mode) {
            ESP_LOGE(TAG, "swap_xy is unavailable when enabling non-copy mode");
            return ESP_ERR_INVALID_ARG;
        }
    }
    epaper_panel->_swap_xy = swap_axes;
    return ESP_OK;
}
static esp_err_t panel_uc8176_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    uc8176_panel_t *epaper_panel = __containerof(panel, uc8176_panel_t, base);
    epaper_panel->gap_x = x_gap;
    epaper_panel->gap_y = y_gap;
    return ESP_OK;
}
static esp_err_t panel_uc8176_disp_on_off(esp_lcd_panel_t *panel, bool off)
{
    return ESP_OK;
}
esp_err_t esp_lcd_new_panel_uc8176(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "1 or more args is NULL");
    esp_lcd_uc8176_config_t *epaper_uc8176_conf = panel_dev_config->vendor_config;
    esp_err_t ret = ESP_OK;
    // --- Allocate epaper_panel memory on HEAP
    uc8176_panel_t *epaper_panel = NULL;
    epaper_panel = calloc(1, sizeof(uc8176_panel_t));
    ESP_GOTO_ON_FALSE(epaper_panel, ESP_ERR_NO_MEM, err, TAG, "no mem for epaper panel");

    // --- Construct panel & implement interface
    // defaults
    epaper_panel->_invert_color = false;
    epaper_panel->_swap_xy = false;
    epaper_panel->_mirror_x = false;
    epaper_panel->_mirror_y = false;
    epaper_panel->_framebuffer = NULL;
    epaper_panel->gap_x = 0;
    epaper_panel->gap_y = 0;
    epaper_panel->bitmap_color = UC8176_EPAPER_BITMAP_BLACK;
    epaper_panel->full_refresh = true;
    // configurations
    epaper_panel->io = io;
    epaper_panel->reset_gpio_num = panel_dev_config->reset_gpio_num;
    epaper_panel->busy_gpio_num = epaper_uc8176_conf->busy_gpio_num;
    epaper_panel->reset_level = panel_dev_config->flags.reset_active_high;
    epaper_panel->_non_copy_mode = epaper_uc8176_conf->non_copy_mode;
    // functions
    epaper_panel->base.del = panel_uc8176_del;
    epaper_panel->base.reset = panel_uc8176_reset;
    epaper_panel->base.init = panel_uc8176_init;
    epaper_panel->base.draw_bitmap = panel_uc8176_draw_bitmap;
    epaper_panel->base.invert_color = panel_uc8176_invert_color;
    epaper_panel->base.set_gap = panel_uc8176_set_gap;
    epaper_panel->base.mirror = panel_uc8176_mirror;
    epaper_panel->base.swap_xy = panel_uc8176_swap_xy;
    epaper_panel->base.disp_on_off = panel_uc8176_disp_on_off;
    *ret_panel = &(epaper_panel->base);
    // --- Init framebuffer
    if (!(epaper_panel->_non_copy_mode)) {
        // todo
        epaper_panel->_framebuffer = heap_caps_malloc(EPD_4IN2_WIDTH * EPD_4IN2_HEIGHT / 8, MALLOC_CAP_DMA);

        ESP_RETURN_ON_FALSE(epaper_panel->_framebuffer, ESP_ERR_NO_MEM, TAG, "epaper_panel_draw_bitmap allocating buffer memory err");
    }

    // --- Init GPIO
    // init RST GPIO
    if (epaper_panel->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line err");
    }
    // init BUSY GPIO
    if (epaper_panel->busy_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_INPUT,
            .pull_down_en = 0x01,
            .pin_bit_mask = 1ULL << epaper_panel->busy_gpio_num,
        };
        io_conf.intr_type = GPIO_INTR_NEGEDGE;
        ESP_LOGI(TAG, "Add handler for GPIO %d", epaper_panel->busy_gpio_num);
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for BUSY line err");
        // ESP_GOTO_ON_ERROR(gpio_isr_handler_add(epaper_panel->busy_gpio_num, epaper_driver_gpio_isr_handler, epaper_panel),
        //                   err, TAG, "configure GPIO for BUSY line err");
        // Enable GPIO intr only before refreshing, to avoid other commands caused intr trigger
        // gpio_intr_disable(epaper_panel->busy_gpio_num);
    }
    ESP_LOGD(TAG, "new epaper panel @%p", epaper_panel);
    return ret;
err:
    if (epaper_panel) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        if (epaper_uc8176_conf->busy_gpio_num >= 0) {
            gpio_reset_pin(epaper_uc8176_conf->busy_gpio_num);
        }
        free(epaper_panel);
    }
    return ret;
}
esp_err_t epaper_panel_uc8176_refresh_screen(esp_lcd_panel_t *panel)
{
    uc8176_panel_t *uc8176 = __containerof(panel, uc8176_panel_t, base);
    esp_lcd_panel_io_handle_t io = uc8176->io;
    ESP_LOGI(TAG, "epaper_panel_uc8176_refresh_screen");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, UC8176_CMD_REFRESH, NULL, 0), TAG, "send refresh command failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    panel_epaper_wait_busy(panel);
    ESP_LOGI(TAG, "epaper_panel_uc8176_refresh_screen succeed");
    return ESP_OK;
}

static inline uint8_t byte_reverse(uint8_t data)
{
    static uint8_t _4bit_reverse_lut[] =  {
        0x00, 0x08, 0x04, 0x0C, 0x02, 0x0A, 0x06, 0x0E,
        0x01, 0x09, 0x05, 0x0D, 0x03, 0x0B, 0x07, 0x0F
    };
    uint8_t result = 0x00;
    // Reverse low 4 bits
    result |= (uint8_t)((_4bit_reverse_lut[data & 0x0f]) << 4);
    // Reverse high 4 bits
    result |= _4bit_reverse_lut[data >> 4];
    return result;
}

esp_err_t EPD_WhiteScreen_ALL(esp_lcd_panel_t *panel, const unsigned char* datasBW,const unsigned char* datasRW)
{
    uc8176_panel_t *uc8176 = __containerof(panel, uc8176_panel_t, base);
    esp_lcd_panel_io_handle_t io = uc8176->io;
    ESP_LOGI(TAG, "EPD_WhiteScreen_ALL tx UC8176_CMD_TRANSFER_BW_DATA");
    // esp_lcd_panel_io_tx_color(io,  UC8176_CMD_TRANSFER_BW_DATA, datasBW, EPD_ARRAY);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io,  UC8176_CMD_TRANSFER_BW_DATA, datasBW, EPD_ARRAY), TAG, "esp_lcd_panel_io_tx_color UC8176_CMD_TRANSFER_BW_DATA failed");
    ESP_LOGI(TAG, "EPD_WhiteScreen_ALL tx UC8176_CMD_TRANSFER_RED_DATA");
    // esp_lcd_panel_io_tx_color(io,  UC8176_CMD_TRANSFER_RED_DATA, datasRW, EPD_ARRAY);
    // ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io,  UC8176_CMD_TRANSFER_RED_DATA, datasRW, EPD_ARRAY), TAG, "ERRRRRRRRRRRRRRRR");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io,  UC8176_CMD_TRANSFER_RED_DATA, datasRW, EPD_ARRAY), TAG, "esp_lcd_panel_io_tx_color UC8176_CMD_TRANSFER_RED_DATA failed");
    
    epaper_panel_uc8176_refresh_screen(panel);
    return ESP_OK;
}
// void EPD_WhiteScreen_White(esp_lcd_panel_t *panel)
// {

// }