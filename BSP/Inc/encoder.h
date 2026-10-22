/**
 * @file    encoder.h
 * @brief   四路正交编码器测速。
 *
 * 硬件资源：
 *   E1(FL) TIM2 CH1/CH2  PA0/PA1
 *   E2(FR) TIM3 CH1/CH2  PA6/PA7
 *   E3(RL) TIM4 CH1/CH2  PB6/PB7
 *   E4(RR) 软件正交解码  PB0/PB1 (EXTI)
 *
 * F103C8 只有 4 个定时器，4 路 PWM 已经占了 TIM1，
 * 所以第 4 路编码器走 EXTI 软件解码；这点点中断开销完全扛得住。
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

#define ENCODER_COUNT 4

void    encoder_init(void);

/** 读取自上次调用以来的计数差，已处理 16 位回绕。 */
int16_t encoder_read_delta(uint8_t idx);

/** 累计计数（调试用）。 */
int32_t encoder_total(uint8_t idx);

void    encoder_reset(void);

/** 供 EXTI ISR 调用。 */
void    encoder_exti_irq(uint16_t gpio_pin);

#endif /* ENCODER_H */
