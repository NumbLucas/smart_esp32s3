
#include "lv_display.h"
#include "esp_log.h"
#include "screen_driver.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define MY_DISP_HOR_RES LCD_HRES
#define MY_DISP_VER_RES LCD_VRES

#define BYTE_PER_PIXEL 2
#define LVGL_TIMER_TASK_STACK 1024

static char* TAG = "LV_DISPLAY";

static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);
static void lvgl_timer_task(void* param);

void disp_init(void)
{
    init_screen();
    
    #if 0
    lv_init();

    lv_tick_set_cb(xTaskGetTickCount);

    lv_display_t * disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
    lv_display_set_flush_cb(disp, disp_flush);
    static uint8_t buf_1_1[MY_DISP_HOR_RES * 10 * BYTE_PER_PIXEL];            /*A buffer for 10 rows*/
    lv_display_set_buffers(disp, buf_1_1, NULL, sizeof(buf_1_1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    xTaskCreate(lvgl_timer_task, "lvgl_timer_task", LVGL_TIMER_TASK_STACK, NULL, configMAX_PRIORITIES - 2, NULL);
    #endif
}

static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    // todo
    draw_bitmap(area->x1, area->y1, area->x2, area->y2, px_map);
    lv_disp_flush_ready(disp);
}

void lvgl_timer_task(void* param)
{   
    while(1){
        ESP_LOGI(TAG, "lvgl_timer_task");
        // lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

}