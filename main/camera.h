#ifndef _CAMERA_H_
#define _CAMERA_H_

#include "esp_err.h"
#include "esp_camera.h"

typedef void (*capture_callback_ptr)(camera_fb_t * fb);

esp_err_t init_camera(void);

esp_err_t camera_capture(void);

esp_err_t regist_capture_callback(capture_callback_ptr func);



#endif