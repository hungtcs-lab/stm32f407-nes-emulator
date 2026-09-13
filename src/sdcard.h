/* SD 卡: SDIO 4 线（PC8~PC12 + PD2），HAL_SD 轮询模式 */
#ifndef SDCARD_H
#define SDCARD_H

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 数据传输时钟 = 48MHz / (SD_CLOCK_DIV + 2)，轮询模式实测（CPU 全速 168MHz，scripts/sd-soak.sh）：
 *   div 18 (2.4MHz)  稳定   读  1.0MB/s
 *   div  8 (4.8MHz)  稳定   读  1.8MB/s
 *   div  2 ( 12MHz)  稳定   读  3.4MB/s  写 2.0MB/s   ← 默认
 *   div  0 ( 24MHz)  不稳定：多块写超时、初始化卡住（换 DMA 后再试）*/
#ifndef SD_CLOCK_DIV
#define SD_CLOCK_DIV  2
#endif

extern SD_HandleTypeDef hsd;

void sd_gpio_init(void);
#define SD_INIT_ATTEMPTS 3
int sd_init(void);           /* 0 = 成功，可重复调用 */
int sd_read(uint8_t *buf, uint32_t block, uint32_t count);
int sd_write(const uint8_t *buf, uint32_t block, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
