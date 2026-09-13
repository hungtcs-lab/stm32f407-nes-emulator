/* NES ROM 存储区：内部 Flash 扇区 6~7（0x08040000，256KB）
 *
 * 大 ROM（放不进 RAM 的，比如 Contra 128KB）直接在 Flash 上运行，零 RAM 占用
 * 布局: [128 字节头][.nes 文件原样]  —— 头最后写入，写一半断电不会被当成有效 ROM
 * PC 端可以直接烧: scripts/flash-rom.sh roms/xxx.nes */
#ifndef ROM_STORE_H
#define ROM_STORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROM_STORE_BASE     0x08040000u
#define ROM_STORE_SIZE     0x00040000u
#define ROM_STORE_HDR_SIZE 128u
#define ROM_STORE_MAX_ROM  (ROM_STORE_SIZE - ROM_STORE_HDR_SIZE)
#define ROM_STORE_MAGIC    0x5253454Eu     /* "NESR" */

typedef struct {
    uint32_t magic;
    uint32_t version;       /* 1 */
    uint32_t size;          /* .nes 文件字节数 */
    uint32_t crc32;         /* .nes 文件 CRC32（zlib 算法）*/
    char     name[64];      /* 文件名，不含路径 */
    uint8_t  reserved[48];
} rom_store_hdr_t;

/* 返回有效的头（magic 正确且 CRC 校验通过），否则 NULL。结果会缓存 */
const rom_store_hdr_t *rom_store_get(void);
static inline const uint8_t *rom_store_data(void) { return (const uint8_t *)(ROM_STORE_BASE + ROM_STORE_HDR_SIZE); }

/* 写入流程: begin（擦除）→ write（可多次，offset 相对数据区）→ finish（写头）*/
int rom_store_begin(void);
int rom_store_write(uint32_t offset, const uint8_t *data, uint32_t len);
int rom_store_finish(const char *name, uint32_t size, uint32_t crc32);

#ifdef __cplusplus
}
#endif

#endif
