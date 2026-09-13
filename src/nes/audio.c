#include "audio.h"
#include "board.h"

#include <string.h>

#define BUF_LEN  2048u                /* 2 的幂，约 93ms */
#define BUF_MASK (BUF_LEN - 1u)

/* DMA 在 SRAM 里读（CCM 访问不到）*/
static uint16_t s_buf[BUF_LEN];
static uint32_t s_w;                  /* 写指针 */
static uint16_t s_last;
uint16_t g_audio_p2p;
uint32_t g_audio_resync;

void audio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DAC_CLK_ENABLE();
    __HAL_RCC_TIM6_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    GPIO_InitTypeDef g = { .Pin = GPIO_PIN_4, .Mode = GPIO_MODE_ANALOG, .Pull = GPIO_NOPULL };
    HAL_GPIO_Init(GPIOA, &g);

    memset(s_buf, 0, sizeof s_buf);

    /* TIM6：APB1 42MHz，定时器时钟 ×2 = 84MHz；84MHz / 3809 = 22053Hz，更新事件作为 TRGO */
    TIM6->CR1 = 0;
    TIM6->PSC = 0;
    TIM6->ARR = (84000000u + AUDIO_RATE / 2) / AUDIO_RATE - 1u;
    TIM6->CR2 = TIM_CR2_MMS_1;        /* MMS = 010：更新事件 → TRGO */

    /* DMA1 Stream5 Channel7 = DAC1，循环模式，16 位，存储器 → 外设，不开中断 */
    DMA_Stream_TypeDef *st = DMA1_Stream5;
    st->CR &= ~DMA_SxCR_EN;
    while (st->CR & DMA_SxCR_EN) {}
    st->PAR  = (uint32_t)&DAC1->DHR12R1;
    st->M0AR = (uint32_t)s_buf;
    st->NDTR = BUF_LEN;
    st->FCR  = 0;
    /* 注意 DMA_SxCR_DIR 在这版头文件里是 2 位掩码(0xC0)，写成 11 是非法值、流使能不上；存储器→外设要用 DIR_0 */
    st->CR   = (7u << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_DIR_0 |
               DMA_SxCR_PSIZE_0 | DMA_SxCR_MSIZE_0;
    st->CR  |= DMA_SxCR_EN;

    /* DAC1：输出缓冲打开，TIM6 TRGO 触发（TSEL=000），DMA 使能 */
    DAC1->CR = DAC_CR_TEN1 | DAC_CR_DMAEN1;
    DAC1->CR |= DAC_CR_EN1;

    s_w = BUF_LEN / 2;
    TIM6->CR1 = TIM_CR1_CEN;
    if (!(st->CR & DMA_SxCR_EN)) log_printf("audio: DMA stream enable failed, cr=%08lx\n", (unsigned long)st->CR);
}

void audio_push(int samples, const uint8_t *w1, const uint8_t *w2, const uint8_t *w3,
                const uint8_t *w4, const uint8_t *w5)
{
    /* DMA 当前读位置；写指针和它保持约半个缓冲的距离。
     * 模拟 60.0988 帧 × 367 样本 ≈ 22056Hz，播放 22053Hz，偏差很小，偶尔重同步一次 */
    uint32_t r = (BUF_LEN - DMA1_Stream5->NDTR) & BUF_MASK;
    uint32_t dist = (s_w - r) & BUF_MASK;
    if (dist < 256u || dist > BUF_LEN - 256u) {
        s_w = (r + BUF_LEN / 2) & BUF_MASK;
        g_audio_resync++;
    }

    uint16_t lo = 0xFFFF, hi = 0;
    for (int i = 0; i < samples; i++) {
        /* 5 路 8 位相加最大 1275，×3 映射到 12 位 DAC（最大 3825）*/
        uint16_t v = (uint16_t)(((unsigned)w1[i] + w2[i] + w3[i] + w4[i] + w5[i]) * 3u);
        s_buf[s_w] = v;
        s_w = (s_w + 1) & BUF_MASK;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    s_last = s_buf[(s_w - 1) & BUF_MASK];
    g_audio_p2p = (samples > 0) ? (uint16_t)(hi - lo) : 0;
}

void audio_silence(void)
{
    /* 停在最后一个电平上，避免"啪"的一声 */
    for (uint32_t i = 0; i < BUF_LEN; i++) s_buf[i] = s_last;
}
