/* 联调分成单轮PWM短脉冲、确认方向、手柄限时试走三个阶段。
 * 行走构建另用START切换启用、R2给油，持续检查同一组健康条件。
 * 这里使用独立的会话许可，不把尚未完成的生产验收标志改成true。
 * SELECT/stop、坏包、反馈超时和主循环停顿仍会撤销动作许可。
 * 主控或TX故障时停车文字可能送不到驱动，人工测试的动力开关须可达。 */
#include "bench_control.h"
#include <math.h>
#include <string.h>

void rm_bench_init(rm_bench_t *b)
{
    memset(b, 0, sizeof *b);
#if RM_FIELD_DRIVE
    /* 用户于2026-11-05逐轮确认了M1右后向前、M2左后向后、M3右前向后、M4左前向前。
     * 行走构建保存这组实测方向；启动仍为禁动，必须回中并新按START。 */
    const int8_t direction[RM_WHEELS] = {1, -1, -1, 1};
    memcpy(b->direction, direction, sizeof b->direction);
#endif
}

static void reset_motion(rm_bench_t *b, rm_bench_state_t state)
{
    b->state = state;
    b->pwm = 0;
    b->started = false;
    b->neutral_tracking = false;
    b->release_seen = false;
    b->previous_start = true; /* 停车后持续按住START不能重新解锁。 */
    b->throttle_release_required = false;
    b->ramp = (rm_twist_t){0};
}

bool rm_bench_begin(rm_bench_t *b)
{
    if (!RM_BENCH_TEST || b->state != RM_BENCH_OFF) return false;
    rm_bench_init(b);
    reset_motion(b, RM_BENCH_IDLE);
    return true;
}

void rm_bench_stop(rm_bench_t *b)
{
    reset_motion(b, b->state == RM_BENCH_OFF ? RM_BENCH_OFF : RM_BENCH_IDLE);
}

void rm_bench_note_console_stop(rm_bench_t *b)
{
    if (b->state != RM_BENCH_OFF) b->stop_checks |= RM_BENCH_STOP_SEEN;
}

bool rm_bench_active(const rm_bench_t *b)
{
    return b->state >= RM_BENCH_WHEEL_READY;
}

bool rm_bench_set_direction(rm_bench_t *b, unsigned channel, int direction)
{
    if (!RM_BENCH_TEST || b->state == RM_BENCH_OFF || channel >= RM_WHEELS ||
        (direction != 1 && direction != -1)) return false;
    rm_bench_stop(b);
    b->direction[channel] = (int8_t)direction;
    return true;
}

static uint32_t health(const rm_config_t *c, const rm_safety_input_t *in, uint32_t now)
{
    uint32_t reason = rm_safety_health(c, in, now);
    if (!isfinite(c->deadzone) || c->deadzone < 0.0f || c->deadzone >= 1.0f ||
        !c->remote_timeout_ms || c->remote_timeout_ms > 1000u ||
        !c->motor_timeout_ms || c->motor_timeout_ms > 1000u ||
        !c->neutral_hold_ms || c->neutral_hold_ms > 5000u) reason |= RM_INHIBIT_CONFIG;
    if (!RM_FIELD_DRIVE && (in->remote.buttons & RM_PS2_L2)) reason |= RM_INHIBIT_ARM_MODE;
    if (RM_FIELD_DRIVE && (!isfinite(in->remote.right_y) || fabsf(in->remote.right_y) > 1.0f))
        reason |= RM_INHIBIT_REMOTE;
    return reason;
}

static bool centered(const rm_config_t *c, const rm_remote_t *r)
{
    return fabsf(r->forward) <= c->deadzone && fabsf(r->left) <= c->deadzone &&
        fabsf(r->yaw) <= c->deadzone && (!RM_FIELD_DRIVE || fabsf(r->right_y) <= c->deadzone);
}

static bool ready(const rm_bench_t *b, const rm_config_t *c,
                  const rm_safety_input_t *in, uint32_t now)
{
    return !health(c, in, now) && centered(c, &in->remote) && !in->remote.arm &&
        !in->remote.stop && !(in->remote.buttons & RM_PS2_R2) &&
        b->stop_checks == (RM_BENCH_SELECT_SEEN | RM_BENCH_STOP_SEEN) &&
        b->neutral_tracking && (uint32_t)(now-b->neutral_since) >= c->neutral_hold_ms;
}

bool rm_bench_arm(rm_bench_t *b, const rm_config_t *c,
                  const rm_safety_input_t *in, uint32_t now)
{
    if (!RM_BENCH_TEST || b->state != RM_BENCH_IDLE || !ready(b, c, in, now)) return false;
    b->state = RM_BENCH_WHEEL_READY;
    b->armed_at = now;
    return true;
}

bool rm_bench_pulse(rm_bench_t *b, const rm_config_t *c, const rm_safety_input_t *in,
                    uint32_t now, unsigned channel, int pwm, unsigned duration_ms)
{
    if (!RM_BENCH_TEST || b->state != RM_BENCH_WHEEL_READY || !ready(b, c, in, now) ||
        (uint32_t)(now-b->armed_at) >= RM_BENCH_ARM_MS || channel >= RM_WHEELS ||
        !pwm || pwm < -RM_BENCH_MAX_PWM || pwm > RM_BENCH_MAX_PWM ||
        duration_ms < 20u || duration_ms > RM_BENCH_PULSE_MS) return false;
    b->state = RM_BENCH_WHEEL_PULSE;
    b->channel = (uint8_t)channel;
    b->pwm = (int16_t)pwm;
    b->duration_ms = (uint16_t)duration_ms;
    b->pulse_at = now;
    b->started = false;
    return true;
}

/* 方向由现场逐轮记录。这里只做转换和合法性检查，绝不猜电机正负号。 */
static bool drive_config(const rm_bench_t *b, const rm_config_t *c,
                         const rm_driver_info_t *driver, rm_config_t *limited)
{
    if (driver->fields != RM_DRIVER_ALL_FIELDS || !driver->motor_type ||
        !driver->pulse_line || !driver->pulse_phase || !rm_driver_diameter_matches(driver, c)) return false;
    /* 专用行走版只接受这台车已回读的编码器配置，更换电机/驱动后先重新核对。 */
    if (RM_FIELD_DRIVE && (driver->motor_type != 1u || driver->pulse_line != 11u ||
                          driver->pulse_phase != 30u)) return false;
    *limited = *c;
    for (unsigned i = 0; i < RM_WHEELS; ++i) {
        if (c->channel[i] >= RM_WHEELS) return false;
        limited->direction[i] = b->direction[c->channel[i]];
    }
    /* 这些是本次人为设置的软件测试上限，和电机的铭牌/实测性能分别记录。 */
    limited->max_v_m_s = 0.05f;
    limited->max_w_rad_s = 0.2f;
    limited->wheel_limit_mm_s = 80.0f;
    limited->accel_m_s2 = 0.15f;
    limited->accel_rad_s2 = 0.6f;
    int32_t zero[RM_WHEELS] = {0}, mapped[RM_WHEELS];
    return rm_mecanum_mm_s(limited, (rm_twist_t){0}, mapped) &&
        rm_motor_map_command(limited, zero, mapped);
}

bool rm_bench_drive(rm_bench_t *b, const rm_config_t *c, const rm_driver_info_t *driver,
                    const rm_safety_input_t *in, uint32_t now)
{
    rm_config_t limited;
    if (!RM_BENCH_TEST || b->state != RM_BENCH_IDLE || !ready(b, c, in, now) ||
        !drive_config(b, c, driver, &limited)) return false;
    reset_motion(b, RM_BENCH_DRIVE_READY);
    return true;
}

bool rm_bench_step(rm_bench_t *b, const rm_config_t *c, const rm_driver_info_t *driver,
                   const rm_safety_input_t *in, uint32_t now, uint32_t elapsed,
                   int32_t channel[RM_WHEELS], bool *speed)
{
    memset(channel, 0, sizeof(int32_t)*RM_WHEELS);
    *speed = false;
    if (!RM_BENCH_TEST || b->state == RM_BENCH_OFF) return false;
    b->inhibit = health(c, in, now);
    if (b->inhibit) { rm_bench_stop(b); return true; }
    if (in->remote.stop) {
        b->stop_checks |= RM_BENCH_SELECT_SEEN;
        rm_bench_stop(b);
        return true;
    }
    if ((unsigned)b->state > RM_BENCH_DRIVE_ACTIVE) {
        b->inhibit = RM_BENCH_SETUP;
        rm_bench_stop(b);
        return true;
    }
    bool neutral = centered(c, &in->remote);
    bool run = (in->remote.buttons & RM_PS2_R2) != 0;
    bool rising = in->remote.arm && !b->previous_start;
    b->previous_start = in->remote.arm;
    if (!in->remote.arm) b->release_seen = true;
    if (!neutral || run) b->neutral_tracking = false;
    else if (!b->neutral_tracking) {
        b->neutral_tracking = true;
        b->neutral_since = now;
    }
    if (b->state == RM_BENCH_IDLE && !RM_FIELD_DRIVE) return true;
    if (b->state == RM_BENCH_WHEEL_READY || b->state == RM_BENCH_WHEEL_PULSE) {
        /* 推摇杆、按START/R2都取消单轮标定，避免误把两种操作叠在一起。 */
        if (!neutral || run || in->remote.arm ||
            (b->state == RM_BENCH_WHEEL_READY && (uint32_t)(now-b->armed_at) >= RM_BENCH_ARM_MS)) {
            rm_bench_stop(b);
            return true;
        }
        if (b->state == RM_BENCH_WHEEL_READY) return true;
        if (b->channel >= RM_WHEELS || !b->pwm || b->pwm < -RM_BENCH_MAX_PWM ||
            b->pwm > RM_BENCH_MAX_PWM || b->duration_ms < 20u ||
            b->duration_ms > RM_BENCH_PULSE_MS ||
            (uint32_t)(now-b->pulse_at) >= (b->started ? b->duration_ms : 100u)) {
            rm_bench_stop(b);
            return true;
        }
        if (!b->started) { b->started = true; b->pulse_at = now; }
        channel[b->channel] = b->pwm;
        return true;
    }
    rm_config_t limited;
    if (!drive_config(b, c, driver, &limited)) {
        b->inhibit = RM_BENCH_SETUP;
        rm_bench_stop(b);
        return true;
    }
    if (b->state == RM_BENCH_DRIVE_READY || b->state == RM_BENCH_IDLE) {
        if (neutral && !run && rising && b->release_seen && b->neutral_tracking &&
            (!RM_FIELD_DRIVE || !(in->remote.buttons & RM_PS2_L2)) &&
            (uint32_t)(now-b->neutral_since) >= c->neutral_hold_ms) {
            b->state = RM_BENCH_DRIVE_ACTIVE;
            b->armed_at = now;
            b->release_seen = false;
        }
        return true;
    }
    /* 行走版的START是启用/停用开关。长按只算一次，停用本轮就输出PWM0。
     * R2只负责给油，松开就停车，保留启用状态，之后可再次按R2继续走。 */
    if (RM_FIELD_DRIVE && rising) {
        reset_motion(b, RM_BENCH_DRIVE_READY);
        return true;
    }
    if (RM_FIELD_DRIVE && b->throttle_release_required) {
        if (!run) b->throttle_release_required = false;
        b->ramp = (rm_twist_t){0};
        return true;
    }
    if (RM_FIELD_DRIVE && (in->remote.buttons & RM_PS2_L2)) {
        b->ramp = (rm_twist_t){0};
        return true; /* 机构模式只停止行走，整车启用状态继续保留。 */
    }
    /* 单轮联调版沿用1秒窗口和首次5秒期限，行走版由START保持启用。 */
    if (!RM_FIELD_DRIVE && ((b->started && (!run ||
            (uint32_t)(now-b->pulse_at) >= RM_BENCH_DRIVE_MS)) ||
            (!b->started && (uint32_t)(now-b->armed_at) >= RM_BENCH_ARM_MS))) {
        reset_motion(b, RM_BENCH_DRIVE_READY);
        return true;
    }
    if (!run || neutral || (RM_FIELD_DRIVE && in->remote.arm)) {
        b->ramp = (rm_twist_t){0};
        return true;
    }
    if (!b->started) { b->started = true; b->pulse_at = now; }
    rm_twist_t target = {
        rm_deadzone(in->remote.forward, c->deadzone)*limited.max_v_m_s,
        rm_deadzone(in->remote.left, c->deadzone)*limited.max_v_m_s,
        rm_deadzone(in->remote.yaw, c->deadzone)*limited.max_w_rad_s
    };
    float norm = hypotf(target.vx, target.vy);
    if (norm > limited.max_v_m_s) {
        target.vx *= limited.max_v_m_s/norm;
        target.vy *= limited.max_v_m_s/norm;
    }
    b->ramp = rm_slew(b->ramp, target, &limited, (float)elapsed*0.001f);
    norm = hypotf(b->ramp.vx, b->ramp.vy);
    if (norm > limited.max_v_m_s) {
        b->ramp.vx *= limited.max_v_m_s/norm;
        b->ramp.vy *= limited.max_v_m_s/norm;
    }
    int32_t wheel[RM_WHEELS];
    if (!rm_mecanum_mm_s(&limited, b->ramp, wheel) || !rm_motor_map_command(&limited, wheel, channel)) {
        memset(channel, 0, sizeof(int32_t)*RM_WHEELS);
        b->inhibit = RM_BENCH_SETUP;
        rm_bench_stop(b);
        return true;
    }
    *speed = true;
    return true;
}
