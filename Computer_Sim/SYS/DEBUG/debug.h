#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "main.h"
#include "usart.h"

#define DEBUG_BUF_SIZE 512   // ⭐ 改大：256 太小了，容易溢出
#define DEBUG_TIME_BUF_SIZE 20   // "HH:MM:SS.mmm\0" 需要 14 字符，留余量
#define USE_FREERTOS
// ===================== 用户可改区域 =====================
#define DEBUG_GET_TICK_MS()     HAL_GetTick()   // 获取毫秒级系统时间
#define DEBUG_UART_SEND(buf, len)  HAL_UART_Transmit(&huart1, (uint8_t*)(buf), (len), 100)  // ⭐ 超时改为 100ms，太长会阻塞

// ANSI颜色
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

// 日志级别颜色
#define LOG_COLOR_INFO    COLOR_GREEN
#define LOG_COLOR_WARN    COLOR_YELLOW
#define LOG_COLOR_ERROR   COLOR_RED
#define LOG_COLOR_DEBUG   COLOR_CYAN

// ===================== 对外接口 =====================
void LOG_init(void);
void sim_printf(char *format, ...);             // 直接发送字符串到调试串口（不带时间戳）
void debug_printf(const char *fmt, ...);        // 带时间戳普通打印
void debug_log(const char *color,const char *level,const char *func,const char *fmt,...); // 带颜色、时间戳、级别的日志输出
// 推荐使用的带颜色日志
#define LOG_OK(fmt, ...)   debug_log(LOG_COLOR_INFO,  "INFO ",  __func__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   debug_log(LOG_COLOR_WARN,  "WARN ",  __func__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)  debug_log(LOG_COLOR_ERROR, "ERROR",  __func__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  debug_log(LOG_COLOR_DEBUG, "DEBUG",  __func__, fmt, ##__VA_ARGS__)



#endif

