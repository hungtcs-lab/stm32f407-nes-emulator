/* LCD 刷新率测试：模拟 NES 输出路径
 *
 * 画面: 256x240 的 8 位调色板索引帧（放在 CCM 里，CPU 独占正合适）
 * 每帧: 设窗口一次 → 61440 次 "查调色板 + 写 FSMC"，和模拟器逐行推屏的工作量一致
 * 分别测不同的 FSMC 写时序，看速度上限和画面是否出错 */
#include "lcd.h"
#include "board.h"

#include <stdio.h>

#define NES_W 256
#define NES_H 240
#define X0    ((320 - NES_W) / 2)     /* 横屏左右各留 32 像素 */

static uint8_t s_frame[NES_W * NES_H] __attribute__((section(".ccmram")));

/* 常见 NES 调色板（RGB888 → RGB565）*/
static const uint32_t nes_rgb[64] = {
    0x7C7C7C,0x0000FC,0x0000BC,0x4428BC,0x940084,0xA80020,0xA81000,0x881400,
    0x503000,0x007800,0x006800,0x005800,0x004058,0x000000,0x000000,0x000000,
    0xBCBCBC,0x0078F8,0x0058F8,0x6844FC,0xD800CC,0xE40058,0xF83800,0xE45C10,
    0xAC7C00,0x00B800,0x00A800,0x00A844,0x008888,0x000000,0x000000,0x000000,
    0xF8F8F8,0x3CBCFC,0x6888FC,0x9878F8,0xF878F8,0xF85898,0xF87858,0xFCA044,
    0xF8B800,0xB8F818,0x58D854,0x58F898,0x00E8D8,0x787878,0x000000,0x000000,
    0xFCFCFC,0xA4E4FC,0xB8B8F8,0xD8B8F8,0xF8B8F8,0xF8A4C0,0xF0D0B0,0xFCE0A8,
    0xF8D878,0xD8F878,0xB8F8B8,0xB8F8D8,0x00FCFC,0xF8D8F8,0x000000,0x000000,
};
static uint16_t s_pal[64];

static void build_frame(void)
{
    for (int i = 0; i < 64; i++) {
        uint32_t c = nes_rgb[i];
        s_pal[i] = RGB565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }
    for (int y = 0; y < NES_H; y++) {
        for (int x = 0; x < NES_W; x++) {
            uint8_t v;
            if (x == 0 || y == 0 || x == NES_W - 1 || y == NES_H - 1)
                v = 0x30;                                   /* 1px 白边框：检查边缘对齐 */
            else if (y < 64)
                v = (uint8_t)(((y / 16) * 16 + x / 16) & 0x3F);   /* 64 色色块 */
            else if (y < 128)
                v = ((x ^ y) & 1) ? 0x30 : 0x0F;           /* 1px 棋盘格：最容易暴露丢数据 */
            else if (y < 192)
                v = (((x + y) & 15) == 0) ? 0x27 : 0x02;   /* 斜线 */
            else
                v = (x & 8) ? 0x16 : 0x2A;                 /* 竖条 */
            s_frame[y * NES_W + x] = v;
        }
    }
}

/* 推一帧，off = 水平滚动偏移（让每帧内容都不一样）*/
static void push_frame(int off)
{
    lcd_set_window(X0, 0, X0 + NES_W - 1, NES_H - 1);
    const uint8_t *src = s_frame;
    for (int y = 0; y < NES_H; y++, src += NES_W)
        for (int x = 0; x < NES_W; x++)
            LCD_DATA_PORT = s_pal[src[(x + off) & (NES_W - 1)]];
}

/* 同样的循环但不写屏，测纯 CPU 开销 */
static volatile uint16_t s_sink;
static void cpu_only_frame(int off)
{
    const uint8_t *src = s_frame;
    for (int y = 0; y < NES_H; y++, src += NES_W)
        for (int x = 0; x < NES_W; x++)
            s_sink = s_pal[src[(x + off) & (NES_W - 1)]];
}

static void set_write_timing(uint32_t addset, uint32_t datast)
{
    /* BWTR[0] = NE1 写时序；模式 A */
    FSMC_Bank1E->BWTR[0] = (addset << FSMC_BWTR1_ADDSET_Pos) |
                           (15u << FSMC_BWTR1_ADDHLD_Pos) |
                           (datast << FSMC_BWTR1_DATAST_Pos);
}

#define BENCH_FRAMES 60

/* ---------------- 精确计时：DWT 周期计数器 ---------------- */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}
static inline uint32_t us_since(uint32_t c0)
{
    return (uint32_t)((uint64_t)(DWT->CYCCNT - c0) * 1000000u / SystemCoreClock);
}

/* ---------------- 写入正确性校验 ----------------
 * 用待测的（快）写时序连续写一行已知像素，再用慢的读时序逐个读回
 * ILI9341 读 GRAM(0x2E)：dummy, (R<<8|G), (B<<8|..)，每个分量 6 位左对齐 */
static uint16_t read_pixel(int x, int y)
{
    lcd_set_window(x, y, x, y);
    LCD_CMD_PORT = 0x2E;
    (void)LCD_DATA_PORT;
    uint16_t rg = LCD_DATA_PORT;
    uint16_t b_ = LCD_DATA_PORT;
    uint16_t r = (rg >> 11) & 0x1F, g = (rg >> 2) & 0x3F, b = (b_ >> 11) & 0x1F;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int verify_write(void)
{
    static uint16_t pat[64];
    for (int i = 0; i < 16; i++) { pat[i] = (uint16_t)(1u << i); pat[16 + i] = (uint16_t)~(1u << i); }
    uint32_t v = 0x12345678;
    for (int i = 32; i < 64; i++) { v = v * 1664525u + 1013904223u; pat[i] = (uint16_t)(v >> 16); }
    pat[32] = 0xFFFF; pat[33] = 0x0000; pat[34] = 0xAAAA; pat[35] = 0x5555;

    const int y = 239;                        /* 最底下一行，不挡测试图 */
    lcd_set_window(0, y, 63, y);
    for (int i = 0; i < 64; i++) LCD_DATA_PORT = pat[i];

    int bad = 0;
    for (int i = 0; i < 64; i++) if (read_pixel(i, y) != pat[i]) bad++;
    return bad;
}

void lcd_bench_run(void)
{
    static const struct { uint8_t a, d; } timings[] = {
        {4, 8}, {3, 5}, {2, 3}, {1, 2}, {1, 1}, {0, 1},
    };
    char line[48];

    build_frame();
    dwt_init();
    lcd_fill(C_BLACK);

    uint32_t c0 = DWT->CYCCNT;
    for (int f = 0; f < BENCH_FRAMES; f++) cpu_only_frame(f);
    uint32_t cpu_us = us_since(c0) / BENCH_FRAMES;
    log_printf("\n-- LCD bench (NES 256x240, palette lookup) --\n");
    log_printf("CPU only (no LCD): %lu us/frame\n", (unsigned long)cpu_us);

    set_write_timing(4, 8);
    log_printf("readback self-check @A4 D8: %d bad of 64\n", verify_write());

    struct { uint32_t us_nes, us_full; int bad; } res[sizeof timings / sizeof timings[0]];

    for (unsigned i = 0; i < sizeof timings / sizeof timings[0]; i++) {
        set_write_timing(timings[i].a, timings[i].d);

        c0 = DWT->CYCCNT;
        for (int f = 0; f < BENCH_FRAMES; f++) push_frame(f);
        res[i].us_nes = us_since(c0) / BENCH_FRAMES;

        c0 = DWT->CYCCNT;
        for (int f = 0; f < 20; f++) lcd_fill(f & 1 ? C_BLACK : C_BLUE);
        res[i].us_full = us_since(c0) / 20;

        res[i].bad = 0;
        for (int k = 0; k < 20; k++) res[i].bad += verify_write();

        uint32_t fps = 10000000u / res[i].us_nes;
        log_printf("ADDSET=%u DATAST=%u: NES %lu us = %lu.%lu fps, full 320x240 %lu us, bad px %d/1280\n",
                   timings[i].a, timings[i].d, (unsigned long)res[i].us_nes,
                   (unsigned long)(fps / 10), (unsigned long)(fps % 10),
                   (unsigned long)res[i].us_full, res[i].bad);

        /* 用这个时序画一张静态测试图，让人眼检查有没有花屏 */
        lcd_fill(C_BLACK);
        push_frame(0);
        snprintf(line, sizeof line, "%u A%u D%u", i + 1, timings[i].a, timings[i].d);
        lcd_draw_string_scaled(X0 + 8, 204, line, 2, C_WHITE, C_BLACK);
        snprintf(line, sizeof line, "%lu fps", (unsigned long)(fps / 10));
        lcd_draw_string_scaled(X0 + 140, 204, line, 2, C_YELLOW, C_BLACK);
        HAL_Delay(3000);
    }

    /* 汇总表 */
    lcd_fill(C_BLACK);
    lcd_draw_string(4, 4, "LCD bench: NES 256x240 frame push", C_CYAN, C_BLACK);
    snprintf(line, sizeof line, "CPU only: %lu us/frame", (unsigned long)cpu_us);
    lcd_draw_string(4, 24, line, C_WHITE, C_BLACK);
    for (unsigned i = 0; i < sizeof timings / sizeof timings[0]; i++) {
        snprintf(line, sizeof line, "%u) A%u D%u  NES %4lu fps  err %d", i + 1, timings[i].a, timings[i].d,
                 (unsigned long)(1000000u / res[i].us_nes), res[i].bad);
        lcd_draw_string(4, 52 + i * 20, line, res[i].bad ? C_RED : C_GREEN, C_BLACK);
    }
    set_write_timing(2, 3);   /* 恢复默认时序（和 lcd.c 一致）*/
    log_printf("bench done\n");
}
