#include "speed_ctrl.h"

#include <math.h>

static float clampf(float x, float lo, float hi)
{
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

void wheel_ctrl_init(wheel_ctrl_t *w, const wheel_ctrl_cfg_t *cfg, float dt)
{
    pid_cfg_t pc = {
        .kp = cfg->kp,
        .ki = cfg->ki,
        .kd = cfg->kd,
        .out_min = cfg->out_min,
        .out_max = cfg->out_max,
        .i_min = cfg->i_min,
        .i_max = cfg->i_max,
        .d_alpha = cfg->d_alpha,
    };
    pid_init(&w->pid, &pc);

    slew_init(&w->sp_slew, cfg->max_accel);
    lpf_init(&w->speed_lpf, cfg->lpf_hz, dt);

    w->ff_gain = cfg->ff_gain;
    w->ff_offset = cfg->ff_offset;
    w->deadzone = cfg->deadzone;
    w->deadzone_min_cmd = cfg->deadzone_min_cmd;

    w->target = 0.0f;
    w->setpoint = 0.0f;
    w->measure = 0.0f;
    w->duty = 0.0f;
}

void wheel_ctrl_set_gains(wheel_ctrl_t *w, float kp, float ki, float kd)
{
    pid_set_gains(&w->pid, kp, ki, kd);
}

void wheel_ctrl_reset(wheel_ctrl_t *w)
{
    pid_reset(&w->pid);
    w->sp_slew.init = false;
    w->sp_slew.y = 0.0f;
    w->speed_lpf.init = false;
    w->target = 0.0f;
    w->setpoint = 0.0f;
    w->measure = 0.0f;
    w->duty = 0.0f;
}

void wheel_ctrl_stop(wheel_ctrl_t *w)
{
    w->target = 0.0f;
    w->setpoint = 0.0f;
    w->duty = 0.0f;
    pid_reset(&w->pid);
}

/**
 * 死区补偿：直流减速电机在低占空比下克服不了静摩擦。
 * 把 PID 输出幅值从 [min_cmd, 1] 线性映射到 [deadzone, 1]。
 */
static float apply_deadzone(float duty, float deadzone, float min_cmd)
{
    const float mag = fabsf(duty);
    if (deadzone <= 0.0f || mag < min_cmd) {
        return (mag < min_cmd) ? 0.0f : duty;
    }
    const float dz = clampf(deadzone, 0.0f, 0.9f);
    const float out = dz + mag * (1.0f - dz);
    return (duty > 0.0f) ? out : -out;
}

float wheel_ctrl_step(wheel_ctrl_t *w, float target_mps, float measured_mps, float dt)
{
    w->target = target_mps;
    w->measure = lpf_update(&w->speed_lpf, measured_mps);
    w->setpoint = slew_update(&w->sp_slew, target_mps, dt);

    /* 目标为 0 且已经基本停住：直接切断，避免 PID 在零附近抖 */
    if (fabsf(target_mps) < 1.0e-3f && fabsf(w->measure) < 0.03f) {
        pid_reset(&w->pid);
        w->duty = 0.0f;
        return w->duty;
    }

    float duty = pid_update(&w->pid, w->setpoint, w->measure, dt);

    /* 前馈：补偿一部分粘滞阻尼和库仑摩擦 */
    const float dir = (w->setpoint > 0.05f) ? 1.0f : ((w->setpoint < -0.05f) ? -1.0f : 0.0f);
    duty += w->ff_gain * w->setpoint + w->ff_offset * dir;

    duty = clampf(duty, w->pid.out_min, w->pid.out_max);
    duty = apply_deadzone(duty, w->deadzone, w->deadzone_min_cmd);

    w->duty = clampf(duty, -1.0f, 1.0f);
    return w->duty;
}
