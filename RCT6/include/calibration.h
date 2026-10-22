/* 单轮标定：先准备，再给一次短脉冲；不自动扫四轮，也不自动重复。 */
#ifndef RM_CALIBRATION_H
#define RM_CALIBRATION_H
#include "safety.h"
typedef enum { RM_CAL_OFF, RM_CAL_READY, RM_CAL_PULSE } rm_cal_state_t;
typedef struct {
    rm_cal_state_t state;
    uint32_t inhibit, neutral_since, armed_at, pulse_at;
    bool neutral_tracking, pulse_started;
    /* pulse_at请求时用于100ms排队截止；首次输出后改为真正的开始时间。 */
    uint16_t duration_ms;
    uint8_t channel; /* 驱动接口0～3，不按还未标定的车轮顺序换算 */
    int16_t pwm;
} rm_calibration_t;
void rm_cal_cancel(rm_calibration_t *cal);
void rm_cal_update(rm_calibration_t *cal, const rm_config_t *c, const rm_safety_input_t *in, uint32_t now);
bool rm_cal_arm(rm_calibration_t *cal, const rm_config_t *c, const rm_safety_input_t *in, uint32_t now);
bool rm_cal_pulse(rm_calibration_t *cal, const rm_config_t *c, const rm_safety_input_t *in,
                  uint32_t now, unsigned channel, int pwm, unsigned duration_ms);
#endif
