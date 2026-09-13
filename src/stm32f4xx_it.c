/* 中断服务函数 */
#include "board.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void NMI_Handler(void)        { error_handler("NMI"); }
void HardFault_Handler(void)  { error_handler("HardFault"); }
void MemManage_Handler(void)  { error_handler("MemManage"); }
void BusFault_Handler(void)   { error_handler("BusFault"); }
void UsageFault_Handler(void) { error_handler("UsageFault"); }
