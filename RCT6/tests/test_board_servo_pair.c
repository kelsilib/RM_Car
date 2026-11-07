/* 编译真正的板级代码，模拟比较值在每次写入后更新，检查引脚和互斥顺序。 */
#include "board_servo.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>

TIM_TypeDef fake_tim3, fake_tim4;
GPIO_TypeDef fake_gpioa, fake_gpiob;
static uint32_t compare[4], gpioa_pins, gpiob_pins, writes[8];
static unsigned write_count, checks;
static bool overlap;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL pair HAL line %d: %s\n",__LINE__,#x); return 1; } } while (0)

uint32_t HAL_RCC_GetPCLK1Freq(void) { return 36000000u; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return 72000000u; }
int HAL_TIM_PWM_Init(TIM_HandleTypeDef *timer) { (void)timer; return HAL_OK; }
int HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *timer, TIM_OC_InitTypeDef *config, uint32_t channel)
{ (void)timer; (void)config; (void)channel; return HAL_OK; }
int HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{ (void)timer; (void)channel; return HAL_OK; }
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config)
{
    if (port==GPIOA) gpioa_pins |= config->Pin;
    if (port==GPIOB) gpiob_pins |= config->Pin;
}
void fake_set_compare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value)
{
    if (timer->Instance!=TIM3 || channel/4u>=4u) { overlap=true; return; }
    if (write_count < sizeof writes/sizeof writes[0]) writes[write_count]=channel;
    ++write_count;
    compare[channel/4u]=value;
    unsigned active=(compare[1]!=0)+(compare[2]!=0)+(compare[3]!=0);
    if (active>1) overlap=true;
}

int main(void)
{
    CHECK(board_servo_pair_init());
    CHECK(gpioa_pins==GPIO_PIN_7 && gpiob_pins==(GPIO_PIN_0|GPIO_PIN_1));
    CHECK(!board_servo_pair_init());
    const uint16_t s2[3]={1530,0,0}, s3[3]={0,1470,0}, s4[3]={0,0,1530}, zero[3]={0};
    CHECK(board_servo_pair_apply(s2));
    CHECK(write_count==3 && writes[0]==TIM_CHANNEL_3 && writes[1]==TIM_CHANNEL_4 && writes[2]==TIM_CHANNEL_2);
    CHECK(compare[1]==1530 && !compare[2] && !compare[3] && !overlap);
    write_count=0; CHECK(board_servo_pair_apply(s3));
    CHECK(write_count==3 && writes[0]==TIM_CHANNEL_2 && writes[1]==TIM_CHANNEL_4 && writes[2]==TIM_CHANNEL_3);
    CHECK(!compare[1] && compare[2]==1470 && !compare[3] && !overlap);
    write_count=0; CHECK(board_servo_pair_apply(s4));
    CHECK(write_count==3 && writes[0]==TIM_CHANNEL_2 && writes[1]==TIM_CHANNEL_3 && writes[2]==TIM_CHANNEL_4);
    CHECK(!compare[1] && !compare[2] && compare[3]==1530 && !overlap);
    const uint16_t multiple[3]={1500,1500,0}, too_low[3]={0,1399,0}, too_high[3]={0,0,1601};
    write_count=0;
    CHECK(!board_servo_pair_apply(NULL) && !board_servo_pair_apply(multiple));
    CHECK(!board_servo_pair_apply(too_low) && !board_servo_pair_apply(too_high));
    CHECK(!write_count && compare[3]==1530);
    CHECK(board_servo_pair_apply(zero));
    CHECK(!compare[1] && !compare[2] && !compare[3] && !overlap);
    printf("PASS pair HAL: %u checks (PC simulated registers)\n",checks);
    return 0;
}
