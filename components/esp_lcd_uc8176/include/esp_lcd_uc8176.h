#ifndef _ESP_LCD_UC8176_H_
#define _ESP_LCD_UC8176_H_

#include <stdbool.h>
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"


typedef enum {
    UC8176_EPAPER_BITMAP_WHITE,
    UC8176_EPAPER_BITMAP_BLACK, /*!< Draw the bitmap in black */
    UC8176_EPAPER_BITMAP_RED    /*!< Draw the bitmap in red */
} esp_lcd_uc8176_bitmap_color_t;

typedef struct {
    int cmd;                /*<! The specific LCD command */
    const void *data;       /*<! Buffer that holds the command specific data */
    int data_bytes;      /*<! Size of `data` in memory, in bytes */
    unsigned int delay_ms;  /*<! Delay in milliseconds after this command */
    unsigned int wait_busy_sig;
} uc8176_lcd_cmds_sequence_t;


typedef struct {
    int busy_gpio_num;         /*!< GPIO num of the BUSY pin */
    bool non_copy_mode;        /*!< If the bitmap would be copied or not.
                                *   Image rotation and mirror is limited when enabling. */
} esp_lcd_uc8176_config_t;

typedef struct {
    const uc8176_lcd_cmds_sequence_t *init_cmds;    /*!< Pointer to initialization commands array.
                                                 *  The array should be declared as `static const` and positioned outside the function.
                                                 *  Please refer to `vendor_specific_init_default` in source file
                                                 */
    int init_cmds_size;    /*<! Number of commands in above array */
} uc8176_vendor_config_t;

typedef bool (*esp_lcd_epaper_panel_cb_t)(const esp_lcd_panel_handle_t handle, const void *edata, void *user_data);

esp_err_t esp_lcd_new_panel_uc8176(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

esp_err_t epaper_panel_uc8176_refresh_screen(esp_lcd_panel_t *panel);
esp_err_t epaper_panel_uc8176_sleep(esp_lcd_panel_t *panel);

esp_err_t EPD_WhiteScreen_ALL(esp_lcd_panel_t *panel, const unsigned char* datasBW,const unsigned char* datasRW);

#define UC8176_PANEL_BUS_SPI_CONFIG(sclk, mosi, max_trans_sz) \
    {                                                           \
        .sclk_io_num = sclk,                                    \
        .mosi_io_num = mosi,                                     \
        .max_transfer_sz = max_trans_sz,                        \
    }

/**
 * @brief LCD panel IO configuration structure
 *
 */
#define UC8176_PANEL_IO_SPI_CONFIG(cs, dc, cb, cb_ctx)             \
    {                                                           \
        .cs_gpio_num = cs,                                      \
        .dc_gpio_num = dc,                                      \
        .spi_mode = 0,                                          \
        .pclk_hz = 1 * 1000 * 1000,                            \
        .trans_queue_depth = 10,                                \
        .on_color_trans_done = cb,                              \
        .user_ctx = cb_ctx,                                     \
        .lcd_cmd_bits = 8,                                     \
        .lcd_param_bits = 8,                                    \
        .flags = {                                              \
            .sio_mode = true,                                  \
        },                                                      \
    }
    

#endif