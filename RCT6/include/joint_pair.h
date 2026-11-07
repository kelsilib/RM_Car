/* S3/S4选路和十字键控制，只用于专用整体联调构建。 */
#ifndef RM_JOINT_PAIR_H
#define RM_JOINT_PAIR_H
#include "shovel_control.h"

#ifndef RM_ARM_PAIR_CONTROL
#define RM_ARM_PAIR_CONTROL 0
#endif
#if RM_ARM_PAIR_CONTROL != 0 && RM_ARM_PAIR_CONTROL != 1
#error "RM_ARM_PAIR_CONTROL must be 0 or 1"
#endif
#if RM_ARM_PAIR_CONTROL && !RM_SHOVEL_CONTROL
#error "RM_ARM_PAIR_CONTROL requires RM_SHOVEL_CONTROL"
#endif

enum { RM_JOINT_MAX_HOLD_MS=5000 };
typedef struct {
    uint16_t pulse_us[2], applied_us[2], previous_buttons;
    uint32_t started_at_ms;
    uint8_t selected; /* 0未选；3下关节，4上关节。 */
    int8_t direction;
    bool active, center_required;
} rm_joint_pair_t;

void rm_joint_pair_init(rm_joint_pair_t *j);
void rm_joint_pair_stop(rm_joint_pair_t *j);
void rm_joint_pair_step(rm_joint_pair_t *j, const rm_shovel_t *s,
                        const rm_remote_t *r, uint32_t now);
void rm_joint_pair_commit(rm_joint_pair_t *j);
#endif
