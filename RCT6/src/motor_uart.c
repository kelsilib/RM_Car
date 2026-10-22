/**
 * @file motor_uart.c
 * @brief 解释驱动发来的文字、拼出要发给驱动的命令，并按实际接线调整四轮顺序。
 *
 * 字节先由串口中断存起来，再经board_motor_read、rm_app_receive送到这里。
 * 调试时看malformed（坏包数）、unknown（未识别的包数）、last_packet（最近的有效包）。
 * 命令必须使用厂家规定的$...#格式，不能自行加校验码或换格式，否则驱动看不懂。
 */
#include "motor_uart.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

void rm_motor_parser_init(rm_motor_parser_t *p) { memset(p, 0, sizeof *p); }

/* 从当前位置读一个整数，可带负号。读完后指针停在逗号或字符串结尾。
 * 每读一位都先检查能否装进32位整数，防止很长的数字溢出成另一个数。 */
static bool integer(const char **s, int32_t *v)
{
    const char *p = *s;
    bool neg = *p == '-';
    if (neg) ++p;
    if (*p < '0' || *p > '9') return false;
    uint32_t n = 0, limit = neg ? 2147483648u : 2147483647u;
    do {
        unsigned d = (unsigned)(*p - '0');
        if (n > (limit - d) / 10u) return false;
        n = n * 10u + d;
        ++p;
    } while (*p >= '0' && *p <= '9');
    *v = neg ? (n == 2147483648u ? INT32_MIN : -(int32_t)n) : (int32_t)n;
    *s = p;
    return true;
}

/* 读取12、-2.5这样的普通数值。空格、1e2、NaN、Inf都不接受。
 * 这里只负责读数；后面还会检查这个速度或电压是否超出允许范围。 */
static bool decimal(const char **s, float *v)
{
    const char *p = *s;
    bool neg = *p == '-';
    if (neg) ++p;
    if (*p < '0' || *p > '9') return false;
    float n = 0.0f;
    do {
        n = n * 10.0f + (float)(*p++ - '0');
        if (n > 1000000.0f) return false;
    } while (*p >= '0' && *p <= '9');
    if (*p == '.') {
        ++p;
        if (*p < '0' || *p > '9') return false;
        float scale = 0.1f;
        do {
            n += (float)(*p++ - '0') * scale;
            scale *= 0.1f;
        } while (*p >= '0' && *p <= '9');
    }
    *s = p;
    *v = neg ? -n : n;
    return isfinite(*v);
}

static bool parse(rm_motor_parser_t *p, rm_motor_packet_t *out, uint32_t now)
{
    /* 看到结尾的#才进这里。frame里只留下中间内容，并加了字符串结束标记。 */
    rm_motor_packet_t pkt = {0};
    const char *s = p->frame;
    if (!strncmp(s, "MAll:", 5)) { pkt.kind = RM_PACKET_COUNT; s += 5; }
    else if (!strncmp(s, "MTEP:", 5)) { pkt.kind = RM_PACKET_DELTA; s += 5; }
    else if (!strncmp(s, "MSPD:", 5)) { pkt.kind = RM_PACKET_SPEED; s += 5; }
    else if (!strncmp(s, "Battery:", 8)) {
        pkt.kind = RM_PACKET_BATTERY; s += 8;
        if (!decimal(&s, &pkt.battery_v) || pkt.battery_v < 0.0f ||
            pkt.battery_v > 30.0f || s[0] != 'V' || s[1] != '\0') goto bad;
        goto done;
    } else { ++p->unknown; return false; } /* 不认识的回复只计数，不把它当成电机反馈 */
    /* 四轮字段必须刚好四个：不能少、不能多，最后不能再有其他字符。 */
    for (unsigned i = 0; i < RM_WHEELS; ++i) {
        if (pkt.kind == RM_PACKET_SPEED) {
            if (!decimal(&s, &pkt.speeds[i]) || fabsf(pkt.speeds[i]) > 1000.0f)
                goto bad;
        } else if (!integer(&s, &pkt.counts[i])) goto bad;
        if (i != RM_WHEELS - 1) {
            if (*s != ',') goto bad;
            ++s;
        }
    }
    if (*s != '\0') goto bad;
done:
    /* 整包都检查通过后才交给调用者，不能只读到前两个轮子就提前使用。 */
    pkt.at_ms = now;
    *out = pkt;
    return true;
bad:
    /* 应用看到坏包数增加，就会记住通信故障并禁止运动，排查后需要复位主控。 */
    ++p->malformed;
    return false;
}

bool rm_motor_feed(rm_motor_parser_t *p, uint8_t b, uint32_t now,
                   rm_motor_packet_t *packet)
{
    /* 一个包收了一半，超过50ms才来下个字节，就不能把新旧内容硬拼起来。
     * 如果一直没有下个字节，safety会通过200ms反馈超时发现断线。 */
    if (p->collecting && (uint32_t)(now - p->last_byte_ms) > 50u) {
        ++p->malformed;
        p->collecting = false;
        p->length = 0;
    }
    p->last_byte_ms = now;
    /* $表示新包开始。如果旧包还没收完就又遇到$，记一次坏包，再从新$重新收。 */
    if (b == '$') {
        if (p->collecting && p->length) ++p->malformed;
        p->length = 0; p->collecting = true;
        return false;
    }
    if (!p->collecting) return false; /* 还没见到$时，乱码和换行都跳过 */
    /* #表示收齐一包，现在才检查里面的数字。半包不能用来证明电机通信正常。 */
    if (b == '#') {
        p->frame[p->length] = '\0';
        p->collecting = false;
        return parse(p, packet, now);
    }
    /* 缓冲区要留1字节放结束标记。内容太长或出现异常字符，就丢掉并等下一个$。 */
    if (b < 0x20 || b > 0x7e || p->length >= sizeof p->frame - 1) {
        ++p->malformed; p->collecting = false; p->length = 0;
        return false;
    }
    p->frame[p->length++] = (char)b;
    return false;
}

bool rm_motor_command(char *out, size_t cap, bool speed_mode,
                      const int32_t channel[RM_WHEELS])
{
    /* speed_mode只决定拼spd还是pwm，不能代替“允不允许运动”的检查。
     * spd:0让驱动继续控速并维持零速；pwm:0退出控速。两者都没有切断电源。 */
    if (!out || !cap || !channel) return false;
    out[0] = '\0';
    int32_t limit = speed_mode ? 1000 : 3600;
    for (unsigned i = 0; i < RM_WHEELS; ++i)
        if (channel[i] < -limit || channel[i] > limit) return false;
    int n = snprintf(out, cap, "$%s:%ld,%ld,%ld,%ld#", speed_mode ? "spd" : "pwm",
        (long)channel[0], (long)channel[1], (long)channel[2], (long)channel[3]);
    if (n < 0 || (size_t)n >= cap) { out[0] = '\0'; return false; }
    return true;
}

/* 检查四个轮子是否各用一个接口，正反方向是否都填好了。
 * 这里仅检查顺序和符号，其他参数是否验收过由safety统一检查。 */
static bool mapping_valid(const rm_config_t *c)
{
    if (!c) return false;
    unsigned bits = 0;
    for (unsigned i = 0; i < RM_WHEELS; ++i) {
        if (c->channel[i] >= RM_WHEELS ||
            (c->direction[i] != -1 && c->direction[i] != 1)) return false;
        unsigned bit = 1u << c->channel[i];
        if (bits & bit) return false;
        bits |= bit;
    }
    return true;
}

bool rm_motor_map_command(const rm_config_t *c, const int32_t wheel[RM_WHEELS],
                          int32_t channel[RM_WHEELS])
{
    if (!mapping_valid(c)) return false;
    /* 先写到临时数组，全部算好再复制。这样输入、输出用同一个数组也不会串位。 */
    int32_t tmp[RM_WHEELS];
    for (unsigned i = 0; i < RM_WHEELS; ++i) {
        /* 最小负数取反会超出32位正数范围；遇到这种情况先返回失败。 */
        if (wheel[i] == INT32_MIN && c->direction[i] == -1) return false;
        tmp[c->channel[i]] = wheel[i] * c->direction[i];
    }
    memcpy(channel, tmp, sizeof tmp);
    return true;
}

bool rm_motor_unmap_counts(const rm_config_t *c,
                           const int32_t channel[RM_WHEELS],
                           int32_t wheel[RM_WHEELS])
{
    /* 把驱动M1～M4的计数，排回“左前、右前、左后、右后”，再调整正反号。
     * 手转一个轮子时，可以检查是否只有对应轮子的计数在变、正负是否正确。 */
    if (!mapping_valid(c)) return false;
    int32_t tmp[RM_WHEELS];
    for (unsigned i = 0; i < RM_WHEELS; ++i) {
        int32_t v = channel[c->channel[i]];
        if (v == INT32_MIN && c->direction[i] == -1) return false;
        tmp[i] = v * c->direction[i];
    }
    memcpy(wheel, tmp, sizeof tmp);
    return true;
}
