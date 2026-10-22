#include "board.h"

#include "stm32f1xx_hal.h"

/* -------------------------------------------------------------------------- */
/* 时钟：HSE 8MHz -> PLL x9 -> 72MHz                                          */
/* -------------------------------------------------------------------------- */

static void board_clock_config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        board_error("osc");
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;   /* 36MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;   /* 72MHz */

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
        board_error("clk");
    }
}

/* -------------------------------------------------------------------------- */
/* DWT 微秒计时                                                                */
/* -------------------------------------------------------------------------- */

static bool s_dwt_ok;

static void board_dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_dwt_ok = (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u;
}

/* -------------------------------------------------------------------------- */

void board_init(void)
{
    HAL_Init();
    board_clock_config();
    board_dwt_init();
    SystemCoreClockUpdate();
}

uint32_t board_millis(void)
{
    return HAL_GetTick();
}

uint32_t board_micros(void)
{
    if (!s_dwt_ok) {
        return HAL_GetTick() * 1000u;
    }
    const uint32_t cycles_per_us = SystemCoreClock / 1000000u;
    if (cycles_per_us == 0u) {
        return HAL_GetTick() * 1000u;
    }
    return DWT->CYCCNT / cycles_per_us;
}

void board_delay_us(uint32_t us)
{
    if (!s_dwt_ok) {
        HAL_Delay((us + 999u) / 1000u);
        return;
    }
    const uint32_t start = DWT->CYCCNT;
    const uint32_t cycles = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < cycles) {
        __NOP();
    }
}

void board_error(const char *msg)
{
    (void)msg;

    __disable_irq();

    /* 直接把 TB6612 的 STBY 拉低，不依赖任何驱动状态 */
    GPIOB->BRR = GPIO_PIN_9;

    for (;;) {
        /* PC13 快闪：可视化"我死机了" */
        GPIOC->ODR ^= GPIO_PIN_13;
        for (volatile uint32_t i = 0; i < 200000u; i++) {
            __NOP();
        }
    }
}
