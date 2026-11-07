/* 比赛机构操作。整车只用一次START启用，常用机构各用一个键切换姿态。
 * 所有目标仍经过逐路限位、限速和验收检查，单键动作不使用阻塞延时。
 * 这里保留的是目标脉宽，MG995没有回传实际角度。 */
#include "arm_control.h"
#include "mecanum.h"
#include <math.h>

bool rm_arm_toggle_poses(const rm_arm_config_t *c, unsigned channel,
                         uint16_t *pose_a_us, uint16_t *pose_b_us)
{
    if (!pose_a_us || !pose_b_us || !rm_arm_channel_ready(c, channel)) return false;
    if (channel == 1) {
        if (!rm_arm_shovel_poses_ready(c)) return false;
        *pose_a_us = c->shovel_scoop_us;
        *pose_b_us = c->shovel_store_us;
    } else if (channel == 0 || channel == 5) {
        const rm_servo_config_t *servo = &c->servo[channel];
        if (!servo->toggle_poses_verified || servo->pose_a_us < servo->min_us ||
            servo->pose_a_us > servo->max_us || servo->pose_b_us < servo->min_us ||
            servo->pose_b_us > servo->max_us || servo->pose_a_us == servo->pose_b_us) return false;
        *pose_a_us = servo->pose_a_us;
        *pose_b_us = servo->pose_b_us;
    } else return false;
    return true;
}

static uint32_t match_health(const rm_arm_t *a, const rm_config_t *shared,
                              const rm_remote_t *remote, uint32_t now, uint32_t elapsed,
                              bool estop, bool transport_ok, bool hardware_ready,
                              bool scheduler_ok)
{
    uint32_t reason = 0;
    if (rm_arm_config_issues(&a->config) || !shared->ps2_wiring_verified ||
        !shared->remote_failsafe_verified || !isfinite(shared->deadzone) ||
        shared->deadzone < 0.0f || shared->deadzone >= 0.5f ||
        !shared->remote_timeout_ms || shared->remote_timeout_ms > 1000)
        reason |= RM_ARM_INHIBIT_CONFIG;
    if (!remote->valid || !isfinite(remote->forward) || !isfinite(remote->left) ||
        !isfinite(remote->yaw) || !isfinite(remote->right_y) ||
        fabsf(remote->forward) > 1.0f || fabsf(remote->left) > 1.0f ||
        fabsf(remote->yaw) > 1.0f || fabsf(remote->right_y) > 1.0f ||
        (uint32_t)(now-remote->at_ms) > shared->remote_timeout_ms)
        reason |= RM_ARM_INHIBIT_REMOTE;
    if (estop) reason |= RM_ARM_INHIBIT_ESTOP;
    if (!transport_ok) reason |= RM_ARM_INHIBIT_TRANSPORT;
    if (!scheduler_ok || elapsed > 100u) reason |= RM_ARM_INHIBIT_SCHEDULER;
    if (!hardware_ready || a->io_fault) reason |= RM_ARM_INHIBIT_IO;
    if (remote->stop) reason |= RM_ARM_INHIBIT_STOP;
    return reason;
}

static void start_channel(rm_arm_t *a, unsigned channel)
{
    uint8_t bit = (uint8_t)(1u << channel);
    if (a->output_mask & bit) return;
    /* 只有明确的机构动作请求才初始化这一路，整车START不会让六台一起定位。 */
    a->pulse_us[channel] = a->config.servo[channel].start_us;
    a->milli_us[channel] = (int32_t)a->pulse_us[channel] * 1000;
    a->output_mask |= bit;
}

static void write_target(rm_arm_t *a, unsigned channel, int64_t next)
{
    const rm_servo_config_t *servo = &a->config.servo[channel];
    int32_t low = (int32_t)servo->min_us * 1000;
    int32_t high = (int32_t)servo->max_us * 1000;
    if (next < low) next = low;
    if (next > high) next = high;
    a->milli_us[channel] = (int32_t)next;
    a->pulse_us[channel] = (uint16_t)((next+500)/1000);
}

void rm_arm_match_step(rm_arm_t *a, const rm_config_t *shared, const rm_remote_t *r,
                       uint32_t now, bool enabled, bool estop, bool transport_ok,
                       bool hardware_ready, bool scheduler_ok)
{
    uint32_t elapsed = now-a->last_step_ms;
    a->last_step_ms = now;
    uint16_t edges = (uint16_t)(r->buttons & (uint16_t)~a->previous_buttons);
    a->previous_buttons = r->buttons;
    a->previous_start = r->arm;
    a->mode = (r->buttons & RM_PS2_L2) != 0;
    uint32_t reason = match_health(a, shared, r, now, elapsed, estop, transport_ok,
                                   hardware_ready, scheduler_ok);
    if (!enabled) reason |= RM_ARM_INHIBIT_STOP;
    if (reason) {
        rm_arm_stop(a);
        a->inhibit = reason;
        return;
    }
    a->armed = true;
    a->inhibit = 0;
    bool base_centered = fabsf(r->forward) <= shared->deadzone &&
        fabsf(r->left) <= shared->deadzone && fabsf(r->yaw) <= shared->deadzone;
    bool run = (r->buttons & RM_PS2_R2) != 0;
    if (run || !base_centered || r->arm) {
        /* 给油、拨动底盘摇杆或按START时，撤销机构动作，保留已写入位置。 */
        rm_arm_stop(a);
        return;
    }

    if (a->mode) {
        a->automatic_mask = 0; /* 进入关节手动模式时，先停止正在执行的预设。 */
        const uint16_t selectors = RM_PS2_SQUARE | RM_PS2_L1 | RM_PS2_R1 |
            RM_PS2_LEFT | RM_PS2_RIGHT;
        uint16_t selection = (uint16_t)(r->buttons & selectors);
        if (a->selected < 2 || a->selected > 4) a->selected = 2;
        if (selection) {
            if (selection & (selection-1u)) return;
            if (edges & selection) {
                if (selection == RM_PS2_SQUARE) a->selected = 2;
                else if (selection == RM_PS2_L1) a->selected = 3;
                else if (selection == RM_PS2_R1) a->selected = 4;
                else if (selection == RM_PS2_RIGHT) a->selected = a->selected == 4 ? 2 : (uint8_t)(a->selected+1);
                else a->selected = a->selected == 2 ? 4 : (uint8_t)(a->selected-1);
            }
            return; /* 换关节这轮先停，松开选路键后即可操作，无需另按START。 */
        }
        if (r->buttons & (RM_PS2_TRIANGLE | RM_PS2_CIRCLE | RM_PS2_CROSS)) return;
        int32_t axis = (int32_t)lroundf(rm_deadzone(r->right_y, shared->deadzone)*1000.0f);
        uint16_t jog = (uint16_t)(r->buttons & (RM_PS2_UP | RM_PS2_DOWN));
        if (jog) {
            if (axis || jog == (RM_PS2_UP | RM_PS2_DOWN)) return;
            axis = jog == RM_PS2_UP ? 1000 : -1000;
        }
        if (!axis || !elapsed) return;
        unsigned channel = a->selected;
        if (!rm_arm_channel_ready(&a->config, channel)) {
            a->inhibit = RM_ARM_INHIBIT_CHANNEL;
            return;
        }
        start_channel(a, channel);
        const rm_servo_config_t *servo = &a->config.servo[channel];
        int64_t next = (int64_t)a->milli_us[channel] +
            (int64_t)servo->max_rate_us_s * elapsed * axis * servo->direction / 1000;
        write_target(a, channel, next);
        return;
    }

    const uint16_t toggles = RM_PS2_TRIANGLE | RM_PS2_CIRCLE | RM_PS2_CROSS;
    uint16_t pressed = (uint16_t)(r->buttons & toggles);
    if ((edges & toggles) && pressed && !(pressed & (pressed-1u))) {
        if (fabsf(r->right_y) > shared->deadzone) return;
        unsigned channel = 5;
        if (pressed == RM_PS2_TRIANGLE) channel = 0;
        else if (pressed == RM_PS2_CIRCLE) channel = 1;
        uint16_t pose_a, pose_b;
        if (!rm_arm_toggle_poses(&a->config, channel, &pose_a, &pose_b)) {
            rm_arm_stop(a);
            a->inhibit = RM_ARM_INHIBIT_POSE;
            return;
        }
        /* 一次只运行一个预设，选另一个机构时先冻结前一路。 */
        a->selected = (uint8_t)channel;
        uint8_t bit = (uint8_t)(1u << channel);
        a->automatic_target_us[channel] = (a->next_pose_b_mask & bit) ? pose_b : pose_a;
        a->next_pose_b_mask ^= bit;
        a->automatic_mask = bit;
        start_channel(a, channel);
    } else if (pressed && (pressed & (pressed-1u))) {
        rm_arm_stop(a);
        return; /* 多个动作键同时按下时，停止预设，不能随机执行其中一个。 */
    }

    for (unsigned channel = 0; channel < RM_SERVOS; ++channel) {
        uint8_t bit = (uint8_t)(1u << channel);
        if (!(a->automatic_mask & bit)) continue;
        uint16_t pose_a, pose_b;
        if (!rm_arm_toggle_poses(&a->config, channel, &pose_a, &pose_b) ||
            (a->automatic_target_us[channel] != pose_a && a->automatic_target_us[channel] != pose_b)) {
            rm_arm_stop(a);
            a->inhibit = RM_ARM_INHIBIT_POSE;
            return;
        }
        int32_t target = (int32_t)a->automatic_target_us[channel]*1000;
        int64_t next = a->milli_us[channel];
        int64_t step = (int64_t)a->config.servo[channel].max_rate_us_s*elapsed;
        if (next < target) {
            next += step;
            if (next > target) next = target;
        } else if (next > target) {
            next -= step;
            if (next < target) next = target;
        }
        write_target(a, channel, next);
        if (next == target) a->automatic_mask &= (uint8_t)~bit;
    }
}
