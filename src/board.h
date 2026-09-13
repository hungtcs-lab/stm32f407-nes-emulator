/* 板级支持：时钟、LED、串口、日志 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LED: 3V3 → LED → 510R → 引脚，低电平点亮 */
#define LED_PORT  GPIOA
#define LED1_PIN  GPIO_PIN_6    /* D2 */
#define LED2_PIN  GPIO_PIN_7    /* D3 */

void board_init(void);
void led_set(uint16_t pin, int on);
void led_toggle(uint16_t pin);

/* 日志同时输出到: USART1(J6, 115200)、LCD 控制台(如果已初始化)、RAM 日志缓冲 g_log
 * g_log 可以不接串口直接用调试器读出: scripts/diag.py */
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_set_lcd_sink(void (*sink)(const char *s));

void error_handler(const char *where) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
