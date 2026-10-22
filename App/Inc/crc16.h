/**
 * @file    crc16.h
 * @brief   CRC-16/CCITT-FALSE（poly=0x1021, init=0xFFFF, 不反射, 不异或输出）。
 *          "123456789" 的校验值应为 0x29B1。
 */
#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stddef.h>

uint16_t crc16_ccitt(const uint8_t *data, size_t len);

/** 流式计算：先传 CRC16_INIT，再逐个喂字节。 */
#define CRC16_INIT 0xFFFFu
uint16_t crc16_update(uint16_t crc, uint8_t byte);

#endif /* CRC16_H */
