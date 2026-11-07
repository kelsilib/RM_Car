/* board_servo.c的PC夹具，只模拟本测试需要的HAL寄存器和GPIO。 */
#ifndef TEST_STM32F1XX_HAL_H
#define TEST_STM32F1XX_HAL_H
#include <stdint.h>

typedef struct { uint32_t CCER; } TIM_TypeDef;
typedef struct { uint32_t Prescaler, CounterMode, Period, ClockDivision, AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct { uint32_t OCMode, Pulse, OCPolarity, OCFastMode; } TIM_OC_InitTypeDef;
typedef struct { uint32_t Pin, Mode, Speed; } GPIO_InitTypeDef;
typedef struct { uint32_t marker; } GPIO_TypeDef;

extern TIM_TypeDef fake_tim3, fake_tim4;
extern GPIO_TypeDef fake_gpioa, fake_gpiob;
#define TIM3 (&fake_tim3)
#define TIM4 (&fake_tim4)
#define GPIOA (&fake_gpioa)
#define GPIOB (&fake_gpiob)
#define TIM_CHANNEL_1 0u
#define TIM_CHANNEL_2 4u
#define TIM_CHANNEL_3 8u
#define TIM_CHANNEL_4 12u
#define GPIO_PIN_0 (1u<<0)
#define GPIO_PIN_1 (1u<<1)
#define GPIO_PIN_6 (1u<<6)
#define GPIO_PIN_7 (1u<<7)
#define GPIO_PIN_8 (1u<<8)
#define GPIO_MODE_AF_PP 1u
#define GPIO_SPEED_FREQ_LOW 1u
#define TIM_COUNTERMODE_UP 0u
#define TIM_CLOCKDIVISION_DIV1 0u
#define TIM_AUTORELOAD_PRELOAD_ENABLE 1u
#define TIM_OCMODE_PWM1 1u
#define TIM_OCPOLARITY_HIGH 1u
#define TIM_OCFAST_DISABLE 0u
#define HAL_OK 0
#define __HAL_RCC_AFIO_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM3_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM4_CLK_ENABLE() ((void)0)
#define __HAL_AFIO_REMAP_TIM3_DISABLE() ((void)0)
#define __HAL_AFIO_REMAP_TIM4_DISABLE() ((void)0)
#define __HAL_TIM_DISABLE(handle) ((void)(handle))
#define __HAL_TIM_SET_COMPARE(handle,channel,value) fake_set_compare((handle),(channel),(value))

uint32_t HAL_RCC_GetPCLK1Freq(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
int HAL_TIM_PWM_Init(TIM_HandleTypeDef *timer);
int HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *timer, TIM_OC_InitTypeDef *config, uint32_t channel);
int HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config);
void fake_set_compare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value);
#endif
