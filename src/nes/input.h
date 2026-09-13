/* 手柄输入：方向 + A/B 外接 6 个 GPIO，SELECT/START 用板载按键
 *
 *   按键     引脚     J2 排针（板子右侧 2x24）
 *   UP       PC0      17
 *   DOWN     PC1      18
 *   LEFT     PC2      19
 *   RIGHT    PC3      20
 *   A        PE5      14
 *   B        PE6      15
 *   GND               9 或 10
 *   按键一端接引脚，另一端接 GND（内部上拉，低电平 = 按下）
 *
 * 板载按键: KEY0 (PE4) = SELECT   KEY1 (PE3) = START   WK_UP (PA0, 高电平有效) = A */
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void    input_init(void);
uint8_t input_read(void);    /* 返回 NES_BTN_* 位组合 */

#ifdef __cplusplus
}
#endif

#endif
