# InfoNES（移植版）

- 上游: https://github.com/jay-kumogata/InfoNES  commit `fe3295c0a86bf5bbecc22aeab3b3af11f6f47908`
- 许可证: Apache-2.0（见 LICENSE）
- 只拷贝了 `src/` 下的核心和 `mapper/`，平台相关目录（linux/sdl/win32…）没要
- 所有文件统一转成 LF 换行；改动处都标了 `[port]`，`grep -rn '\[port\]' lib/infones` 可以列出全部

## 改动

| 改动 | 原因 |
|---|---|
| `DWORD/WORD/BYTE` 改成 `uint32_t/uint16_t/uint8_t` | 原来 `DWORD = unsigned long`，64 位主机上是 8 字节，`InfoNES_GetSprHitY` 把 ChrBuf 当 DWORD 数组读会读错；统一后主机测试和 MCU 行为一致 |
| 整帧缓冲 `WorkFrame[256*240]`（120KB）→ 单行 `WorkLine[256]`，每行画完调用新增的 `InfoNES_LoadLine()` | F407 主 RAM 只有 128KB |
| 精灵合成时 `pPoint -= (NES_DISP_WIDTH - PPU_Scr_H_Bit)` → `pPoint = WorkLine` | 关屏（R1_SHOW_SCR=0）时 pPoint 没有前移，倒推会越界。整帧缓冲里只是写到上一行，单行缓冲下会踩内存 |
| `RAM/SRAM/PPURAM/ChrBuf` 加 `NES_FAST_RAM` 修饰 | MCU 上放进 CCM（正好 64KB），主机上为空 |
| `INFONES_SMALL_RAM` 下去掉 Mapper 5/6/19/85/188/235 | 它们各自带 8KB~256KB 的静态数组 |
| `INFONES_SMALL_RAM` 下 `APU_EVENT_MAX` 15000 → 512，并加越界检查 | 原来 ARM 上占 120KB，而且写入时没有边界检查 |
| 精灵合成：精灵缓冲延迟到找到第一个精灵才清零，合成循环只扫有精灵的 X 范围 | 大部分扫描线没有精灵，原来每行都 memset 263 字节 + 扫 256 像素；**板上整体快了约 5ms/帧**，输出逐像素不变 |
| 背景标记位 `0x8000` → 宏 `NES_BG_FLAG` | MCU 上像素直接用 RGB565，标记放 bit5，推屏不用转换格式 |
| `InfoNES_HSync` 调 `DrawLine` 前先问平台层 `InfoNES_ShouldDrawLine()` | 平台层实现隔行渲染（性能不够时的降级手段）|
| `DrawLine` 里 `MapperPPU` 是空回调 `Map0_PPU` 时不调用 | 省函数指针调用 |
| `pAPU_QUALITY` 加 `#ifndef` | MCU 上用 22050Hz |
| Mapper 1: `Map1_Size_t Map1_Size;` → `enum Map1_Size_t Map1_Size;` | 源文件后缀是 .cpp，但全部按 C 编译（工具链里没有 arm-none-eabi-g++）；这是唯一一处 C 不兼容 |

## 验证

`host/` 下是主机端测试程序，用同一份核心代码跑 ROM、按脚本按键、输出截图和每帧 CRC32，
MCU 固件编译时加 `NES_CRC_FRAMES` / `NES_AUTOPLAY` 后输出的 CRC 应该完全一致。
