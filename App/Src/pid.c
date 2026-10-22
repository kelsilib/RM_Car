#include "pid.h"

#include <stddef.h>

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

void pid_init(pid_t *p, const pid_cfg_t *cfg)
{
    if (p == NULL || cfg == NULL) {
        return;
    }
    p->kp = cfg->kp;
    p->ki = cfg->ki;
    p->kd = cfg->kd;
    p->out_min = cfg->out_min;
    p->out_max = cfg->out_max;
    p->i_min = cfg->i_min;
    p->i_max = cfg->i_max;
    p->d_alpha = clampf(cfg->d_alpha, 0.0f, 1.0f);
    pid_reset(p);
}

void pid_set_gains(pid_t *p, float kp, float ki, float kd)
{
    if (p == NULL) {
        return;
    }
    p->kp = kp;
    p->ki = ki;
    p->kd = kd;
}

void pid_reset(pid_t *p)
{
    if (p == NULL) {
        return;
    }
    p->integ = 0.0f;
    p->prev_meas = 0.0f;
    p->d_state = 0.0f;
    p->out = 0.0f;
    p->started = false;
}

static float pid_common(pid_t *p, float sp, float meas, float dt, bool use_d)
{
    if (dt <= 0.0f) {
        dt = 1.0e-3f;
    }

    const float err = sp - meas;
    const float p_term = p->kp * err;

    float d_term = 0.0f;
    if (use_d && p->kd != 0.0f) {
        if (p->started) {
            /* 微分先行：对测量值求导，再取负号 */
            const float d_raw = -(meas - p->prev_meas) / dt;
            p->d_state += p->d_alpha * (d_raw - p->d_state);
            d_term = p->kd * p->d_state;
        }
        /* 第一次不产生微分，避免上电冲击 */
    }
    p->prev_meas = meas;
    p->started = true;

    float integ_new = p->integ + p->ki * err * dt;
    integ_new = clampf(integ_new, p->i_min, p->i_max);

    float out = p_term + integ_new + d_term;

    /* 条件积分：饱和且误差继续推向饱和时，冻结积分 */
    if (out > p->out_max) {
        out = p->out_max;
        if (err < 0.0f) {
            p->integ = integ_new;
        }
    } else if (out < p->out_min) {
        out = p->out_min;
        if (err > 0.0f) {
            p->integ = integ_new;
        }
    } else {
        p->integ = integ_new;
    }

    out = clampf(out, p->out_min, p->out_max);
    p->out = out;
    return out;
}

float pid_update(pid_t *p, float setpoint, float measure, float dt)
{
    return pid_common(p, setpoint, measure, dt, true);
}

float pid_update_pi(pid_t *p, float setpoint, float measure, float dt)
{
    return pid_common(p, setpoint, measure, dt, false);
}
