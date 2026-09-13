/* 手柄输入：8 个 GPIO，接按键到 GND（内部上拉，低电平 = 按下）
 *
 *   按键     引脚     J2 排针（板子右侧 2x24）
 *   UP       PC0
 *   DOWN     PC1
 *   LEFT     PC2
 *   RIGHT    PC3
 *   A        PE5
 *   B        PE6
 *   SELECT   PE2
 *   START    PC13
 *   GND      J2 的 9/10 脚
 *
 * 另外板载按键也映射进来，不接线也能开始游戏:
 *   KEY0 (PE4) = START   KEY1 (PE3) = SELECT   WK_UP (PA0, 高电平有效) = A */
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
