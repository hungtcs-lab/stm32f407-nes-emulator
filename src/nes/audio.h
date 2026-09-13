/* 声音输出：DAC1 (PA4)，TIM6 触发 22050Hz，DMA1 循环搬运 —— 播放过程不占 CPU
 *
 * PA4 在 J2 排针上，输出 0~3.3V 的单端模拟信号。不能直接驱动喇叭：
 * 接一个功放模块（比如 PAM8403）的输入，或者串 1µF 电容 + 耳机试听 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RATE 22050

void audio_init(void);
/* 喂一帧的样本（InfoNES 的 5 路 8 位波形），内部混音后写进环形缓冲 */
void audio_push(int samples, const uint8_t *w1, const uint8_t *w2, const uint8_t *w3,
                const uint8_t *w4, const uint8_t *w5);
void audio_silence(void);

/* 调试：最近一次 audio_push 混音后的峰峰值（0~4095）和累计重同步次数 */
extern uint16_t g_audio_p2p;
extern uint32_t g_audio_resync;

#ifdef __cplusplus
}
#endif

#endif
