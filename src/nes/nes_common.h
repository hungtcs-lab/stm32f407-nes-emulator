/* 主机测试程序和 MCU 共用的小工具（纯 C，不依赖 HAL） */
#ifndef NES_COMMON_H
#define NES_COMMON_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* InfoNES 输出的像素已经是 RGB565，只需清掉 bit5 上的背景标记（NES_BG_FLAG = 0x0020）*/
static inline uint16_t nes_to_rgb565(uint16_t p)
{
    return (uint16_t)(p & 0xFFDFu);
}

/* zlib 兼容的 CRC32（和 Python zlib.crc32 结果一致）*/
uint32_t nes_crc32(uint32_t crc, const uint8_t *data, size_t len);

/* NES 手柄位（InfoNES 的 PAD1_Latch 定义）*/
#define NES_BTN_A      0x01
#define NES_BTN_B      0x02
#define NES_BTN_SELECT 0x04
#define NES_BTN_START  0x08
#define NES_BTN_UP     0x10
#define NES_BTN_DOWN   0x20
#define NES_BTN_LEFT   0x40
#define NES_BTN_RIGHT  0x80

/* 自动按键脚本，用于无人值守测试。格式: "start:120-126,right:200-400,a:250-260"
 * 按帧号（从 1 开始计的 VBlank 次数）返回当前应按下的键 */
uint8_t nes_script_buttons(const char *script, uint32_t frame);

#ifdef __cplusplus
}
#endif

#endif
