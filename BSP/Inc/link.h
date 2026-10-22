/**
 * @file    link.h
 * @brief   上位机串口链路（USART2, PA2/PA3）。
 *
 * 收发都走环形缓冲区：
 *   - 接收：1 字节中断 + 环形缓冲，主循环取；
 *   - 发送：中断分块发送，link_write 不阻塞，
 *     避免发遥测时把 100Hz 控制周期拖慢。
 */
#ifndef LINK_H
#define LINK_H

#include <stdint.h>

void     link_init(void);
void     link_poll(void);

/** 取出收到的字节，返回实际取出的数量。 */
uint32_t link_read(uint8_t *dst, uint32_t max);

/** 写入发送队列（满则丢弃），不阻塞。 */
void     link_write(const uint8_t *src, uint32_t len);
void     link_write_str(const char *s);

uint32_t link_rx_overflow(void);

/** 供 stm32f1xx_it.c 调用。 */
void     link_irq_handler(void);

#endif /* LINK_H */
