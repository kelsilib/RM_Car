/* 标定只允许固定初试窗口，每条步进最多20us，以100us/s靠近目标。
 * 操作者确认是本次空载试验的声明，生产config中的verified标志继续保留原值。
 * stop/故障冻结最后已写入脉宽，承重机构必须另有支撑。 */
#include "servo_cal.h"
#include <string.h>

static bool profile_valid(const rm_servo_cal_t *c)
{
    return c && c->channel >= 1 && c->channel <= 6;
}

void rm_servo_cal_init(rm_servo_cal_t *c, unsigned channel, uint32_t now)
{
    memset(c, 0, sizeof *c);
    c->channel = channel >= 1 && channel <= 6 ? (uint8_t)channel : 0;
    c->last_step_ms = now;
}

void rm_servo_cal_stop(rm_servo_cal_t *c)
{
    c->armed = false;
    c->output_enabled = c->applied_enabled;
    c->pulse_us = c->applied_us;
    c->current_milli_us = c->target_milli_us = c->applied_milli_us;
}

void rm_servo_cal_fault(rm_servo_cal_t *c)
{
    rm_servo_cal_stop(c);
    c->setup_acknowledged = false;
    c->fault = true;
}

bool rm_servo_cal_confirm(rm_servo_cal_t *c)
{
    if (!profile_valid(c) || c->fault || c->armed) return false;
    c->setup_acknowledged = true;
    return true;
}

bool rm_servo_cal_arm(rm_servo_cal_t *c, uint32_t now, bool hardware_ready)
{
    if (!profile_valid(c) || c->fault || !c->setup_acknowledged || !hardware_ready) return false;
    c->armed = true;
    c->armed_at = now;
    return true;
}

static bool permission(const rm_servo_cal_t *c, uint32_t now)
{
    return profile_valid(c) && c->setup_acknowledged && c->armed && !c->fault &&
        (uint32_t)(now-c->armed_at) < RM_SCAL_ARM_MS;
}

bool rm_servo_cal_center(rm_servo_cal_t *c, uint32_t now)
{
    if (!permission(c, now) || c->output_enabled || c->applied_enabled) return false;
    /* 第一次显式定位只用于已卸开舵盘/连杆的舵机。首次定位本身可能有较大转动。 */
    c->output_enabled = true;
    c->pulse_us = RM_SCAL_START_US;
    c->current_milli_us = c->target_milli_us = RM_SCAL_START_US*1000;
    return true;
}

bool rm_servo_cal_move(rm_servo_cal_t *c, uint32_t now, int delta_us)
{
    if (!permission(c, now) || !c->applied_enabled || !delta_us ||
        delta_us < -RM_SCAL_MAX_STEP_US || delta_us > RM_SCAL_MAX_STEP_US ||
        c->target_milli_us != c->applied_milli_us) return false;
    int32_t next = c->applied_milli_us+delta_us*1000;
    if (next < RM_SCAL_MIN_US*1000 || next > RM_SCAL_MAX_US*1000) return false;
    c->target_milli_us = next;
    return true;
}

bool rm_servo_cal_mark(rm_servo_cal_t *c, unsigned mark)
{
    if (!profile_valid(c) || c->fault || mark >= RM_SCAL_MARKS || !c->applied_enabled ||
        c->target_milli_us != c->applied_milli_us) return false;
    c->marked_us[mark] = c->applied_us;
    c->marks |= (uint8_t)(1u << mark);
    return true;
}

bool rm_servo_cal_direction(rm_servo_cal_t *c, int direction)
{
    if (!profile_valid(c) || c->fault || (direction != 1 && direction != -1)) return false;
    c->direction = (int8_t)direction;
    return true;
}

bool rm_servo_cal_result_ready(const rm_servo_cal_t *c)
{
    if (!profile_valid(c) || c->fault || (c->marks & 7u) != 7u ||
        (c->direction != 1 && c->direction != -1)) return false;
    uint16_t low = c->marked_us[RM_SCAL_MARK_MIN], high = c->marked_us[RM_SCAL_MARK_MAX];
    if (low < RM_SCAL_MIN_US || high > RM_SCAL_MAX_US || low >= high ||
        c->marked_us[RM_SCAL_MARK_START] < low || c->marked_us[RM_SCAL_MARK_START] > high) return false;
    if (c->channel == 1 || c->channel == 2 || c->channel == 6) {
        uint16_t a = c->marked_us[RM_SCAL_MARK_A], b = c->marked_us[RM_SCAL_MARK_B];
        if ((c->marks & 24u) != 24u || a < low || a > high || b < low || b > high || a == b) return false;
    }
    return true; /* 只检查记录完整性，实物验收和生产配置仍需人工复核。 */
}

void rm_servo_cal_step(rm_servo_cal_t *c, uint32_t now, bool link_ok)
{
    uint32_t elapsed = now-c->last_step_ms;
    c->last_step_ms = now;
    if (!link_ok || elapsed > 100u) { rm_servo_cal_fault(c); return; }
    if (!permission(c, now)) { rm_servo_cal_stop(c); return; }
    int32_t step = (int32_t)elapsed*RM_SCAL_RATE_US_S;
    if (c->current_milli_us < c->target_milli_us) {
        c->current_milli_us += step;
        if (c->current_milli_us > c->target_milli_us) c->current_milli_us = c->target_milli_us;
    } else if (c->current_milli_us > c->target_milli_us) {
        c->current_milli_us -= step;
        if (c->current_milli_us < c->target_milli_us) c->current_milli_us = c->target_milli_us;
    }
    if (c->output_enabled) c->pulse_us = (uint16_t)((c->current_milli_us+500)/1000);
}

void rm_servo_cal_commit(rm_servo_cal_t *c)
{
    c->applied_enabled = c->output_enabled;
    c->applied_us = c->pulse_us;
    c->applied_milli_us = c->current_milli_us;
}
