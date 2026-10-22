/**
 * 单轮标定不使用未确认的轮径、轮序和速度单位，直接选择驱动M1～M4。
 * 仍要求手柄和编码器反馈正常、摇杆回中、START/SELECT松开、独立停车保护已确认。
 * cal arm只给5秒准备时间；cal pulse用掉这次机会，到时归零，下一次必须重新准备。
 */
#include "calibration.h"
#include <math.h>

void rm_cal_cancel(rm_calibration_t *cal)
{
    cal->state = RM_CAL_OFF;
    cal->neutral_tracking = false;
    cal->pwm = 0;
    cal->pulse_started = false;
}

void rm_cal_update(rm_calibration_t *cal, const rm_config_t *c, const rm_safety_input_t *in, uint32_t now)
{
    uint32_t reason = rm_safety_health(c, in, now);
    if (!rm_config_calibration_ready(c)) reason |= RM_INHIBIT_CONFIG;
    /* 即使有人在调试器里改坏正在执行的参数，也不允许越界选轮或继续超过新限值。 */
    if ((unsigned)cal->state > (unsigned)RM_CAL_PULSE ||
        (cal->state == RM_CAL_PULSE && (cal->channel >= RM_WHEELS || !cal->pwm ||
            cal->pwm < -(int)c->calibration_max_pwm || cal->pwm > (int)c->calibration_max_pwm ||
            cal->duration_ms < 20 || cal->duration_ms > c->calibration_max_ms)))
        reason |= RM_INHIBIT_CONFIG;
    /* 标定期间推摇杆或按START都退出，避免同时进入整车行驶模式。SELECT也会停车。 */
    if (in->remote.arm || fabsf(in->remote.forward) > c->deadzone ||
        fabsf(in->remote.left) > c->deadzone || fabsf(in->remote.yaw) > c->deadzone)
        reason |= RM_INHIBIT_REMOTE;
    if (in->remote.stop) reason |= RM_INHIBIT_ESTOP;
    cal->inhibit = reason;
    if (reason) { rm_cal_cancel(cal); return; }
    if (!cal->neutral_tracking) {
        cal->neutral_tracking = true;
        cal->neutral_since = now;
    }
    if ((cal->state == RM_CAL_READY && (uint32_t)(now - cal->armed_at) >= 5000u) ||
        (cal->state == RM_CAL_PULSE &&
            (uint32_t)(now - cal->pulse_at) >= (cal->pulse_started ? cal->duration_ms : 100u)))
        rm_cal_cancel(cal);
}

bool rm_cal_arm(rm_calibration_t *cal, const rm_config_t *c, const rm_safety_input_t *in, uint32_t now)
{
    rm_cal_update(cal, c, in, now);
    if (cal->state != RM_CAL_OFF || cal->inhibit || !cal->neutral_tracking ||
        (uint32_t)(now - cal->neutral_since) < c->neutral_hold_ms) return false;
    cal->state = RM_CAL_READY;
    cal->armed_at = now;
    return true;
}

bool rm_cal_pulse(rm_calibration_t *cal, const rm_config_t *c, const rm_safety_input_t *in,
                  uint32_t now, unsigned channel, int pwm, unsigned duration_ms)
{
    rm_cal_update(cal, c, in, now);
    if (cal->state != RM_CAL_READY || cal->inhibit || channel >= RM_WHEELS || !pwm ||
        pwm < -(int)c->calibration_max_pwm || pwm > (int)c->calibration_max_pwm ||
        duration_ms < 20 || duration_ms > c->calibration_max_ms) return false;
    cal->state = RM_CAL_PULSE;
    cal->channel = (uint8_t)channel;
    cal->pwm = (int16_t)pwm;
    cal->duration_ms = (uint16_t)duration_ms;
    cal->pulse_at = now;
    cal->pulse_started = false;
    return true;
}
