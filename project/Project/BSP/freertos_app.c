/**
 * @file freertos_app.c
 * @brief FreeRTOS 多任务应用层实现
 *
 * 将原裸机程序的 while(1) 超级循环 + 定时器中断，
 * 重构为 6 个 FreeRTOS 任务 + 互斥量 + 二值信号量的多任务架构。
 *
 * @author 基于第17届蓝桥杯嵌入式省赛真题改造
 */

#include "freertos_app.h"
#include "headfile.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*==================== 全局变量句柄定义 ====================*/

SemaphoreHandle_t xSensorDataMutex;
SemaphoreHandle_t xControlDataMutex;
SemaphoreHandle_t xLcdMutex;
SemaphoreHandle_t xTim6Semaphore;
SemaphoreHandle_t xTim7Semaphore;

TaskHandle_t xSensorTaskHandle;
TaskHandle_t xDisplayTaskHandle;
TaskHandle_t xFlowAccumTaskHandle;
TaskHandle_t xPwmControlTaskHandle;
TaskHandle_t xKeyTaskHandle;
TaskHandle_t xLedTaskHandle;

/*==================== 私有任务函数声明 ====================*/

static void Task_Sensor(void *pvParameters);
static void Task_Display(void *pvParameters);
static void Task_FlowAccumulation(void *pvParameters);
static void Task_PWMControl(void *pvParameters);
static void Task_KeyInput(void *pvParameters);
static void Task_LEDUpdate(void *pvParameters);

/*==================== 公共接口实现 ====================*/

/**
 * @brief 初始化所有 FreeRTOS 内核对象和任务
 */
void FreeRTOS_App_Init(void)
{
    /* 1. 创建互斥量 */
    xSensorDataMutex = xSemaphoreCreateMutex();
    xControlDataMutex = xSemaphoreCreateMutex();
    xLcdMutex = xSemaphoreCreateMutex();

    configASSERT(xSensorDataMutex != NULL);
    configASSERT(xControlDataMutex != NULL);
    configASSERT(xLcdMutex != NULL);

    /* 2. 创建二值信号量 */
    xTim6Semaphore = xSemaphoreCreateBinary();
    xTim7Semaphore = xSemaphoreCreateBinary();

    configASSERT(xTim6Semaphore != NULL);
    configASSERT(xTim7Semaphore != NULL);

    /* 3. 创建任务
     *    栈大小 256 words = 1024 bytes（足够 sprintf 使用）
     *    优先级设计：
     *      控制类(PWM) = 4（最高，闭环控制实时性要求高）
     *      采集类(Sensor/Key/Flow) = 3（中高）
     *      显示类(Display) = 2（中，允许轻微延迟）
     *      指示类(LED) = 1（最低）
     */
    xTaskCreate(Task_Sensor, "Sensor", 256, NULL, 3, &xSensorTaskHandle);
    xTaskCreate(Task_Display, "Display", 256, NULL, 2, &xDisplayTaskHandle);
    xTaskCreate(Task_FlowAccumulation, "FlowAccum", 256, NULL, 3, &xFlowAccumTaskHandle);
    xTaskCreate(Task_PWMControl, "PWMControl", 256, NULL, 4, &xPwmControlTaskHandle);
    xTaskCreate(Task_KeyInput, "KeyInput", 256, NULL, 3, &xKeyTaskHandle);
    xTaskCreate(Task_LEDUpdate, "LED", 256, NULL, 1, &xLedTaskHandle);
}

/*==================== 中断回调（替代原 fun.c 中的 HAL 回调） ====================*/

/**
 * @brief TIM6/TIM7 周期中断回调（ISR 中只发信号量，不执行业务逻辑）
 *
 * @note 设计原则：ISR 尽可能简短。
 *       TIM6(100ms) 通知 FlowAccumulation 任务做 Q 累计；
 *       TIM7(1s)   通知 PWMControl 任务做占空比调节。
 *
 *       使用 HAL 标准回调名，HAL_TIM_IRQHandler() 会自动调用此函数。
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (htim->Instance == TIM6)
    {
        xSemaphoreGiveFromISR(xTim6Semaphore, &xHigherPriorityTaskWoken);
    }
    else if (htim->Instance == TIM7)
    {
        xSemaphoreGiveFromISR(xTim7Semaphore, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief TIM3 输入捕获中断回调（精简 ISR）
 *
 * @note 仅计算频率并写入 r39_fre（uint32_t，32位 ARM 上单指令原子写入）。
 *       流量 F 的换算放到 Sensor 任务中，避免在 ISR 中处理 double 类型。
 *
 *       使用 HAL 标准回调名，HAL_TIM_IRQHandler() 会自动调用此函数。
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
        uint32_t val = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        TIM3->CNT = 0;
        r39_fre = 80000000 / (80 * val);
    }
}

/*==================== 任务定义 ====================*/

/**
 * @brief 传感器采集任务
 * @param pvParameters 未使用
 *
 * @details
 *   - 周期：50 ms（使用 vTaskDelayUntil 保证精确周期）
 *   - 优先级：3（中高）
 *   - 职责：ADC 采样 → 计算压力 P → 从 r39_fre 计算流量 F
 *   - 同步：通过 xSensorDataMutex 保护 P / F / r37_volt
 */
static void Task_Sensor(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    (void)pvParameters;

    while (1)
    {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));

        /* 1. ADC 软件触发采集 */
        HAL_ADC_Start(&hadc2);
        HAL_ADC_PollForConversion(&hadc2, 10);
        uint32_t adc_val = HAL_ADC_GetValue(&hadc2);
        double volt = 3.3 * adc_val / 4095.0;

        /* 2. 读取频率（TIM3 中断原子写入，无需加锁） */
        uint32_t local_fre = r39_fre;

        /* 3. 计算流量 F */
        double flow;
        if (local_fre >= 800 && local_fre <= 8000)
            flow = local_fre / 200.0;
        else if (local_fre < 800)
            flow = 0;
        else
            flow = 40;

        /* 4. 计算压力 P */
        double press;
        if (volt >= v_offset)
            press = (volt - v_offset) / (3.3 - v_offset) * 10.0;
        else
            press = 0;

        /* 5. 写入共享变量（互斥量保护） */
        xSemaphoreTake(xSensorDataMutex, portMAX_DELAY);
        r37_volt = volt;
        P = press;
        F = flow;
        xSemaphoreGive(xSensorDataMutex);
    }
}

/**
 * @brief LCD 显示刷新任务
 * @param pvParameters 未使用
 *
 * @details
 *   - 周期：100 ms
 *   - 优先级：2（中）
 *   - 职责：调用 lcd_show() 刷新界面
 *   - 同步：先获取 xSensorDataMutex 再获取 xControlDataMutex，
 *          与 PWMControl 任务保持相同加锁顺序（先 Sensor 后 Control），
 *          避免死锁。
 */
static void Task_Display(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    (void)pvParameters;

    while (1)
    {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));

        /* 按统一顺序加锁：先 Sensor → 后 Control */
        xSemaphoreTake(xSensorDataMutex, portMAX_DELAY);
        xSemaphoreTake(xControlDataMutex, portMAX_DELAY);
        xSemaphoreTake(xLcdMutex, portMAX_DELAY);

        lcd_show();

        xSemaphoreGive(xLcdMutex);
        xSemaphoreGive(xControlDataMutex);
        xSemaphoreGive(xSensorDataMutex);
    }
}

/**
 * @brief 流量累计任务
 * @param pvParameters 未使用
 *
 * @details
 *   - 触发：TIM6 二值信号量（100 ms 周期）
 *   - 优先级：3（中高）
 *   - 职责：接收信号后读取流量 F，更新累计流量 Q
 *   - 同步：读取 F 时用 xSensorDataMutex，更新 Q 时用 xControlDataMutex
 */
static void Task_FlowAccumulation(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        /* 阻塞等待 TIM6 中断发出的信号 */
        xSemaphoreTake(xTim6Semaphore, portMAX_DELAY);

        /* 读取流量（只读，加锁后复制到局部变量） */
        xSemaphoreTake(xSensorDataMutex, portMAX_DELAY);
        double local_F = F;
        xSemaphoreGive(xSensorDataMutex);

        /* 更新累计流量 Q */
        xSemaphoreTake(xControlDataMutex, portMAX_DELAY);
        Q += (uint32_t)(local_F - 0.5);
        xSemaphoreGive(xControlDataMutex);
    }
}

/**
 * @brief PWM 控制任务
 * @param pvParameters 未使用
 *
 * @details
 *   - 触发：TIM7 二值信号量（1 s 周期）
 *   - 优先级：4（最高）
 *   - 职责：占空比调节（手动模式趋近 TarD，自动模式 Bang-Bang 控制）
 *   - 同步：先读取 P（xSensorDataMutex），再修改 D（xControlDataMutex），
 *          不持有双锁，避免死锁。
 */
static void Task_PWMControl(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        /* 阻塞等待 TIM7 中断发出的信号 */
        xSemaphoreTake(xTim7Semaphore, portMAX_DELAY);

        /* 手动模式：读取 TarD（在 ControlData 中） */
        xSemaphoreTake(xControlDataMutex, portMAX_DELAY);
        uint32_t local_TarD = TarD;
        uint8_t local_mode = work_mode;
        xSemaphoreGive(xControlDataMutex);

        if (local_mode == 0)  /* 手动模式 */
        {
            xSemaphoreTake(xControlDataMutex, portMAX_DELAY);
            if (D > local_TarD) D--;
            else if (D < local_TarD) D++;
            xSemaphoreGive(xControlDataMutex);
        }
        else  /* 自动模式 */
        {
            /* 读取当前压力 P */
            xSemaphoreTake(xSensorDataMutex, portMAX_DELAY);
            double local_P = P;
            xSemaphoreGive(xSensorDataMutex);

            /* 读取目标压力 TarP */
            xSemaphoreTake(xControlDataMutex, portMAX_DELAY);
            double local_TarP = TarP;
            xSemaphoreGive(xControlDataMutex);

            /* Bang-Bang 控制 */
            xSemaphoreTake(xControlDataMutex, portMAX_DELAY);
            if (local_P > (local_TarP + 0.5)) {
                if (D > 5) D--;
            }
            else if (local_P < (local_TarP - 0.5)) {
                if (D < 95) D++;
            }
            xSemaphoreGive(xControlDataMutex);
        }

        /* LED4 占空比变化指示 */
        xSemaphoreTake(xControlDataMutex, portMAX_DELAY);
        if (D_last == D)
            led4_mode = 0;
        else
            led4_mode = 1;
        D_last = D;

        /* 更新 PWM 硬件寄存器 */
        TIM2->CCR2 = (uint32_t)((D / 100.0) * (TIM2->ARR + 1));

        xSemaphoreGive(xControlDataMutex);
    }
}

/**
 * @brief 按键输入任务
 * @param pvParameters 未使用
 *
 * @details
 *   - 周期：50 ms
 *   - 优先级：3（中高）
 *   - 职责：按键扫描、界面切换、参数调节
 *   - 同步：key_scan() 会修改大量全局变量，调用前获取 Sensor+Control 锁
 */
static void Task_KeyInput(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    (void)pvParameters;

    while (1)
    {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));

        /* 统一加锁顺序：先 Sensor → 后 Control → 再 LCD */
        xSemaphoreTake(xSensorDataMutex, portMAX_DELAY);
        xSemaphoreTake(xControlDataMutex, portMAX_DELAY);
        xSemaphoreTake(xLcdMutex, portMAX_DELAY);

        key_scan();

        xSemaphoreGive(xLcdMutex);
        xSemaphoreGive(xControlDataMutex);
        xSemaphoreGive(xSensorDataMutex);
    }
}

/**
 * @brief LED 指示任务
 * @param pvParameters 未使用
 *
 * @details
 *   - 周期：100 ms
 *   - 优先级：1（最低）
 *   - 职责：刷新 LED 状态（报警、异常、调节指示）
 */
static void Task_LEDUpdate(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    (void)pvParameters;

    while (1)
    {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));

        xSemaphoreTake(xSensorDataMutex, portMAX_DELAY);
        xSemaphoreTake(xControlDataMutex, portMAX_DELAY);

        led_proc();

        xSemaphoreGive(xControlDataMutex);
        xSemaphoreGive(xSensorDataMutex);
    }
}
