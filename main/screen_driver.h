#ifndef _SCREEN_H_
#define _SCREEN_H_

#define LCD_HRES 466
#define LCD_VRES 466

void init_screen(void);

void draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data);

void full_white(void);
void full_black(void);
void full_red(void);
void full_default_image(void);

#endif