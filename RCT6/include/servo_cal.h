/* 单路空载标定入口，与比赛许可和生产验收分开。 */
#ifndef RM_SERVO_CAL_H
#define RM_SERVO_CAL_H
#include <stdbool.h>
#include <stdint.h>

#ifndef RM_SERVO_CAL_CHANNEL
#define RM_SERVO_CAL_CHANNEL 0
#endif
#if RM_SERVO_CAL_CHANNEL < 0 || RM_SERVO_CAL_CHANNEL > 6
#error "RM_SERVO_CAL_CHANNEL must be 0 or S1..S6"
#endif

enum { RM_SCAL_PERIOD_US=20000, RM_SCAL_MIN_US=1400, RM_SCAL_MAX_US=1600,
    RM_SCAL_START_US=1500, RM_SCAL_RATE_US_S=100, RM_SCAL_MAX_STEP_US=20,
    RM_SCAL_ARM_MS=30000 };
enum { RM_SCAL_MARK_MIN, RM_SCAL_MARK_MAX, RM_SCAL_MARK_START,
       RM_SCAL_MARK_A, RM_SCAL_MARK_B, RM_SCAL_MARKS };
typedef struct {
    uint8_t channel, marks;
    uint16_t pulse_us, applied_us, marked_us[RM_SCAL_MARKS];
    int32_t current_milli_us, applied_milli_us, target_milli_us;
    int8_t direction;
    uint32_t last_step_ms, armed_at;
    bool setup_acknowledged, armed, fault, output_enabled, applied_enabled;
} rm_servo_cal_t;

void rm_servo_cal_init(rm_servo_cal_t *c, unsigned channel, uint32_t now);
void rm_servo_cal_stop(rm_servo_cal_t *c);
void rm_servo_cal_fault(rm_servo_cal_t *c);
bool rm_servo_cal_confirm(rm_servo_cal_t *c);
bool rm_servo_cal_arm(rm_servo_cal_t *c, uint32_t now, bool hardware_ready);
bool rm_servo_cal_center(rm_servo_cal_t *c, uint32_t now);
bool rm_servo_cal_move(rm_servo_cal_t *c, uint32_t now, int delta_us);
bool rm_servo_cal_mark(rm_servo_cal_t *c, unsigned mark);
bool rm_servo_cal_direction(rm_servo_cal_t *c, int direction);
bool rm_servo_cal_result_ready(const rm_servo_cal_t *c);
void rm_servo_cal_step(rm_servo_cal_t *c, uint32_t now, bool link_ok);
void rm_servo_cal_commit(rm_servo_cal_t *c);
#endif
