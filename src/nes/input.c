#include "input.h"
#include "nes_common.h"
#include "board.h"

typedef struct { GPIO_TypeDef *port; uint16_t pin; uint8_t btn; uint8_t active_high; } key_t;

static const key_t keys[] = {
    /* 外接 6 个 */
    { GPIOC, GPIO_PIN_0,  NES_BTN_UP,     0 },
    { GPIOC, GPIO_PIN_1,  NES_BTN_DOWN,   0 },
    { GPIOC, GPIO_PIN_2,  NES_BTN_LEFT,   0 },
    { GPIOC, GPIO_PIN_3,  NES_BTN_RIGHT,  0 },
    { GPIOE, GPIO_PIN_5,  NES_BTN_A,      0 },
    { GPIOE, GPIO_PIN_6,  NES_BTN_B,      0 },
    /* 板载按键 */
    { GPIOE, GPIO_PIN_4,  NES_BTN_SELECT, 0 },   /* KEY0 */
    { GPIOE, GPIO_PIN_3,  NES_BTN_START,  0 },   /* KEY1 */
    { GPIOA, GPIO_PIN_0,  NES_BTN_A,      1 },   /* WK_UP，和外接 A 并联 */
};

void input_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        GPIO_InitTypeDef g = {
            .Pin = keys[i].pin, .Mode = GPIO_MODE_INPUT,
            .Pull = keys[i].active_high ? GPIO_PULLDOWN : GPIO_PULLUP,
            .Speed = GPIO_SPEED_FREQ_LOW,
        };
        HAL_GPIO_Init(keys[i].port, &g);
    }
}

uint8_t input_read(void)
{
    uint8_t out = 0;
#ifdef NES_INPUT_SCRIPT
    /* 测试用：按开机后的时间（单位 10ms）模拟按键，格式同 NES_AUTOPLAY */
    out |= nes_script_buttons(NES_INPUT_SCRIPT, HAL_GetTick() / 10u);
#endif
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        int level = (keys[i].port->IDR & keys[i].pin) != 0;
        if (level == keys[i].active_high) out |= keys[i].btn;
    }
    return out;
}
