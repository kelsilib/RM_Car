/**
 * @file    led.h
 * @brief   板载 LED（PC13，低电平点亮）状态指示。
 */
#ifndef LED_H
#define LED_H

#include <stdint.h>

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_ON,
    LED_MODE_SLOW,   /* 1Hz：未使能 */
    LED_MODE_FAST,   /* 10Hz：故障 */
} led_mode_t;

void led_init(void);
void led_set(led_mode_t mode);
void led_task(uint32_t now_ms);

#endif /* LED_H */
