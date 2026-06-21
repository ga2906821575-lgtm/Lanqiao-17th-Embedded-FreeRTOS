# FreeRTOS 改造实战手册

> **项目**: 第17届蓝桥杯嵌入式省赛真题 — FreeRTOS多任务改造  
> **平台**: STM32G431RBT6 (Cortex-M4F, 80MHz)  
> **创建日期**: 2026-06-20  
> **状态**: 持续更新中

---

## 目录

1. [项目架构总览](#1-项目架构总览)
2. [CubeMX 配置要点](#2-cubemx-配置要点)
3. [编译报错速查表](#3-编译报错速查表)
4. [关键设计决策](#4-关键设计决策)
5. [文件保护机制](#5-文件保护机制)
6. [FreeRTOS 配置详解](#6-freertos-配置详解)
7. [调试技巧](#7-调试技巧)
8. [后续优化方向](#8-后续优化方向)

---

## 1. 项目架构总览

### 1.1 任务划分

| 任务名 | 优先级 | 周期/触发 | 职责 |
|--------|--------|-----------|------|
| Task_Sensor | 3 | 50ms | ADC采集 + 压力/流量计算 |
| Task_PWMControl | 4 | TIM7信号量(1s) | 占空比调节（手动/自动模式） |
| Task_KeyInput | 3 | 50ms | 按键扫描 + 参数修改 |
| Task_FlowAccum | 3 | TIM6信号量(100ms) | 累计流量Q计算 |
| Task_Display | 2 | 100ms | LCD界面刷新 |
| Task_LEDUpdate | 1 | 100ms | LED状态指示 |

### 1.2 同步对象

| 对象 | 类型 | 保护/同步的数据 |
|------|------|----------------|
| xSensorDataMutex | 互斥量 | P, F, r37_volt |
| xControlDataMutex | 互斥量 | D, TarD, TarP, Q, work_mode, v_offset |
| xLcdMutex | 互斥量 | LCD硬件访问 |
| xTim6Semaphore | 二值信号量 | TIM6 → FlowAccum任务 |
| xTim7Semaphore | 二值信号量 | TIM7 → PWMControl任务 |

### 1.3 加锁顺序（死锁防护）

所有任务遵循**统一顺序**：
```
1. xSensorDataMutex → 2. xControlDataMutex → 3. xLcdMutex
```

---

## 2. CubeMX 配置要点

### 2.1 FREERTOS 配置参数

| 参数名 | 设置值 | 说明 |
|--------|--------|------|
| Interface | **CMSIS-RTOS_V2** | 推荐，API更现代 |
| USE_PREEMPTION | Enabled | 抢占式调度 |
| TICK_RATE_HZ | **1000** | 1kHz系统节拍 |
| MAX_PRIORITIES | 56 (默认) | 优先级数量 |
| TOTAL_HEAP_SIZE | **10240** (10KB) | 堆内存 |
| USE_MUTEXES | Enabled | 互斥量支持 |
| USE_COUNTING_SEMAPHORES | Enabled | 计数信号量 |
| USE_TASK_NOTIFICATIONS | Enabled | 任务通知 |
| CHECK_FOR_STACK_OVERFLOW | Method 2 | 栈溢出检测 |

### 2.2 重要警告处理

#### 警告："When RTOS is used, it is strongly recommended to use a HAL timebase source other than the Systick"

- **处理方式**: 点 **Yes** 继续生成，**不改配置**
- **原因**: FreeRTOS 已接管 SysTick，并在 `xPortSysTickHandler` 中同时调用 `HAL_IncTick()`，两者不会冲突
- **这是STM32 FreeRTOS的标准配置**，99%的项目都这样配

### 2.3 中断优先级设置

在 **System Core → NVIC** 中：

| 中断 | Preemption Priority |
|------|---------------------|
| TIM3 global interrupt | **5** |
| TIM6 global and DAC1/3 | **5** |
| TIM7 global interrupt | **5** |

**为什么必须是5？**
- `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`
- 优先级数值 **< 5** 的中断不能调用 `FromISR` API（如 `xSemaphoreGiveFromISR`）
- 违反此规则会导致系统崩溃

---

## 3. 编译报错速查表

### 3.1 cannot open source input file "FreeRTOS.h" / "portmacro.h"

| 项目 | 内容 |
|------|------|
| **现象** | `error: #5: cannot open source input file "FreeRTOS.h"` 或 `portmacro.h` |
| **原因** | Keil 的 Include Paths 中缺少 FreeRTOS 头文件路径 |
| **解决** | 在工程选项 → C/C++ → Include Paths 中**添加以下三条路径**：<br>1. `..\Middlewares\Third_Party\FreeRTOS\Source\include`<br>2. `..\Middlewares\Third_Party\FreeRTOS\Source\CMSIS_RTOS_V2`<br>3. `..\Middlewares\Third_Party\FreeRTOS\Source\portable\RVDS\ARM_CM4F` |
| **根因** | CubeMX "Library creation and copy have a problem" 导致路径未完整写入 |

> **注意**：`portmacro.h` 位于 `portable\RVDS\ARM_CM4F\` 目录下，这是 Cortex-M4F 的移植层头文件，容易遗漏。

### 3.2 cannot open source input file "cmsis_os.h"

| 项目 | 内容 |
|------|------|
| **现象** | `error: #5: cannot open source input file "cmsis_os.h"` |
| **原因** | 缺少 CMSIS-RTOS V2 包装层头文件路径 |
| **解决** | 添加路径：`..\Middlewares\Third_Party\FreeRTOS\Source\CMSIS_RTOS_V2` |

### 3.3 multiply defined SVC_Handler / PendSV_Handler / SysTick_Handler

| 项目 | 内容 |
|------|------|
| **现象** | Linker 报错：多重定义 |
| **原因** | `stm32g4xx_it.c` 和 FreeRTOS `port.c` 同时定义了这三个 Handler |
| **解决** | 删除 `stm32g4xx_it.c` 中的 `SVC_Handler`、`PendSV_Handler`、`SysTick_Handler` |
| **注意** | CubeMX 最新版本生成的代码已正确处理（保留 `SysTick_Handler` 并调用 `xPortSysTickHandler`） |

### 3.4 undefined reference to xTaskCreate / xSemaphoreCreateMutex

| 项目 | 内容 |
|------|------|
| **现象** | Linker 报错：找不到 FreeRTOS API 函数 |
| **原因** | `tasks.c`、`queue.c` 等 FreeRTOS 内核源文件未添加到 Keil 工程 |
| **解决** | 在 Keil 中新建分组，添加：tasks.c, queue.c, list.c, timers.c, heap_4.c, port.c |

### 3.5 configASSERT failed / 程序卡在 vTaskStartScheduler 后不动

| 项目 | 内容 |
|------|------|
| **现象** | 调度器启动后，程序进入 `Error_Handler` 或卡死 |
| **原因1** | `configTOTAL_HEAP_SIZE` 太小，任务创建失败 |
| **解决1** | 增大 `TOTAL_HEAP_SIZE`（如 15KB） |
| **原因2** | 中断优先级设置错误（< 5 的中断调用了 FromISR API） |
| **解决2** | 检查 NVIC 中 TIM3/TIM6/TIM7 的优先级是否 >= 5 |

### 3.6 warning: last line of file ends without a newline

| 项目 | 内容 |
|------|------|
| **现象** | `warning: #1-D: last line of file ends without a newline` |
| **原因** | 文件最后一行没有换行符（C标准问题） |
| **解决** | 在文件末尾按一下 Enter，保存 |
| **影响** | 无害，可忽略 |

---

## 4. 关键设计决策

### 4.1 为什么 ISR 只做最少工作？

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    BaseType_t xWoken = pdFALSE;
    if (htim->Instance == TIM6)
        xSemaphoreGiveFromISR(xTim6Semaphore, &xWoken);
    else if (htim->Instance == TIM7)
        xSemaphoreGiveFromISR(xTim7Semaphore, &xWoken);
    portYIELD_FROM_ISR(xWoken);
}
```

**设计原则**（RTOS 黄金法则）：
- ISR 要尽可能短（微秒级）
- 复杂计算放到任务中做（毫秒级）
- 原因：ISR 打断所有任务，执行时间越长，系统实时性越差

### 4.2 为什么用互斥量而不是关中断？

| 方案 | 优点 | 缺点 |
|------|------|------|
| **关中断** (`__disable_irq`) | 简单、绝对安全 | 影响系统响应，RTOS 下不推荐 |
| **互斥量** (`xSemaphoreTake`) | 只阻塞竞争任务，不影响中断 | 有开销，可能优先级反转 |

FreeRTOS 互斥量自带**优先级继承**机制，能缓解优先级反转问题。

### 4.3 为什么 Sensor 和 PWMControl 优先级高？

```
PWMControl (优先级4) > Sensor/Key/Flow (优先级3) > Display (优先级2) > LED (优先级1)
```

- **PWMControl (4)**: 闭环控制，实时性要求最高，延迟会影响控制稳定性
- **Sensor/Flow/Key (3)**: 数据采集和输入，需要及时响应
- **Display (2)**: LCD 刷新允许 10~20ms 的延迟，人眼无感知
- **LED (1)**: 状态指示最不敏感，优先级最低

---

## 5. 文件保护机制

### 5.1 CubeMX USER CODE 区域

CubeMX 重新生成代码时，**只保留** `/* USER CODE BEGIN xxx */` 和 `/* USER CODE END xxx */` 之间的内容。

**必须在 USER CODE 区域内写的代码**：

| 文件 | 代码位置 | 内容 |
|------|---------|------|
| `main.c` | `/* USER CODE BEGIN 2 */` | 外设启动、LCD初始化、中断优先级 |
| `main.c` | `/* USER CODE BEGIN Includes */` | 自定义头文件包含 |
| `app_freertos.c` | `/* USER CODE BEGIN RTOS_THREADS */` | `FreeRTOS_App_Init()` 调用 |
| `app_freertos.c` | `/* USER CODE BEGIN Includes */` | `freertos_app.h` 包含 |

### 5.2 CubeMX 不会覆盖的文件

以下文件 CubeMX 不知道，**永远不会被覆盖**：
- `BSP/freertos_app.c`
- `BSP/freertos_app.h`
- `BSP/fun.c` / `BSP/fun.h`（除非在 CubeMX 中修改了对应外设配置）

### 5.3 重新生成代码后的检查清单

每次 GENERATE CODE 后，确认：
- [ ] `main.c` 的 `USER CODE BEGIN 2` 区域代码还在
- [ ] `app_freertos.c` 的 `FreeRTOS_App_Init()` 调用还在
- [ ] `stm32g4xx_it.c` 的 SysTick_Handler 正确（调用 xPortSysTickHandler）
- [ ] Keil 工程中 `BSP/freertos_app.c` 没有丢失

---

## 6. FreeRTOS 配置详解

### 6.1 FreeRTOSConfig.h 关键宏

```c
#define configCPU_CLOCK_HZ                  (SystemCoreClock)  /* 80MHz */
#define configTICK_RATE_HZ                  1000                /* 1ms */
#define configTOTAL_HEAP_SIZE               10240               /* 10KB */
#define configMAX_PRIORITIES                56                  /* 优先级数 */

/* 中断优先级 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY       15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5

/* Handler 映射 */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
```

### 6.2 堆管理选择

| 算法 | 文件 | 特点 | 适用场景 |
|------|------|------|---------|
| heap_1 | heap_1.c | 只分配不释放，无碎片 | 简单应用，无动态删除任务 |
| heap_2 | heap_2.c | 最佳匹配，有碎片 | 不推荐 |
| **heap_4** | **heap_4.c** | **首次适配 + 合并相邻空闲块** | **推荐，碎片少** |
| heap_5 | heap_5.c | 跨多个内存区域 | 有外部RAM时 |

**本项目使用 heap_4**。

### 6.3 内存占用估算

| 项目 | 大小 |
|------|------|
| FreeRTOS 内核代码 | ~8~10 KB Flash |
| 内核数据 + 任务TCB | ~1~2 KB RAM |
| 6个任务栈 (256 words × 4 bytes × 6) | ~6 KB RAM |
| 互斥量/信号量 | ~几百字节 RAM |
| **总计** | **Flash: ~10KB, RAM: ~8~10KB** |

STM32G431 (128KB Flash, 32KB RAM) 完全够用。

---

## 7. 调试技巧

### 7.1 查看任务栈使用水位

在任务中加入：
```c
UBaseType_t uxHighWaterMark;
uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
// uxHighWaterMark 是剩余栈空间（单位：words）
// 如果接近0，说明栈设置太小，需要增大
```

需要在 `FreeRTOSConfig.h` 中启用：
```c
#define INCLUDE_uxTaskGetStackHighWaterMark  1
```

### 7.2 查看堆剩余空间

```c
size_t xFreeHeapSpace = xPortGetFreeHeapSize();
size_t xMinEverFreeHeap = xPortGetMinimumEverFreeHeapSize();
```

### 7.3 使用 Keil RTX/Task 视图

Keil 调试时，在 **Debug → OS Support → RTX Tasks and System** 可以查看：
- 各任务状态（Running/Blocked/Ready）
- 任务优先级
- 栈使用情况

### 7.4 断言定位问题

如果触发了 `configASSERT`，程序会关中断并死循环。在 `Error_Handler` 中设置断点，查看调用栈定位问题。

---

## 8. 后续优化方向

### 8.1 短期优化（1~2天）

- [ ] 用 **消息队列** 替代 key_scan 中的全局变量，实现按键事件驱动
- [ ] 在 **空闲任务钩子** 中添加看门狗喂狗
- [ ] 用 **任务通知** 替代 LED4 的 led4_mode 全局变量

### 8.2 中期优化（1周内）

- [ ] 自动模式从 **Bang-Bang 控制** 升级为 **PID 控制**
- [ ] ADC 从软件触发改为 **DMA + 定时器触发**
- [ ] 添加 **Flash 模拟 EEPROM**，保存参数掉电不丢失

### 8.3 长期优化

- [ ] 添加 **串口调试 Shell**，通过命令行查看/修改参数
- [ ] 接入 **上位机通信**（Modbus RTU 或自定义协议）
- [ ] 添加 **看门狗 + 异常复位记录**

---

## 附录：快速命令参考

### FreeRTOS API 速查

```c
/* 任务 */
xTaskCreate(TaskFunc, "Name", StackSize, Param, Priority, &Handle);
vTaskDelete(Handle);
vTaskDelay(pdMS_TO_TICKS(100));           /* 相对延时 */
vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(100));  /* 绝对延时 */

/* 互斥量 */
xSemaphoreCreateMutex();
xSemaphoreTake(Mutex, portMAX_DELAY);      /* 永久等待 */
xSemaphoreTake(Mutex, pdMS_TO_TICKS(100)); /* 超时100ms */
xSemaphoreGive(Mutex);

/* 二值信号量 */
xSemaphoreCreateBinary();
xSemaphoreGive(Sem);                       /* 任务中 */
xSemaphoreGiveFromISR(Sem, &xWoken);     /* ISR中 */
xSemaphoreTake(Sem, portMAX_DELAY);

/* 任务通知 */
xTaskNotifyGive(Handle);
ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
```

### 3.7 A1586E: Bad operand types for operator (  （port.c 汇编报错）

| 项目 | 内容 |
|------|------|
| **现象** | `port.c(465): error: A1586E: Bad operand types (UnDefOT, Constant) for operator (` |
| **原因** | `FreeRTOSConfig.h` 中 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 定义为带括号的表达式 `(5 << (8 - 4))`，在 Keil `__asm` 函数中被预处理器展开后，汇编器看到 `#(80)`，不认识带括号的立即数 |
| **解决** | 修改 `FreeRTOSConfig.h`，将宏改为纯数字：<br>`#define configMAX_SYSCALL_INTERRUPT_PRIORITY 0x50` |
| **计算** | `(configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))` = `(5 << 4)` = `80` = `0x50` |

> **注意**：这是 Keil AC5 编译器的 `__asm` 函数与 C 预处理器交互的已知问题。AC6 编译器无此问题。  
*最后更新: 2026-06-20*  
*维护方式: 遇到新问题随时追加到对应章节*
