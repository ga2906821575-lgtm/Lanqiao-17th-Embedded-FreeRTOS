#ifndef __FUN_H
#define __FUN_H

void lcd_show(void);
void key_scan(void);
void led_show(uint8_t led,uint8_t mode);
void led_proc(void);
void adc_proc(void);
void pwm_proc(void);

extern double duty;







#endif