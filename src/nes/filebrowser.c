#include "filebrowser.h"
#include "board.h"
#include "lcd.h"
#include "input.h"
#include "nes_common.h"
#include "ff.h"
#include "screenshot.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define COLS        40              /* 320 / 8 */
#define LIST_TOP    20
#define LIST_ROWS   11              /* 20 ~ 196 */
#define MAX_ENTRIES 128
#define VIEW_MAX    4096

#define C_HILITE    RGB565(0x20, 0x48, 0x90)
#define C_DIR       C_YELLOW
#define C_ROM       C_GREEN

typedef struct {
    char     name[64];
    uint32_t size;
    uint8_t  is_dir;
} entry_t;

static char    s_cwd[128] = "/";   /* 保留上次所在的目录 */
static entry_t s_ent[MAX_ENTRIES];
static int     s_count;
static int     s_truncated;

/* ---------------- 小工具 ---------------- */
static void textf(int x, int y, uint16_t fg, uint16_t bg, const char *fmt, ...)
{
    char buf[COLS + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    lcd_draw_string(x, y, buf, fg, bg);
}

static int is_nes(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".nes") == 0;
}

static void format_size(char *out, size_t n, uint32_t size)
{
    if (size < 1024u)                snprintf(out, n, "%luB", (unsigned long)size);
    else if (size < 1024u * 1024u)   snprintf(out, n, "%lu.%luK", (unsigned long)(size / 1024u),
                                              (unsigned long)(size * 10u / 1024u % 10u));
    else                             snprintf(out, n, "%lu.%luM", (unsigned long)(size / 1048576u),
                                              (unsigned long)(size / 104857u % 10u));
}

/* 按键：返回"刚按下"的键，上/下按住 350ms 后每 60ms 重复一次 */
static uint8_t read_keys(void)
{
    static uint8_t prev;
    static uint32_t t_down, t_rep;
    uint8_t b = input_read();
    uint8_t pressed = b & ~prev;
    uint32_t now = HAL_GetTick();
    const uint8_t rep_keys = NES_BTN_UP | NES_BTN_DOWN;
    if (pressed & rep_keys) { t_down = now; t_rep = now; }
    if ((b & rep_keys) && !(pressed & rep_keys) && now - t_down > 350u && now - t_rep > 60u) {
        pressed |= b & rep_keys;
        t_rep = now;
    }
    prev = b;
    return pressed;
}

static void wait_release(void)
{
    while (input_read()) HAL_Delay(10);
    read_keys();   /* 同步 prev */
}

/* ---------------- 目录 ---------------- */
static int entry_cmp(const entry_t *a, const entry_t *b)
{
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return strcasecmp(a->name, b->name);
}

static FRESULT load_dir(void)
{
    DIR dir;
    FILINFO fno;
    s_count = 0;
    s_truncated = 0;

    if (strcmp(s_cwd, "/") != 0) {                     /* 上一级 */
        strcpy(s_ent[0].name, "..");
        s_ent[0].size = 0;
        s_ent[0].is_dir = 1;
        s_count = 1;
    }

    FRESULT fr = f_opendir(&dir, s_cwd);
    if (fr != FR_OK) return fr;
    while ((fr = f_readdir(&dir, &fno)) == FR_OK && fno.fname[0]) {
        if (fno.fattrib & (AM_HID | AM_SYS)) continue;   /* 跳过 System Volume Information 之类 */
        if (s_count >= MAX_ENTRIES) { s_truncated = 1; break; }
        entry_t *e = &s_ent[s_count++];
        snprintf(e->name, sizeof e->name, "%.63s", fno.fname);
        e->size = (uint32_t)fno.fsize;
        e->is_dir = (fno.fattrib & AM_DIR) ? 1 : 0;
    }
    f_closedir(&dir);
    log_printf("files: %s (%d entries%s)\n", s_cwd, s_count, s_truncated ? ", truncated" : "");

    /* 插入排序：目录在前，按名字排（".." 固定在第一个）*/
    int start = (strcmp(s_cwd, "/") != 0) ? 1 : 0;
    for (int i = start + 1; i < s_count; i++) {
        entry_t t = s_ent[i];
        int j = i - 1;
        while (j >= start && entry_cmp(&s_ent[j], &t) > 0) { s_ent[j + 1] = s_ent[j]; j--; }
        s_ent[j + 1] = t;
    }
    return fr;
}

static void path_join(char *out, size_t n, const char *dir, const char *name)
{
    if (strcmp(dir, "/") == 0) snprintf(out, n, "/%.63s", name);
    else                       snprintf(out, n, "%.127s/%.63s", dir, name);
}

static void path_up(char *path)
{
    char *slash = strrchr(path, '/');
    if (!slash || slash == path) strcpy(path, "/");
    else *slash = 0;
}

/* ---------------- 画面 ---------------- */
static void draw_header(const char *title, const char *right)
{
    lcd_fill_rect(0, 0, 320, 20, C_BLACK);
    int rlen = (int)strlen(right);
    int max = COLS - rlen - 1;
    int tlen = (int)strlen(title);
    /* 路径太长时保留结尾部分 */
    if (tlen > max) textf(0, 1, C_CYAN, C_BLACK, "~%s", title + tlen - (max - 1));
    else            textf(0, 1, C_CYAN, C_BLACK, "%s", title);
    textf(320 - rlen * 8, 1, C_GRAY, C_BLACK, "%s", right);
    lcd_fill_rect(0, 18, 320, 1, C_GRAY);
}

static void draw_row(int row, const entry_t *e, int selected)
{
    int y = LIST_TOP + row * 16;
    uint16_t bg = selected ? C_HILITE : C_BLACK;
    lcd_fill_rect(0, y, 320, 16, bg);
    if (!e) return;

    char size[12] = "";
    if (!e->is_dir) format_size(size, sizeof size, e->size);
    uint16_t fg = e->is_dir ? C_DIR : (is_nes(e->name) ? C_ROM : C_WHITE);
    if (e->is_dir) textf(4, y, fg, bg, "%.33s/", e->name);
    else           textf(4, y, fg, bg, "%.32s", e->name);
    if (size[0]) textf(320 - 4 - (int)strlen(size) * 8, y, C_GRAY, bg, "%s", size);
}

static void draw_footer(const char *line1, const char *line2)
{
    lcd_fill_rect(0, 200, 320, 40, C_BLACK);
    lcd_fill_rect(0, 200, 320, 1, C_GRAY);
    textf(0, 204, C_GRAY, C_BLACK, "%s", line1);
    textf(0, 222, C_GRAY, C_BLACK, "%s", line2);
}

/* ---------------- 查看文件（前 4KB）---------------- */
static uint8_t s_view[VIEW_MAX];

static void view_file(const char *path, uint32_t fsize)
{
    FIL f;
    UINT br = 0;
    if (f_open(&f, path, FA_READ) != FR_OK) return;
    f_read(&f, s_view, VIEW_MAX, &br);
    f_close(&f);

    /* 控制字符超过 5% 就当二进制，按十六进制显示 */
    unsigned ctrl = 0;
    for (UINT i = 0; i < br; i++) {
        uint8_t c = s_view[i];
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') ctrl++;
    }
    int hex = br && ctrl * 20u > br;
    log_printf("files: view %s %lu bytes as %s\n", path, (unsigned long)fsize, hex ? "hex" : "text");

    /* 文本：先算出每一行的起始偏移（按 40 列折行）*/
    static uint16_t line_off[1025];   /* 最多显示前 1024 行 */
    int lines = 0;
    if (hex) {
        lines = (int)((br + 7) / 8);
    } else {
        UINT i = 0;
        while (i < br && lines < (int)(sizeof line_off / sizeof line_off[0]) - 1) {
            line_off[lines++] = (uint16_t)i;
            int col = 0;
            while (i < br && s_view[i] != '\n' && col < COLS) { if (s_view[i] != '\r') col++; i++; }
            if (i < br && s_view[i] == '\n') i++;
        }
        line_off[lines] = (uint16_t)br;
    }

    const int rows = 11;
    int top = 0, drawn = -1;
    char right[16], sz[12];
    format_size(sz, sizeof sz, fsize);
    snprintf(right, sizeof right, "%s%s", sz, hex ? " HEX" : "");
    lcd_fill(C_BLACK);
    draw_header(path, right);
    draw_footer("UP/DOWN scroll  L/R page  B back",
                fsize > VIEW_MAX ? "(showing first 4KB)" : "");
    wait_release();

    for (;;) {
        if (drawn != top) {
            for (int r = 0; r < rows; r++) {
                int y = LIST_TOP + r * 16;
                int ln = top + r;
                lcd_fill_rect(0, y, 320, 16, C_BLACK);
                if (ln >= lines) continue;
                char buf[COLS + 1];
                if (hex) {
                    uint32_t off = (uint32_t)ln * 8u;
                    int n = (int)((br - off) < 8 ? (br - off) : 8);
                    int p = snprintf(buf, sizeof buf, "%06lX ", (unsigned long)off);
                    for (int k = 0; k < 8; k++)
                        p += snprintf(buf + p, sizeof buf - (size_t)p, k < n ? "%02X" : "  ", s_view[off + (uint32_t)k]);
                    buf[p++] = ' ';
                    for (int k = 0; k < n && p < COLS; k++) {
                        uint8_t c = s_view[off + (uint32_t)k];
                        buf[p++] = (c >= 32 && c < 127) ? (char)c : '.';
                    }
                    buf[p] = 0;
                    lcd_draw_string(0, y, buf, C_WHITE, C_BLACK);
                } else {
                    int p = 0;
                    for (UINT i = line_off[ln]; i < line_off[ln + 1] && p < COLS; i++) {
                        uint8_t c = s_view[i];
                        if (c == '\n' || c == '\r') continue;
                        buf[p++] = (c == '\t') ? ' ' : (c >= 32 && c < 127) ? (char)c : '?';
                    }
                    buf[p] = 0;
                    lcd_draw_string(0, y, buf, C_WHITE, C_BLACK);
                }
            }
            drawn = top;
        }

        screenshot_poll();
        uint8_t k = read_keys();
        int max_top = lines > rows ? lines - rows : 0;
        if (k & NES_BTN_B) break;
        if (k & NES_BTN_DOWN)  top = top < max_top ? top + 1 : max_top;
        if (k & NES_BTN_UP)    top = top > 0 ? top - 1 : 0;
        if (k & NES_BTN_RIGHT) top = top + rows < max_top ? top + rows : max_top;
        if (k & NES_BTN_LEFT)  top = top > rows ? top - rows : 0;
        HAL_Delay(15);
    }
    wait_release();
}

/* 回到上一级目录，并把光标放在刚才所在的子目录上 */
static void go_up(int *sel, int *first)
{
    char child[64];
    const char *slash = strrchr(s_cwd, '/');
    snprintf(child, sizeof child, "%s", slash ? slash + 1 : "");
    path_up(s_cwd);
    load_dir();
    for (*sel = 0; *sel < s_count; (*sel)++) if (strcmp(s_ent[*sel].name, child) == 0) break;
    if (*sel >= s_count) *sel = 0;
    *first = *sel > LIST_ROWS / 2 ? *sel - LIST_ROWS / 2 : 0;
}

/* ---------------- 主循环 ---------------- */
int file_browser(char *out, size_t out_size)
{
    int sel = 0, first = 0;
    int need_load = 1, need_full = 1, drawn_sel = -1, drawn_first = -1;

    wait_release();
    for (;;) {
        if (need_load) {
            FRESULT fr = load_dir();
            if (fr != FR_OK) {
                lcd_fill(C_BLACK);
                draw_header(s_cwd, "");
                textf(4, 40, C_RED, C_BLACK, "Cannot open directory (err %d)", (int)fr);
                draw_footer("B back", "");
                if (strcmp(s_cwd, "/") == 0) {
                    while (!(read_keys() & NES_BTN_B)) HAL_Delay(20);
                    wait_release();
                    return 0;
                }
                path_up(s_cwd);
                HAL_Delay(1500);
                continue;
            }
            if (sel >= s_count) sel = s_count ? s_count - 1 : 0;
            need_load = 0;
            need_full = 1;
        }

        if (sel < first) first = sel;
        if (sel >= first + LIST_ROWS) first = sel - LIST_ROWS + 1;

        if (need_full) {
            char right[16];
            snprintf(right, sizeof right, "%d%s", s_count - (strcmp(s_cwd, "/") ? 1 : 0), s_truncated ? "+" : "");
            lcd_fill(C_BLACK);
            draw_header(s_cwd, right);
            draw_footer("A open  B back  L/R page", "");
            drawn_sel = drawn_first = -1;
            need_full = 0;
        }

        if (first != drawn_first) {
            for (int r = 0; r < LIST_ROWS; r++) {
                int i = first + r;
                draw_row(r, i < s_count ? &s_ent[i] : NULL, i == sel);
            }
        } else if (sel != drawn_sel) {
            if (drawn_sel >= first && drawn_sel < first + LIST_ROWS)
                draw_row(drawn_sel - first, &s_ent[drawn_sel], 0);
            draw_row(sel - first, &s_ent[sel], 1);
        }
        if (sel != drawn_sel || first != drawn_first) {
            /* 底栏第二行显示选中项的详细信息 */
            lcd_fill_rect(0, 222, 320, 16, C_BLACK);
            if (s_count) {
                const entry_t *e = &s_ent[sel];
                if (e->is_dir)           textf(0, 222, C_GRAY, C_BLACK, "%s", strcmp(e->name, "..") ? "directory" : "parent directory");
                else if (is_nes(e->name)) textf(0, 222, C_ROM, C_BLACK, "NES ROM, %lu bytes - A to play", (unsigned long)e->size);
                else                     textf(0, 222, C_GRAY, C_BLACK, "%lu bytes - A to view", (unsigned long)e->size);
            } else {
                textf(0, 222, C_GRAY, C_BLACK, "(empty)");
            }
            drawn_sel = sel;
            drawn_first = first;
        }

        screenshot_poll();
        uint8_t k = read_keys();
        if (k & NES_BTN_DOWN)  { if (sel + 1 < s_count) sel++; }
        if (k & NES_BTN_UP)    { if (sel > 0) sel--; }
        if (k & NES_BTN_RIGHT) { sel = (sel + LIST_ROWS < s_count) ? sel + LIST_ROWS : (s_count ? s_count - 1 : 0); first = sel; }
        if (k & NES_BTN_LEFT)  { sel = (sel > LIST_ROWS) ? sel - LIST_ROWS : 0; first = sel; }

        if (k & NES_BTN_B) {
            if (strcmp(s_cwd, "/") == 0) { wait_release(); return 0; }
            go_up(&sel, &first);
            need_full = 1;
            wait_release();
            continue;
        }

        if ((k & NES_BTN_A) && s_count) {
            const entry_t *e = &s_ent[sel];
            if (e->is_dir) {
                if (strcmp(e->name, "..") == 0) {
                    go_up(&sel, &first);
                    need_full = 1;
                } else {
                    char next[sizeof s_cwd + 64];
                    path_join(next, sizeof next, s_cwd, e->name);
                    if (strlen(next) < sizeof s_cwd) {   /* 路径太深就不进去 */
                        strcpy(s_cwd, next);
                        sel = 0; first = 0;
                        need_load = 1;
                    }
                }
                wait_release();
            } else if (is_nes(e->name)) {
                path_join(out, out_size, s_cwd, e->name);
                wait_release();
                return 1;
            } else {
                char path[sizeof s_cwd + 64];
                path_join(path, sizeof path, s_cwd, e->name);
                view_file(path, e->size);
                need_full = 1;
            }
        }
        HAL_Delay(15);
    }
}
