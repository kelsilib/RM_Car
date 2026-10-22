/**
 * @file    filters.h
 * @brief   一阶低通、斜率限制、互补滤波姿态解算。
 *
 * 全部是纯 C、无动态内存，方便在 PC 上做单元测试。
 */
#ifndef FILTERS_H
#define FILTERS_H

#include <stdbool.h>
#include <stdint.h>

/** 一阶低通 y[k] = y[k-1] + a*(x - y[k-1])。 */
typedef struct {
    float alpha;
    float y;
    bool  init;
} lpf_t;

void  lpf_init(lpf_t *f, float cutoff_hz, float dt);
void  lpf_reset(lpf_t *f, float value);
float lpf_update(lpf_t *f, float x);

/** 一阶低通的 alpha 计算，dt 变化时调用。 */
float lpf_alpha(float cutoff_hz, float dt);

/** 斜率限制器：输出变化率不超过 max_rate（单位/秒）。 */
typedef struct {
    float max_rate;
    float y;
    bool  init;
} slew_t;

void  slew_init(slew_t *s, float max_rate);
void  slew_reset(slew_t *s, float value);
float slew_update(slew_t *s, float target, float dt);

/** 互补滤波姿态：陀螺积分 + 加速度计重力方向校正。 */
typedef struct {
    float roll;   /* rad */
    float pitch;  /* rad */
} attitude_t;

void  attitude_reset(attitude_t *a);
void  attitude_update(attitude_t *a, float ax, float ay, float az,
                      float gx, float gy, float dt);

/** 角度归一化到 [-pi, pi)。 */
float wrap_pi(float angle);

#endif /* FILTERS_H */
