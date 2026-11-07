/** S1 PA6、S2 PA7、S3 PB0、S4 PB1、S5 PB6、S6 PB7、S7 PB8。
 * TIM3/TIM4硬件输出，不用HAL_Delay，也不在中断里逐个翻转舵机信号。
 * 舵机电源来自独立大电流支路；GPIO只接信号，JP2 LCD和IIC1本方案不使用。 */
#include "board_servo.h"
#include "stm32f1xx_hal.h"

#if RM_SERVO_CAL_CHANNEL && !RM_ARM_WIRED
#error "Servo calibration requires RM_ARM_WIRED=1"
#endif

#if RM_ARM_WIRED
static TIM_HandleTypeDef timer3, timer4;
static rm_arm_config_t active_config;
static bool initialized;
static uint8_t held_mask;
static const uint32_t channels[] = {TIM_CHANNEL_1,TIM_CHANNEL_2,TIM_CHANNEL_3,TIM_CHANNEL_4};

static bool timer_init(TIM_HandleTypeDef *t, TIM_TypeDef *instance, uint32_t prescaler,
                       uint32_t period, unsigned count)
{
    t->Instance = instance;
    t->Init.Prescaler = prescaler;
    t->Init.CounterMode = TIM_COUNTERMODE_UP;
    t->Init.Period = period-1u;
    t->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    t->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(t) != HAL_OK) return false;
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0; /* 启动计数也只有恒低电平，绝不上电发送1500us中位脉冲。 */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    for (unsigned i=0; i<count; ++i)
        if (HAL_TIM_PWM_ConfigChannel(t,&oc,channels[i]) != HAL_OK) return false;
    for (unsigned i=0; i<count; ++i)
        if (HAL_TIM_PWM_Start(t,channels[i]) != HAL_OK) return false;
    return true;
}

bool board_servo_init(const rm_arm_config_t *c)
{
    if (initialized || rm_arm_config_issues(c)) return false;
    uint32_t timer_clock = HAL_RCC_GetPCLK1Freq();
    /* APB1分频不为1时，定时器时钟是PCLK1的两倍；不能把72MHz写死到预分频。 */
    if (timer_clock != HAL_RCC_GetHCLKFreq()) timer_clock *= 2u;
    if (timer_clock < 1000000u || timer_clock % 1000000u ||
        timer_clock / 1000000u > 65536u) return false;
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_AFIO_REMAP_TIM3_DISABLE();
    __HAL_AFIO_REMAP_TIM4_DISABLE();
    bool ok = timer_init(&timer3,TIM3,timer_clock/1000000u-1u,c->period_us,4) &&
              timer_init(&timer4,TIM4,timer_clock/1000000u-1u,c->period_us,3);
    if (!ok) {
        /* 初始化失败时所有比较值仍为0，也尚未把GPIO交给定时器。 */
        TIM3->CCER = TIM4->CCER = 0;
        __HAL_TIM_DISABLE(&timer3);
        if (timer4.Instance) __HAL_TIM_DISABLE(&timer4);
        return false;
    }
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA,&g);
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8;
    HAL_GPIO_Init(GPIOB,&g);
    active_config = *c;
    held_mask = 0;
    initialized = true;
    return true;
}

bool board_servo_apply(const uint16_t pulse_us[RM_SERVOS], uint8_t mask)
{
    if (!pulse_us || (mask & ~RM_SERVO_ALL)) return false;
    if (!initialized) {
        if (mask) return false;
        for (unsigned i=0; i<RM_SERVOS; ++i) if (pulse_us[i]) return false;
        return true;
    }
    /* 已经承重的通道不能被一条不完整指令关掉；正常停车保持最后脉宽。 */
    if ((mask & held_mask) != held_mask) return false;
    for (unsigned i=0; i<RM_SERVOS; ++i) {
        if (mask & (1u<<i)) {
            if (!rm_arm_channel_ready(&active_config,i) ||
                pulse_us[i] < active_config.servo[i].min_us ||
                pulse_us[i] > active_config.servo[i].max_us) return false;
        } else if (pulse_us[i]) return false;
    }
    /* PWM配置已启用比较预装载；新脉宽在各自下个周期生效，不忙等、不强行重置周期。 */
    for (unsigned i=0; i<RM_SERVOS; ++i) {
        TIM_HandleTypeDef *t = i<4 ? &timer3 : &timer4;
        __HAL_TIM_SET_COMPARE(t,channels[i<4 ? i : i-4],pulse_us[i]);
    }
    held_mask = mask;
    return true;
}
#if RM_SERVO_CAL_CHANNEL
/* 标定使用独立初始化，只把指定的一根GPIO交给PWM，其他舵机信号保持原状态。
 * 这里没有改动生产config验收项，开放初试窗口由独立构建和操作者确认共同控制。 */
static bool cal_initialized, cal_held;
static TIM_HandleTypeDef *cal_timer;
static uint32_t cal_channel;

bool board_servo_cal_init(void)
{
    if (initialized || cal_initialized) return false;
    const unsigned index = RM_SERVO_CAL_CHANNEL-1u;
    const uint16_t pins[] = {GPIO_PIN_6,GPIO_PIN_7,GPIO_PIN_0,GPIO_PIN_1,GPIO_PIN_6,GPIO_PIN_7};
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    if (clock != HAL_RCC_GetHCLKFreq()) clock *= 2u;
    if (clock < 1000000u || clock % 1000000u || clock/1000000u > 65536u) return false;
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    if (index < 4) {
        __HAL_RCC_TIM3_CLK_ENABLE();
        __HAL_AFIO_REMAP_TIM3_DISABLE();
        cal_timer = &timer3;
    } else {
        __HAL_RCC_TIM4_CLK_ENABLE();
        __HAL_AFIO_REMAP_TIM4_DISABLE();
        cal_timer = &timer4;
    }
    cal_channel = channels[index < 4 ? index : index-4];
    if (!timer_init(cal_timer,index < 4 ? TIM3 : TIM4,clock/1000000u-1u,
                    RM_SCAL_PERIOD_US,index < 4 ? 4 : 3)) return false;
    GPIO_InitTypeDef pin = {0};
    pin.Pin = pins[index];
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(index < 2 ? GPIOA : GPIOB,&pin);
    cal_initialized = true;
    cal_held = false;
    return true;
}

bool board_servo_cal_apply(uint16_t pulse_us, bool enabled)
{
    if (!enabled) {
        if (pulse_us) return false;
#if RM_SHOVEL_CONTROL
        /* S2专用固件松键时撤销PWM，下个定时器更新后PA7保持低电平。
         * 只有S2这一根信号，其他承重机构继续使用各自的保持策略。 */
        if (cal_initialized) __HAL_TIM_SET_COMPARE(cal_timer,cal_channel,0);
        cal_held = false;
        return true;
#else
        return !cal_held;
#endif
    }
    if (!cal_initialized || pulse_us < RM_SCAL_MIN_US || pulse_us > RM_SCAL_MAX_US) return false;
    __HAL_TIM_SET_COMPARE(cal_timer,cal_channel,pulse_us);
    cal_held = true;
    return true;
}
#if RM_ARM_PAIR_CONTROL
bool board_servo_pair_init(void)
{
    if (!board_servo_cal_init()) return false; /* 已启动TIM3，PA7只对应S2。 */
    GPIO_InitTypeDef pin = {0};
    pin.Pin = GPIO_PIN_0 | GPIO_PIN_1; /* TIM3_CH3 PB0为S3，CH4 PB1为S4。 */
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB,&pin);
    return true;
}

bool board_servo_pair_apply(const uint16_t pulse_us[3])
{
    if (!cal_initialized || !pulse_us) return false;
    unsigned active = 0;
    for (unsigned i=0; i<3; ++i) {
        if (!pulse_us[i]) continue;
        if (pulse_us[i] < RM_SCAL_MIN_US || pulse_us[i] > RM_SCAL_MAX_US) return false;
        ++active;
    }
    if (active > 1) return false;
    /* 比较值有预装载。先写零再写非零，换通道时不会把新目标排在旧目标之前。 */
    for (unsigned i=0; i<3; ++i)
        if (!pulse_us[i]) __HAL_TIM_SET_COMPARE(cal_timer,channels[i+1],0);
    for (unsigned i=0; i<3; ++i)
        if (pulse_us[i]) __HAL_TIM_SET_COMPARE(cal_timer,channels[i+1],pulse_us[i]);
    return true;
}
#endif
#endif
#else
bool board_servo_init(const rm_arm_config_t *c) { (void)c; return false; }
bool board_servo_apply(const uint16_t pulse_us[RM_SERVOS], uint8_t mask)
{
    if (!pulse_us || mask) return false;
    for (unsigned i=0; i<RM_SERVOS; ++i) if (pulse_us[i]) return false;
    return true; /* 默认构建不访问舵机GPIO和定时器；只允许“全部未输出”。 */
}
#endif
