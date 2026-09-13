/* 3.2 寸 16 位并口 TFT，走 FSMC Bank1 NE1，RS = FSMC_A18（VET6 版板子）
 * 支持控制器: ILI9341（首选），ILI9325/ILI9328（回退，竖屏） */
#ifndef LCD_H
#define LCD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_BLUE    0x001F
#define C_YELLOW  0xFFE0
#define C_CYAN    0x07FF
#define C_MAGENTA 0xF81F
#define C_GRAY    0x8410

/* 像素数据口：设好窗口后连续写即可（热路径直接用，不经函数调用）*/
#define LCD_DATA_PORT (*(volatile uint16_t *)0x60080000u)
#define LCD_CMD_PORT  (*(volatile uint16_t *)0x60000000u)

typedef struct {
    uint16_t id;             /* 识别到的控制器 ID，0 = 未识别 */
    const char *name;
    uint16_t width, height;
    uint8_t  raw_d3[4];      /* 0xD3 读回的原始字节，排障用 */
    uint16_t raw_reg00;      /* 寄存器 0x0000 读回值 */
    int      bus_readback_ok;/* 写 MADCTL 再读回是否一致（仅 ILI9341）*/
} lcd_info_t;

extern lcd_info_t g_lcd;

int  lcd_init(void);         /* 0 = 成功 */
void lcd_set_window(int x0, int y0, int x1, int y1);
void lcd_fill(uint16_t color);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
void lcd_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg);
void lcd_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg);
void lcd_color_bars(void);
void lcd_draw_string_scaled(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg);
void lcd_set_madctl(uint8_t madctl);   /* 仅 ILI9341：切换扫描方向 */

/* 简易文本控制台，满屏自动清屏回到顶部 */
void lcd_con_clear(void);
void lcd_con_color(uint16_t fg);
void lcd_con_puts(const char *s);

#ifdef __cplusplus
}
#endif

#endif
