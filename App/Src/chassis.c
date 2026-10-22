#include "chassis.h"

#include <math.h>

void chassis_ik(const chassis_vel_t *body, const chassis_geom_t *geom,
                float wheel_mps[WHEEL_COUNT])
{
    /* 麦轮旋转项的力臂 = 半轴距 + 半轮距 */
    const float k = geom->half_wheelbase + geom->half_track;

    wheel_mps[WHEEL_FL] = body->vx - body->vy - k * body->wz;
    wheel_mps[WHEEL_FR] = body->vx + body->vy + k * body->wz;
    wheel_mps[WHEEL_RL] = body->vx + body->vy - k * body->wz;
    wheel_mps[WHEEL_RR] = body->vx - body->vy + k * body->wz;
}

float chassis_ik_limited(const chassis_vel_t *body, const chassis_geom_t *geom,
                         float max_wheel_mps, float wheel_mps[WHEEL_COUNT])
{
    chassis_ik(body, geom, wheel_mps);

    float peak = 0.0f;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        const float a = fabsf(wheel_mps[i]);
        if (a > peak) {
            peak = a;
        }
    }

    if (peak <= max_wheel_mps || peak <= 1.0e-6f) {
        return 1.0f;
    }

    const float scale = max_wheel_mps / peak;
    for (int i = 0; i < WHEEL_COUNT; i++) {
        wheel_mps[i] *= scale;
    }
    return scale;
}

void chassis_odom(const float wheel_mps[WHEEL_COUNT], const chassis_geom_t *geom,
                  chassis_vel_t *body)
{
    const float k = geom->half_wheelbase + geom->half_track;

    body->vx = 0.25f * (wheel_mps[WHEEL_FL] + wheel_mps[WHEEL_FR] +
                        wheel_mps[WHEEL_RL] + wheel_mps[WHEEL_RR]);
    body->vy = 0.25f * (-wheel_mps[WHEEL_FL] + wheel_mps[WHEEL_FR] +
                        wheel_mps[WHEEL_RL] - wheel_mps[WHEEL_RR]);
    body->wz = 0.25f * (-wheel_mps[WHEEL_FL] + wheel_mps[WHEEL_FR] -
                        wheel_mps[WHEEL_RL] + wheel_mps[WHEEL_RR]) / k;
}

void chassis_limit_body_velocity(chassis_vel_t *v, float max_linear, float max_wz)
{
    const float linear = sqrtf(v->vx * v->vx + v->vy * v->vy);
    if (linear > max_linear && linear > 1.0e-6f) {
        const float s = max_linear / linear;
        v->vx *= s;
        v->vy *= s;
    }
    if (v->wz > max_wz) {
        v->wz = max_wz;
    } else if (v->wz < -max_wz) {
        v->wz = -max_wz;
    }
}
