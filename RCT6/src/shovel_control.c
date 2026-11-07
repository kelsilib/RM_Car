/* □键每次按下交替下降和上升，松开撤销PWM。脉宽代表方向和速度，实际角度需观察。 */
#include "shovel_control.h"
#include <math.h>
#include <string.h>

void rm_shovel_init(rm_shovel_t *s, uint32_t now)
{
    memset(s,0,sizeof *s);
    s->up_us = 1530; /* 用户观察增加脉宽时铲斗上翻，当前只是低速候选。 */
    s->down_us = 1470; /* 反向候选，实际方向及速度在松键验收时一并确认。 */
    s->next_direction = -1; /* 首次按下先下降。重启后重新从下降开始。 */
    s->last_step_ms = now;
    s->release_required = true;
}

void rm_shovel_stop(rm_shovel_t *s)
{
    s->enabled = s->active = false;
    s->pulse_us = 0;
    s->direction = 0;
    s->neutral_tracking = s->release_seen = false;
    s->release_required = true;
}

void rm_shovel_fault(rm_shovel_t *s)
{
    rm_shovel_stop(s);
    s->fault = true;
}

bool rm_shovel_set_pulses(rm_shovel_t *s, int up_us, int down_us)
{
    if (!s || s->fault || s->enabled || s->pulse_us || s->applied_us ||
        up_us < RM_SHOVEL_MIN_US || up_us > RM_SHOVEL_MAX_US ||
        down_us < RM_SHOVEL_MIN_US || down_us > RM_SHOVEL_MAX_US || up_us == down_us) return false;
    s->up_us = (uint16_t)up_us;
    s->down_us = (uint16_t)down_us;
    s->neutral_tracking = false;
    return true;
}

static bool remote_healthy(const rm_remote_t *r, uint32_t now)
{
    return r->valid && (uint32_t)(now-r->at_ms) <= 200u &&
        isfinite(r->forward) && fabsf(r->forward) <= 1.0f &&
        isfinite(r->left) && fabsf(r->left) <= 1.0f &&
        isfinite(r->yaw) && fabsf(r->yaw) <= 1.0f &&
        isfinite(r->right_y) && fabsf(r->right_y) <= 1.0f;
}

void rm_shovel_step(rm_shovel_t *s, const rm_remote_t *r, uint32_t now, bool links_ok)
{
    uint32_t elapsed = now-s->last_step_ms;
    s->last_step_ms = now;
    bool start_edge = r->arm && !s->previous_start;
    s->previous_start = r->arm;
    s->pulse_us = 0;
    if (s->fault || !links_ok || elapsed > 100u || !remote_healthy(r,now) || r->stop) {
        rm_shovel_stop(s);
        return;
    }
    if (!r->arm) s->release_seen = true;
    bool neutral = fabsf(r->forward) <= 0.08f && fabsf(r->left) <= 0.08f &&
        fabsf(r->yaw) <= 0.08f && fabsf(r->right_y) <= 0.08f &&
        !(r->buttons & (uint16_t)~RM_PS2_START);
    if (!s->enabled) {
        if (!neutral) s->neutral_tracking = false;
        else if (!s->neutral_tracking) {
            s->neutral_tracking = true;
            s->neutral_at_ms = now;
        }
        if (start_edge && s->release_seen && s->neutral_tracking &&
            (uint32_t)(now-s->neutral_at_ms) >= RM_SHOVEL_NEUTRAL_MS) {
            s->enabled = true;
            s->release_required = true;
        }
        return;
    }
    if (start_edge) {
        rm_shovel_stop(s);
        return;
    }
    if (r->arm || (r->buttons & (RM_PS2_L2 | RM_PS2_R2))) {
        s->active = false;
        s->release_required = true;
        return;
    }
    if (!(r->buttons & RM_PS2_SQUARE)) {
        s->active = false;
        s->direction = 0;
        s->release_required = false;
        return;
    }
    if (s->release_required) return;
    if (!s->active) {
        s->active = true;
        s->direction = s->next_direction;
        s->direction_committed = false;
        s->started_at_ms = now;
    }
    if ((uint32_t)(now-s->started_at_ms) >= RM_SHOVEL_MAX_HOLD_MS) {
        s->active = false;
        s->release_required = true;
        return; /* 单次按住最多5秒，到期后要松键重按，不能自动重复。 */
    }
    s->pulse_us = s->direction > 0 ? s->up_us : s->down_us;
}

void rm_shovel_commit(rm_shovel_t *s)
{
    s->applied_us = s->pulse_us;
    if (s->active && s->pulse_us && !s->direction_committed) {
        /* 本次输出真正写入后，下一次新按才换方向；写入失败不会跳过方向。 */
        s->next_direction = (int8_t)-s->direction;
        s->direction_committed = true;
    }
}
