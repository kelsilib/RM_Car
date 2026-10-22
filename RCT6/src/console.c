/**
 * 电脑串口助手选择115200/8N1，把整行指令加回车一次发出。
 * help看指令，ps2看摇杆，counts看编码器，config看缺哪些参数，stop随时取消许可。
 * 不提供set verified或原始$pwm透传。cal arm/pulse仍必须通过应用层的受限检查。
 */
#include "console.h"
#include "firmware_version.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void rm_console_init(rm_console_t *c) { memset(c, 0, sizeof *c); }
void rm_console_fault(rm_console_t *c)
{
    rm_console_init(c);
    c->failed = true;
}

/* 让GCC也检查这里的格式和参数类型，避免把格式错误带到单片机上。 */
static void reply(char *out, size_t cap, const char *format, ...) __attribute__((format(printf,3,4)));
static void reply(char *out, size_t cap, const char *format, ...)
{
    /* 自动日志没有这个前缀。电脑工具只把带前缀的完整行当作指令回复。 */
    const size_t prefix = sizeof RM_CONSOLE_REPLY_PREFIX - 1u;
    memcpy(out, RM_CONSOLE_REPLY_PREFIX, prefix);
    va_list args;
    va_start(args, format);
    int n = vsnprintf(out + prefix, cap - prefix, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= cap - prefix)
        (void)snprintf(out, cap, RM_CONSOLE_REPLY_PREFIX "ERR reply_too_long\r\n");
}

static bool number(const char *word, int *out)
{
    bool negative = *word == '-';
    if (negative) ++word;
    if (!*word) return false;
    int value = 0;
    for (; *word; ++word) {
        if (*word < '0' || *word > '9' || value > 10000) return false;
        value = value * 10 + (*word - '0');
    }
    *out = negative ? -value : value;
    return true;
}

/* 浮点数只在内部计算；输出整数千分比，不依赖nano C库默认没打开的浮点printf。 */
static long per_mille(float value)
{
    return isfinite(value) && fabsf(value) <= 1.0f ? lroundf(value * 1000.0f) : 0;
}

static void execute(rm_app_t *a, char *line, uint32_t now, char *out, size_t cap, rm_console_action_t *action)
{
    char *words[5];
    unsigned count = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (count == 5) { reply(out, cap, "ERR too_many_arguments\r\n"); return; }
        words[count++] = p;
        while (*p && *p != ' ') ++p;
        if (*p) *p++ = '\0';
    }
    if (!count) { reply(out, cap, "ERR empty_command\r\n"); return; }
    if (count == 1 && !strcmp(words[0], "help")) {
        reply(out, cap, "help version status ps2 counts config output battery stop cal arm cal pulse M PWM MS cal cancel driver read driver reply driver voltage\r\n");
    } else if (count == 1 && !strcmp(words[0], "version")) {
        reply(out, cap, "version=" RM_FIRMWARE_ID "\r\n");
    } else if (count == 1 && !strcmp(words[0], "status")) {
        reply(out, cap, "state=%u inhibit=0x%02lX motor_seen=%u age_ms=%lu bad=%lu unknown=%lu cal=%u\r\n",
            (unsigned)a->safety.state, (unsigned long)a->safety.inhibit, (unsigned)a->motor_seen,
            (unsigned long)(now-a->motor_at_ms), (unsigned long)a->parser.malformed,
            (unsigned long)a->parser.unknown, (unsigned)a->calibration.state);
    } else if (count == 1 && !strcmp(words[0], "ps2")) {
        reply(out, cap, "ps2 valid=%u fwd=%ld left=%ld yaw=%ld start=%u select=%u age_ms=%lu\r\n",
            (unsigned)a->remote.valid, per_mille(a->remote.forward), per_mille(a->remote.left),
            per_mille(a->remote.yaw), (unsigned)a->remote.arm, (unsigned)a->remote.stop,
            (unsigned long)(now-a->remote.at_ms));
    } else if (count == 1 && !strcmp(words[0], "counts")) {
        reply(out, cap, "counts seen=%u M1=%ld M2=%ld M3=%ld M4=%ld age_ms=%lu\r\n",
            (unsigned)a->motor_seen, (long)a->motor_counts[0], (long)a->motor_counts[1],
            (long)a->motor_counts[2], (long)a->motor_counts[3], (unsigned long)(now-a->motor_at_ms));
    } else if (count == 1 && !strcmp(words[0], "config")) {
        reply(out, cap, "config issues=0x%02lX speed_unit=%u cal_enabled=%u cal_max_pwm=%u cal_max_ms=%u\r\n",
            (unsigned long)rm_config_issues(&a->config), (unsigned)a->config.speed_unit,
            (unsigned)a->config.calibration_enabled, (unsigned)a->config.calibration_max_pwm,
            (unsigned)a->config.calibration_max_ms);
    } else if (count == 1 && !strcmp(words[0], "output")) {
        reply(out, cap, "output mode=%s M1=%ld M2=%ld M3=%ld M4=%ld\r\n", a->last_speed_mode ? "spd" : "pwm",
            (long)a->last_channel[0], (long)a->last_channel[1], (long)a->last_channel[2], (long)a->last_channel[3]);
    } else if (count == 1 && !strcmp(words[0], "battery")) {
        reply(out, cap, "battery seen=%u millivolts=%ld age_ms=%lu\r\n", (unsigned)a->battery_seen,
            a->battery_seen ? lroundf(a->battery_v*1000.0f) : 0L, (unsigned long)(now-a->battery_at_ms));
    } else if ((count == 1 && !strcmp(words[0], "stop")) ||
               (count == 2 && !strcmp(words[0], "cal") && !strcmp(words[1], "cancel"))) {
        rm_app_stop(a);
        *action = RM_CONSOLE_STOP;
        reply(out, cap, "OK stopped; fresh_START_required\r\n");
    } else if (count == 2 && !strcmp(words[0], "cal") && !strcmp(words[1], "arm")) {
        bool ok = rm_app_cal_arm(a, now);
        if (ok) reply(out, cap, "OK cal_ready window_ms=5000\r\n");
        else reply(out, cap, "ERR cal_not_ready state=%u inhibit=0x%02lX\r\n",
                   (unsigned)a->safety.state, (unsigned long)a->calibration.inhibit);
    } else if (count == 5 && !strcmp(words[0], "cal") && !strcmp(words[1], "pulse")) {
        int channel, pwm, duration;
        if (!number(words[2], &channel) || !number(words[3], &pwm) || !number(words[4], &duration) ||
            channel < 1 || channel > 4 || duration < 20) {
            reply(out, cap, "ERR usage: cal pulse M1_to_4 signed_pwm duration_ms\r\n");
            return;
        }
        bool ok = rm_app_cal_pulse(a, now, (unsigned)(channel-1), pwm, (unsigned)duration);
        reply(out, cap, ok ? "OK one_pulse_queued\r\n" : "ERR pulse_denied; check_limits_and_cal_arm\r\n");
    } else if (count == 2 && !strcmp(words[0], "driver") && !strcmp(words[1], "reply")) {
        reply(out, cap, "driver_reply seen=%u age_ms=%lu frame=%s\r\n", (unsigned)a->driver_reply_seen,
            (unsigned long)(now-a->driver_reply_at_ms), a->driver_reply_seen ? a->driver_reply : "none");
    } else if (count == 2 && !strcmp(words[0], "driver") &&
               (!strcmp(words[1], "read") || !strcmp(words[1], "voltage"))) {
        if (rm_app_output_active(a) || a->transport_fault) {
            reply(out, cap, "ERR stop_first_or_reset_transport_fault\r\n");
            return;
        }
        *action = !strcmp(words[1], "read") ? RM_CONSOLE_READ_DRIVER : RM_CONSOLE_READ_VOLTAGE;
        reply(out, cap, "OK query_queued; inspect_feedback_not_ACK\r\n");
    } else {
        reply(out, cap, "ERR unknown_command; send_help\r\n");
    }
}

bool rm_console_feed(rm_console_t *c, rm_app_t *a, uint8_t byte, uint32_t received_ms,
                     uint32_t now, char *out, size_t cap, rm_console_action_t *action)
{
    if (!c || !a || !out || cap < RM_CONSOLE_REPLY_CAP || !action) return false;
    *action = RM_CONSOLE_NONE;
    out[0] = '\0';
    if (c->failed) return false;
    if (c->swallow_lf) {
        c->swallow_lf = false;
        if (byte == '\n') return false;
    }
    if (!c->length && !c->dropping) c->started_at = received_ms;
    if (byte == '\r' || byte == '\n') {
        bool bad = c->dropping;
        c->swallow_lf = byte == '\r';
        c->line[c->length] = '\0';
        /* 旧缓存里的动作指令不能在主循环恢复后突然执行。电脑工具应一次发送整行。 */
        if (bad) reply(out, cap, "ERR invalid_or_long_line\r\n");
        else if ((uint32_t)(now-received_ms) > 200u || (uint32_t)(now-c->started_at) > 2000u)
            reply(out, cap, "ERR stale_command; resend_whole_line\r\n");
        else execute(a, c->line, now, out, cap, action);
        c->length = 0;
        c->dropping = false;
        return true;
    }
    if (c->dropping) return false;
    if (byte < 0x20 || byte > 0x7e || c->length >= sizeof c->line-1) c->dropping = true;
    else c->line[c->length++] = (char)byte;
    return false;
}
