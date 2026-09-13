/* 外设测试：LCD（FSMC）+ SD 卡（SDIO + FatFs）
 *
 * 结果同时输出到: 屏幕、USART1(J6, 115200)、RAM 日志(scripts/diag.py 用调试器读)
 * LED: 测试中 D2 常亮；全部通过两灯交替慢闪；有失败两灯同时快闪
 */
#include "board.h"
#include "lcd.h"
#include "sdcard.h"
#include "ff.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t magic;          /* 0x7E57AE51 */
    uint32_t stage;          /* 当前跑到第几步，卡死时看这个 */
    uint32_t pass, fail;
    uint32_t done;
} test_result_t;

test_result_t g_test = { .magic = 0x7E57AE51u };

static void lcd_sink(const char *s) { lcd_con_puts(s); }

/* 1 = 跑 LCD 刷新率测试（代替 SD 测试）*/
#define LCD_BENCH 0
void lcd_bench_run(void);

/* ---------------- 屏幕方向选择器 ----------------
 * 1 = 轮播 4 种横屏方向，每种 5 秒。已选定 0xA8（写在 lcd.c），平时保持 0 */
#define LCD_PICK_ORIENTATION 0

#if LCD_PICK_ORIENTATION
static void orientation_picker(void)
{
    static const uint8_t modes[] = { 0x28, 0x68, 0xA8, 0xE8 };
    char line[32];
    for (;;) {
        for (int i = 0; i < 4; i++) {
            lcd_set_madctl(modes[i]);
            lcd_fill(C_BLACK);
            int w = g_lcd.width, h = g_lcd.height;
            lcd_fill_rect(0, 0, 24, 24, C_RED);            /* 左上 红 */
            lcd_fill_rect(w - 24, 0, 24, 24, C_GREEN);     /* 右上 绿 */
            lcd_fill_rect(0, h - 24, 24, 24, C_BLUE);      /* 左下 蓝 */
            lcd_fill_rect(w - 24, h - 24, 24, 24, C_YELLOW);/* 右下 黄 */

            lcd_draw_string_scaled(w / 2 - 12, 20, "^", 3, C_WHITE, C_BLACK);
            line[0] = (char)('1' + i); line[1] = 0;
            lcd_draw_string_scaled(w / 2 - 24, 70, line, 6, C_CYAN, C_BLACK);
            lcd_draw_string_scaled(40, 176, "ABC 123", 3, C_WHITE, C_BLACK);
            snprintf(line, sizeof line, "MADCTL=0x%02X", modes[i]);
            lcd_draw_string(112, 216, line, C_GRAY, C_BLACK);
            HAL_Delay(5000);
        }
    }
}
#endif

static void check(int ok, const char *what)
{
    if (ok) { g_test.pass++; lcd_con_color(C_GREEN); log_printf("[PASS] %s\n", what); }
    else    { g_test.fail++; lcd_con_color(C_RED);   log_printf("[FAIL] %s\n", what); }
    lcd_con_color(C_WHITE);
}

/* ---------------- LCD ---------------- */
static void test_lcd(void)
{
    g_test.stage = 10;
    int r = lcd_init();
    log_printf("LCD 0xD3 : %02X %02X %02X %02X\n",
               g_lcd.raw_d3[0], g_lcd.raw_d3[1], g_lcd.raw_d3[2], g_lcd.raw_d3[3]);
    log_printf("LCD R00  : %04X\n", g_lcd.raw_reg00);
    if (r != 0) { check(0, "LCD controller detect"); return; }

    log_printf("LCD      : %s %ux%u\n", g_lcd.name, g_lcd.width, g_lcd.height);

    g_test.stage = 11;
    uint32_t t0 = HAL_GetTick();
    lcd_color_bars();
    uint32_t t_bars = HAL_GetTick() - t0;
    HAL_Delay(1500);

    t0 = HAL_GetTick();
    for (int i = 0; i < 10; i++) lcd_fill(i & 1 ? C_BLACK : C_BLUE);
    uint32_t t_fill = (HAL_GetTick() - t0) / 10;

    lcd_con_clear();
    log_set_lcd_sink(lcd_sink);          /* 从这里开始日志也上屏 */

    lcd_con_color(C_CYAN);
    log_printf("== STM32F407 LCD + SD test ==\n");
    lcd_con_color(C_WHITE);
    check(1, "LCD controller detect");
    log_printf("  %s %ux%u  ID=%04X\n", g_lcd.name, g_lcd.width, g_lcd.height, g_lcd.id);
    if (g_lcd.id == 0x9341) check(g_lcd.bus_readback_ok, "LCD bus readback (MADCTL)");
    log_printf("  color bars %lums, full fill %lums\n",
               (unsigned long)t_bars, (unsigned long)t_fill);
}

/* ---------------- SD ---------------- */
static const char *card_type(uint32_t t)
{
    switch (t) {
    case CARD_SDSC:          return "SDSC";
    case CARD_SDHC_SDXC:     return "SDHC/SDXC";
    case CARD_SECURED:       return "secured";
    default:                 return "?";
    }
}

static const char *fs_type(BYTE t)
{
    switch (t) {
    case FS_FAT12: return "FAT12";
    case FS_FAT16: return "FAT16";
    case FS_FAT32: return "FAT32";
    case FS_EXFAT: return "exFAT";
    default:       return "?";
    }
}

static FATFS s_fs;
static FIL   s_file;
static uint8_t s_buf[4096];

static void test_sd(void)
{
    g_test.stage = 20;
    log_printf("\n-- SD card (clkdiv %d) --\n", SD_CLOCK_DIV);

    int r = sd_init();
    check(r == 0, "SDIO init + 4-bit bus");
    if (r != 0) return;

    HAL_SD_CardInfoTypeDef info;
    HAL_SD_GetCardInfo(&hsd, &info);
    HAL_SD_CardCIDTypeDef cid;
    HAL_SD_GetCardCID(&hsd, &cid);
    log_printf("  %s, %lu MB, blk %lu, MID %02X\n", card_type(info.CardType),
               (unsigned long)((uint64_t)info.LogBlockNbr * info.LogBlockSize / 1048576u),
               (unsigned long)info.LogBlockSize, cid.ManufacturerID);

    /* 读 MBR：最后两个字节应为 55 AA */
    g_test.stage = 21;
    r = sd_read(s_buf, 0, 1);
    check(r == 0 && s_buf[510] == 0x55 && s_buf[511] == 0xAA, "read block 0 (MBR 55AA)");

    g_test.stage = 22;
    FRESULT fr = f_mount(&s_fs, "", 1);
    check(fr == FR_OK, "f_mount");
    if (fr != FR_OK) { log_printf("  f_mount err=%d\n", fr); return; }

    DWORD free_clust;
    FATFS *pfs;
    fr = f_getfree("", &free_clust, &pfs);
    if (fr == FR_OK) {
        uint32_t clus_kb = (uint32_t)pfs->csize * 512u / 1024u;
        log_printf("  %s, cluster %luK, total %lu MB, free %lu MB\n", fs_type(pfs->fs_type),
                   (unsigned long)clus_kb,
                   (unsigned long)((pfs->n_fatent - 2) / 1024u * clus_kb),
                   (unsigned long)(free_clust / 1024u * clus_kb));
    }

    /* 读 PC 上写好的 TEST.TXT */
    g_test.stage = 23;
    UINT br = 0;
    fr = f_open(&s_file, "TEST.TXT", FA_READ);
    if (fr == FR_OK) {
        fr = f_read(&s_file, s_buf, sizeof s_buf - 1, &br);
        f_close(&s_file);
        s_buf[br] = 0;
    }
    check(fr == FR_OK && strncmp((char *)s_buf, "HELLO STM32F407", 15) == 0, "read TEST.TXT");
    if (fr == FR_OK) {
        lcd_con_color(C_YELLOW);
        log_printf("%s", (char *)s_buf);
        lcd_con_color(C_WHITE);
    } else {
        log_printf("  f_open/f_read err=%d\n", fr);
    }

    /* 列根目录 */
    g_test.stage = 24;
    DIR dir; FILINFO fno; int n = 0;
    if (f_opendir(&dir, "/") == FR_OK) {
        log_printf("  root:");
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] && n < 8) {
            log_printf(" %s%s", fno.fname, (fno.fattrib & AM_DIR) ? "/" : "");
            n++;
        }
        log_printf("\n");
        f_closedir(&dir);
    }

    /* 写文件再读回比对 */
    g_test.stage = 25;
    static const char msg[] = "Written by STM32F407 via SDIO+FatFs\r\n";
    UINT bw = 0;
    fr = f_open(&s_file, "STM32.TXT", FA_CREATE_ALWAYS | FA_WRITE);
    if (fr == FR_OK) { fr = f_write(&s_file, msg, sizeof msg - 1, &bw); f_close(&s_file); }
    int ok = (fr == FR_OK && bw == sizeof msg - 1);
    if (ok) {
        fr = f_open(&s_file, "STM32.TXT", FA_READ);
        if (fr == FR_OK) { fr = f_read(&s_file, s_buf, sizeof s_buf, &br); f_close(&s_file); }
        ok = (fr == FR_OK && br == sizeof msg - 1 && memcmp(s_buf, msg, br) == 0);
    }
    check(ok, "write + readback STM32.TXT");

    /* 速度：写 1MB 再读回校验 */
    g_test.stage = 26;
    const uint32_t total = 1024u * 1024u;
    uint32_t t_w = 0, t_r = 0, done = 0;
    int data_ok = 1;
    fr = f_open(&s_file, "SPEED.BIN", FA_CREATE_ALWAYS | FA_WRITE);
    if (fr == FR_OK) {
        uint32_t t0 = HAL_GetTick();
        for (done = 0; done < total && fr == FR_OK; done += sizeof s_buf) {
            for (unsigned i = 0; i < sizeof s_buf; i++) s_buf[i] = (uint8_t)((done >> 12) + i);
            fr = f_write(&s_file, s_buf, sizeof s_buf, &bw);
        }
        f_close(&s_file);
        t_w = HAL_GetTick() - t0;
    }
    if (fr == FR_OK) {
        fr = f_open(&s_file, "SPEED.BIN", FA_READ);
        uint32_t t0 = HAL_GetTick();
        for (done = 0; done < total && fr == FR_OK; done += sizeof s_buf) {
            fr = f_read(&s_file, s_buf, sizeof s_buf, &br);
            for (unsigned i = 0; i < br; i++)
                if (s_buf[i] != (uint8_t)((done >> 12) + i)) { data_ok = 0; break; }
        }
        f_close(&s_file);
        t_r = HAL_GetTick() - t0;
        f_unlink("SPEED.BIN");
    }
    check(fr == FR_OK && data_ok, "1MB write/read verify");
    if (fr == FR_OK && t_w && t_r)
        log_printf("  write %lu KB/s, read %lu KB/s\n",
                   (unsigned long)(1024000u / t_w), (unsigned long)(1024000u / t_r));
    else
        log_printf("  err=%d data_ok=%d\n", fr, data_ok);

    f_mount(NULL, "", 0);
}

int main(void)
{
    board_init();
    led_set(LED1_PIN, 1);

    log_printf("\n\n===== STM32F407 LCD + SD test =====\n");
    log_printf("SYSCLK %lu  HCLK %lu  PCLK1 %lu  PCLK2 %lu\n",
               (unsigned long)HAL_RCC_GetSysClockFreq(), (unsigned long)HAL_RCC_GetHCLKFreq(),
               (unsigned long)HAL_RCC_GetPCLK1Freq(), (unsigned long)HAL_RCC_GetPCLK2Freq());

    test_lcd();
#if LCD_PICK_ORIENTATION
    if (g_lcd.id == 0x9341) orientation_picker();
#endif
#if LCD_BENCH
    if (g_lcd.id) { log_set_lcd_sink(NULL); lcd_bench_run(); } else
#endif
    test_sd();

    g_test.stage = 99;
    g_test.done  = 1;
    lcd_con_color(g_test.fail ? C_RED : C_GREEN);
    log_printf("\n== %lu passed, %lu failed ==\n", (unsigned long)g_test.pass, (unsigned long)g_test.fail);

    for (;;) {
        if (g_test.fail) {
            led_set(LED1_PIN, 1); led_set(LED2_PIN, 1); HAL_Delay(100);
            led_set(LED1_PIN, 0); led_set(LED2_PIN, 0); HAL_Delay(100);
        } else {
            led_set(LED1_PIN, 1); led_set(LED2_PIN, 0); HAL_Delay(500);
            led_set(LED1_PIN, 0); led_set(LED2_PIN, 1); HAL_Delay(500);
        }
    }
}
