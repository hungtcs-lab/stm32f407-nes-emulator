/* 编译 InfoNES 核心时强制包含（-include），MCU 专用配置 */
#ifndef INFONES_CONFIG_H
#define INFONES_CONFIG_H

/* RAM/SRAM/PPURAM/ChrBuf 共 64KB，正好放满 CCM（CPU 独占，零等待）*/
#define NES_FAST_RAM __attribute__((section(".ccmram")))

/* 去掉占用大块 RAM 的 Mapper（5/6/19/85/188/235），APU 事件队列缩到 512 */
#define INFONES_SMALL_RAM 1

/* 像素直接用 RGB565，背景标记放 bit5（见 InfoNES_Types.h）*/
#define NES_BG_FLAG 0x0020

/* APU 22050Hz（上游默认 44100，采样点减半省 CPU）*/
#define pAPU_QUALITY 2

#endif
