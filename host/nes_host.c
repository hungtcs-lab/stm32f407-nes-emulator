/* 主机端 InfoNES 测试程序：不需要板子，验证模拟器核心和移植补丁
 *
 * 用法: nes_host <rom.nes> [--frames N] [--shots 60,300] [--out DIR] [--input "start:120-126"] [--interlace 1] [--audio 1] [--raw 1]
 * 输出: 每个截图帧打印 CRC32（算法和 MCU 一致，可以直接比对），并保存 PPM */
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"
#include "nes_common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static const char *g_rom;
static unsigned g_frames = 600;
static const char *g_shots = "";
static const char *g_out = ".";
static const char *g_input = "";
static unsigned g_frame;
static uint16_t g_fb[NES_DISP_WIDTH * NES_DISP_HEIGHT];
static int g_menu_calls;
static int g_interlace;
static int g_audio;
static int g_raw;     /* 1 = 像素不清背景标记位（对应 MCU 的 NES_LCD_DMA）*/

static int in_list(const char *list, unsigned v)
{
    const char *p = list;
    while (*p) {
        char *end;
        unsigned long x = strtoul(p, &end, 10);
        if (end == p) break;
        if (x == v) return 1;
        p = end;
        while (*p == ',') p++;
    }
    return 0;
}

static void save_ppm(unsigned frame)
{
    char path[512];
    snprintf(path, sizeof path, "%s/frame_%05u.ppm", g_out, frame);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", NES_DISP_WIDTH, NES_DISP_HEIGHT);
    for (int i = 0; i < NES_DISP_WIDTH * NES_DISP_HEIGHT; i++) {
        uint16_t c = g_fb[i];
        unsigned char rgb[3] = { (unsigned char)(((c >> 11) & 31) * 255 / 31),
                                 (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                 (unsigned char)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int InfoNES_Menu()
{
    if (g_menu_calls++) return -1;
    if (InfoNES_Load(g_rom) != 0) { fprintf(stderr, "load failed\n"); return -1; }
    APU_Mute = g_audio ? 0 : 1;   /* APU 是否模拟会影响 $4015 读回值，要和 MCU 保持一致才能比 CRC */
    return 0;
}

int InfoNES_ReadRom(const char *name)
{
    FILE *fp = fopen(name, "rb");
    if (!fp) return -1;
    if (fread(&NesHeader, sizeof NesHeader, 1, fp) != 1 || memcmp(NesHeader.byID, "NES\x1a", 4)) { fclose(fp); return -1; }
    memset(SRAM, 0, SRAM_SIZE);
    if ((NesHeader.byInfo1 & 4) && fread(&SRAM[0x1000], 512, 1, fp) != 1) { fclose(fp); return -1; }
    ROM = (BYTE *)malloc(NesHeader.byRomSize * 0x4000);
    if (fread(ROM, 0x4000, NesHeader.byRomSize, fp) != NesHeader.byRomSize) { fclose(fp); return -1; }
    if (NesHeader.byVRomSize) {
        VROM = (BYTE *)malloc(NesHeader.byVRomSize * 0x2000);
        if (fread(VROM, 0x2000, NesHeader.byVRomSize, fp) != NesHeader.byVRomSize) { fclose(fp); return -1; }
    }
    fclose(fp);
    printf("ROM: PRG %dK CHR %dK mapper %d\n", NesHeader.byRomSize * 16, NesHeader.byVRomSize * 8,
           (NesHeader.byInfo1 >> 4) | (NesHeader.byInfo2 & 0xF0));
    return 0;
}

void InfoNES_ReleaseRom()
{
    free(ROM); ROM = NULL;
    free(VROM); VROM = NULL;
}

void InfoNES_LoadLine(int nLine, const WORD *pLine)
{
    if (nLine < 0 || nLine >= NES_DISP_HEIGHT) return;
    for (int x = 0; x < NES_DISP_WIDTH; x++) g_fb[nLine * NES_DISP_WIDTH + x] = g_raw ? pLine[x] : nes_to_rgb565(pLine[x]);
}

void InfoNES_LoadFrame() {}

int InfoNES_ShouldDrawLine(int nLine)
{
    return !g_interlace || ((nLine ^ (int)g_frame) & 1) == 0;
}

void InfoNES_PadState(DWORD *pad1, DWORD *pad2, DWORD *system)
{
    g_frame++;
    if (in_list(g_shots, g_frame)) {
        /* CRC 按小端 16 位像素、从第 0 行到第 239 行计算，和 MCU 端逐行累加的结果一致 */
        uint8_t le[sizeof g_fb];
        for (size_t i = 0; i < sizeof g_fb / 2; i++) { le[2 * i] = (uint8_t)g_fb[i]; le[2 * i + 1] = (uint8_t)(g_fb[i] >> 8); }
        printf("frame %u crc %08X\n", g_frame, nes_crc32(0, le, sizeof le));
        save_ppm(g_frame);
    }
    *pad1 = nes_script_buttons(g_input, g_frame);
    *pad2 = 0;
    *system = (g_frame >= g_frames) ? PAD_SYS_QUIT : 0;
}

void *InfoNES_MemoryCopy(void *dest, const void *src, int count) { return memcpy(dest, src, (size_t)count); }
void *InfoNES_MemorySet(void *dest, int c, int count) { return memset(dest, c, (size_t)count); }
void InfoNES_DebugPrint(char *msg) { fputs(msg, stderr); }
void InfoNES_Wait() {}
void InfoNES_SoundInit(void) {}
int InfoNES_SoundOpen(int a, int b) { (void)a; (void)b; return 1; }
void InfoNES_SoundClose(void) {}
void InfoNES_SoundOutput(int n, BYTE *a, BYTE *b, BYTE *c, BYTE *d, BYTE *e) { (void)n; (void)a; (void)b; (void)c; (void)d; (void)e; }
void InfoNES_MessageBox(char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s rom.nes [--frames N] [--shots a,b] [--out dir] [--input script]\n", argv[0]); return 1; }
    g_rom = argv[1];
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--frames")) g_frames = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--out")) g_out = argv[i + 1];
        else if (!strcmp(argv[i], "--input")) g_input = argv[i + 1];
        else if (!strcmp(argv[i], "--shots")) g_shots = argv[i + 1];
        else if (!strcmp(argv[i], "--interlace")) g_interlace = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--audio")) g_audio = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--raw")) g_raw = atoi(argv[i + 1]);
    }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    InfoNES_Main();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("%u frames in %.2fs (%.0f fps on host)\n", g_frame, s, g_frame / s);
    return 0;
}
