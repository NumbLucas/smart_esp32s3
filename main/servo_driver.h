#ifndef SERVO_DRIVER_H_
#define SERVO_DRIVER_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TS90A servo is a 5V-compatible analog servo using 50Hz PWM control.
 * Power it from an external 5V source, and keep ESP32 GND connected to the servo GND.
 */
void servo_init(void);
void servo_set_angle(int angle);
void servo_update_temperature(float temperature_c);
void servo_demo_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif
