#include "board.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 调试器可读的诊断区 ----------------
 * 固定开头的 magic，host 端脚本按符号地址读出 */
#define LOG_SIZE 4096
typedef struct {
    uint32_t magic;          /* 0xD1A6C0DE */
    uint32_t len;            /* g_log 中有效字节数（环形时为总写入量）*/
    char     buf[LOG_SIZE];
} diag_log_t;

diag_log_t g_log;   /* 放 .bss，magic 在 board_init 里写 */

static UART_HandleTypeDef huart1;
static void (*s_lcd_sink)(const char *s);

/* ---------------- 时钟: HSE 8MHz → PLL → 168MHz ---------------- */
static void clock_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 8;      /* 8MHz / 8   = 1MHz   */
    osc.PLL.PLLN       = 336;    /* 1MHz * 336 = 336MHz */
    osc.PLL.PLLP       = RCC_PLLP_DIV2;  /* 168MHz 系统时钟 */
    osc.PLL.PLLQ       = 7;      /* 48MHz 给 SDIO / USB */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) error_handler("HAL_RCC_OscConfig");

    clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                         RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* 168MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV4;   /*  42MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV2;   /*  84MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) error_handler("HAL_RCC_ClockConfig");
}

static void led_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(LED_PORT, LED1_PIN | LED2_PIN, GPIO_PIN_SET);   /* 灭 */
    GPIO_InitTypeDef g = {
        .Pin = LED1_PIN | LED2_PIN, .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(LED_PORT, &g);
}

static void uart_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef g = {
        .Pin = GPIO_PIN_9 | GPIO_PIN_10, .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_PULLUP, .Speed = GPIO_SPEED_FREQ_HIGH, .Alternate = GPIO_AF7_USART1,
    };
    HAL_GPIO_Init(GPIOA, &g);

    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

void board_init(void)
{
    g_log.magic = 0xD1A6C0DEu;
    HAL_Init();          /* SysTick 1ms + ART(预取/指令缓存/数据缓存) */
    clock_init();
    led_init();
    uart_init();
}

void led_set(uint16_t pin, int on)
{
    HAL_GPIO_WritePin(LED_PORT, pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void led_toggle(uint16_t pin)
{
    HAL_GPIO_TogglePin(LED_PORT, pin);
}

/* ---------------- 日志 ---------------- */
void log_set_lcd_sink(void (*sink)(const char *s))
{
    s_lcd_sink = sink;
}

void log_printf(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof line) n = sizeof line - 1;

    /* RAM 日志（环形） */
    for (int i = 0; i < n; i++) g_log.buf[(g_log.len + i) % LOG_SIZE] = line[i];
    g_log.len += (uint32_t)n;

    /* 串口：\n 补 \r */
    if (huart1.Instance) {
        for (int i = 0; i < n; i++) {
            if (line[i] == '\n') HAL_UART_Transmit(&huart1, (uint8_t *)"\r", 1, 10);
            HAL_UART_Transmit(&huart1, (uint8_t *)&line[i], 1, 10);
        }
    }

    if (s_lcd_sink) s_lcd_sink(line);
}

void error_handler(const char *where)
{
    log_printf("\n!!! FATAL: %s\n", where ? where : "?");
    for (;;) {                       /* 双灯快闪 = 致命错误 */
        led_set(LED1_PIN, 1); led_set(LED2_PIN, 1);
        for (volatile uint32_t i = 0; i < 800000u; i++) { }
        led_set(LED1_PIN, 0); led_set(LED2_PIN, 0);
        for (volatile uint32_t i = 0; i < 800000u; i++) { }
    }
}
