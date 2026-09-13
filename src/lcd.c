#include "lcd.h"
#include "board.h"
#include "font8x16.h"

/* NE1 → 0x60000000；16 位总线 FSMC_A[n] 对应 HADDR[n+1]，A18 → bit19 */
#define LCD_CMD   (*(volatile uint16_t *)0x60000000u)   /* A18 = 0 */
#define LCD_DATA  (*(volatile uint16_t *)0x60080000u)   /* A18 = 1 */

/* 背光 PB1：实测高电平点亮（核心板 Q2 + 屏端 Q1 两级 PNP 反相）*/
#define LCD_BL_ON_LEVEL  GPIO_PIN_SET

/* FSMC 写时序（单位 HCLK 周期 ≈ 6ns），可以编译时覆盖 */
#ifndef LCD_WR_ADDSET
#define LCD_WR_ADDSET 2
#endif
#ifndef LCD_WR_DATAST
#define LCD_WR_DATAST 3
#endif

/* ILI9341 扫描方向 MADCTL: MY=0x80 MX=0x40 MV=0x20 BGR=0x08
 * 实测 0xA8 (MY|MV|BGR) 为正确横屏方向；0x28 是镜像的 */
#define ILI9341_MADCTL   0xA8

lcd_info_t g_lcd;

static SRAM_HandleTypeDef hsram;

static inline void wr_cmd(uint16_t c)  { LCD_CMD = c; }
static inline void wr_data(uint16_t d) { LCD_DATA = d; }
static inline uint16_t rd_data(void)   { return LCD_DATA; }

static void wr_reg(uint16_t reg, uint16_t val) { wr_cmd(reg); wr_data(val); }

static void cmd_params(uint8_t cmd, const uint8_t *p, int n)
{
    wr_cmd(cmd);
    for (int i = 0; i < n; i++) wr_data(p[i]);
}
#define CMD(c, ...) do { const uint8_t _p[] = { __VA_ARGS__ }; cmd_params(c, _p, sizeof _p); } while (0)

/* ---------------- FSMC ---------------- */
static void fsmc_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_FSMC_CLK_ENABLE();

    GPIO_InitTypeDef g = { .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_NOPULL,
                           .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = GPIO_AF12_FSMC };
    /* PD0/1=D2/3  PD4=NOE  PD5=NWE  PD7=NE1  PD8/9/10=D13/14/15  PD13=A18  PD14/15=D0/1 */
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7 |
            GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &g);
    /* PE7~PE15 = D4~D12 */
    g.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
            GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &g);

    /* 背光 PB1 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, LCD_BL_ON_LEVEL);
    GPIO_InitTypeDef bl = { .Pin = GPIO_PIN_1, .Mode = GPIO_MODE_OUTPUT_PP,
                            .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW };
    HAL_GPIO_Init(GPIOB, &bl);

    hsram.Instance  = FSMC_NORSRAM_DEVICE;
    hsram.Extended  = FSMC_NORSRAM_EXTENDED_DEVICE;
    hsram.Init.NSBank             = FSMC_NORSRAM_BANK1;
    hsram.Init.DataAddressMux     = FSMC_DATA_ADDRESS_MUX_DISABLE;
    hsram.Init.MemoryType         = FSMC_MEMORY_TYPE_SRAM;
    hsram.Init.MemoryDataWidth    = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    hsram.Init.BurstAccessMode    = FSMC_BURST_ACCESS_MODE_DISABLE;
    hsram.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    hsram.Init.WrapMode           = FSMC_WRAP_MODE_DISABLE;
    hsram.Init.WaitSignalActive   = FSMC_WAIT_TIMING_BEFORE_WS;
    hsram.Init.WriteOperation     = FSMC_WRITE_OPERATION_ENABLE;
    hsram.Init.WaitSignal         = FSMC_WAIT_SIGNAL_DISABLE;
    hsram.Init.ExtendedMode       = FSMC_EXTENDED_MODE_ENABLE;   /* 读写时序分开 */
    hsram.Init.AsynchronousWait   = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    hsram.Init.WriteBurst         = FSMC_WRITE_BURST_DISABLE;
    hsram.Init.PageSize           = FSMC_PAGE_SIZE_NONE;

    /* HCLK 168MHz，1 个单位 ≈ 6ns
     * 写时序实测（lcd_bench.c，逐像素读回校验）：A1 D1 开始出现错像素，A1 D2 及以上 0 错
     * 取 A2 D3 留余量：全屏 320x240 约 3.4ms，NES 帧推送已经受 CPU 限制不再变快 */
    FSMC_NORSRAM_TimingTypeDef rd = {
        .AddressSetupTime = 15, .AddressHoldTime = 15, .DataSetupTime = 200,
        .BusTurnAroundDuration = 0, .CLKDivision = 16, .DataLatency = 17,
        .AccessMode = FSMC_ACCESS_MODE_A,
    };
    FSMC_NORSRAM_TimingTypeDef wr = {
        .AddressSetupTime = LCD_WR_ADDSET, .AddressHoldTime = 15, .DataSetupTime = LCD_WR_DATAST,
        .BusTurnAroundDuration = 0, .CLKDivision = 16, .DataLatency = 17,
        .AccessMode = FSMC_ACCESS_MODE_A,
    };
    if (HAL_SRAM_Init(&hsram, &rd, &wr) != HAL_OK) error_handler("HAL_SRAM_Init");
}

/* ---------------- 控制器识别 ---------------- */
static void read_ids(void)
{
    wr_cmd(0xD3);                                    /* ILI9341: xx 00 93 41 */
    for (int i = 0; i < 4; i++) g_lcd.raw_d3[i] = (uint8_t)rd_data();

    wr_cmd(0x0000);                                  /* ILI9325 家族: 读寄存器 0 */
    g_lcd.raw_reg00 = rd_data();
}

/* ---------------- ILI9341 ---------------- */
static void ili9341_init(void)
{
    wr_cmd(0x01); HAL_Delay(120);                    /* 软复位（LCD 硬复位接的是板子 RST）*/
    wr_cmd(0x28);
    CMD(0xCF, 0x00, 0xC1, 0x30);
    CMD(0xED, 0x64, 0x03, 0x12, 0x81);
    CMD(0xE8, 0x85, 0x10, 0x7A);
    CMD(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);
    CMD(0xF7, 0x20);
    CMD(0xEA, 0x00, 0x00);
    CMD(0xC0, 0x1B);                                 /* 电源控制 1 */
    CMD(0xC1, 0x01);                                 /* 电源控制 2 */
    CMD(0xC5, 0x30, 0x30);                           /* VCOM */
    CMD(0xC7, 0xB7);
    CMD(0x36, ILI9341_MADCTL);                       /* 横屏 + BGR */
    CMD(0x3A, 0x55);                                 /* 16 位色 */
    CMD(0xB1, 0x00, 0x1A);                           /* 帧率 */
    CMD(0xB6, 0x0A, 0xA2);
    CMD(0xF2, 0x00);
    CMD(0x26, 0x01);
    CMD(0xE0, 0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00);
    CMD(0xE1, 0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F);
    wr_cmd(0x11); HAL_Delay(120);                    /* 退出睡眠 */
    wr_cmd(0x29);                                    /* 开显示 */

    /* 总线回读自检：读 MADCTL（0x0B）应该等于刚写进去的值 */
    wr_cmd(0x0B);
    (void)rd_data();
    g_lcd.bus_readback_ok = ((rd_data() & 0xFF) == ILI9341_MADCTL);

    g_lcd.width = 320; g_lcd.height = 240;
}

/* ---------------- ILI9325 / ILI9328（竖屏 240x320）---------------- */
static void ili9325_init(void)
{
    static const uint16_t seq[][2] = {
        {0x00E5,0x78F0},{0x0001,0x0100},{0x0002,0x0700},{0x0003,0x1030},{0x0004,0x0000},
        {0x0008,0x0202},{0x0009,0x0000},{0x000A,0x0000},{0x000C,0x0000},{0x000D,0x0000},
        {0x000F,0x0000},{0x0010,0x0000},{0x0011,0x0007},{0x0012,0x0000},{0x0013,0x0000},
        {0x0007,0x0001},{0xFFFF,200},
        {0x0010,0x1690},{0x0011,0x0227},{0xFFFF,50},{0x0012,0x009D},{0xFFFF,50},
        {0x0013,0x1900},{0x0029,0x0025},{0x002B,0x000D},{0xFFFF,50},
        {0x0020,0x0000},{0x0021,0x0000},
        {0x0030,0x0007},{0x0031,0x0303},{0x0032,0x0003},{0x0035,0x0206},{0x0036,0x0008},
        {0x0037,0x0406},{0x0038,0x0304},{0x0039,0x0007},{0x003C,0x0602},{0x003D,0x0008},
        {0x0050,0x0000},{0x0051,0x00EF},{0x0052,0x0000},{0x0053,0x013F},
        {0x0060,0xA700},{0x0061,0x0001},{0x006A,0x0000},
        {0x0080,0x0000},{0x0081,0x0000},{0x0082,0x0000},{0x0083,0x0000},{0x0084,0x0000},{0x0085,0x0000},
        {0x0090,0x0010},{0x0092,0x0600},{0x0007,0x0133},
    };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        if (seq[i][0] == 0xFFFF) HAL_Delay(seq[i][1]);
        else wr_reg(seq[i][0], seq[i][1]);
    }
    g_lcd.width = 240; g_lcd.height = 320;
}

int lcd_init(void)
{
    fsmc_init();
    HAL_Delay(50);
    read_ids();

    uint16_t id9341 = (uint16_t)((g_lcd.raw_d3[2] << 8) | g_lcd.raw_d3[3]);
    if (id9341 == 0x9341) {
        g_lcd.id = 0x9341; g_lcd.name = "ILI9341";
        ili9341_init();
    } else if (g_lcd.raw_reg00 == 0x9325 || g_lcd.raw_reg00 == 0x9328) {
        g_lcd.id = g_lcd.raw_reg00; g_lcd.name = (g_lcd.id == 0x9325) ? "ILI9325" : "ILI9328";
        ili9325_init();
    } else {
        g_lcd.id = 0; g_lcd.name = "unknown";
        return -1;
    }
    lcd_fill(C_BLACK);
    return 0;
}

/* ---------------- 绘图 ---------------- */
void lcd_set_window(int x0, int y0, int x1, int y1)
{
    if (g_lcd.id == 0x9341) {
        wr_cmd(0x2A); wr_data(x0 >> 8); wr_data(x0 & 0xFF); wr_data(x1 >> 8); wr_data(x1 & 0xFF);
        wr_cmd(0x2B); wr_data(y0 >> 8); wr_data(y0 & 0xFF); wr_data(y1 >> 8); wr_data(y1 & 0xFF);
        wr_cmd(0x2C);
    } else {
        wr_reg(0x0050, x0); wr_reg(0x0051, x1);
        wr_reg(0x0052, y0); wr_reg(0x0053, y1);
        wr_reg(0x0020, x0); wr_reg(0x0021, y0);
        wr_cmd(0x0022);
    }
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!g_lcd.id || w <= 0 || h <= 0) return;
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    for (uint32_t n = (uint32_t)w * (uint32_t)h; n; n--) LCD_DATA = color;
}

void lcd_fill(uint16_t color)
{
    lcd_fill_rect(0, 0, g_lcd.width, g_lcd.height, color);
}

void lcd_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    if (!g_lcd.id) return;
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    const uint8_t *glyph = font8x16[c - FONT_FIRST];
    lcd_set_window(x, y, x + FONT_W - 1, y + FONT_H - 1);
    for (int row = 0; row < FONT_H; row++)
        for (int col = 0; col < FONT_W; col++)
            LCD_DATA = (glyph[row] & (0x80 >> col)) ? fg : bg;
}

void lcd_draw_string_scaled(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg)
{
    for (; *s; s++, x += FONT_W * scale) {
        char c = (*s < FONT_FIRST || *s > FONT_LAST) ? '?' : *s;
        const uint8_t *glyph = font8x16[c - FONT_FIRST];
        for (int row = 0; row < FONT_H; row++)
            for (int col = 0; col < FONT_W; col++)
                lcd_fill_rect(x + col * scale, y + row * scale, scale, scale,
                              (glyph[row] & (0x80 >> col)) ? fg : bg);
    }
}

void lcd_set_madctl(uint8_t madctl)
{
    if (g_lcd.id != 0x9341) return;
    CMD(0x36, madctl);
    if (madctl & 0x20) { g_lcd.width = 320; g_lcd.height = 240; }
    else               { g_lcd.width = 240; g_lcd.height = 320; }
}

void lcd_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++, x += FONT_W) lcd_draw_char(x, y, *s, fg, bg);
}

void lcd_color_bars(void)
{
    static const uint16_t bars[] = { C_WHITE, C_YELLOW, C_CYAN, C_GREEN,
                                     C_MAGENTA, C_RED, C_BLUE, C_BLACK };
    int bw = g_lcd.width / 8;
    for (int i = 0; i < 8; i++) lcd_fill_rect(i * bw, 0, bw, g_lcd.height, bars[i]);
    lcd_draw_string(4, 4, "TOP-LEFT", C_BLACK, C_WHITE);
}

/* ---------------- 控制台 ---------------- */
static int s_col, s_row;
static uint16_t s_fg = C_WHITE;

void lcd_con_clear(void)
{
    lcd_fill(C_BLACK);
    s_col = s_row = 0;
}

void lcd_con_color(uint16_t fg) { s_fg = fg; }

void lcd_con_puts(const char *s)
{
    if (!g_lcd.id) return;
    int cols = g_lcd.width / FONT_W, rows = g_lcd.height / FONT_H;
    for (; *s; s++) {
        if (*s == '\r') continue;
        if (*s == '\n' || s_col >= cols) {
            s_col = 0;
            if (++s_row >= rows) lcd_con_clear();
            if (*s == '\n') continue;
        }
        lcd_draw_char(s_col * FONT_W, s_row * FONT_H, *s, s_fg, C_BLACK);
        s_col++;
    }
}
