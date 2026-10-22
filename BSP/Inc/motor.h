/**
 * @file    motor.h
 * @brief   TB6612FNG / DRV8833 类"PWM + 双方向"驱动板抽象。
 */
#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>
#include <stdint.h>

#define MOTOR_COUNT 4

void motor_init(void);

/** duty: -1.0 ~ +1.0，正为前进。 */
void motor_set_duty(uint8_t idx, float duty);

/** brake=true 时短路制动，false 时滑行。 */
void motor_stop_all(bool brake);

/** STBY 使能。关闭时所有输出高阻。 */
void motor_enable(bool en);
bool motor_is_enabled(void);

/** 供 HardFault 用：只写寄存器，不调用 HAL。 */
void motor_emergency_stop(void);

#endif /* MOTOR_H */
