/**
 * @file    stm32f1xx_it.c
 * @brief   中断向量实现。
 */
#include "stm32f1xx_hal.h"

#include "encoder.h"
#include "link.h"
#include "motor.h"

void NMI_Handler(void)        { for (;;) { } }
void MemManage_Handler(void)  { for (;;) { } }
void BusFault_Handler(void)   { for (;;) { } }
void UsageFault_Handler(void) { for (;;) { } }
void SVC_Handler(void)        { }
void DebugMon_Handler(void)   { }
void PendSV_Handler(void)     { }

void HardFault_Handler(void)
{
    /* 先把动力关掉，再原地闪灯，避免"炸机后还带着油门" */
    __disable_irq();
    motor_emergency_stop();

    for (;;) {
        GPIOC->ODR ^= GPIO_PIN_13;
        for (volatile uint32_t i = 0; i < 100000u; i++) {
            __NOP();
        }
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void EXTI0_IRQHandler(void)
{
    encoder_exti_irq(GPIO_PIN_0);
}

void EXTI1_IRQHandler(void)
{
    encoder_exti_irq(GPIO_PIN_1);
}

void USART2_IRQHandler(void)
{
    link_irq_handler();
}
