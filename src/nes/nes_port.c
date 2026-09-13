/* InfoNES 平台层（STM32F407 + ILI9341 + SDIO）
 *
 * 流程: InfoNES_Main → InfoNES_Menu（选 ROM、准备 ROM 指针）→ InfoNES_Cycle（跑游戏）
 *       游戏中按住 SELECT+START 1 秒退回菜单
 *
 * ROM 放哪:  ≤ NES_ROM_RAM_MAX 的拷进 RAM（不用擦写 Flash）
 *           更大的写进内部 Flash 存储区直接跑（rom_store.c）。实测 Flash 上跑和 RAM 上跑速度差不多
 *
 * 调试开关（编译时 -D 定义）:
 *   NES_AUTOPLAY="start:150-156,right:300-600"  按帧号自动按键，无人值守测试用
 *   NES_CRC_FRAMES="100,300,450,600"             这些帧算画面 CRC32，和 host/nes_host 输出对比
 *   NES_NO_PACING                                不限速，测最高帧率
 *   NES_AUTOSTART_MS=3000                        菜单无操作 N 毫秒后自动开始（默认 0 = 不自动开始，只给无人值守测试用）
 *   NES_BENCH                                    统计第 300~900 帧的平均耗时并写日志
 *   NES_FRAMESKIP=n                              固定跳帧、关闭自动降级（n=60000 相当于完全不渲染，测纯 CPU 模拟开销）
 *   NES_NO_INTERLACE                             自动降级时不使用隔行渲染，直接跳帧
 *   NES_NO_AUDIO                                 关掉声音（APU 不模拟，省 CPU）
 *   NES_MENU_PICK="2048.nes"                     菜单默认选中指定文件（自动化测试用）
 *   NES_TEST_SAVE                                把任何 ROM 都当成带电池，每次检查前改一下 SRAM[0]，测存档流程
 *   NES_SAVE_CHECK_FRAMES=n                      存档检查间隔（帧），默认 1800
 *
 * 存档: 带电池的游戏（iNES 头 flags6 bit1）会把 SRAM 存到 SD:/NES/SAVE/<ROM 名>.sav
 *       开始游戏时读回；游戏中每 30 秒检查一次，有变化就写；退回菜单时也写
 */
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"

#include "board.h"
#include "lcd.h"
#include "sdcard.h"
#include "ff.h"
#include "rom_store.h"
#include "input.h"
#include "audio.h"
#include "filebrowser.h"
#include "nes_common.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#ifndef NES_AUTOSTART_MS
#define NES_AUTOSTART_MS 0   /* 0 = 等按键；scripts/nes-bench.sh 会设成 500 */
#endif
#ifndef NES_SAVE_CHECK_FRAMES
#define NES_SAVE_CHECK_FRAMES 1800   /* 30 秒 */
#endif
#ifndef NES_AUTOPLAY
#define NES_AUTOPLAY ""
#endif
#ifndef NES_CRC_FRAMES
#define NES_CRC_FRAMES ""
#endif

#define NES_ROM_RAM_MAX  (44u * 1024u)       /* 放得下 SMB（40KB）这类 NROM */
#define NES_X0           ((320 - NES_DISP_WIDTH) / 2)
#define NES_DIR          "/NES"
#define ROM_DIR          "/NES/ROMS"     /* .nes */
#define SAVE_DIR         "/NES/SAVE"     /* 存档 .sav */
#define LAST_FILE        "/NES/LAST.TXT" /* 上次玩的游戏 */
#define MAX_ROMS         32

/* InfoNES 自带调色板转成 RGB565（绿色最低位恒为 0，留给 NES_BG_FLAG）*/
WORD NesPalette[64] = {
    0x738e, 0x20d1, 0x0015, 0x4013, 0x880e, 0xa802, 0xa000, 0x7840,
    0x4140, 0x0200, 0x0280, 0x01c2, 0x19cb, 0x0000, 0x0000, 0x0000,
    0xbdd7, 0x039d, 0x21dd, 0x801e, 0xb817, 0xe00b, 0xd940, 0xca41,
    0x8b80, 0x0480, 0x0540, 0x0487, 0x0411, 0x0000, 0x0000, 0x0000,
    0xffdf, 0x3ddf, 0x5c9f, 0x445f, 0xf3df, 0xfb96, 0xfb8c, 0xfcc7,
    0xf5c7, 0x8682, 0x4ec9, 0x5fd3, 0x075b, 0x0000, 0x0000, 0x0000,
    0xffdf, 0xaf1f, 0xc69f, 0xd65f, 0xfe1f, 0xfe1b, 0xfdd6, 0xfed5,
    0xff14, 0xe7d4, 0xaf97, 0xb7d9, 0x9fde, 0x0000, 0x0000, 0x0000,
};

/* ---------------- 调试器可读的统计（scripts/diag.py 会显示）---------------- */
typedef struct {
    uint32_t magic;          /* 0x4E455321 "NES!" */
    uint32_t frames;         /* VBlank 计数 */
    uint32_t fps_x10;        /* 最近 1 秒平均帧率 ×10 */
    uint32_t emu_us;         /* 最近 1 秒平均每帧耗时（不含限速等待）*/
    uint32_t lcd_us;         /* 其中推屏耗时 */
    uint32_t frame_skip;     /* 实际存的是自动降级档位 s_quality */
    uint32_t rom_in_ram;
    uint32_t rom_size;
    char     rom_name[48];
} nes_stats_t;
nes_stats_t g_nes = { .magic = 0x4E455321u };

static uint8_t s_rom_ram[NES_ROM_RAM_MAX];
static const uint8_t *s_rom;        /* 当前 .nes 文件（含 16 字节头）*/
static uint32_t s_rom_size;
static char s_rom_name[64];

static FATFS s_fs;
static int s_sd_ok;

/* ---------------- 计时 ---------------- */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
static inline uint32_t cyc_to_us(uint32_t cyc) { return (uint32_t)((uint64_t)cyc * 1000000u / SystemCoreClock); }

/* ---------------- 屏幕文字（菜单用）---------------- */
static void text(int x, int y, uint16_t fg, const char *fmt, ...)
{
    char buf[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    lcd_draw_string(x, y, buf, fg, C_BLACK);
}

/* ---------------- SD 卡 ---------------- */
static void migrate_old_layout(void);

static int sd_mount(void)
{
    if (s_sd_ok) return 0;
    if (f_mount(&s_fs, "", 1) != FR_OK) return -1;
    f_mkdir(NES_DIR);                  /* 已存在会返回 FR_EXIST，忽略 */
    f_mkdir(ROM_DIR);
    f_mkdir(SAVE_DIR);
    migrate_old_layout();
    s_sd_ok = 1;
    return 0;
}

/* 兼容旧目录结构：
 *   /NES 下的 .nes       → /NES/ROMS/
 *   /NES 下的 .sav       → /NES/SAVE/
 *   /NES/SAVE/LAST.TXT   → /NES/LAST.TXT */
static void move_file(const char *from, const char *to)
{
    FILINFO fno;
    if (f_stat(from, &fno) != FR_OK) return;
    f_unlink(to);                        /* 目标已存在时以旧位置的为准 */
    FRESULT fr = f_rename(from, to);
    log_printf("migrate: %s -> %s %s\n", from, to, fr == FR_OK ? "ok" : "FAIL");
}

static void migrate_old_layout(void)
{
    static char names[MAX_ROMS][64];
    int n = 0;
    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, NES_DIR) == FR_OK) {
        while (n < MAX_ROMS && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
            if (fno.fattrib & AM_DIR) continue;
            size_t len = strlen(fno.fname);
            if (len > 4 && (strcasecmp(fno.fname + len - 4, ".nes") == 0 || strcasecmp(fno.fname + len - 4, ".sav") == 0))
                snprintf(names[n++], 64, "%.63s", fno.fname);
        }
        f_closedir(&dir);   /* 遍历完再改名，不在遍历途中改目录 */
    }
    for (int i = 0; i < n; i++) {
        char from[96], to[96];
        size_t len = strlen(names[i]);
        snprintf(from, sizeof from, NES_DIR "/%.63s", names[i]);
        snprintf(to, sizeof to, "%s/%.63s", strcasecmp(names[i] + len - 4, ".nes") == 0 ? ROM_DIR : SAVE_DIR, names[i]);
        move_file(from, to);
    }
    move_file(SAVE_DIR "/LAST.TXT", LAST_FILE);
}

static int has_nes_ext(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && (strcasecmp(name + n - 4, ".nes") == 0);
}

static int scan_roms(char names[][64], int max)
{
    DIR dir;
    FILINFO fno;
    int n = 0;
    if (f_opendir(&dir, ROM_DIR) != FR_OK) return 0;
    while (n < max && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if ((fno.fattrib & AM_DIR) || !has_nes_ext(fno.fname)) continue;
        snprintf(names[n], 64, "%.63s", fno.fname);
        n++;
    }
    f_closedir(&dir);
    /* 简单排序 */
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (strcasecmp(names[j], names[i]) < 0) {
                char t[64]; strcpy(t, names[i]); strcpy(names[i], names[j]); strcpy(names[j], t);
            }
    return n;
}

/* 开发便利：Flash 存储区里有 ROM（PC 端 scripts/flash-rom.sh 烧进去的）但 SD 上没有，就导出一份到 SD */
static void export_store_to_sd(void)
{
    const rom_store_hdr_t *h = rom_store_get();
    if (!h || !s_sd_ok) return;
    char path[96];
    snprintf(path, sizeof path, ROM_DIR "/%s", h->name);
    FILINFO fno;
    if (f_stat(path, &fno) == FR_OK && fno.fsize == h->size) return;

    FIL f;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        log_printf("export: open %s fail (long name? FF_USE_LFN)\n", path);
        return;
    }
    UINT bw = 0;
    FRESULT fr = f_write(&f, rom_store_data(), h->size, &bw);
    f_close(&f);
    log_printf("export: %s -> SD %s (%lu bytes, fr=%d)\n", h->name,
               (fr == FR_OK && bw == h->size) ? "ok" : "FAIL", (unsigned long)bw, fr);
}

/* 记住上次玩的游戏（SD:/NES/LAST.TXT），菜单默认选中它 */
static void last_save(const char *name)
{
    FIL f;
    if (!s_sd_ok || f_open(&f, LAST_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return;
    UINT bw;
    f_write(&f, name, (UINT)strlen(name), &bw);
    f_close(&f);
}

static int last_load(char *out, size_t size)
{
    FIL f;
    if (!s_sd_ok || f_open(&f, LAST_FILE, FA_READ) != FR_OK) return -1;
    UINT br = 0;
    f_read(&f, out, (UINT)(size - 1), &br);
    f_close(&f);
    out[br] = 0;
    return br ? 0 : -1;
}

/* ---------------- 存档（电池 SRAM）---------------- */
static char s_save_path[96];
static uint32_t s_sram_crc;
static uint32_t s_deadline;   /* 帧限速的下一个截止时刻（DWT 周期数），0 = 重新同步 */

static int has_battery(void)
{
#ifdef NES_TEST_SAVE
    return 1;
#else
    return ROM_SRAM != 0;
#endif
}

static void save_path_for(const char *rom)
{
    const char *dot = strrchr(rom, '.');
    int n = dot ? (int)(dot - rom) : (int)strlen(rom);
    snprintf(s_save_path, sizeof s_save_path, SAVE_DIR "/%.*s.sav", n, rom);
}

static void sram_load(void)
{
    s_save_path[0] = 0;
    if (!has_battery() || !s_sd_ok) return;
    save_path_for(s_rom_name);
    FIL f;
    UINT br = 0;
    if (f_open(&f, s_save_path, FA_READ) == FR_OK) {
        f_read(&f, SRAM, SRAM_SIZE, &br);
        f_close(&f);
    }
    s_sram_crc = nes_crc32(0, SRAM, SRAM_SIZE);
    log_printf("save: %s %s (SRAM[0]=%u)\n", s_save_path, br == SRAM_SIZE ? "loaded" : "new", SRAM[0]);
}

static void sram_save_if_changed(void)
{
    if (!s_save_path[0]) return;
    uint32_t crc = nes_crc32(0, SRAM, SRAM_SIZE);
    if (crc == s_sram_crc) return;
    FIL f;
    UINT bw = 0;
    if (f_open(&f, s_save_path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
        f_write(&f, SRAM, SRAM_SIZE, &bw);
        f_close(&f);
    }
    if (bw == SRAM_SIZE) s_sram_crc = crc;
    s_deadline = 0;   /* 写卡要十几毫秒，别让限速去追这段时间（否则会误触发自动降级）*/
    log_printf("save: write %s %s (SRAM[0]=%u)\n", s_save_path, bw == SRAM_SIZE ? "ok" : "FAIL", SRAM[0]);
}

/* 从 SD 读 ROM：小的进 RAM，大的写 Flash 存储区（已经是同一个就跳过）
 * path 是完整路径，name 是文件名（用于显示、存档名和 Flash 存储区记录）*/
static int load_from_sd(const char *path, const char *name)
{
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return -1;
    uint32_t size = (uint32_t)f_size(&f);
    if (size < 16 || size > ROM_STORE_MAX_ROM) {
        f_close(&f);
        log_printf("load: %s size %lu not supported (max %lu)\n", name, (unsigned long)size,
                   (unsigned long)ROM_STORE_MAX_ROM);
        return -2;
    }

    UINT br = 0;
    if (size <= NES_ROM_RAM_MAX) {
        FRESULT fr = f_read(&f, s_rom_ram, size, &br);
        f_close(&f);
        if (fr != FR_OK || br != size) return -3;
        s_rom = s_rom_ram;
        g_nes.rom_in_ram = 1;
    } else {
        /* 先算 CRC，和存储区一样就不重写 Flash */
        static uint8_t buf[4096];
        uint32_t crc = 0, done = 0;
        while (done < size) {
            if (f_read(&f, buf, sizeof buf, &br) != FR_OK || br == 0) break;
            crc = nes_crc32(crc, buf, br);
            done += br;
        }
        const rom_store_hdr_t *h = rom_store_get();
        if (!(h && h->size == size && h->crc32 == crc)) {
            lcd_fill_rect(0, 200, 320, 16, C_BLACK);
            text(8, 200, C_YELLOW, "Writing flash, %luKB ...", (unsigned long)(size / 1024));
            uint32_t t0 = HAL_GetTick();
            f_lseek(&f, 0);
            if (rom_store_begin() != 0) { f_close(&f); return -4; }
            for (done = 0; done < size; done += br) {
                if (f_read(&f, buf, sizeof buf, &br) != FR_OK || br == 0) break;
                if (rom_store_write(done, buf, br) != 0) { f_close(&f); return -5; }
            }
            if (rom_store_finish(name, size, crc) != 0) { f_close(&f); return -6; }
            log_printf("load: flashed %s in %lums\n", name, (unsigned long)(HAL_GetTick() - t0));
        }
        f_close(&f);
        s_rom = rom_store_data();
        g_nes.rom_in_ram = 0;
    }
    s_rom_size = size;
    snprintf(s_rom_name, sizeof s_rom_name, "%s", name);
    return 0;
}

static int load_from_store(void)
{
    const rom_store_hdr_t *h = rom_store_get();
    if (!h) return -1;
    if (h->size <= NES_ROM_RAM_MAX) {
        memcpy(s_rom_ram, rom_store_data(), h->size);
        s_rom = s_rom_ram;
        g_nes.rom_in_ram = 1;
    } else {
        s_rom = rom_store_data();
        g_nes.rom_in_ram = 0;
    }
    s_rom_size = h->size;
    snprintf(s_rom_name, sizeof s_rom_name, "%.63s", h->name);
    return 0;
}

/* ---------------- 菜单 ---------------- */
static uint8_t wait_buttons_released(void)
{
    while (input_read()) HAL_Delay(10);
    return 0;
}

static int s_autostart_off;   /* 加载失败过就不再自动开始，免得反复加载同一个坏 ROM */

/* 显示一次菜单并加载选中的 ROM。成功返回 0 */
static void draw_menu_header(void)
{
    lcd_fill(C_BLACK);
    lcd_draw_string_scaled(60, 8, "NES", 3, C_RED, C_BLACK);
    text(140, 24, C_GRAY, "STM32F407 InfoNES");
}

static int menu_once(void)
{
    static char names[MAX_ROMS][64];
    char browse_path[128] = "";
    draw_menu_header();

    int sd = (sd_init() == 0 && sd_mount() == 0);
    if (sd) export_store_to_sd();
    int n = sd ? scan_roms(names, MAX_ROMS) : 0;

    const rom_store_hdr_t *store = rom_store_get();
    log_printf("menu: sd=%d roms=%d store=%s\n", sd, n, store ? store->name : "(empty)");

    int sel = 0;
    if (n == 0) {
        if (!store) {
            text(8, 80, C_RED, sd ? "No ROM in SD:/NES/ROMS/" : "SD card not found");
            text(8, 100, C_WHITE, "and flash ROM store is empty.");
            text(8, 130, C_GRAY, "PC: scripts/flash-rom.sh xxx.nes");
            if (sd) text(8, 222, C_GRAY, "B: browse SD card");
            for (;;) {
                if (sd && (input_read() & NES_BTN_B)) {
                    if (file_browser(browse_path, sizeof browse_path)) goto load;
                    return -1;   /* 重新进菜单 */
                }
                HAL_Delay(20);
            }
        }
    } else {
        char last[64];
        const char *want = (last_load(last, sizeof last) == 0) ? last : (store ? store->name : "");
#ifdef NES_MENU_PICK
        want = NES_MENU_PICK;
#endif
        for (int i = 0; i < n; i++) if (strcmp(names[i], want) == 0) sel = i;
    }

    /* 列表 + 自动开始倒计时 */
    /* 屏幕高 240，字高 16：列表 48~176，倒计时 180，帮助 204/222（最后一行到 238）*/
    const int top = 48, rows = 8;
    uint32_t t_last = HAL_GetTick();
    uint8_t prev = wait_buttons_released();
    int drawn_sel = -1, drawn_left = -1;
    for (;;) {
        int first = (sel >= rows) ? sel - rows + 1 : 0;
        if (drawn_sel != sel) {
            lcd_fill_rect(0, top, 320, rows * 16, C_BLACK);
            if (n == 0) {
                text(8, top, C_YELLOW, "> [flash] %.32s", store->name);
            } else {
                for (int i = first; i < n && i < first + rows; i++)
                    text(8, top + (i - first) * 16, i == sel ? C_YELLOW : C_WHITE, "%c %.36s",
                         i == sel ? '>' : ' ', names[i]);
            }
            text(8, 204, C_GRAY, "UP/DOWN/SELECT(KEY0): choose  B: files");
            text(8, 222, C_GRAY, "A/START(KEY1): play");
            drawn_sel = sel;
        }
        int autostart = (NES_AUTOSTART_MS > 0) && !s_autostart_off;
        int left = autostart ? (int)NES_AUTOSTART_MS - (int)(HAL_GetTick() - t_last) : 1000000;
        if (left / 1000 != drawn_left) {
            lcd_fill_rect(0, 180, 320, 16, C_BLACK);
            if (autostart && left > 0) text(8, 180, C_CYAN, "auto start in %d s", left / 1000 + 1);
            drawn_left = left / 1000;
        }
        if (left <= 0) break;

        uint8_t b = input_read(), pressed = b & ~prev;
        prev = b;
        if (pressed) t_last = HAL_GetTick();
        if (pressed & (NES_BTN_A | NES_BTN_START)) break;
        if (sd && (pressed & NES_BTN_B)) {
            if (file_browser(browse_path, sizeof browse_path)) break;
            draw_menu_header();
            drawn_sel = -1; drawn_left = -1;
            prev = wait_buttons_released();
            continue;
        }
        if (n && (pressed & (NES_BTN_DOWN | NES_BTN_SELECT))) sel = (sel + 1) % n;   /* SELECT 也能翻：只用板载按键时 KEY0 翻页 */
        if (n && (pressed & NES_BTN_UP))   sel = (sel + n - 1) % n;
        HAL_Delay(20);
    }

load:
    lcd_fill_rect(0, 180, 320, 60, C_BLACK);
    text(8, 180, C_WHITE, "Loading ...");
    int r;
    int from_list = 0;
    if (browse_path[0]) {                      /* 从文件管理里选的 */
        const char *slash = strrchr(browse_path, '/');
        r = load_from_sd(browse_path, slash ? slash + 1 : browse_path);
    } else if (n > 0) {
        char path[96];
        snprintf(path, sizeof path, ROM_DIR "/%s", names[sel]);
        r = load_from_sd(path, names[sel]);
        from_list = 1;
    } else {
        r = load_from_store();
    }
#ifdef NES_TEST_LOAD_FAIL
    { static int once; if (!once++) r = -99; }
#endif
    if (r != 0) {
        InfoNES_MessageBox((char *)"Load failed (%d)", r);
        return -1;
    }
    if (InfoNES_Load(s_rom_name) != 0) {   /* 比如不支持的 Mapper，InfoNES 自己会弹提示 */
        return -1;
    }
    if (from_list) last_save(names[sel]);
#ifdef NES_NO_AUDIO
    APU_Mute = 1;
#else
    APU_Mute = 0;
#endif
    sram_load();
#ifdef NES_FRAMESKIP
    FrameSkip = NES_FRAMESKIP;
#endif
    snprintf(g_nes.rom_name, sizeof g_nes.rom_name, "%.47s", s_rom_name);
    g_nes.rom_size = s_rom_size;
    log_printf("run: %s %lu bytes, %s, mapper %d\n", s_rom_name, (unsigned long)s_rom_size,
               g_nes.rom_in_ram ? "RAM" : "flash", MapperNo);
    lcd_fill(C_BLACK);
    wait_buttons_released();
    return 0;
}

int InfoNES_Menu()
{
    sram_save_if_changed();   /* 从游戏退回菜单时 */
#ifndef NES_NO_AUDIO
    audio_silence();
#endif
    while (menu_once() != 0) {
        log_printf("menu: load failed, autostart disabled\n");
        s_autostart_off = 1;
#ifdef NES_TEST_LOAD_FAIL
        s_autostart_off = 0;  /* 测试时仍然自动开始，好让流程继续 */
#endif
    }
    return 0;
}

/* ---------------- ROM ---------------- */
int InfoNES_ReadRom(const char *pszFileName)
{
    (void)pszFileName;   /* ROM 已在菜单里准备好，见 s_rom */
    if (!s_rom || s_rom_size < 16) return -1;
    memcpy(&NesHeader, s_rom, sizeof NesHeader);
    if (memcmp(NesHeader.byID, "NES\x1a", 4) != 0) {
        InfoNES_MessageBox((char *)"Not an iNES file");
        return -1;
    }
    uint32_t off = 16;
    memset(SRAM, 0, SRAM_SIZE);
    if (NesHeader.byInfo1 & 4) {
        memcpy(&SRAM[0x1000], s_rom + off, 512);
        off += 512;
    }
    uint32_t need = off + NesHeader.byRomSize * 0x4000u + NesHeader.byVRomSize * 0x2000u;
    if (need > s_rom_size) {
        InfoNES_MessageBox((char *)"ROM truncated: need %lu have %lu", (unsigned long)need, (unsigned long)s_rom_size);
        return -1;
    }
    ROM  = (BYTE *)(s_rom + off);
    VROM = NesHeader.byVRomSize ? (BYTE *)(s_rom + off + NesHeader.byRomSize * 0x4000u) : NULL;
    return 0;
}

void InfoNES_ReleaseRom()
{
    ROM = NULL;
    VROM = NULL;
}

/* ---------------- 画面 ---------------- */
static uint32_t s_crc;
static int s_crc_this_frame;
static uint32_t s_lcd_cyc;

void InfoNES_LoadLine(int y, const WORD *line)
{
    uint32_t c0 = DWT->CYCCNT;
    /* 直接写 ILI9341 窗口命令（不调 lcd.c，避免跳回 Flash）*/
    const uint16_t x1 = NES_X0 + NES_DISP_WIDTH - 1;
    LCD_CMD_PORT = 0x2A; LCD_DATA_PORT = NES_X0 >> 8; LCD_DATA_PORT = NES_X0 & 0xFF;
    LCD_DATA_PORT = x1 >> 8; LCD_DATA_PORT = x1 & 0xFF;
    LCD_CMD_PORT = 0x2B; LCD_DATA_PORT = (uint16_t)(y >> 8); LCD_DATA_PORT = (uint16_t)(y & 0xFF);
    LCD_DATA_PORT = (uint16_t)(y >> 8); LCD_DATA_PORT = (uint16_t)(y & 0xFF);
    LCD_CMD_PORT = 0x2C;
    if (s_crc_this_frame) {
        uint8_t le[NES_DISP_WIDTH * 2];
        for (int x = 0; x < NES_DISP_WIDTH; x++) {
            uint16_t c = nes_to_rgb565(line[x]);
            LCD_DATA_PORT = c;
            le[2 * x] = (uint8_t)c;
            le[2 * x + 1] = (uint8_t)(c >> 8);
        }
        s_crc = nes_crc32(s_crc, le, sizeof le);
    } else {
        for (int x = 0; x < NES_DISP_WIDTH; x++) LCD_DATA_PORT = nes_to_rgb565(line[x]);
    }
    s_lcd_cyc += DWT->CYCCNT - c0;
}

void InfoNES_LoadFrame() {}

/* 隔行渲染：奇偶帧各画一半的行，另一半保留屏幕上的旧内容。
 * 每帧渲染+推屏的耗时减半，画面仍然 60fps 刷新，代价是运动时有轻微的梳状纹 */
static uint8_t s_interlace, s_field;

int InfoNES_ShouldDrawLine(int nLine)
{
    return !s_interlace || ((nLine ^ s_field) & 1) == 0;
}

/* 自动降级档位：0=全画质  1=隔行  2=跳 1 帧  3=跳 2 帧（NES_NO_INTERLACE 时没有隔行这一档）*/
static int s_quality;

static void apply_quality(int level)
{
#ifdef NES_NO_INTERLACE
    s_interlace = 0;
    FrameSkip = (WORD)level;
#else
    s_interlace = (level >= 1);
    FrameSkip = (WORD)(level >= 2 ? level - 1 : 0);
    /* 跳帧时也保持隔行：只画被选中帧的一半行，余量更大 */
#endif
    s_quality = level;
}

/* ---------------- 手柄 + 帧同步（每个 VBlank 调一次）---------------- */
static int crc_frame_listed(uint32_t f)
{
    const char *p = NES_CRC_FRAMES;
    while (*p) {
        char *end;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p) break;
        p = end;
        if (v == f) return 1;
        while (*p == ',' || *p == ' ') p++;
    }
    return 0;
}

void InfoNES_PadState(DWORD *pad1, DWORD *pad2, DWORD *system)
{
    static uint32_t last_cyc, win_start_cyc, win_frames, win_emu_cyc, win_lcd_cyc, quit_hold_ms, last_tick;

    uint32_t now = DWT->CYCCNT;
    g_nes.frames++;
    s_field ^= 1;

#ifdef NES_BENCH
    {
        static uint32_t b_cyc, b_lcd, b_work;
        if (g_nes.frames == 300) { b_cyc = now; b_lcd = 0; b_work = 0; }
        else if (g_nes.frames > 300 && g_nes.frames <= 900) { b_lcd += s_lcd_cyc; b_work += now - last_cyc; }
        if (g_nes.frames == 900)
            log_printf("bench 300-900: %lu us/frame, work %lu us, lcd %lu us, skip %u, quality %d, audio p2p %u resync %lu\n",
                       (unsigned long)(cyc_to_us(now - b_cyc) / 600u), (unsigned long)(cyc_to_us(b_work) / 600u),
                       (unsigned long)(cyc_to_us(b_lcd) / 600u),
                       (unsigned)FrameSkip, s_quality, g_audio_p2p, (unsigned long)g_audio_resync);
    }
#endif

    /* 当前帧（刚画完的）CRC */
    if (s_crc_this_frame) {
        log_printf("frame %lu crc %08lX\n", (unsigned long)g_nes.frames, (unsigned long)s_crc);
    }
    s_crc_this_frame = crc_frame_listed(g_nes.frames + 1);
    s_crc = 0;

    /* 统计 */
    if (last_cyc) {
        win_frames++;
        win_emu_cyc += now - last_cyc;
        win_lcd_cyc += s_lcd_cyc;
    } else {
        win_start_cyc = now;
    }
    s_lcd_cyc = 0;
    if (cyc_to_us(now - win_start_cyc) >= 1000000u && win_frames) {
        uint32_t span_us = cyc_to_us(now - win_start_cyc);
        g_nes.fps_x10 = (uint32_t)((uint64_t)win_frames * 10000000u / span_us);
        g_nes.emu_us  = cyc_to_us(win_emu_cyc) / win_frames;   /* last_cyc 取在限速等待之后，已不含等待 */
        g_nes.lcd_us  = cyc_to_us(win_lcd_cyc) / win_frames;
        g_nes.frame_skip = (uint32_t)s_quality;
        /* 左边黑边上显示帧率 */
        char s[12];
        snprintf(s, sizeof s, "%2lu", (unsigned long)(g_nes.fps_x10 / 10) % 100);
        lcd_draw_string(4, 0, s, C_GRAY, C_BLACK);
        win_start_cyc = now; win_frames = 0; win_emu_cyc = 0; win_lcd_cyc = 0;
    }

#ifndef NES_NO_PACING
    /* 限速到 NTSC 60.0988Hz，按绝对时间表排期：某帧慢了，后面快的帧会把时间追回来 */
    {
        const uint32_t period = (uint32_t)((uint64_t)SystemCoreClock * 16639u / 1000000u);
        uint32_t t = DWT->CYCCNT;
        if (!s_deadline) s_deadline = t;
        s_deadline += period;
        int32_t slack = (int32_t)(s_deadline - t);
        if (slack > 0) {
            while ((int32_t)(s_deadline - DWT->CYCCNT) > 0) {}
        } else if (-slack > (int32_t)(period * 3)) {
            s_deadline = DWT->CYCCNT;        /* 落后太多（比如刚从菜单回来），不追了 */
        }

#ifndef NES_FRAMESKIP
        /* 自动降级：只在一个跳帧周期结束时判断（此时 FrameCnt 已回到 0），
         * 周期内慢帧欠的时间会被快帧追回，所以看的是"整个周期结束时还欠不欠" */
        if (FrameCnt == 0) {
            /* 连续 3 个周期结束时仍落后 1/4 帧以上 → 降一档。
             * 在某档稳定一段时间后，试着升回上一档；试探失败就把下次试探的间隔加倍（10s → 最长 2min）*/
            static int late_cycles, probing;
            static uint32_t ok_cycles, probe_interval = 600;
            if (slack < -(int32_t)(period / 4)) { late_cycles++; ok_cycles = 0; }
            else { late_cycles = 0; ok_cycles++; }

            if (late_cycles >= 3 && s_quality < 3) {
                apply_quality(s_quality + 1);
                if (probing) probe_interval = (probe_interval * 2 > 7200u) ? 7200u : probe_interval * 2;
                late_cycles = 0; ok_cycles = 0; probing = 0;
            } else if (probing && ok_cycles >= 120) {
                probing = 0; probe_interval = 600;          /* 试探成功，就停在这一档 */
            } else if (!probing && s_quality > 0 && ok_cycles >= probe_interval) {
                apply_quality(s_quality - 1);
                ok_cycles = 0; probing = 1;
            }
        }
#endif
    }
#endif
    last_cyc = DWT->CYCCNT;

    if (g_nes.frames % NES_SAVE_CHECK_FRAMES == 0) {
#ifdef NES_TEST_SAVE
        SRAM[0]++;
#endif
        sram_save_if_changed();
    }

    uint8_t b = input_read();
    const char *script = NES_AUTOPLAY;
    if (*script) b |= nes_script_buttons(script, g_nes.frames);
    *pad1 = b;
    *pad2 = 0;

    /* SELECT + START 按住 1 秒退回菜单 */
    uint32_t tick = HAL_GetTick();
    if ((b & (NES_BTN_SELECT | NES_BTN_START)) == (NES_BTN_SELECT | NES_BTN_START))
        quit_hold_ms += tick - last_tick;
    else
        quit_hold_ms = 0;
    last_tick = tick;
    *system = (quit_hold_ms > 1000) ? PAD_SYS_QUIT : 0;
    if (*system) quit_hold_ms = 0;
}

/* ---------------- 其余接口 ---------------- */
void *InfoNES_MemoryCopy(void *dest, const void *src, int count) { return memcpy(dest, src, (size_t)count); }
void *InfoNES_MemorySet(void *dest, int c, int count) { return memset(dest, c, (size_t)count); }
void InfoNES_DebugPrint(char *msg) { log_printf("%s", msg); }
void InfoNES_Wait() {}
void InfoNES_SoundInit(void) {}
int  InfoNES_SoundOpen(int samples_per_sync, int sample_rate) { (void)samples_per_sync; (void)sample_rate; return 1; }
void InfoNES_SoundClose(void) {}
void InfoNES_SoundOutput(int samples, BYTE *w1, BYTE *w2, BYTE *w3, BYTE *w4, BYTE *w5)
{
#ifndef NES_NO_AUDIO
    audio_push(samples, w1, w2, w3, w4, w5);
#else
    (void)samples; (void)w1; (void)w2; (void)w3; (void)w4; (void)w5;
#endif
}

void InfoNES_MessageBox(char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_printf("MessageBox: %s\n", buf);
    lcd_fill_rect(0, 224, 320, 16, C_BLACK);
    lcd_draw_string(0, 224, buf, C_RED, C_BLACK);
    HAL_Delay(3000);
}

void nes_run(void)
{
    dwt_init();
    input_init();
#ifndef NES_NO_AUDIO
    audio_init();
#endif
    InfoNES_Main();
}
