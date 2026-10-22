/**
 * @file    board.h
 * @brief   板级基础：时钟、微秒计时、错误停机。
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

/** HAL_Init + 72MHz 时钟 + DWT 微秒计数器。 */
void board_init(void);

uint32_t board_millis(void);
uint32_t board_micros(void);
void     board_delay_us(uint32_t us);

/** 不可恢复错误：关输出、快闪 LED、停机。 */
void board_error(const char *msg);

#endif /* BOARD_H */
