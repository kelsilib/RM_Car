/**
 * @file    protocol.h
 * @brief   上位机通信协议：帧格式、命令字、遥测布局。
 *
 * 帧格式（小端）：
 *   +------+------+-----+------+---------------+--------+--------+
 *   | 0xAA | 0x55 | LEN | TYPE | PAYLOAD[LEN]  | CRC_L  | CRC_H  |
 *   +------+------+-----+------+---------------+--------+--------+
 *   LEN   = PAYLOAD 字节数（0..80）
 *   CRC16 = CRC-16/CCITT-FALSE，覆盖 LEN、TYPE、PAYLOAD
 *
 * 同时兼容纯文本命令（见 commander.c），方便用串口助手直接调试。
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define PROTO_SOF1        0xAAu
#define PROTO_SOF2        0x55u
#define PROTO_MAX_PAYLOAD 80u
#define PROTO_MAX_FRAME   (PROTO_MAX_PAYLOAD + 6u)
#define PROTO_VERSION     1u

typedef enum {
    /* 上位机 -> 下位机 */
    CMD_VEL        = 0x01,  /* f32 vx, f32 vy, f32 wz */
    CMD_ARM        = 0x02,  /* - */
    CMD_DISARM     = 0x03,  /* - */
    CMD_PID        = 0x04,  /* u8 wheel(0..3, 0xFF=全部), f32 kp, ki, kd */
    CMD_TELEM_RATE = 0x05,  /* u16 周期(ms) */
    CMD_PING       = 0x06,  /* - */
    CMD_STOP       = 0x07,  /* 速度清零，保持 armed 状态 */
    CMD_YAW_ZERO   = 0x08,  /* 把当前航向置为 0 */

    /* 下位机 -> 上位机 */
    MSG_TELEMETRY  = 0x81,
    MSG_ACK        = 0x82,  /* u8 被确认的命令字 */
    MSG_NAK        = 0x83,  /* u8 命令字, u8 原因 */
    MSG_TEXT       = 0x84,
} proto_msg_t;

/* MSG_NAK 原因码 */
#define NAK_BAD_STATE 1u
#define NAK_BAD_PARAM 2u
#define NAK_UNSUPPORTED 3u

/* 遥测负载布局（共 68 字节） */
#define TELEM_LEN        68u
#define TELEM_OFF_TICK   0u
#define TELEM_OFF_STATE  4u
#define TELEM_OFF_FLAGS  5u
#define TELEM_OFF_VX     6u
#define TELEM_OFF_VY     10u
#define TELEM_OFF_WZ     14u
#define TELEM_OFF_CMD_VX 18u
#define TELEM_OFF_CMD_VY 22u
#define TELEM_OFF_CMD_WZ 26u
#define TELEM_OFF_YAW    30u
#define TELEM_OFF_WHEEL  34u   /* 4 x f32 */
#define TELEM_OFF_DUTY   50u   /* 4 x f32 */
#define TELEM_OFF_IMU_OK 66u
#define TELEM_OFF_RSVD   67u

/* 遥测 flags 位定义 */
#define TELEM_FLAG_ARMED      (1u << 0)
#define TELEM_FLAG_TIMEOUT    (1u << 1)
#define TELEM_FLAG_YAW_HOLD   (1u << 2)
#define TELEM_FLAG_IMU_OK     (1u << 3)

/** 组帧，返回总字节数；缓冲区至少 PROTO_MAX_FRAME。 */
size_t proto_encode(uint8_t type, const uint8_t *payload, uint8_t len, uint8_t *out);

void   proto_put_f32(uint8_t *p, float v);
float  proto_get_f32(const uint8_t *p);
void   proto_put_u16(uint8_t *p, uint16_t v);
uint16_t proto_get_u16(const uint8_t *p);
void   proto_put_u32(uint8_t *p, uint32_t v);
uint32_t proto_get_u32(const uint8_t *p);

#endif /* PROTOCOL_H */
