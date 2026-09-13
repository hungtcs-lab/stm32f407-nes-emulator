#include "nes_common.h"

#include <string.h>

static uint32_t crc_table[256];
static int crc_ready;

uint32_t nes_crc32(uint32_t crc, const uint8_t *data, size_t len)
{
    if (!crc_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
        crc_ready = 1;
    }
    crc = ~crc;
    while (len--) crc = crc_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static uint8_t button_by_name(const char *s, size_t n)
{
    static const struct { const char *name; uint8_t bit; } map[] = {
        {"a", NES_BTN_A}, {"b", NES_BTN_B}, {"select", NES_BTN_SELECT}, {"start", NES_BTN_START},
        {"up", NES_BTN_UP}, {"down", NES_BTN_DOWN}, {"left", NES_BTN_LEFT}, {"right", NES_BTN_RIGHT},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (strlen(map[i].name) == n && strncmp(map[i].name, s, n) == 0) return map[i].bit;
    return 0;
}

uint8_t nes_script_buttons(const char *script, uint32_t frame)
{
    uint8_t out = 0;
    const char *p = script;
    while (p && *p) {
        const char *colon = strchr(p, ':');
        if (!colon) break;
        uint8_t bit = button_by_name(p, (size_t)(colon - p));
        unsigned long a = 0, b = 0;
        const char *q = colon + 1;
        while (*q >= '0' && *q <= '9') a = a * 10 + (unsigned long)(*q++ - '0');
        if (*q == '-') { q++; while (*q >= '0' && *q <= '9') b = b * 10 + (unsigned long)(*q++ - '0'); }
        else b = a;
        if (frame >= a && frame <= b) out |= bit;
        p = strchr(q, ',');
        if (p) p++;
    }
    return out;
}
