#include "debug.h"
#include "stdarg.h"
#include "stdio.h"
#include "string.h"
#include "FreeRTOS.h"
#include "semphr.h"

//重定向串口
int fputc(int ch, FILE *f)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xffff);
  return ch;
}
// ==================== 全局/静态缓冲区 ====================
static char log_buffer[DEBUG_BUF_SIZE];           // 日志输出缓冲区
static char debug_time_str[DEBUG_TIME_BUF_SIZE];  // 格式化时间缓冲区

// 原生 FreeRTOS Mutex
static SemaphoreHandle_t log_mutex = NULL;

// ===================== 加锁 / 解锁 =====================
static void log_lock(void)
{
    if (log_mutex != NULL)
    {
        xSemaphoreTake(log_mutex, portMAX_DELAY);   // 无限等待拿到锁
    }
}

static void log_unlock(void)
{
    if (log_mutex != NULL)
    {
        xSemaphoreGive(log_mutex);
    }
}

// ===================== 初始化（重要！） =====================
void LOG_init(void)
{
    if (log_mutex == NULL)
    {
        log_mutex = xSemaphoreCreateMutex();   // 创建互斥量

        if (log_mutex == NULL)
        {
            // 创建失败，通常是 heap 内存不足
            // 你可以在这里加个死循环或错误指示灯提醒
            // while(1);
        }
    }
}

// ===================== 获取格式化时间字符串 =====================
/**
 * @brief 将系统 Tick 转换为 HH:MM:SS.mmm 格式的时间字符串
 * @return 静态字符串指针，指向格式化后的时间（"HH:MM:SS.mmm"）
 *
 * 原理：
 * 系统 Tick 是从启动开始的毫秒数
 * 例：启动后 3661001ms
 *     ├─ msec = 3661001 % 1000 = 001 (毫秒)
 *     ├─ sec = (3661001/1000) % 60 = 01 (秒)
 *     ├─ min = (3661/60) % 60 = 01 (分钟)
 *     └─ hour = (61/60) = 1 (小时)
 *     结果：01:01:01.001
 */
static char* debug_get_time(void)
{
    uint32_t time_ms = DEBUG_GET_TICK_MS();

    // 逐级提取：毫秒 → 秒 → 分 → 小时
    uint16_t msec = time_ms % 1000;           // 余数 = 毫秒部分
    time_ms /= 1000;                           // 整除，得到秒数

    uint8_t sec = time_ms % 60;                // 秒数对60求余
    time_ms /= 60;                             // 转换为分钟

    uint8_t min = time_ms % 60;                // 分钟对60求余
    time_ms /= 60;                             // 转换为小时

    uint8_t hour = time_ms;                    // 小时不需要再取余（除非超过24小时）

    // 格式化输出：使用 %02d 确保不足两位时前面补0
    snprintf(debug_time_str, sizeof(debug_time_str),
             "%02d:%02d:%02d.%03d",
             hour, min, sec, msec);

    return debug_time_str;
}

// ===================== 核心日志函数（推荐主力使用） =====================
/**
 * @brief 带颜色和时间戳的格式化日志输出
 * @param color 颜色代码（如 COLOR_RED、COLOR_GREEN 等 ANSI 颜色）
 * @param level 日志级别（如 "INFO"、"WARN"、"ERROR"）
 * @param func 函数名（通常用 __func__ 宏）
 * @param fmt 格式字符串（如 printf 用法）
 * @param ... 可变参数
 *
 * 输出示例：
 * [32m[01:23:45.678][INFO ][main] Hello World[0m
 * ├─ [32m          ← 绿色颜色代码
 * ├─ [01:23:45.678] ← HH:MM:SS.mmm 格式时间
 * ├─ [INFO ]         ← 日志级别
 * ├─ [main]          ← 函数名
 * ├─ Hello World     ← 用户内容
 * └─ [0m             ← 重置颜色代码
 */
void debug_log(const char *color,
                      const char *level,
                      const char *func,
                      const char *fmt,
                      ...)
{
    log_lock();

    int len = 0;
    const char *time_str = debug_get_time();  // 获取格式化时间

    // 输出格式：颜色 + [时间][级别][函数名] + 用户内容 + 颜色重置 + 换行
    len += snprintf(log_buffer + len, sizeof(log_buffer) - len,
                    "%s[%s][%s][%s] ",
                    color, time_str, level, func);

    // 用户格式化内容
    va_list args;
    va_start(args, fmt);
    len += vsnprintf(log_buffer + len, sizeof(log_buffer) - len, fmt, args);
    va_end(args);

    // 恢复颜色 + 换行
    len += snprintf(log_buffer + len, sizeof(log_buffer) - len,
                    "%s\r\n", COLOR_RESET);

    // 防止缓冲区溢出
    if (len >= (int)sizeof(log_buffer))
        len = sizeof(log_buffer) - 1;

    DEBUG_UART_SEND(log_buffer, len);

    log_unlock();
}

// ===================== 带时间戳的普通打印 =====================
/**
 * @brief 简单的格式化日志输出（无级别、无函数名）
 * @param fmt 格式字符串（如 printf 用法）
 * @param ... 可变参数
 *
 * 输出示例：
 * [01:23:45.678] Value = 42
 */
void debug_printf(const char *fmt, ...)
{
    log_lock();

    int len = 0;
    const char *time_str = debug_get_time();  // 获取格式化时间

    len += snprintf(log_buffer, sizeof(log_buffer), "[%s] ", time_str);

    va_list args;
    va_start(args, fmt);
    len += vsnprintf(log_buffer + len, sizeof(log_buffer) - len, fmt, args);
    va_end(args);

    // 补充换行符
    if (len < (int)sizeof(log_buffer) - 2)
    {
        log_buffer[len++] = '\r';
        log_buffer[len++] = '\n';
    }

    if (len > 0)
    {
        if (len >= (int)sizeof(log_buffer))
            len = sizeof(log_buffer) - 1;
        DEBUG_UART_SEND(log_buffer, len);
    }

    log_unlock();
}

void sim_printf(char *format, ...)
{
    log_lock();
    memset(log_buffer, 0, sizeof(log_buffer));
    va_list arg;
    va_start(arg, format);
    vsnprintf(log_buffer + strlen(log_buffer), sizeof(log_buffer) - strlen(log_buffer), format, arg);
    va_end(arg);
    DEBUG_UART_SEND(log_buffer, strlen(log_buffer));
    log_unlock();
}
