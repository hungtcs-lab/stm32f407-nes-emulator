#include "rom_store.h"
#include "nes_common.h"
#include "board.h"

#include <string.h>

static const rom_store_hdr_t *s_cached;
static int s_checked;

const rom_store_hdr_t *rom_store_get(void)
{
    if (s_checked) return s_cached;
    s_checked = 1;
    s_cached = NULL;

    const rom_store_hdr_t *h = (const rom_store_hdr_t *)ROM_STORE_BASE;
    if (h->magic != ROM_STORE_MAGIC || h->version != 1) return NULL;
    if (h->size < 16 || h->size > ROM_STORE_MAX_ROM) return NULL;
    if (nes_crc32(0, rom_store_data(), h->size) != h->crc32) {
        log_printf("rom_store: CRC mismatch\n");
        return NULL;
    }
    s_cached = h;
    return h;
}

int rom_store_begin(void)
{
    s_checked = 0;
    s_cached = NULL;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    FLASH_EraseInitTypeDef er = {
        .TypeErase = FLASH_TYPEERASE_SECTORS, .Sector = FLASH_SECTOR_6,
        .NbSectors = 2, .VoltageRange = FLASH_VOLTAGE_RANGE_3,
    };
    uint32_t bad = 0;
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&er, &bad);
    if (st != HAL_OK) {
        log_printf("rom_store: erase fail st=%d sector=%lu err=%lx\n", st, (unsigned long)bad,
                   (unsigned long)HAL_FLASH_GetError());
        HAL_FLASH_Lock();
        return -1;
    }
    return 0;
}

static int program_bytes(uint32_t addr, const uint8_t *data, uint32_t len)
{
    /* 对齐部分按字写（快 4 倍），其余按字节 */
    while (len && (addr & 3)) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr++, *data++) != HAL_OK) return -1;
        len--;
    }
    while (len >= 4) {
        uint32_t w;
        memcpy(&w, data, 4);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, w) != HAL_OK) return -1;
        addr += 4; data += 4; len -= 4;
    }
    while (len--) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr++, *data++) != HAL_OK) return -1;
    }
    return 0;
}

int rom_store_write(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (offset + len > ROM_STORE_MAX_ROM) return -1;
    return program_bytes(ROM_STORE_BASE + ROM_STORE_HDR_SIZE + offset, data, len);
}

int rom_store_finish(const char *name, uint32_t size, uint32_t crc32)
{
    rom_store_hdr_t h;
    memset(&h, 0xFF, sizeof h);
    h.magic = ROM_STORE_MAGIC;
    h.version = 1;
    h.size = size;
    h.crc32 = crc32;
    memset(h.name, 0, sizeof h.name);
    strncpy(h.name, name, sizeof h.name - 1);
    int r = program_bytes(ROM_STORE_BASE, (const uint8_t *)&h, sizeof h);
    HAL_FLASH_Lock();
    s_checked = 0;
    if (r != 0 || rom_store_get() == NULL) {
        log_printf("rom_store: verify fail\n");
        return -1;
    }
    return 0;
}
