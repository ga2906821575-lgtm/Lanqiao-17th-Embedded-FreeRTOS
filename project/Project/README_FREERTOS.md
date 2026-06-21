# FreeRTOS 改造版项目说明

> **作者**: 孙苏明  
> **平台**: STM32G431RBT6 (Cortex-M4F, 80MHz)  
> **架构**: FreeRTOS 多任务系统（6 任务 + 3 互斥量 + 2 二值信号量）

## 项目概述

本项目基于**第17届蓝桥杯嵌入式省赛真题**（压力/流量监测与控制系统），将原裸机 `while(1)` 超级循环架构，重构为 **FreeRTOS 多任务架构**。

## 架构对比

| 维度 | 原裸机版本 | FreeRTOS版本 |
|------|-----------|-------------|
| 主循环 | `while(1)` 轮询 | 6个独立任务 |
| 延时方式 | `HAL_GetTick()` 轮询 | `vTaskDelayUntil()` 精确周期 |
| 定时中断 | ISR中直接执行业务逻辑 | ISR只发信号量，任务中执行业务 |
| 数据同步 | 无保护（全局变量裸奔） | 互斥量(Mutex)保护 |
| 代码耦合 | 高（中断回调写死在fun.c） | 低（任务独立，通过同步对象通信） |
| 扩展性 | 差（加功能要改主循环时序） | 好（加任务不影响现有任务） |

## 文件变更清单

### 新增文件

| 文件 | 说明 |
|------|------|
| `BSP/freertos_app.h` | 任务、互斥量、信号量声明 |
| `BSP/freertos_app.c` | 6个任务实现 + HAL中断回调 |
| `Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOSConfig.h` | FreeRTOS内核配置 |

### 修改文件

| 文件 | 修改内容 |
|------|---------|
| `Core/Src/main.c` | 删除裸机while循环，改为创建任务+启动调度器 |
| `Core/Src/stm32g4xx_it.c` | 删除SysTick/PendSV/SVC Handler（由FreeRTOS接管） |
| `BSP/fun.c` | 移除`HAL_GetTick()`轮询；`#if 0`包裹已迁移的回调函数 |
| `BSP/fun.h` | 清理已迁移函数的声明 |

## 任务设计

```
┌─────────────────────────────────────────────────────────────┐
│                     FreeRTOS 调度器                          │
├─────────────┬─────────────┬─────────────┬───────────────────┤
│  Task_Sensor │ Task_Control │ Task_Input  │   Task_Display    │
│  优先级: 3   │  优先级: 4   │ 优先级: 3   │    优先级: 2      │
│  周期: 50ms  │ 触发: 信号量 │ 周期: 50ms  │    周期: 100ms    │
│  ADC采集     │  PWM调节     │ 按键扫描    │    LCD刷新        │
│  压力/流量   │  Q累计       │ 参数修改    │                   │
└─────────────┴─────────────┴─────────────┴───────────────────┘
├─────────────────────┬─────────────────────┐
│  Task_FlowAccum     │    Task_LED         │
│  优先级: 3          │    优先级: 1        │
│  触发: TIM6信号量   │    周期: 100ms      │
│  Q累计              │    LED刷新          │
└─────────────────────┴─────────────────────┘
```

### 优先级设计思路

| 优先级 | 任务 | 设计理由 |
|--------|------|---------|
| 4 (最高) | PWMControl | 闭环控制，实时性要求最高 |
| 3 (中高) | Sensor / FlowAccum / KeyInput | 数据采集和输入，需要及时响应 |
| 2 (中) | Display | LCD刷新允许轻微延迟 |
| 1 (最低) | LED | 状态指示，最不敏感 |

### 同步对象

| 对象 | 类型 | 保护/同步的数据 | 使用位置 |
|------|------|----------------|---------|
| xSensorDataMutex | 互斥量 | P, F, r37_volt | Sensor(写) / Display/PWM/LED(读) |
| xControlDataMutex | 互斥量 | D, TarD, TarP, Q, work_mode | PWM/FlowAccum/Key/Display/LED |
| xLcdMutex | 互斥量 | LCD硬件 | Display(写) / Key(清屏) |
| xTim6Semaphore | 二值信号量 | TIM6→FlowAccum | TIM6 ISR发，FlowAccum收 |
| xTim7Semaphore | 二值信号量 | TIM7→PWMControl | TIM7 ISR发，PWMControl收 |

**死锁防护**：所有任务遵循统一加锁顺序：**先 SensorData → 后 ControlData → 再 LCD**。

## FreeRTOS 源文件集成步骤

本项目提供了所有**本地代码文件**的改造，但**FreeRTOS内核源文件**需要你手动添加到Keil工程中。有两种方式：

### 方式一：推荐——用 STM32CubeMX 重新生成（最简单）

1. 用 STM32CubeMX 打开 `Project.ioc`
2. 在 **Middleware** 标签页中勾选 **FREERTOS**，选择 **CMSIS-RTOS_V2**
3. 点击 **GENERATE CODE**
4. CubeMX 会自动：
   - 下载并放置 FreeRTOS 源文件到 `Middlewares/Third_Party/FreeRTOS/`
   - 修改 Keil 工程文件（.uvprojx），添加所有源文件
   - 配置好头文件路径
   - 修改 `stm32g4xx_it.c`（但我们的版本已经优化过，可覆盖）
5. 用 CubeMX 生成的 `Middlewares/` 目录**替换**本项目中的同名目录
6. **保留**本项目中自定义的文件：
   - `BSP/freertos_app.h`
   - `BSP/freertos_app.c`
   - `BSP/fun.c`（已改造）
   - `BSP/fun.h`（已改造）
   - `Core/Src/main.c`（已改造）
   - `Core/Src/stm32g4xx_it.c`（已改造）
7. 打开 Keil 工程，编译

### 方式二：手动添加 FreeRTOS 源文件

如果你不想用 CubeMX，可以手动从 **STM32CubeG4 固件包**或 **FreeRTOS 官网**获取源文件。

#### 需要添加的源文件清单

将以下文件复制到 `Middlewares/Third_Party/FreeRTOS/Source/` 目录下：

**核心文件（必需）**
```
Source/
├── tasks.c                 # 任务管理
├── queue.c                 # 队列、信号量、互斥量
├── list.c                  # 链表（调度器核心）
├── include/                # 头文件目录
│   ├── FreeRTOS.h
│   ├── task.h
│   ├── queue.h
│   ├── semphr.h
│   ├── list.h
│   ├── projdefs.h
│   ├── portable.h
│   ├── deprecated_definitions.h
│   └── mpu_wrappers.h
└── portable/
    ├── MemMang/
    │   └── heap_4.c        # 内存管理算法4（推荐）
    └── RVDS/
        └── ARM_CM4F/
            └── port.c      # Cortex-M4F 移植层（含FPU支持）
```

**可选文件**
```
Source/
├── timers.c                # 软件定时器（本项目未使用）
├── event_groups.c          # 事件组（本项目未使用）
└── stream_buffer.c         # 流缓冲区（本项目未使用）
```

#### Keil 工程配置

1. **添加源文件到工程**：
   - 在 Keil 中右键工程 → **Manage Project Items**
   - 新建分组 `Middlewares/FreeRTOS`
   - 添加以下文件：
     - `tasks.c`
     - `queue.c`
     - `list.c`
     - `heap_4.c`
     - `port.c`

2. **添加头文件路径**：
   - 工程选项 → **C/C++** → **Include Paths**
   - 添加：
     ```
     Middlewares\Third_Party\FreeRTOS\Source\include
     ```

3. **编译选项**：
   - 确保使用 **AC6 (ARM Compiler 6)** 或 **AC5** 均可
   - FreeRTOSConfig.h 已配置好 Cortex-M4F 映射

## 关键设计要点

### 1. 中断优先级配置

```c
HAL_NVIC_SetPriority(TIM3_IRQn, 5, 0);
HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 5, 0);
HAL_NVIC_SetPriority(TIM7_IRQn, 5, 0);
```

**为什么设为 5？**
- FreeRTOSConfig.h 中 `configMAX_SYSCALL_INTERRUPT_PRIORITY = (5 << 4)`
- 这意味着**优先级数值 >= 5** 的中断才能安全调用 `FromISR` API
- 如果优先级设为 0~4，调用 `xSemaphoreGiveFromISR()` 会导致系统崩溃

### 2. ISR 精简原则

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    BaseType_t xWoken = pdFALSE;
    if (htim->Instance == TIM6)
        xSemaphoreGiveFromISR(xTim6Semaphore, &xWoken);
    else if (htim->Instance == TIM7)
        xSemaphoreGiveFromISR(xTim7Semaphore, &xWoken);
    portYIELD_FROM_ISR(xWoken);  // 可能触发上下文切换
}
```

- ISR 中**只做一件事**：发信号量
- 实际业务逻辑（Q累计、PWM调节）放到任务中执行
- 这是 RTOS 编程的**黄金法则**：ISR 要短，任务做重活

### 3. 启动顺序铁律：定时器中断必须在调度器启动后开启

**坑**：如果在 `osKernelStart()` 之前调用 `HAL_TIM_Base_Start_IT()`，定时器中断立刻就可能触发。中断回调里如果调 `xSemaphoreGiveFromISR`——而这时调度器还没起来——直接 hard fault，板子"完全死"。

**正确做法**：

`main.c`（调度器启动前）：
```c
HAL_NVIC_SetPriority(TIM3_IRQn, 5, 0);
HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 5, 0);
HAL_NVIC_SetPriority(TIM7_IRQn, 5, 0);
HAL_NVIC_DisableIRQ(TIM3_IRQn);       // 关键：屏蔽中断
HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
HAL_NVIC_DisableIRQ(TIM7_IRQn);

// ... osKernelInitialize(); MX_FREERTOS_Init(); osKernelStart();
```

`freertos_app.c Task_Sensor`（调度器已起来）：
```c
HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1);
HAL_TIM_Base_Start_IT(&htim6);
HAL_TIM_Base_Start_IT(&htim7);
HAL_NVIC_EnableIRQ(TIM3_IRQn);
HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
HAL_NVIC_EnableIRQ(TIM7_IRQn);
```

### 4. 互斥量 vs 裸机全局变量

原裸机代码中，`P`、`F`、`D` 等全局变量被中断和主循环同时访问，存在**数据竞争**：
- `double F` 是 64 位，在 32 位 ARM 上需要两条指令读写，可能被中断打断
- 裸机中碰巧不崩溃，是因为运气；在RTOS中必须显式保护

改造后：
```c
xSemaphoreTake(xSensorDataMutex, portMAX_DELAY);
P = press;
F = flow;
xSemaphoreGive(xSensorDataMutex);
```

### 4. vTaskDelayUntil 保证精确周期

```c
TickType_t xLastWakeTime = xTaskGetTickCount();
while (1) {
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
    // 业务逻辑...
}
```

- `vTaskDelayUntil` 是**绝对延时**：从任务上次唤醒的时间点开始计算
- 即使本次执行耗时 10ms，下次仍然在精确的 50ms 边界唤醒
- 比裸机的 `HAL_GetTick()` 轮询更精确、CPU占用更低

## 学习路线建议

### 第一阶段：理解架构（30分钟）

1. 对比 `main.c` 的裸机版本和 FreeRTOS 版本
2. 看 `freertos_app.c` 中的任务创建函数 `FreeRTOS_App_Init()`
3. 理解 6 个任务的职责和优先级分配

### 第二阶段：理解同步（1小时）

1. 在 `freertos_app.c` 中搜索 `xSemaphoreTake` / `xSemaphoreGive`
2. 理解为什么 Sensor 任务写数据时要加锁，Display 任务读时也要加锁
3. 理解二值信号量的用法：ISR 发信号 → 任务阻塞等待 → 任务唤醒执行

### 第三阶段：动手修改（2~3小时）

1. **加一个新任务**：比如 `Task_Beep`，在告警时蜂鸣
2. **加一个队列**：用 `xQueue` 把按键事件从 KeyInput 任务发给 Display 任务，实现更清晰的解耦
3. **尝试优先级反转**：故意在 LED 任务中长时间持有互斥量，观察高优先级任务的阻塞现象

### 第四阶段：深入内核（进阶）

1. 阅读 `FreeRTOSConfig.h` 中每个宏的含义
2. 阅读 `port.c` 中的 `xPortPendSVHandler`，理解上下文切换的汇编实现
3. 用 Keil 的 **Logic Analyzer** 观察任务的执行时序

## 常见问题

**Q1: 编译报错 "undefined reference to xTaskCreate"**
> 说明 FreeRTOS 源文件没有添加到 Keil 工程。检查是否添加了 `tasks.c`、`queue.c`、`list.c`、`port.c`、`heap_4.c`。

**Q2: 编译报错 "multiply defined SysTick_Handler"**
> `stm32g4xx_it.c` 和 `port.c` 都定义了 SysTick_Handler。确保使用了本项目中修改过的 `stm32g4xx_it.c`（已删除 SysTick_Handler）。

**Q3: 程序卡在 `vTaskStartScheduler()` 之后不运行**
> 通常是 `configTOTAL_HEAP_SIZE` 太小，导致任务创建失败。检查 `FreeRTOSConfig.h` 中的堆大小（当前为 10KB）。

**Q4: 信号量接收不到，任务一直阻塞**
> 检查中断优先级是否 <= 4（数值太小）。必须设为 5~15 才能调用 `FromISR` API。

**Q5: LCD 显示乱闪或花屏**
> `lcd_show()` 和 `key_scan()` 中的 `LCD_Clear` 可能竞争 LCD 硬件。确保 `xLcdMutex` 已在两个任务中获取。

## 参考资源

- [FreeRTOS 官方文档](https://www.freertos.org/Documentation/RTOS_book.html)
- [STM32CubeG4 固件包](https://www.st.com/en/embedded-software/stm32cubeg4.html)
- [FreeRTOS 快速入门指南](https://www.freertos.org/Documentation/01-FreeRTOS-quick-start/01-Beginners-guide/02-Quick-start-guide)

---

**改造日期**: 2026-06-22  
**原始项目**: 第17届蓝桥杯嵌入式省赛真题 — 压力/流量监测与控制系统  
**目标平台**: STM32G431RBT6 (Cortex-M4F, 80MHz)

## 更新日志

### 2026-06-22
- 修复启动顺序问题：定时器中断必须在 `osKernelStart()` 之后开启，否则中断回调中的 `xSemaphoreGiveFromISR` 在调度器未启动状态下调用导致 hard fault
- `main.c`：调度器启动前只设 NVIC 优先级并显式 DisableIRQ
- `freertos_app.c Task_Sensor`：调度器已起来后再开启 TIM3/TIM6/TIM7 中断

### 2026-06-20
- 完成裸机 → FreeRTOS 多任务架构重构
- 6 个任务 + 3 互斥量 + 2 二值信号量
- 验证通过，LCD/按键/LED 全部正常工作
