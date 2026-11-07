/* 今晚的人工联调入口。常规固件默认不开放，实测方向只保存在本次上电的内存里。 */
#ifndef RM_BENCH_CONTROL_H
#define RM_BENCH_CONTROL_H
#include "mecanum.h"
#include "motor_uart.h"
#include "safety.h"

#ifndef RM_BENCH_TEST
#define RM_BENCH_TEST 0
#endif
#ifndef RM_FIELD_DRIVE
#define RM_FIELD_DRIVE 0
#endif
#if (RM_FIELD_DRIVE != 0 && RM_FIELD_DRIVE != 1) || (RM_FIELD_DRIVE && !RM_BENCH_TEST)
#error "RM_FIELD_DRIVE requires RM_BENCH_TEST=1 and must be 0 or 1"
#endif
#if RM_BENCH_TEST != 0 && RM_BENCH_TEST != 1
#error "RM_BENCH_TEST must be 0 or 1"
#endif

enum {
    RM_BENCH_MAX_PWM = 100, RM_BENCH_PULSE_MS = 200,
    RM_BENCH_DRIVE_MS = 1000, RM_BENCH_ARM_MS = 5000,
    RM_BENCH_SELECT_SEEN = 1u, RM_BENCH_STOP_SEEN = 2u,
    RM_BENCH_SETUP = 1u << 8, RM_BENCH_ARM_IO = 1u << 9
};
typedef enum {
    RM_BENCH_OFF, RM_BENCH_IDLE, RM_BENCH_WHEEL_READY,
    RM_BENCH_WHEEL_PULSE, RM_BENCH_DRIVE_READY, RM_BENCH_DRIVE_ACTIVE
} rm_bench_state_t;
typedef struct {
    rm_bench_state_t state;
    int8_t direction[RM_WHEELS]; /* 按驱动M1～M4记录，0表示该路还没确认。 */
    uint8_t stop_checks, channel;
    int16_t pwm;
    uint16_t duration_ms;
    uint32_t inhibit, neutral_since, armed_at, pulse_at;
    bool neutral_tracking, release_seen, previous_start, started;
    bool throttle_release_required; /* 机构动作后先松R2，避免一直按着油门就自动起步。 */
    rm_twist_t ramp;
} rm_bench_t;

void rm_bench_init(rm_bench_t *b);
bool rm_bench_begin(rm_bench_t *b);
/* 停车保留本次记录的方向，撤销所有动作许可。只有off/重启会清除方向。 */
void rm_bench_stop(rm_bench_t *b);
void rm_bench_note_console_stop(rm_bench_t *b);
bool rm_bench_active(const rm_bench_t *b);
bool rm_bench_set_direction(rm_bench_t *b, unsigned channel, int direction);
bool rm_bench_arm(rm_bench_t *b, const rm_config_t *c,
                  const rm_safety_input_t *in, uint32_t now);
bool rm_bench_pulse(rm_bench_t *b, const rm_config_t *c, const rm_safety_input_t *in,
                    uint32_t now, unsigned channel, int pwm, unsigned duration_ms);
bool rm_bench_drive(rm_bench_t *b, const rm_config_t *c, const rm_driver_info_t *driver,
                    const rm_safety_input_t *in, uint32_t now);
/* true表示本轮由联调入口接管输出，包括停车。false表示入口未开启。 */
bool rm_bench_step(rm_bench_t *b, const rm_config_t *c, const rm_driver_info_t *driver,
                   const rm_safety_input_t *in, uint32_t now, uint32_t elapsed,
                   int32_t channel[RM_WHEELS], bool *speed);
#endif
