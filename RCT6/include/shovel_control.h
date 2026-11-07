/* S2按住动作、松开撤销PWM。只有S2专用构建使用，整车许可仍分别管理。 */
#ifndef RM_SHOVEL_CONTROL_H
#define RM_SHOVEL_CONTROL_H
#include "remote_ps2.h"

#ifndef RM_SHOVEL_CONTROL
#define RM_SHOVEL_CONTROL 0
#endif
#if RM_SHOVEL_CONTROL != 0 && RM_SHOVEL_CONTROL != 1
#error "RM_SHOVEL_CONTROL must be 0 or 1"
#endif

enum { RM_SHOVEL_MIN_US=1400, RM_SHOVEL_MAX_US=1600,
    RM_SHOVEL_NEUTRAL_MS=500, RM_SHOVEL_MAX_HOLD_MS=5000 };
typedef struct {
    uint16_t up_us, down_us, pulse_us, applied_us;
    uint32_t last_step_ms, neutral_at_ms, started_at_ms;
    int8_t direction, next_direction; /* -1下降，+1上升；下次方向只在PWM写入后改变。 */
    bool enabled, active, fault, previous_start, release_seen;
    bool neutral_tracking, release_required, direction_committed;
} rm_shovel_t;

void rm_shovel_init(rm_shovel_t *s, uint32_t now);
void rm_shovel_stop(rm_shovel_t *s);
void rm_shovel_fault(rm_shovel_t *s);
bool rm_shovel_set_pulses(rm_shovel_t *s, int up_us, int down_us);
void rm_shovel_step(rm_shovel_t *s, const rm_remote_t *r, uint32_t now, bool links_ok);
void rm_shovel_commit(rm_shovel_t *s);
#endif
