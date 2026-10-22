/**
 * @file    speed_ctrl.h
 * @brief   单轮速度闭环：目标线速度 -> 电机占空比。
 */
#ifndef SPEED_CTRL_H
#define SPEED_CTRL_H

#include <stdbool.h>

#include "filters.h"
#include "pid.h"

typedef struct {
    float kp, ki, kd;
    float i_min, i_max;
    float out_min, out_max;
    float d_alpha;

    float ff_gain;          /* 前馈：占空比 = ff_gain * 目标速度 */
    float ff_offset;        /* 前馈：占空比 += ff_offset * sign(目标) */

    float deadzone;         /* 0..1，电机最小有效占空比 */
    float deadzone_min_cmd; /* 小于该 PID 输出时不做补偿 */

    float max_accel;        /* m/s² */
    float lpf_hz;           /* 测速低通 */
} wheel_ctrl_cfg_t;

typedef struct {
    pid_t  pid;
    slew_t sp_slew;
    lpf_t  speed_lpf;

    float ff_gain;
    float ff_offset;
    float deadzone;
    float deadzone_min_cmd;

    float target;    /* 期望线速度 m/s */
    float setpoint;  /* 斜率限制后的目标 */
    float measure;   /* 滤波后的实测线速度 */
    float duty;      /* 输出 -1..1 */
} wheel_ctrl_t;

void  wheel_ctrl_init(wheel_ctrl_t *w, const wheel_ctrl_cfg_t *cfg, float dt);
float wheel_ctrl_step(wheel_ctrl_t *w, float target_mps, float measured_mps, float dt);
void  wheel_ctrl_set_gains(wheel_ctrl_t *w, float kp, float ki, float kd);
void  wheel_ctrl_reset(wheel_ctrl_t *w);
void  wheel_ctrl_stop(wheel_ctrl_t *w);

#endif /* SPEED_CTRL_H */
