/* △选S3，○选S4。十字键控制选中的一台，松开撤销PWM。
 * S3与S4的候选方向相反；实际方向和松手停转由整体联调确认。 */
#include "joint_pair.h"
#include <math.h>
#include <string.h>

void rm_joint_pair_init(rm_joint_pair_t *j)
{
    memset(j,0,sizeof *j);
    j->center_required = true;
}

void rm_joint_pair_stop(rm_joint_pair_t *j)
{
    j->pulse_us[0] = j->pulse_us[1] = 0;
    j->selected = 0;
    j->direction = 0;
    j->active = false;
    j->center_required = true;
}

void rm_joint_pair_step(rm_joint_pair_t *j, const rm_shovel_t *s,
                        const rm_remote_t *r, uint32_t now)
{
    uint16_t edges = (uint16_t)(r->buttons & (uint16_t)~j->previous_buttons);
    j->previous_buttons = r->buttons;
    j->pulse_us[0] = j->pulse_us[1] = 0;
    if (!s->enabled || s->fault || !r->valid || r->stop ||
        (uint32_t)(now-r->at_ms) > 200u || !isfinite(r->right_y) || fabsf(r->right_y) > 1.0f) {
        rm_joint_pair_stop(j);
        return;
    }
    /* 操作S2、START或油门时暂停关节，方向键要松开一次才能重新动作。 */
    if (s->active || r->arm || (r->buttons & (RM_PS2_SQUARE | RM_PS2_L2 | RM_PS2_R2))) {
        j->active = false;
        j->center_required = true;
        return;
    }
    uint16_t keys = (uint16_t)(r->buttons & (RM_PS2_TRIANGLE | RM_PS2_CIRCLE));
    if (keys) {
        j->active = false;
        if (keys == (RM_PS2_TRIANGLE | RM_PS2_CIRCLE)) {
            j->center_required = true;
        } else if (edges & keys) {
            j->selected = keys == RM_PS2_TRIANGLE ? 3 : 4;
            j->center_required = (r->buttons & (RM_PS2_UP | RM_PS2_DOWN)) != 0;
        }
        return; /* 选路键松开后才响应方向键，不让换路和动作挤在一轮。 */
    }
    if (!j->selected) return;
    uint16_t directions = (uint16_t)(r->buttons & (RM_PS2_UP | RM_PS2_DOWN));
    if (!directions) {
        j->active = false;
        j->direction = 0;
        j->center_required = false;
        return;
    }
    if (directions == (RM_PS2_UP | RM_PS2_DOWN)) {
        j->active = false;
        j->center_required = true;
        return; /* 上下同时按下不猜方向，全部松开后才能重试。 */
    }
    if (j->center_required) return;
    int8_t direction = directions == RM_PS2_UP ? 1 : -1;
    if (!j->active) {
        j->active = true;
        j->started_at_ms = now;
    }
    j->direction = direction; /* 直接换向时持续计时不重置。 */
    if ((uint32_t)(now-j->started_at_ms) >= RM_JOINT_MAX_HOLD_MS) {
        j->active = false;
        j->center_required = true;
        return; /* 单次最多5秒，上下键松开后才能再动。 */
    }
    /* S3沿用原候选方向，S4相反；调S2候选值后仍保持相反关系。 */
    bool use_up_pulse = (j->selected == 3) ? direction < 0 : direction > 0;
    j->pulse_us[j->selected-3u] = use_up_pulse ? s->up_us : s->down_us;
}

void rm_joint_pair_commit(rm_joint_pair_t *j)
{
    j->applied_us[0] = j->pulse_us[0];
    j->applied_us[1] = j->pulse_us[1];
}
