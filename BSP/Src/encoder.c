#include "encoder.h"

#include "stm32f1xx_hal.h"

#define ENC_SW_INDEX 3u

static TIM_HandleTypeDef s_htim[3];
static int16_t s_last[3];
static int32_t s_total[3];

/* 第 4 路：软件正交解码 */
static volatile int32_t s_sw_total;
static int32_t s_sw_last;
static uint8_t s_sw_state;

/* 上一状态<<2 | 当前状态 -> 计数增量 */
static const int8_t s_quad_table[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};

static void encoder_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;

    /* TIM2 CH1/CH2 */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* TIM3 CH1/CH2 */
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* TIM4 CH1/CH2 */
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* 软件解码：两路都是双边沿中断 */
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void encoder_tim_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    TIM_Encoder_InitTypeDef enc = {
        .EncoderMode = TIM_ENCODERMODE_TI12,
        .IC1Polarity = TIM_ICPOLARITY_RISING,
        .IC1Selection = TIM_ICSELECTION_DIRECTTI,
        .IC1Prescaler = TIM_ICPSC_DIV1,
        .IC1Filter = 6u,
        .IC2Polarity = TIM_ICPOLARITY_RISING,
        .IC2Selection = TIM_ICSELECTION_DIRECTTI,
        .IC2Prescaler = TIM_ICPSC_DIV1,
        .IC2Filter = 6u,
    };

    TIM_TypeDef *const instances[3] = {TIM2, TIM3, TIM4};

    for (int i = 0; i < 3; i++) {
        s_htim[i].Instance = instances[i];
        s_htim[i].Init.Prescaler = 0u;
        s_htim[i].Init.CounterMode = TIM_COUNTERMODE_UP;
        s_htim[i].Init.Period = 0xFFFFu;
        s_htim[i].Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        s_htim[i].Init.RepetitionCounter = 0u;
        s_htim[i].Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

        (void)HAL_TIM_Encoder_Init(&s_htim[i], &enc);
        (void)HAL_TIM_Encoder_Start(&s_htim[i], TIM_CHANNEL_ALL);

        __HAL_TIM_SET_COUNTER(&s_htim[i], 0u);
        s_last[i] = 0;
        s_total[i] = 0;
    }
}

void encoder_init(void)
{
    encoder_gpio_init();
    encoder_tim_init();

    s_sw_state = (uint8_t)((HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) << 1) |
                           HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1));
    s_sw_total = 0;
    s_sw_last = 0;

    HAL_NVIC_SetPriority(EXTI0_IRQn, 6u, 0u);
    HAL_NVIC_SetPriority(EXTI1_IRQn, 6u, 0u);
    HAL_NVIC_ClearPendingIRQ(EXTI0_IRQn);
    HAL_NVIC_ClearPendingIRQ(EXTI1_IRQn);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
}

int16_t encoder_read_delta(uint8_t idx)
{
    if (idx >= ENCODER_COUNT) {
        return 0;
    }

    if (idx <= 2u) {
        const uint16_t cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&s_htim[idx]);
        const int16_t delta = (int16_t)(cnt - (uint16_t)s_last[idx]);
        s_last[idx] = (int16_t)cnt;
        s_total[idx] += delta;
        return delta;
    }

    const int32_t now = s_sw_total;
    int32_t delta = now - s_sw_last;
    s_sw_last = now;

    if (delta > 32767) {
        delta = 32767;
    } else if (delta < -32768) {
        delta = -32768;
    }
    return (int16_t)delta;
}

int32_t encoder_total(uint8_t idx)
{
    if (idx < 3u) {
        return s_total[idx];
    }
    return s_sw_total;
}

void encoder_reset(void)
{
    for (int i = 0; i < 3; i++) {
        __HAL_TIM_SET_COUNTER(&s_htim[i], 0u);
        s_last[i] = 0;
        s_total[i] = 0;
    }
    s_sw_total = 0;
    s_sw_last = 0;
}

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    if (gpio_pin != GPIO_PIN_0 && gpio_pin != GPIO_PIN_1) {
        return;
    }

    const uint8_t a = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_SET) ? 1u : 0u;
    const uint8_t b = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_SET) ? 1u : 0u;
    const uint8_t cur = (uint8_t)((a << 1) | b);

    s_sw_total += s_quad_table[((s_sw_state & 0x03u) << 2) | cur];
    s_sw_state = cur;
}

void encoder_exti_irq(uint16_t gpio_pin)
{
    HAL_GPIO_EXTI_IRQHandler(gpio_pin);
}
