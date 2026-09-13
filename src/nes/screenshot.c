#include "screenshot.h"
#include "board.h"
#include "lcd.h"
#include "ff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240

static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* ILI9341 读 GRAM(0x2E)：dummy，(R<<8|G)，(B<<8|..)，每个分量 6 位左对齐 */
static void read_pixel_rgb(int x, int y, uint8_t *bgr)
{
    lcd_set_window(x, y, x, y);
    LCD_CMD_PORT = 0x2E;
    (void)LCD_DATA_PORT;
    uint16_t rg = LCD_DATA_PORT, b_ = LCD_DATA_PORT;
    uint8_t r6 = (uint8_t)((rg >> 10) & 0x3F), g6 = (uint8_t)((rg >> 2) & 0x3F), b6 = (uint8_t)((b_ >> 10) & 0x3F);
    bgr[0] = (uint8_t)(b6 << 2 | b6 >> 4);
    bgr[1] = (uint8_t)(g6 << 2 | g6 >> 4);
    bgr[2] = (uint8_t)(r6 << 2 | r6 >> 4);
}

int screenshot_save_bmp(const char *path)
{
    static uint8_t row[W * 3];
    uint8_t hdr[54] = { 'B', 'M' };
    uint32_t data = (uint32_t)W * H * 3u;          /* 960 字节一行，正好 4 的倍数，不用补齐 */
    put_le32(hdr + 2, 54u + data);
    put_le32(hdr + 10, 54u);
    put_le32(hdr + 14, 40u);
    put_le32(hdr + 18, W);
    put_le32(hdr + 22, H);                          /* 正数 = 自底向上 */
    put_le16(hdr + 26, 1);
    put_le16(hdr + 28, 24);
    put_le32(hdr + 34, data);

    FIL f;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return -1;
    UINT bw;
    int err = f_write(&f, hdr, sizeof hdr, &bw) != FR_OK;
    for (int y = H - 1; y >= 0 && !err; y--) {
        for (int x = 0; x < W; x++) read_pixel_rgb(x, y, row + x * 3);
        err = f_write(&f, row, sizeof row, &bw) != FR_OK || bw != sizeof row;
    }
    f_close(&f);
    return err ? -2 : 0;
}

int screenshot_poll(void)
{
#ifdef NES_SHOT_TIMES
    static const char *p = NES_SHOT_TIMES;
    static unsigned long next = (unsigned long)-1;
    static int index;
    if (next == (unsigned long)-1) {
        if (!*p) return 0;
        next = strtoul(p, (char **)&p, 10);
    }
    if (HAL_GetTick() / 10u < next) return 0;

    char path[32];
    f_mkdir("/NES/SHOT");
    snprintf(path, sizeof path, "/NES/SHOT/%03d.bmp", ++index);
    uint32_t t0 = HAL_GetTick();
    int r = screenshot_save_bmp(path);
    uint8_t px[3];
    read_pixel_rgb(2, 22, px);   /* 自检颜色通道：文件管理里选中行的蓝底应该是 (33,73,148) 左右 */
    log_printf("shot: %s @%lu %s (%lums) px(2,22)=%u,%u,%u\n", path, next, r == 0 ? "ok" : "FAIL",
               (unsigned long)(HAL_GetTick() - t0), px[2], px[1], px[0]);

    while (*p == ',' || *p == ' ') p++;
    next = *p ? strtoul(p, (char **)&p, 10) : (unsigned long)-2;   /* -2：已经没有了 */
    return 1;
#else
    return 0;
#endif
}
