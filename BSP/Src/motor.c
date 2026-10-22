#include "motor.h"

#include <math.h>

#include "stm32f1xx_hal.h"

/* -------------------------------------------------------------------------
 * 引脚分配（与 docs/pinmap.md 一致）
 *   M1  PWM PA8  (TIM1_CH1)  IN1 PB12  IN2 PB13
 *   M2  PWM PA9  (TIM1_CH2)  IN1 PB14  IN2 PB15
 *   M3  PWM PA10 (TIM1_CH3)  IN1 PA4   IN2 PA5
 *   M4  PWM PA11 (TIM1_CH4)  IN1 PA12  IN2 PA15
 *   STBY PB9
 * ------------------------------------------------------------------------- */

#define PWM_ARR      3599u          /* 72MHz / (3599+1) = 20kHz */
#define STBY_PIN     GPIO_PIN_9
#define STBY_PORT    GPIOB

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} gpio_pin_t;

static const gpio_pin_t s_in1[MOTOR_COUNT] = {
    {GPIOB, GPIO_PIN_12},
    {GPIOB, GPIO_PIN_14},
    {GPIOA, GPIO_PIN_4},
    {GPIOA, GPIO_PIN_12},
};

static const gpio_pin_t s_in2[MOTOR_COUNT] = {
    {GPIOB, GPIO_PIN_13},
    {GPIOB, GPIO_PIN_15},
    {GPIOA, GPIO_PIN_5},
    {GPIOA, GPIO_PIN_15},
};

static const uint32_t s_channel[MOTOR_COUNT] = {
    TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3, TIM_CHANNEL_4
};

static TIM_HandleTypeDef s_htim;
static bool s_enabled;

/* -------------------------------------------------------------------------- */

static void motor_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /* 释放 PA15/PB3/PB4 的 JTAG 功能，保留 SWD 下载调试 */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    GPIO_InitTypeDef gpio = {0};

    /* PWM 引脚：复用推挽 */
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* 方向引脚：推挽输出，默认低 */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pull = GPIO_NOPULL;

    gpio.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_12 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOA, &gpio);

    for (int i = 0; i < MOTOR_COUNT; i++) {
        HAL_GPIO_WritePin(s_in1[i].port, s_in1[i].pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(s_in2[i].port, s_in2[i].pin, GPIO_PIN_RESET);
    }

    /* 待机引脚 */
    gpio.Pin = STBY_PIN;
    HAL_GPIO_Init(STBY_PORT, &gpio);
    HAL_GPIO_WritePin(STBY_PORT, STBY_PIN, GPIO_PIN_RESET);
}

static void motor_pwm_init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();

    s_htim.Instance = TIM1;
    s_htim.Init.Prescaler = 0u;
    s_htim.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_htim.Init.Period = PWM_ARR;
    s_htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_htim.Init.RepetitionCounter = 0u;
    s_htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    (void)HAL_TIM_PWM_Init(&s_htim);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0u;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;

    for (int i = 0; i < MOTOR_COUNT; i++) {
        (void)HAL_TIM_PWM_ConfigChannel(&s_htim, &oc, s_channel[i]);
        (void)HAL_TIM_PWM_Start(&s_htim, s_channel[i]);
    }
}

/* -------------------------------------------------------------------------- */

void motor_init(void)
{
    motor_gpio_init();
    motor_pwm_init();
    s_enabled = false;
}

void motor_set_duty(uint8_t idx, float duty)
{
    if (idx >= MOTOR_COUNT) {
        return;
    }

    if (duty > 1.0f) {
        duty = 1.0f;
    } else if (duty < -1.0f) {
        duty = -1.0f;
    }

    /* 方向 */
    if (duty > 0.001f) {
        HAL_GPIO_WritePin(s_in1[idx].port, s_in1[idx].pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(s_in2[idx].port, s_in2[idx].pin, GPIO_PIN_RESET);
    } else if (duty < -0.001f) {
        HAL_GPIO_WritePin(s_in1[idx].port, s_in1[idx].pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(s_in2[idx].port, s_in2[idx].pin, GPIO_PIN_SET);
    } else {
        /* 滑行：两个方向脚都拉低 */
        HAL_GPIO_WritePin(s_in1[idx].port, s_in1[idx].pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(s_in2[idx].port, s_in2[idx].pin, GPIO_PIN_RESET);
    }

    uint32_t ccr = (uint32_t)(fabsf(duty) * (float)(PWM_ARR + 1u));
    if (ccr > PWM_ARR) {
        ccr = PWM_ARR;
    }
    __HAL_TIM_SET_COMPARE(&s_htim, s_channel[idx], ccr);
}

void motor_stop_all(bool brake)
{
    for (int i = 0; i < MOTOR_COUNT; i++) {
        __HAL_TIM_SET_COMPARE(&s_htim, s_channel[i], 0u);
        const GPIO_PinState level = brake ? GPIO_PIN_SET : GPIO_PIN_RESET;
        HAL_GPIO_WritePin(s_in1[i].port, s_in1[i].pin, level);
        HAL_GPIO_WritePin(s_in2[i].port, s_in2[i].pin, level);
    }
}

void motor_enable(bool en)
{
    if (!en) {
        motor_stop_all(false);
    }
    HAL_GPIO_WritePin(STBY_PORT, STBY_PIN, en ? GPIO_PIN_SET : GPIO_PIN_RESET);
    s_enabled = en;
}

bool motor_is_enabled(void)
{
    return s_enabled;
}

void motor_emergency_stop(void)
{
    /* 直接操作寄存器：即使 HAL 状态机已经乱了也能关掉输出 */
    GPIOB->BRR = GPIO_PIN_9;
    for (int i = 0; i < MOTOR_COUNT; i++) {
        HAL_GPIO_WritePin(s_in1[i].port, s_in1[i].pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(s_in2[i].port, s_in2[i].pin, GPIO_PIN_RESET);
    }
    TIM1->CCR1 = 0u;
    TIM1->CCR2 = 0u;
    TIM1->CCR3 = 0u;
    TIM1->CCR4 = 0u;
}
