/**
 * @file    pid.h
 * @brief   带抗积分饱和、微分先行、微分低通的通用 PID。
 *
 * 设计取舍（新手常踩的坑都在这）：
 *   - 微分作用在"测量值"而不是"误差"上：目标突变时不会打出微分尖峰；
 *   - 输出饱和时停止朝饱和方向积分，避免退饱和迟缓；
 *   - 积分项单独限幅，堵转几秒钟松手也不会"飞车"。
 */
#ifndef PID_H
#define PID_H

#include <stdbool.h>

typedef struct {
    float kp;
    float ki;
    float kd;

    float out_min;
    float out_max;

    float i_min;
    float i_max;

    /** 微分低通系数 0..1，1 表示不滤波。 */
    float d_alpha;

    /* 运行时状态 */
    float integ;
    float prev_meas;
    float d_state;
    float out;
    bool  started;
} pid_t;

typedef struct {
    float kp;
    float ki;
    float kd;
    float out_min;
    float out_max;
    float i_min;
    float i_max;
    float d_alpha;
} pid_cfg_t;

void  pid_init(pid_t *p, const pid_cfg_t *cfg);
void  pid_set_gains(pid_t *p, float kp, float ki, float kd);
void  pid_reset(pid_t *p);
float pid_update(pid_t *p, float setpoint, float measure, float dt);

/** 只算 P 和 I（某些场景不需要微分）。 */
float pid_update_pi(pid_t *p, float setpoint, float measure, float dt);

#endif /* PID_H */
