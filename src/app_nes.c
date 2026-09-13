/* NES 模拟器主程序 */
#include "board.h"
#include "lcd.h"

#include <string.h>

void nes_run(void);

extern uint8_t _sccm, _eccm;
int main(void)
{
    board_init();
    memset(&_sccm, 0, (size_t)(&_eccm - &_sccm));   /* CCM 是 NOLOAD 段，上电内容随机 */

    log_printf("\n\n===== STM32F407 NES =====\n");
    if (lcd_init() != 0) error_handler("lcd_init");
    log_printf("LCD %s\n", g_lcd.name);

    nes_run();
    error_handler("nes_run returned");
}
