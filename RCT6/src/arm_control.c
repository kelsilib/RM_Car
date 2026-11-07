/** 七路逐路控制：上电无输出，显式许可后才给所选舵机第一条脉冲。
 * 松开使能、失联、SELECT或错误时冻结最后实际脉宽，不自动回中、不自动扫限位。
 * 这是开环位置舵机接口：保持脉宽不等于测得关节位置，也不能保证断电不坠落。 */
#include "arm_control.h"
#include "mecanum.h"
#include <math.h>
#include <string.h>

static bool servo_values_valid(const rm_arm_config_t *c, unsigned i)
{
    if (!c || i >= RM_SERVOS) return false;
    const rm_servo_config_t *s = &c->servo[i];
    return s->limits_verified && s->start_pose_verified && s->min_us > 0 &&
        s->min_us < s->max_us && s->max_us < c->period_us &&
        s->start_us >= s->min_us && s->start_us <= s->max_us &&
        s->max_rate_us_s > 0 && s->max_rate_us_s <= 10000 &&
        (s->direction == 1 || s->direction == -1);
}

uint32_t rm_arm_config_issues(const rm_arm_config_t *c)
{
    if (!c) return 0xffu;
    uint32_t reason = 0;
    if (!c->enabled) reason |= RM_ARM_CFG_DISABLED;
    /* 这里是16位定时器按1us计数的实现范围，不是MG995厂家允许的频率范围。 */
    if (c->period_us < 1000 || c->period_us > 65535) reason |= RM_ARM_CFG_PERIOD;
    if (!c->wiring_verified) reason |= RM_ARM_CFG_WIRING;
    if (!c->power_verified) reason |= RM_ARM_CFG_POWER;
    if (!c->base_safe_verified) reason |= RM_ARM_CFG_BASE;
    if (!c->fault_hold_verified) reason |= RM_ARM_CFG_HOLD;
    unsigned enabled = 0;
    for (unsigned i = 0; i < RM_SERVOS; ++i) {
        if (!c->servo[i].enabled) continue;
        ++enabled;
        if (!servo_values_valid(c, i)) reason |= RM_ARM_CFG_LIMITS;
    }
    if (!enabled) reason |= RM_ARM_CFG_CHANNELS;
    return reason;
}

bool rm_arm_channel_ready(const rm_arm_config_t *c, unsigned channel)
{
    return c && channel < RM_SERVOS && c->servo[channel].enabled &&
        rm_arm_config_issues(c) == 0 && servo_values_valid(c, channel);
}

bool rm_arm_shovel_poses_ready(const rm_arm_config_t *c)
{
    if (!rm_arm_channel_ready(c, 1) || !c->shovel_poses_verified) return false;
    const rm_servo_config_t *s = &c->servo[1];
    return c->shovel_scoop_us >= s->min_us && c->shovel_scoop_us <= s->max_us &&
        c->shovel_store_us >= s->min_us && c->shovel_store_us <= s->max_us &&
        c->shovel_scoop_us != c->shovel_store_us;
}

const char *rm_arm_channel_name(unsigned channel)
{
    static const char *const names[RM_SERVOS] = {
        "lift", "shovel", "shoulder", "elbow", "wrist", "gripper", "reserved"
    };
    return channel < RM_SERVOS ? names[channel] : "invalid";
}

void rm_arm_init(rm_arm_t *a, const rm_arm_config_t *c, uint32_t now)
{
    memset(a, 0, sizeof *a);
    a->config = *c;
    a->last_step_ms = now;
}

void rm_arm_stop(rm_arm_t *a)
{
    a->armed = a->neutral_tracking = a->release_seen = false;
    a->automatic_mask = 0; /* 急停等入口取消正在执行的预设，恢复后不会接着跑旧动作。 */
    a->output_mask = a->applied_mask;
    memcpy(a->pulse_us, a->applied_us, sizeof a->pulse_us);
    memcpy(a->milli_us, a->applied_milli_us, sizeof a->milli_us);
    a->inhibit |= RM_ARM_INHIBIT_STOP;
}

void rm_arm_fault(rm_arm_t *a)
{
    rm_arm_stop(a);
    a->io_fault = true;
    a->inhibit |= RM_ARM_INHIBIT_IO;
}

void rm_arm_commit(rm_arm_t *a)
{
    a->applied_mask = a->output_mask;
    memcpy(a->applied_us, a->pulse_us, sizeof a->applied_us);
    memcpy(a->applied_milli_us, a->milli_us, sizeof a->applied_milli_us);
}

void rm_arm_step(rm_arm_t *a, const rm_config_t *shared, const rm_remote_t *r,
                 uint32_t now, bool estop, bool transport_ok, bool hardware_ready,
                 bool scheduler_ok)
{
    uint32_t elapsed = now - a->last_step_ms;
    a->last_step_ms = now;
    bool was_mode = a->mode;
    a->mode = (r->buttons & RM_PS2_L2) != 0;
    bool rising_start = r->arm && !a->previous_start;
    a->previous_start = r->arm;
    uint16_t edges = (uint16_t)(r->buttons & (uint16_t)~a->previous_buttons);
    a->previous_buttons = r->buttons;
    uint32_t reason = 0;
    bool shared_valid = isfinite(shared->deadzone) && shared->deadzone >= 0.0f &&
        shared->deadzone < 0.5f && shared->remote_timeout_ms > 0 &&
        shared->remote_timeout_ms <= 1000 && shared->neutral_hold_ms > 0 &&
        shared->neutral_hold_ms <= 5000;
    if (rm_arm_config_issues(&a->config) || !shared_valid ||
        !shared->ps2_wiring_verified || !shared->remote_failsafe_verified)
        reason |= RM_ARM_INHIBIT_CONFIG;
    bool axes_ok = isfinite(r->forward) && isfinite(r->left) && isfinite(r->yaw) &&
        isfinite(r->right_y) && fabsf(r->forward) <= 1.0f && fabsf(r->left) <= 1.0f &&
        fabsf(r->yaw) <= 1.0f && fabsf(r->right_y) <= 1.0f;
    if (!r->valid || !axes_ok || (uint32_t)(now-r->at_ms) > shared->remote_timeout_ms)
        reason |= RM_ARM_INHIBIT_REMOTE;
    if (!transport_ok) reason |= RM_ARM_INHIBIT_TRANSPORT;
    if (estop) reason |= RM_ARM_INHIBIT_ESTOP;
    if (!scheduler_ok || elapsed > 100u) reason |= RM_ARM_INHIBIT_SCHEDULER;
    if (!hardware_ready || a->io_fault) reason |= RM_ARM_INHIBIT_IO;
    if (!a->mode) reason |= RM_ARM_INHIBIT_MODE;
    if (r->stop) reason |= RM_ARM_INHIBIT_STOP;
    a->inhibit = reason;
    if (reason) {
        rm_arm_stop(a);
        a->inhibit = reason;
        return;
    }

    /* 刚进入L2模式时，已按住的选路键要先松开，底盘许可也需要重新申请。 */
    if (!was_mode) { rm_arm_stop(a); edges = 0; }
    /* 已启用时再按START就停用。先处理停用，按着R2或摇杆也能停止。 */
    if (a->armed && rising_start) {
        rm_arm_stop(a);
        return;
    }
    bool deadman = (r->buttons & RM_PS2_R2) != 0;
    const uint16_t selection_buttons = RM_PS2_LEFT | RM_PS2_RIGHT | RM_PS2_TRIANGLE |
        RM_PS2_CIRCLE | RM_PS2_SQUARE | RM_PS2_L1 | RM_PS2_R1 | RM_PS2_CROSS;
    unsigned select = edges & selection_buttons;
    /* 一次只接受一个选路键，多键同时按下时冻结，避免随机选中另一关节。 */
    if (!deadman && select && !(select & (select-1u)) &&
        (r->buttons & selection_buttons) == select) {
        if (select == RM_PS2_TRIANGLE) {
            a->selected = 0; /* △直接选中承载K210的S1升降台，再回中、新按START许可。 */
        } else if (select == RM_PS2_CIRCLE) {
            a->selected = 1; /* ○直接选中S2前部储矿铲斗；换路先撤销原来的动作许可。 */
        } else if (select == RM_PS2_SQUARE) {
            a->selected = 2; /* □选肩关节。 */
        } else if (select == RM_PS2_L1) {
            a->selected = 3; /* L1选肘关节。 */
        } else if (select == RM_PS2_R1) {
            a->selected = 4; /* R1选腕关节。 */
        } else if (select == RM_PS2_CROSS) {
            a->selected = 5; /* ×选夹爪。 */
        } else {
            a->selected = (uint8_t)((a->selected + (select == RM_PS2_RIGHT ? 1u : RM_SERVOS-1u)) % RM_SERVOS);
        }
        rm_arm_stop(a); /* 换关节后也要重新回中、新按START，不能带着旧许可切换。 */
        if (!rm_arm_channel_ready(&a->config, a->selected)) {
            a->inhibit = RM_ARM_INHIBIT_CHANNEL;
            return;
        }
    }
    if (!rm_arm_channel_ready(&a->config, a->selected)) {
        rm_arm_stop(a);
        a->inhibit = RM_ARM_INHIBIT_CHANNEL;
        return; /* 未启用的路可以继续选走，但不能输出或申请许可。 */
    }
    bool centered = fabsf(r->forward) <= shared->deadzone && fabsf(r->left) <= shared->deadzone &&
        fabsf(r->yaw) <= shared->deadzone && fabsf(r->right_y) <= shared->deadzone &&
        !deadman && !(r->buttons & (selection_buttons | RM_PS2_UP | RM_PS2_DOWN));
    if (!a->armed) {
        if (!centered) a->neutral_tracking = false;
        else if (!a->neutral_tracking) { a->neutral_since = now; a->neutral_tracking = true; }
        if (!r->arm) a->release_seen = true;
        if (centered && a->neutral_tracking && a->release_seen && rising_start &&
            (uint32_t)(now-a->neutral_since) >= shared->neutral_hold_ms) {
            a->armed = true;
            a->release_seen = false;
            unsigned i = a->selected;
            uint8_t bit = (uint8_t)(1u << i);
            if (!(a->output_mask & bit)) {
                /* 只有首次显式许可该路，才用已核实的起始脉宽；其他路仍不输出。 */
                a->pulse_us[i] = a->config.servo[i].start_us;
                a->milli_us[i] = (int32_t)a->pulse_us[i] * 1000;
                a->output_mask |= bit;
            }
        }
        a->inhibit = 0;
        return;
    }
    a->inhibit = 0;
    if (!deadman || !elapsed || r->arm) return; /* 松R2只停止调整，保留当前位置和当前路许可。 */
    /* 两个选路键一起按、或者推了底盘摇杆时冻结，不把多种操作混成关节动作。 */
    if ((r->buttons & selection_buttons) ||
        fabsf(r->forward) > shared->deadzone || fabsf(r->left) > shared->deadzone ||
        fabsf(r->yaw) > shared->deadzone) return;
    unsigned i = a->selected;
    const rm_servo_config_t *s = &a->config.servo[i];
    int32_t axis = (int32_t)lroundf(rm_deadzone(r->right_y, shared->deadzone)*1000.0f);
    unsigned jog = r->buttons & (RM_PS2_UP | RM_PS2_DOWN);
    int32_t pose_milli_us = 0;
    if (jog) {
        /* 摇杆和按钮不能争抢同一路。按钮松开即停，不保存排队的定位动作。 */
        if (axis || jog == (RM_PS2_UP | RM_PS2_DOWN)) return;
        if (i == 1) {
            if (!rm_arm_shovel_poses_ready(&a->config)) {
                a->inhibit = RM_ARM_INHIBIT_POSE;
                return;
            }
            pose_milli_us = (int32_t)(jog == RM_PS2_UP ? a->config.shovel_scoop_us :
                                     a->config.shovel_store_us) * 1000;
            if (pose_milli_us > a->milli_us[i]) axis = 1000;
            else if (pose_milli_us < a->milli_us[i]) axis = -1000;
            else axis = 0;
            /* 预设位置已经是脉宽，后面的direction不能再次翻转它。 */
            axis *= s->direction;
        } else {
            axis = jog == RM_PS2_UP ? 1000 : -1000;
        }
    }
    int64_t next = (int64_t)a->milli_us[i] +
        (int64_t)s->max_rate_us_s * elapsed * axis * s->direction / 1000;
    if (pose_milli_us && ((a->milli_us[i] <= pose_milli_us && next > pose_milli_us) ||
                         (a->milli_us[i] >= pose_milli_us && next < pose_milli_us))) {
        next = pose_milli_us; /* 到了铲矿/储矿位就停止，不越过标定点。 */
    }
    int32_t low = (int32_t)s->min_us * 1000, high = (int32_t)s->max_us * 1000;
    if (next < low) next = low;
    if (next > high) next = high;
    a->milli_us[i] = (int32_t)next;
    a->pulse_us[i] = (uint16_t)((next+500)/1000);
}
