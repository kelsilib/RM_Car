#include "commander.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "app_config.h"
#include "crc16.h"
#include "link.h"
#include "protocol.h"

/* -------------------------------------------------------------------------- */
/* 内部状态                                                                    */
/* -------------------------------------------------------------------------- */

typedef enum {
    BIN_SYNC1 = 0,
    BIN_SYNC2,
    BIN_LEN,
    BIN_BODY,
    BIN_CRC_L,
    BIN_CRC_H
} bin_state_t;

static struct {
    const commander_hooks_t *hooks;
    bin_state_t st;

    uint8_t len;
    uint8_t type;
    uint8_t idx;
    uint8_t body[PROTO_MAX_PAYLOAD + 1u];
    uint8_t crc_l;

    char    line[96];
    uint8_t line_len;
    bool    line_overflow;

    uint32_t frame_errors;
} s_cmd;

/* -------------------------------------------------------------------------- */
/* 工具                                                                        */
/* -------------------------------------------------------------------------- */

static bool is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

static bool parse_float(const char *str, float *out)
{
    if (str == NULL || out == NULL || *str == '\0') {
        return false;
    }

    const char *p = str;
    bool neg = false;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }

    bool any = false;
    float value = 0.0f;

    while (is_digit(*p)) {
        value = value * 10.0f + (float)(*p - '0');
        p++;
        any = true;
    }

    if (*p == '.') {
        p++;
        float scale = 0.1f;
        while (is_digit(*p)) {
            value += (float)(*p - '0') * scale;
            scale *= 0.1f;
            p++;
            any = true;
        }
    }

    if (!any || *p != '\0') {
        return false;
    }

    *out = neg ? -value : value;
    return true;
}

static bool parse_int(const char *str, long *out)
{
    if (str == NULL || out == NULL || *str == '\0') {
        return false;
    }
    const char *p = str;
    bool neg = false;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    if (!is_digit(*p)) {
        return false;
    }
    long v = 0;
    while (is_digit(*p)) {
        v = v * 10 + (*p - '0');
        p++;
        if (p - str > 10) {
            return false;
        }
    }
    if (*p != '\0') {
        return false;
    }
    *out = neg ? -v : v;
    return true;
}

static bool str_ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z') {
            ca = (char)(ca - 'a' + 'A');
        }
        if (cb >= 'a' && cb <= 'z') {
            cb = (char)(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return false;
        }
    }
    return (*a == '\0' && *b == '\0');
}

static int tokenize(char *line, char *argv[], int max)
{
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < max) {
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == '=') {
            *p = '\0';
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != ',' && *p != '=') {
            p++;
        }
    }
    return argc;
}

static void reply_u32(uint32_t v)
{
    char tmp[11];
    int i = 10;
    tmp[i] = '\0';
    do {
        tmp[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v > 0u && i > 0);
    link_write_str(&tmp[i]);
}

static void send_ack(uint8_t cmd)
{
    uint8_t frame[8];
    const uint8_t payload[1] = {cmd};
    const size_t n = proto_encode(MSG_ACK, payload, 1u, frame);
    link_write(frame, (uint32_t)n);
}

static void send_nak(uint8_t cmd, uint8_t reason)
{
    uint8_t frame[8];
    const uint8_t payload[2] = {cmd, reason};
    const size_t n = proto_encode(MSG_NAK, payload, 2u, frame);
    link_write(frame, (uint32_t)n);
}

/* -------------------------------------------------------------------------- */
/* 文本命令                                                                    */
/* -------------------------------------------------------------------------- */

static void handle_text(char *line)
{
    char *argv[6];
    const int argc = tokenize(line, argv, 6);
    if (argc == 0) {
        return;
    }

    if (str_ieq(argv[0], "V") || str_ieq(argv[0], "VEL")) {
        float vx, vy, wz;
        if (argc != 4 || !parse_float(argv[1], &vx) ||
            !parse_float(argv[2], &vy) || !parse_float(argv[3], &wz)) {
            link_write_str("ERR ARG\r\n");
            return;
        }
        if (s_cmd.hooks->on_vel != NULL) {
            s_cmd.hooks->on_vel(vx, vy, wz);
        }
        link_write_str("OK\r\n");
        return;
    }

    if (str_ieq(argv[0], "ARM")) {
        if (s_cmd.hooks->on_arm != NULL) {
            s_cmd.hooks->on_arm();
        }
        link_write_str("OK\r\n");
        return;
    }

    if (str_ieq(argv[0], "DISARM")) {
        if (s_cmd.hooks->on_disarm != NULL) {
            s_cmd.hooks->on_disarm();
        }
        link_write_str("OK\r\n");
        return;
    }

    if (str_ieq(argv[0], "STOP")) {
        if (s_cmd.hooks->on_stop != NULL) {
            s_cmd.hooks->on_stop();
        }
        link_write_str("OK\r\n");
        return;
    }

    if (str_ieq(argv[0], "PID")) {
        long wheel;
        float kp, ki, kd;
        if (argc != 5 || !parse_int(argv[1], &wheel) ||
            !parse_float(argv[2], &kp) || !parse_float(argv[3], &ki) ||
            !parse_float(argv[4], &kd) || wheel < 0 || wheel > 255) {
            link_write_str("ERR ARG\r\n");
            return;
        }
        if (s_cmd.hooks->on_pid != NULL) {
            s_cmd.hooks->on_pid((uint8_t)wheel, kp, ki, kd);
        }
        link_write_str("OK\r\n");
        return;
    }

    if (str_ieq(argv[0], "RATE")) {
        long hz;
        if (argc != 2 || !parse_int(argv[1], &hz) || hz < 1 || hz > 100) {
            link_write_str("ERR ARG\r\n");
            return;
        }
        if (s_cmd.hooks->on_rate != NULL) {
            s_cmd.hooks->on_rate((uint16_t)(1000 / hz));
        }
        link_write_str("OK\r\n");
        return;
    }

    if (str_ieq(argv[0], "YAWZERO") || str_ieq(argv[0], "YAW")) {
        if (s_cmd.hooks->on_yaw_zero != NULL) {
            s_cmd.hooks->on_yaw_zero();
        }
        link_write_str("OK\r\n");
        return;
    }

    if (str_ieq(argv[0], "PING")) {
        if (s_cmd.hooks->on_ping != NULL) {
            s_cmd.hooks->on_ping();
        }
        return;
    }

    if (str_ieq(argv[0], "HELP")) {
        link_write_str("CMD: V vx vy wz | ARM | DISARM | STOP | PID w kp ki kd | "
                       "RATE hz | YAWZERO | PING\r\n");
        return;
    }

    link_write_str("ERR CMD\r\n");
}

static void text_feed(uint8_t b)
{
    if (b == '\r' || b == '\n') {
        if (s_cmd.line_len > 0u) {
            s_cmd.line[s_cmd.line_len] = '\0';
            handle_text(s_cmd.line);
            s_cmd.line_len = 0u;
        }
        s_cmd.line_overflow = false;
        return;
    }

    if (b < 0x20u || b > 0x7Eu) {
        return;
    }

    if (s_cmd.line_len < (sizeof(s_cmd.line) - 1u)) {
        s_cmd.line[s_cmd.line_len++] = (char)b;
    } else {
        s_cmd.line_len = 0u;
        s_cmd.line_overflow = true;
    }
}

/* -------------------------------------------------------------------------- */
/* 二进制命令                                                                  */
/* -------------------------------------------------------------------------- */

static void dispatch_binary(uint8_t type, const uint8_t *payload, uint8_t len)
{
    const commander_hooks_t *h = s_cmd.hooks;

    switch (type) {
    case CMD_VEL:
        if (len != 12u || h->on_vel == NULL) {
            send_nak(type, NAK_BAD_PARAM);
            return;
        }
        h->on_vel(proto_get_f32(&payload[0]), proto_get_f32(&payload[4]),
                  proto_get_f32(&payload[8]));
        return;  /* 速度指令不逐帧 ACK，省带宽 */

    case CMD_ARM:
        if (h->on_arm == NULL) {
            send_nak(type, NAK_UNSUPPORTED);
        } else {
            h->on_arm();
            send_ack(type);
        }
        return;

    case CMD_DISARM:
        if (h->on_disarm == NULL) {
            send_nak(type, NAK_UNSUPPORTED);
        } else {
            h->on_disarm();
            send_ack(type);
        }
        return;

    case CMD_STOP:
        if (h->on_stop == NULL) {
            send_nak(type, NAK_UNSUPPORTED);
        } else {
            h->on_stop();
            send_ack(type);
        }
        return;

    case CMD_PID:
        if (len != 13u || h->on_pid == NULL) {
            send_nak(type, NAK_BAD_PARAM);
            return;
        }
        h->on_pid(payload[0], proto_get_f32(&payload[1]),
                  proto_get_f32(&payload[5]), proto_get_f32(&payload[9]));
        send_ack(type);
        return;

    case CMD_TELEM_RATE:
        if (len != 2u || h->on_rate == NULL) {
            send_nak(type, NAK_BAD_PARAM);
            return;
        }
        h->on_rate(proto_get_u16(&payload[0]));
        send_ack(type);
        return;

    case CMD_PING:
        if (h->on_ping != NULL) {
            h->on_ping();
        }
        send_ack(type);
        return;

    case CMD_YAW_ZERO:
        if (h->on_yaw_zero == NULL) {
            send_nak(type, NAK_UNSUPPORTED);
        } else {
            h->on_yaw_zero();
            send_ack(type);
        }
        return;

    default:
        send_nak(type, NAK_UNSUPPORTED);
        return;
    }
}

static void binary_finish(uint8_t crc_l, uint8_t crc_h)
{
    const uint16_t rx_crc = (uint16_t)crc_l | ((uint16_t)crc_h << 8);

    uint16_t calc = crc16_update(CRC16_INIT, s_cmd.len);
    for (uint8_t i = 0; i < (uint8_t)(s_cmd.len + 1u); i++) {
        calc = crc16_update(calc, s_cmd.body[i]);
    }

    if (calc == rx_crc) {
        dispatch_binary(s_cmd.type, &s_cmd.body[1], s_cmd.len);
    } else {
        s_cmd.frame_errors++;
    }
}

/* -------------------------------------------------------------------------- */
/* 字节流状态机                                                                */
/* -------------------------------------------------------------------------- */

static void feed(uint8_t b)
{
    switch (s_cmd.st) {
    case BIN_SYNC1:
        if (b == PROTO_SOF1) {
            s_cmd.st = BIN_SYNC2;
            s_cmd.line_len = 0u;   /* 二进制帧开始，丢掉半截文本 */
        } else {
            text_feed(b);
        }
        break;

    case BIN_SYNC2:
        if (b == PROTO_SOF2) {
            s_cmd.st = BIN_LEN;
        } else if (b == PROTO_SOF1) {
            /* 连续 0xAA：保持在 SYNC2，等待 0x55 */
        } else {
            s_cmd.st = BIN_SYNC1;
            text_feed(PROTO_SOF1);
            text_feed(b);
        }
        break;

    case BIN_LEN:
        if (b > PROTO_MAX_PAYLOAD) {
            s_cmd.frame_errors++;
            s_cmd.st = BIN_SYNC1;
        } else {
            s_cmd.len = b;
            s_cmd.idx = 0u;
            s_cmd.st = BIN_BODY;
        }
        break;

    case BIN_BODY:
        s_cmd.body[s_cmd.idx++] = b;
        if (s_cmd.idx >= (uint8_t)(s_cmd.len + 1u)) {
            s_cmd.type = s_cmd.body[0];
            s_cmd.st = BIN_CRC_L;
        }
        break;

    case BIN_CRC_L:
        s_cmd.crc_l = b;
        s_cmd.st = BIN_CRC_H;
        break;

    case BIN_CRC_H:
        binary_finish(s_cmd.crc_l, b);
        s_cmd.st = BIN_SYNC1;
        break;

    default:
        s_cmd.st = BIN_SYNC1;
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* 对外接口                                                                    */
/* -------------------------------------------------------------------------- */

void commander_init(const commander_hooks_t *hooks)
{
    s_cmd.hooks = hooks;
    s_cmd.st = BIN_SYNC1;
    s_cmd.line_len = 0u;
    s_cmd.frame_errors = 0u;
    s_cmd.idx = 0u;
    s_cmd.len = 0u;
}

void commander_poll(void)
{
    uint8_t chunk[32];
    uint32_t n;

    while ((n = link_read(chunk, sizeof(chunk))) > 0u) {
        for (uint32_t i = 0; i < n; i++) {
            feed(chunk[i]);
        }
    }
}

uint32_t commander_frame_errors(void)
{
    return s_cmd.frame_errors;
}
