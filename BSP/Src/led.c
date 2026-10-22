#include "led.h"

#include <stdbool.h>

#include "stm32f1xx_hal.h"

static led_mode_t s_mode;
static uint32_t s_last_toggle_ms;
static bool s_on;

void led_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    s_mode = LED_MODE_SLOW;
    s_last_toggle_ms = 0u;
    s_on = false;

    /* 低电平点亮 */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

void led_set(led_mode_t mode)
{
    s_mode = mode;
}

static void led_write(bool on)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void led_task(uint32_t now_ms)
{
    switch (s_mode) {
    case LED_MODE_OFF:
        led_write(false);
        break;

    case LED_MODE_ON:
        led_write(true);
        break;

    case LED_MODE_SLOW:
    case LED_MODE_FAST: {
        const uint32_t period = (s_mode == LED_MODE_FAST) ? 50u : 500u;
        if ((now_ms - s_last_toggle_ms) >= period) {
            s_last_toggle_ms = now_ms;
            s_on = !s_on;
            led_write(s_on);
        }
        break;
    }

    default:
        led_write(false);
        break;
    }
}
