/**
 * @file board_hal.c
 * @brief 真正操作主控硬件的地方：时钟、引脚、串口和PS2接线。
 *
 * USART1 PA9/PA10：板载CH340调试口；USART2 PA2/PA3：电机驱动板。
 * 改PS2接线需同时改本文件的GPIO初始化和四个ps2_*回调，不能只改一处。
 * 按键含义放在应用代码里；这里仅负责具体哪个引脚拉高、拉低和收发数据。
 */
#include "board_hal.h"
#include "stm32f1xx_hal.h"
#include <string.h>

static UART_HandleTypeDef motor_uart, debug_uart; /* 分别对应USART2、USART1 */
/* 串口中断把新字节放进head位置，主循环从tail位置取走。到末尾后绕回开头。
 * 缓存满时记录故障，不用新字节覆盖还没处理的旧字节。 */
enum { RX_CAP = 512 };
static uint8_t rx_bytes[RX_CAP];
static uint32_t rx_times[RX_CAP]; /* 连同字节到达时间一起保存，不能把积压的旧包当成新反馈 */
static volatile uint16_t rx_head, rx_tail;
static volatile bool uart_failed; /* 串口出错就记为true；查线并复位主控后才清除 */
/* 电脑发来的指令单独缓存，不能把调试文字混进电机反馈解析器。 */
enum { DEBUG_RX_CAP = 128 };
static uint8_t debug_rx_bytes[DEBUG_RX_CAP];
static uint32_t debug_rx_times[DEBUG_RX_CAP];
static volatile uint16_t debug_rx_head, debug_rx_tail;
static volatile bool debug_failed;

static bool clock_init(void)
{
    /* 资料中外部晶振是8MHz，经PLL乘9得到72MHz。换板时先核对晶振，不能直接照搬。 */
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return false;
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV2; /* APB2也为36MHz，与资料中的UART示例一致 */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) return false;
    SystemCoreClockUpdate();
    /* DWT会数CPU运行了多少个时钟，用它给PS2做微秒延时，不占用TIM定时器。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return true;
}

void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG(); /* 只关JTAG，保留PA13/PA14的SWD下载调试 */
}

static bool uart_init(UART_HandleTypeDef *u, USART_TypeDef *instance)
{
    /* 两个UART均为115200、8数据位、无校验、1停止位、无硬件流控。 */
    u->Instance = instance;
    u->Init.BaudRate = 115200;
    u->Init.WordLength = UART_WORDLENGTH_8B;
    u->Init.StopBits = UART_STOPBITS_1;
    u->Init.Parity = UART_PARITY_NONE;
    u->Init.Mode = UART_MODE_TX_RX;
    u->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    u->Init.OverSampling = UART_OVERSAMPLING_16;
    return HAL_UART_Init(u) == HAL_OK;
}

bool board_init(void)
{
    HAL_Init();
    if (!clock_init()) return false;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_2 | GPIO_PIN_9; /* PA2电机TX、PA9调试TX，复用推挽输出 */
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_3 | GPIO_PIN_10; /* PA3电机RX、PA10调试RX，输入上拉 */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);
    if (!uart_init(&debug_uart, USART1) || !uart_init(&motor_uart, USART2)) return false;
    /* 电机接收优先级更高；电脑指令也只在中断里存字节，留给主循环解释。 */
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    SET_BIT(USART2->CR1, USART_CR1_RXNEIE | USART_CR1_PEIE);
    SET_BIT(USART2->CR3, USART_CR3_EIE);
    HAL_NVIC_SetPriority(USART1_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    SET_BIT(USART1->CR1, USART_CR1_RXNEIE | USART_CR1_PEIE);
    SET_BIT(USART1->CR3, USART_CR3_EIE);
    return true;
}

uint32_t board_millis(void) { return HAL_GetTick(); }

/* 串口一来字节就进这里：只保存字节、时间和错误，尽快返回。
 * 解释完整报文和算轮速都交给主循环，以免中断占用太久。 */
void USART2_IRQHandler(void)
{
    uint32_t sr = USART2->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
        uint8_t b = (uint8_t)USART2->DR; /* F1要求先读SR再读DR，以清除对应错误标志 */
        if (sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
            uart_failed = true;
            return;
        }
        /* 下一个写入位置碰到尚未读的位置，说明缓存满了：记故障，不覆盖旧内容。 */
        uint16_t next = (uint16_t)((rx_head + 1u) % RX_CAP);
        if (next == rx_tail) { uart_failed = true; return; }
        rx_bytes[rx_head] = b;
        rx_times[rx_head] = HAL_GetTick();
        __DMB(); /* 先把内容和时间存好，再更新head告诉主循环“有新字节了” */
        rx_head = next;
    }
}

bool board_motor_read(uint8_t *byte, uint32_t *received_ms)
{
    /* false只表示缓存暂时空了。要过了允许的等待时间，才能认为反馈断了。 */
    if (rx_head == rx_tail) return false;
    __DMB();
    *byte = rx_bytes[rx_tail];
    *received_ms = rx_times[rx_tail];
    __DMB();
    rx_tail = (uint16_t)((rx_tail + 1u) % RX_CAP);
    return true;
}

bool board_uart_fault(void) { return uart_failed; }

void USART1_IRQHandler(void)
{
    uint32_t sr = USART1->SR;
    if (!(sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))) return;
    uint8_t b = (uint8_t)USART1->DR;
    uint16_t next = (uint16_t)((debug_rx_head + 1u) % DEBUG_RX_CAP);
    if ((sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) || next == debug_rx_tail) {
        debug_failed = true;
        /* 出错后停止接收中断，避免大量乱码一直占CPU；发送日志仍然保留。 */
        CLEAR_BIT(USART1->CR1, USART_CR1_RXNEIE | USART_CR1_PEIE);
        CLEAR_BIT(USART1->CR3, USART_CR3_EIE);
        return;
    }
    debug_rx_bytes[debug_rx_head] = b;
    debug_rx_times[debug_rx_head] = HAL_GetTick();
    __DMB();
    debug_rx_head = next;
}

bool board_debug_read(uint8_t *byte, uint32_t *received_ms)
{
    if (debug_rx_head == debug_rx_tail || debug_failed) return false;
    __DMB();
    *byte = debug_rx_bytes[debug_rx_tail];
    *received_ms = debug_rx_times[debug_rx_tail];
    __DMB();
    debug_rx_tail = (uint16_t)((debug_rx_tail + 1u) % DEBUG_RX_CAP);
    return true;
}

bool board_debug_fault(void) { return debug_failed; }

bool board_motor_send(const char *frame)
{
    size_t n = strlen(frame);
    if (!n || n > 48) return false;
    /* 48字节正常约4.2ms发完，最多等待约5ms。驱动不会逐条回复运动命令。
     * 返回true只说明主控把字节发出去了，不能证明线没断或驱动已经执行。 */
    if (HAL_UART_Transmit(&motor_uart, (uint8_t *)frame, (uint16_t)n, 5) != HAL_OK) {
        uart_failed = true;
        return false;
    }
    return true;
}

void board_debug(const char *s)
{
    /* 日志和带reply前缀的指令回复从CH340输出。192字节正常约16.7ms，最多等约20ms。
     * printf不会自动走这里。失败会被主循环发现，标定许可随即取消。 */
    size_t n = strlen(s);
    if (n > BOARD_DEBUG_TX_CAP || HAL_UART_Transmit(&debug_uart, (uint8_t *)s, (uint16_t)n, 20) != HAL_OK)
        debug_failed = true;
}

#if RM_PS2_WIRED
/* PS2候选接线：PB12=ATT/CS片选，PB13=CLK时钟，PB14=DAT接收，PB15=CMD发送。
 * ATT拉低表示开始通信，CLK平时为高；DAT读接收器，CMD是发给接收器的指令。 */
static void ps2_select(void *ctx, bool active)
{ (void)ctx; HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, active ? GPIO_PIN_RESET : GPIO_PIN_SET); }
static void ps2_clock(void *ctx, bool high)
{ (void)ctx; HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, high ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static void ps2_command(void *ctx, bool high)
{ (void)ctx; HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, high ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static bool ps2_data(void *ctx)
{ (void)ctx; return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14) == GPIO_PIN_SET; }
static void ps2_delay(void *ctx, uint32_t us)
{
    /* 用“当前计数-起始计数”等够指定微秒数，计数器从最大值回到0也能正常算。 */
    (void)ctx;
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000u);
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) __NOP();
}
static const rm_ps2_bus_t ps2 = {NULL, ps2_select, ps2_clock, ps2_command, ps2_data, ps2_delay};
#endif

bool board_ps2_start(void)
{
#if RM_PS2_WIRED
    /* 先准备好高电平，再把引脚切成输出，避免初始化时意外发出一个低脉冲。
     * 重连会再调用这里，所以每次都从这组已知电平开始。 */
    GPIO_InitTypeDef g = {0};
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;
    g.Mode = GPIO_MODE_OUTPUT_PP; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_14; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP; /* DAT只读 */
    HAL_GPIO_Init(GPIOB, &g);
    return rm_ps2_configure_analog(&ps2);
#else
    /* 接线开关未打开：四个引脚都不动。即使主循环重试，也不会给接收器发信号。 */
    return false;
#endif
}

bool board_ps2_read(rm_remote_t *remote)
{
#if RM_PS2_WIRED
    return rm_ps2_poll(&ps2, board_millis(), remote);
#else
    /* 没启用接线就每次清空结果，避免误把上次的值看成这次读包成功。 */
    *remote = (rm_remote_t){.at_ms = board_millis()};
    return false;
#endif
}

void board_panic(void)
{
    __disable_irq();
    /* 严重异常时尽力发一次PWM0，然后停住。中断已经关闭，不能再靠HAL毫秒计时等待。
     * 主控死机或TX线断开时，这条命令可能送不到；独立停车保护仍然必需。
     * 调试器停在这里时，先检查故障和初始化结果，不要强行跳过继续跑。 */
    if ((RCC->APB1ENR & RCC_APB1ENR_USART2EN) && (USART2->CR1 & USART_CR1_UE)) {
        static const char stop[] = "$pwm:0,0,0,0#";
        for (size_t i = 0; i < sizeof stop - 1; ++i) {
            unsigned budget = 100000;
            while (!(USART2->SR & USART_SR_TXE) && --budget) { }
            if (!budget) break;
            USART2->DR = (uint8_t)stop[i];
        }
    }
    for (;;) __NOP();
}
