#include "link.h"

#include <string.h>

#include "app_config.h"
#include "board.h"

#define LINK_TX_BUFFER_SIZE 512u
#define LINK_TX_CHUNK       64u

static UART_HandleTypeDef s_huart;

static uint8_t  s_rx_byte;
static volatile uint8_t  s_rx_ring[LINK_RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint32_t s_rx_overflow;

static uint8_t  s_tx_ring[LINK_TX_BUFFER_SIZE];
static volatile uint16_t s_tx_head;
static volatile uint16_t s_tx_tail;
static uint8_t  s_tx_chunk[LINK_TX_CHUNK];
static volatile bool s_tx_busy;

/* -------------------------------------------------------------------------- */

static void link_start_rx(void)
{
    (void)HAL_UART_Receive_IT(&s_huart, &s_rx_byte, 1u);
}

static void link_pump_tx(void)
{
    if (s_tx_busy || s_tx_head == s_tx_tail) {
        return;
    }

    uint16_t n = 0;
    while (n < LINK_TX_CHUNK && s_tx_head != s_tx_tail) {
        s_tx_chunk[n++] = s_tx_ring[s_tx_tail];
        s_tx_tail = (uint16_t)((s_tx_tail + 1u) % LINK_TX_BUFFER_SIZE);
    }

    if (n > 0u) {
        s_tx_busy = true;
        if (HAL_UART_Transmit_IT(&s_huart, s_tx_chunk, n) != HAL_OK) {
            s_tx_busy = false;
        }
    }
}

/* -------------------------------------------------------------------------- */

void link_init(void)
{
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_2;              /* TX */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_3;              /* RX */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    s_huart.Instance = USART2;
    s_huart.Init.BaudRate = LINK_BAUDRATE;
    s_huart.Init.WordLength = UART_WORDLENGTH_8B;
    s_huart.Init.StopBits = UART_STOPBITS_1;
    s_huart.Init.Parity = UART_PARITY_NONE;
    s_huart.Init.Mode = UART_MODE_TX_RX;
    s_huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_huart.Init.OverSampling = UART_OVERSAMPLING_16;
    (void)HAL_UART_Init(&s_huart);

    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_tx_head = 0u;
    s_tx_tail = 0u;
    s_tx_busy = false;

    HAL_NVIC_SetPriority(USART2_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    link_start_rx();
}

void link_poll(void)
{
    link_pump_tx();
}

uint32_t link_read(uint8_t *dst, uint32_t max)
{
    uint32_t n = 0u;
    while (n < max && s_rx_tail != s_rx_head) {
        dst[n++] = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) % LINK_RX_BUFFER_SIZE);
    }
    return n;
}

void link_write(const uint8_t *src, uint32_t len)
{
    if (src == NULL) {
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        const uint16_t next = (uint16_t)((s_tx_head + 1u) % LINK_TX_BUFFER_SIZE);
        if (next == s_tx_tail) {
            break;  /* 队列满：丢弃，宁可丢遥测也不能阻塞控制 */
        }
        s_tx_ring[s_tx_head] = src[i];
        s_tx_head = next;
    }
    link_pump_tx();
}

void link_write_str(const char *s)
{
    if (s == NULL) {
        return;
    }
    link_write((const uint8_t *)s, (uint32_t)strlen(s));
}

uint32_t link_rx_overflow(void)
{
    return s_rx_overflow;
}

void link_irq_handler(void)
{
    HAL_UART_IRQHandler(&s_huart);
}

/* -------------------------------------------------------------------------- */
/* HAL 回调                                                                    */
/* -------------------------------------------------------------------------- */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART2) {
        return;
    }

    const uint16_t next = (uint16_t)((s_rx_head + 1u) % LINK_RX_BUFFER_SIZE);
    if (next != s_rx_tail) {
        s_rx_ring[s_rx_head] = s_rx_byte;
        s_rx_head = next;
    } else {
        s_rx_overflow++;
    }

    link_start_rx();
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        s_tx_busy = false;
        /* 不在中断里继续发送，交给 link_poll，避免和主循环竞争 */
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        (void)HAL_UART_AbortReceive(&s_huart);
        link_start_rx();
    }
}
