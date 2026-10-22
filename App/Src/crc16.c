#include "crc16.h"

uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    for (uint8_t i = 0; i < 8u; i++) {
        if (crc & 0x8000u) {
            crc = (uint16_t)((crc << 1) ^ 0x1021u);
        } else {
            crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = CRC16_INIT;
    for (size_t i = 0; i < len; i++) {
        crc = crc16_update(crc, data[i]);
    }
    return crc;
}
