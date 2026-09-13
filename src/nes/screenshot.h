/* 截图：从 ILI9341 显存逐像素读回，存成 24 位 BMP（320x240）到 SD 卡
 * 只用来给文档配图，正式固件不开。编译时定义 NES_SHOT_TIMES="300,500,..."（开机后的时间，单位 10ms），
 * 到点时在菜单 / 文件管理 / 游戏的循环里自动截一张，存为 /NES/SHOT/NNN.bmp */
#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

int screenshot_save_bmp(const char *path);   /* 0 = 成功 */
int screenshot_poll(void);                   /* 到点就截图，截了返回 1 */

#ifdef __cplusplus
}
#endif

#endif
