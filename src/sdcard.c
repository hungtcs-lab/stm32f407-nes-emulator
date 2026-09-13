#include "sdcard.h"
#include "board.h"

#include "ff.h"
#include "diskio.h"

SD_HandleTypeDef hsd;
static int s_ready;

void sd_gpio_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDIO_CLK_ENABLE();

    /* 板上已有 10K 上拉，这里内部上拉也开着，不冲突 */
    GPIO_InitTypeDef g = { .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP,
                           .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = GPIO_AF12_SDIO };
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;    /* D0~D3 */
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_2;                                             /* CMD */
    HAL_GPIO_Init(GPIOD, &g);
    g.Pin = GPIO_PIN_12; g.Pull = GPIO_NOPULL;                      /* CK */
    HAL_GPIO_Init(GPIOC, &g);

}

#define SD_TIMEOUT_MS 2000u


/* ---------------- 初始化 ----------------
 * 两个坑（见 NOTE.md）：
 * 1. HAL 的 ACMD41 循环无间隔重试 65535 次，卡一直 busy 时会死等一分多钟
 *    → 先自己跑一次 ACMD41（带超时），卡 ready 了再交给 HAL
 * 2. HAL_SD_Init 偶发超时（约 1/5）→ 失败后 DeInit 重试 */
static int acmd41_wait_ready(uint32_t timeout_ms)
{
    SDIO_InitTypeDef init = {
        .ClockEdge = SDIO_CLOCK_EDGE_RISING, .ClockBypass = SDIO_CLOCK_BYPASS_DISABLE,
        .ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE, .BusWide = SDIO_BUS_WIDE_1B,
        .HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE, .ClockDiv = SDIO_INIT_CLK_DIV,
    };
    SDIO_Init(SDIO, init);
    SDIO_PowerState_ON(SDIO);
    __SDIO_ENABLE(SDIO);
    HAL_Delay(10);

    SDMMC_CmdGoIdleState(SDIO);
    SDMMC_CmdOperCond(SDIO);
    uint32_t t0 = HAL_GetTick(), ocr = 0;
    do {
        if (SDMMC_CmdAppCommand(SDIO, 0) != 0) break;
        if (SDMMC_CmdAppOperCommand(SDIO, SDMMC_VOLTAGE_WINDOW_SD | SDMMC_HIGH_CAPACITY |
                                          SD_SWITCH_1_8V_CAPACITY) != 0) break;
        ocr = SDIO_GetResponse(SDIO, SDIO_RESP1);
        if (ocr & 0x80000000u) break;
        HAL_Delay(10);
    } while (HAL_GetTick() - t0 < timeout_ms);

    SDIO_PowerState_OFF(SDIO);
    HAL_Delay(10);
    if (!(ocr & 0x80000000u)) {
        log_printf("  SD not ready after %lums, OCR=%08lX\n",
                   (unsigned long)(HAL_GetTick() - t0), (unsigned long)ocr);
        return -1;
    }
    return 0;
}

static int sd_init_once(void)
{
    if (acmd41_wait_ready(1500) != 0) return -1;

    hsd.Instance                 = SDIO;
    hsd.Init.ClockEdge           = SDIO_CLOCK_EDGE_RISING;
    hsd.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
    hsd.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide             = SDIO_BUS_WIDE_1B;
    /* F4 勘误: SDIO 硬件流控会导致数据错误，不开 */
    hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd.Init.ClockDiv            = SD_CLOCK_DIV;

    if (HAL_SD_Init(&hsd) != HAL_OK) {
        log_printf("  HAL_SD_Init fail, err=0x%08lX\n", (unsigned long)hsd.ErrorCode);
        return -2;
    }
    if (HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B) != HAL_OK) {
        log_printf("  4-bit fail, err=0x%08lX\n", (unsigned long)hsd.ErrorCode);
        return -3;
    }
    return 0;
}

int sd_init(void)
{
    if (s_ready) return 0;
    sd_gpio_init();
    for (int attempt = 1; attempt <= SD_INIT_ATTEMPTS; attempt++) {
        if (sd_init_once() == 0) {
            if (attempt > 1) log_printf("  SD init ok on attempt %d\n", attempt);
            s_ready = 1;
            return 0;
        }
        HAL_SD_DeInit(&hsd);
        HAL_Delay(50);
    }
    return -1;
}

static int wait_transfer_state_logged(const char *op, uint32_t block, uint32_t count)
{
    uint32_t t0 = HAL_GetTick();
    HAL_SD_CardStateTypeDef st;
    while ((st = HAL_SD_GetCardState(&hsd)) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - t0 > SD_TIMEOUT_MS) {
            log_printf("  %s blk=%lu n=%lu: card state %lu timeout\n", op,
                       (unsigned long)block, (unsigned long)count, (unsigned long)st);
            return -1;
        }
    }
    return 0;
}

int sd_read(uint8_t *buf, uint32_t block, uint32_t count)
{
    if (HAL_SD_ReadBlocks(&hsd, buf, block, count, SD_TIMEOUT_MS) != HAL_OK) {
        log_printf("  read blk=%lu n=%lu err=%08lX\n", (unsigned long)block,
                   (unsigned long)count, (unsigned long)hsd.ErrorCode);
        hsd.ErrorCode = HAL_SD_ERROR_NONE;
        return -1;
    }
    return wait_transfer_state_logged("read", block, count);
}

int sd_write(const uint8_t *buf, uint32_t block, uint32_t count)
{
    if (HAL_SD_WriteBlocks(&hsd, (uint8_t *)buf, block, count, SD_TIMEOUT_MS) != HAL_OK) {
        log_printf("  write blk=%lu n=%lu err=%08lX\n", (unsigned long)block,
                   (unsigned long)count, (unsigned long)hsd.ErrorCode);
        hsd.ErrorCode = HAL_SD_ERROR_NONE;
        return -1;
    }
    return wait_transfer_state_logged("write", block, count);
}

/* ---------------- FatFs 底层接口 ---------------- */
DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    return sd_init() == 0 ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0 && s_ready) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !s_ready) return RES_NOTRDY;
    return sd_read(buff, (uint32_t)sector, count) == 0 ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || !s_ready) return RES_NOTRDY;
    return sd_write(buff, (uint32_t)sector, count) == 0 ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0 || !s_ready) return RES_NOTRDY;
    HAL_SD_CardInfoTypeDef info;
    HAL_SD_GetCardInfo(&hsd, &info);
    switch (cmd) {
    case CTRL_SYNC:        return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = info.LogBlockNbr;  return RES_OK;
    case GET_SECTOR_SIZE:  *(WORD *)buff  = (WORD)info.LogBlockSize; return RES_OK;
    case GET_BLOCK_SIZE:   *(DWORD *)buff = 1; return RES_OK;
    default:               return RES_PARERR;
    }
}
