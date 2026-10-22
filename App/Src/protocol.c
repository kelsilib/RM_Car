#include "protocol.h"

#include <string.h>

#include "crc16.h"

size_t proto_encode(uint8_t type, const uint8_t *payload, uint8_t len, uint8_t *out)
{
    if (out == NULL || (payload == NULL && len > 0u)) {
        return 0u;
    }
    if (len > PROTO_MAX_PAYLOAD) {
        return 0u;
    }

    out[0] = PROTO_SOF1;
    out[1] = PROTO_SOF2;
    out[2] = len;
    out[3] = type;

    if (len > 0u) {
        memcpy(&out[4], payload, len);
    }

    /* CRC 覆盖 LEN、TYPE、PAYLOAD */
    const uint16_t crc = crc16_ccitt(&out[2], (size_t)len + 2u);
    out[4u + len] = (uint8_t)(crc & 0xFFu);
    out[5u + len] = (uint8_t)(crc >> 8);

    return (size_t)len + 6u;
}

void proto_put_f32(uint8_t *p, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    p[0] = (uint8_t)(bits & 0xFFu);
    p[1] = (uint8_t)((bits >> 8) & 0xFFu);
    p[2] = (uint8_t)((bits >> 16) & 0xFFu);
    p[3] = (uint8_t)((bits >> 24) & 0xFFu);
}

float proto_get_f32(const uint8_t *p)
{
    const uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                          ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

void proto_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

uint16_t proto_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void proto_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

uint32_t proto_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
