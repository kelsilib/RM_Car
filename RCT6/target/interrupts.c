/* CPU出现异常，或毫秒时钟到点时，会进入这些函数。
 * SysTick只把毫秒数加1，控制代码留在main里；USART1/2接收中断写在board_hal.c。
 * HardFault等严重异常会进入board_panic尝试停车，但仍需要独立的动力保护。 */
#include "board_hal.h"
#include "stm32f1xx_hal.h"
void SysTick_Handler(void) { HAL_IncTick(); }
void HardFault_Handler(void) { board_panic(); }
void MemManage_Handler(void) { board_panic(); }
void BusFault_Handler(void) { board_panic(); }
void UsageFault_Handler(void) { board_panic(); }
void NMI_Handler(void) { board_panic(); }
void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }
