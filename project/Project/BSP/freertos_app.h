/**
 * @file freertos_app.h
 * @brief FreeRTOS 应用层任务声明与同步对象接口
 *
 * 原裸机程序改造为 FreeRTOS 多任务架构后，
 * 所有任务创建、互斥量、信号量在此声明。
 */

#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

#include "main.h"          /* HAL 类型定义（含 TIM_HandleTypeDef） */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*==================== 互斥量句柄（外部引用） ====================*/

/**
 * @brief 传感器数据互斥量
 * @note  保护全局变量：r37_volt, P, F
 *        P（压力）和 F（流量）被 Sensor任务写入，Display/LED/Control任务读取
 */
extern SemaphoreHandle_t xSensorDataMutex;

/**
 * @brief 控制参数互斥量
 * @note  保护全局变量：D, TarD, TarP, work_mode, Q, v_offset
 *        被 Key/PWM/FlowAccum/Display/LED 任务访问
 */
extern SemaphoreHandle_t xControlDataMutex;

/**
 * @brief LCD 显示互斥量
 * @note  保护 LCD 硬件访问，避免 Display任务 与 Key任务（含 LCD_Clear）冲突
 */
extern SemaphoreHandle_t xLcdMutex;

/*==================== 二值信号量句柄 ====================*/

/**
 * @brief TIM6 周期信号量（100ms）
 * @note  TIM6 中断每 100ms 发送一次，FlowAccumulation 任务接收后执行 Q 累计
 */
extern SemaphoreHandle_t xTim6Semaphore;

/**
 * @brief TIM7 周期信号量（1s）
 * @note  TIM7 中断每 1s 发送一次，PWMControl 任务接收后执行占空比调节
 */
extern SemaphoreHandle_t xTim7Semaphore;

/*==================== 任务句柄（可选，用于调试或外部控制） ====================*/

extern TaskHandle_t xSensorTaskHandle;
extern TaskHandle_t xDisplayTaskHandle;
extern TaskHandle_t xFlowAccumTaskHandle;
extern TaskHandle_t xPwmControlTaskHandle;
extern TaskHandle_t xKeyTaskHandle;
extern TaskHandle_t xLedTaskHandle;

/*==================== 公共接口 ====================*/

/**
 * @brief 初始化 FreeRTOS 内核对象并创建所有任务
 * @note  在 main() 中调用，必须在 vTaskStartScheduler() 之前执行
 */
void FreeRTOS_App_Init(void);

/**
 * @brief TIM6/TIM7 中断回调（替代原 fun.c 中的回调）
 * @note  仅发送二值信号量，不做实际业务逻辑，确保 ISR 简短
 *        使用 HAL 标准回调名，HAL 库会自动调用
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

/**
 * @brief TIM3 输入捕获中断回调
 * @note  精简 ISR，仅更新频率原始值 r39_fre（uint32_t，原子写入）
 *        使用 HAL 标准回调名，HAL 库会自动调用
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim);

#endif /* FREERTOS_APP_H */
