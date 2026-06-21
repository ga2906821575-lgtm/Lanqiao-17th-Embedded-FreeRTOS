#ifndef __FUN_H
#define __FUN_H

/* 保留的裸机业务函数（由 FreeRTOS 任务在互斥量保护下调用） */
void lcd_show(void);
void key_scan(void);
void led_show(uint8_t led,uint8_t mode);
void led_proc(void);

/*==================== 全局变量 extern 声明 ====================
 * 这些变量定义在 fun.c 中，被 freertos_app.c 等文件引用
 */

/* 传感器数据 */
extern double r37_volt;
extern double P;
extern uint32_t r39_fre;
extern double F;

/* 控制参数 */
extern uint8_t work_mode;
extern uint32_t TarD, TarD_temp;
extern uint32_t D;
extern double TarP, TarP_temp;
extern uint32_t Q;
extern double v_offset;

/* 阈值参数 */
extern double FH, FH_temp;
extern double FL, FL_temp;
extern double PL, PL_temp;
extern uint32_t DH, DH_temp;

/* 状态标志 */
extern uint32_t D_last;
extern uint8_t led4_mode;
extern uint8_t warn_flag;

/* 界面状态 */
extern uint8_t lcd_face;
extern uint8_t lcd0_first, lcd1_first, lcd2_first;
extern uint8_t lcd1_select;
extern uint8_t lcd2_select;

#endif