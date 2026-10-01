#include "servo_driver.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SERVO_GPIO_NUM 18
#define SERVO_BUTTON_GPIO 0
#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_CHANNEL LEDC_CHANNEL_1
#define SERVO_MODE LEDC_LOW_SPEED_MODE
#define SERVO_LOW_TEMP_C 22.0f
#define SERVO_HIGH_TEMP_C 28.0f
#define SERVO_LOW_ANGLE 30
#define SERVO_MID_ANGLE 90
#define SERVO_HIGH_ANGLE 150

static const char *TAG = "ts90a_servo";
static bool sg90_inited = false;
static float s_last_temperature_c = 25.0f;

void servo_update_temperature(float temperature_c)
{
    s_last_temperature_c = temperature_c;
}

void servo_init(void)
{
    if (sg90_inited) {
        return;
    }

    gpio_config_t button_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << SERVO_BUTTON_GPIO),
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_conf));

    ledc_timer_config_t timer_conf = {
        .speed_mode = SERVO_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = SERVO_TIMER,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num = SERVO_GPIO_NUM,
        .speed_mode = SERVO_MODE,
        .channel = SERVO_CHANNEL,
        .timer_sel = SERVO_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    sg90_inited = true;
    servo_set_angle(SERVO_MID_ANGLE);
    ESP_LOGI(TAG, "TS90A servo initialized on GPIO %d, button on GPIO %d", SERVO_GPIO_NUM, SERVO_BUTTON_GPIO);
}

void servo_set_angle(int angle)
{
    if (!sg90_inited) {
        servo_init();
    }

    if (angle < 0) {
        angle = 0;
    } else if (angle > 180) {
        angle = 180;
    }

    const int min_pulse_us = 500;
    const int max_pulse_us = 2500;
    const int pulse_us = min_pulse_us + (int)((max_pulse_us - min_pulse_us) * (angle / 180.0f));
    const uint32_t max_duty = (1U << 13) - 1U;
    const uint32_t duty = (uint32_t)((pulse_us * (float)max_duty) / 20000.0f);

    ESP_ERROR_CHECK(ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(SERVO_MODE, SERVO_CHANNEL));
}

#define TEST_SERVO

void servo_demo_task(void *arg)
{
    (void)arg;

    servo_init();

    while (1) {
        #ifdef TEST_SERVO
        static uint8_t counter = 0;
        counter++;
        if (counter % 2 == 0) {
            ESP_LOGI(TAG, "SERVO_LOW_ANGLE");
            servo_set_angle(SERVO_LOW_ANGLE);
        } else {
            ESP_LOGI(TAG, "SERVO_HIGH_ANGLE");
            servo_set_angle(SERVO_HIGH_ANGLE);
        }
        #else
        bool button_pressed = (gpio_get_level(SERVO_BUTTON_GPIO) == 0);

        if (button_pressed) {
            int manual_angle = ((xTaskGetTickCount() / pdMS_TO_TICKS(250)) % 2 == 0) ? 0 : 180;
            servo_set_angle(manual_angle);
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        if (s_last_temperature_c >= SERVO_HIGH_TEMP_C) {
            servo_set_angle(SERVO_HIGH_ANGLE);
        } else if (s_last_temperature_c <= SERVO_LOW_TEMP_C) {
            servo_set_angle(SERVO_LOW_ANGLE);
        } else {
            servo_set_angle(SERVO_MID_ANGLE);
        }
        #endif

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
