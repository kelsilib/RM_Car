#include "filters.h"

#include <math.h>

#define PI_F 3.14159265358979f

float lpf_alpha(float cutoff_hz, float dt)
{
    if (cutoff_hz <= 0.0f || dt <= 0.0f) {
        return 1.0f;
    }
    const float tau = 1.0f / (2.0f * PI_F * cutoff_hz);
    return dt / (tau + dt);
}

void lpf_init(lpf_t *f, float cutoff_hz, float dt)
{
    f->alpha = lpf_alpha(cutoff_hz, dt);
    f->y = 0.0f;
    f->init = false;
}

void lpf_reset(lpf_t *f, float value)
{
    f->y = value;
    f->init = true;
}

float lpf_update(lpf_t *f, float x)
{
    if (!f->init) {
        f->y = x;
        f->init = true;
        return f->y;
    }
    f->y += f->alpha * (x - f->y);
    return f->y;
}

void slew_init(slew_t *s, float max_rate)
{
    s->max_rate = max_rate;
    s->y = 0.0f;
    s->init = false;
}

void slew_reset(slew_t *s, float value)
{
    s->y = value;
    s->init = true;
}

float slew_update(slew_t *s, float target, float dt)
{
    if (!s->init) {
        s->y = target;
        s->init = true;
        return s->y;
    }
    if (s->max_rate <= 0.0f || dt <= 0.0f) {
        s->y = target;
        return s->y;
    }
    const float max_step = s->max_rate * dt;
    float delta = target - s->y;
    if (delta > max_step) {
        delta = max_step;
    } else if (delta < -max_step) {
        delta = -max_step;
    }
    s->y += delta;
    return s->y;
}

void attitude_reset(attitude_t *a)
{
    a->roll = 0.0f;
    a->pitch = 0.0f;
}

void attitude_update(attitude_t *a, float ax, float ay, float az,
                     float gx, float gy, float dt)
{
    /* 加速度计解算：静止时重力矢量指向上，可直接求倾角 */
    const float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm > 0.2f) {
        const float roll_acc  = atan2f(ay, az);
        const float pitch_acc = atan2f(-ax, sqrtf(ay * ay + az * az));

        const float k = 0.02f; /* 互补系数，兼顾动态与漂移 */
        a->roll  += gx * dt;
        a->pitch += gy * dt;
        a->roll  += k * wrap_pi(roll_acc - a->roll);
        a->pitch += k * wrap_pi(pitch_acc - a->pitch);
    } else {
        a->roll  += gx * dt;
        a->pitch += gy * dt;
    }
}

float wrap_pi(float angle)
{
    while (angle >= PI_F) {
        angle -= 2.0f * PI_F;
    }
    while (angle < -PI_F) {
        angle += 2.0f * PI_F;
    }
    return angle;
}
