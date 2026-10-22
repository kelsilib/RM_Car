/**
 * @file safety.c
 * @brief 专门判断“现在能不能让车动”，这里不操作引脚，也不发送串口命令。
 *
 * 测试时先看g_app.safety.inhibit：它记录哪些条件没满足；state记录当前状态。
 * 故障消失后也不会自己解锁，要把摇杆放回中间，再松开并重新按START。
 * 检查顺序：参数和通信是否正常 -> 是否要求停车 -> 摇杆是否回中 -> 是否新按START。
 * 主循环约每20ms都要检查一次；即使没收到新包也要检查，才能发现断线。
 */
#include "safety.h"
#include <math.h>
#include <string.h>

/* 开机先禁止运动。结构体清零后，state正好是RM_DISARMED。 */
void rm_safety_init(rm_safety_t *s) { memset(s, 0, sizeof *s); }

uint32_t rm_safety_health(const rm_config_t *c, const rm_safety_input_t *in, uint32_t now)
{
    const rm_remote_t *r = &in->remote;
    /* 第一步：逐项找出不能动的原因。同一时刻可以同时有多个问题。 */
    uint32_t reason = 0;
    /* 摇杆必须在-1到+1之间，也不能是NaN/无穷大这样的无效数值。 */
    bool axes_valid = isfinite(r->forward) && isfinite(r->left) && isfinite(r->yaw)
        && fabsf(r->forward) <= 1.0f && fabsf(r->left) <= 1.0f && fabsf(r->yaw) <= 1.0f;
    /* 用“现在-上次”算多久没收到数据。这样毫秒计数从最大值回到0时也能正确计算。 */
    if (!r->valid || !axes_valid ||
        (uint32_t)(now - r->at_ms) > c->remote_timeout_ms) reason |= RM_INHIBIT_REMOTE;
    if (!in->motor_seen || (uint32_t)(now - in->motor_at_ms) > c->motor_timeout_ms)
        reason |= RM_INHIBIT_MOTOR;
    if (!in->transport_ok) reason |= RM_INHIBIT_TRANSPORT;
    if (!in->scheduler_ok) reason |= RM_INHIBIT_SCHEDULER;
    if (in->estop) reason |= RM_INHIBIT_ESTOP;
    return reason;
}

bool rm_safety_step(rm_safety_t *s, const rm_config_t *c,
                    const rm_safety_input_t *in, uint32_t now)
{
    const rm_remote_t *r = &in->remote;
    uint32_t reason = rm_safety_health(c, in, now);
    if (!rm_config_motion_ready(c)) reason |= RM_INHIBIT_CONFIG;
    s->inhibit = reason;
    /* 只有“上次没按、本次按了”才算新按一次。一直按住START不能反复解锁。 */
    bool rising = r->arm && !s->previous_arm;
    s->previous_arm = r->arm;
    /* 第二步：有一个条件不满足就禁止运动，之前回中、松键的记录也清掉。
     * 调用者会发送PWM0。若主控死机或发送线断了，还要靠驱动自身的停车保护。 */
    if (reason) {
        s->state = in->estop ? RM_ESTOP :
            ((reason == RM_INHIBIT_CONFIG) ? RM_DISARMED : RM_FAULT);
        s->neutral_tracking = false;
        s->release_seen = false;
        return false;
    }
    /* SELECT的优先级高于START：两键同时按也不会解锁。 */
    if (r->stop) {
        s->state = RM_DISARMED;
        s->neutral_tracking = false;
        s->release_seen = false;
        return false;
    }
    /* 第三步：前后、左右、旋转三个方向都回到中间附近，才开始计时。
     * “死区”就是忽略轻微漂移的范围；中途又推摇杆，回中时间要重新算。 */
    bool centered = fabsf(r->forward) <= c->deadzone &&
                    fabsf(r->left) <= c->deadzone && fabsf(r->yaw) <= c->deadzone;
    if (!centered) {
        s->neutral_tracking = false;
    } else if (!s->neutral_tracking) {
        s->neutral_since = now;
        s->neutral_tracking = true;
    }
    /* 第四步：通信正常时见过START松开、回中已够300ms、本次又新按下，才解锁。
     * 提前按住START等时间到也不行；要松开，再按一次。300ms来自配置文件。 */
    if (!r->arm) s->release_seen = true;
    if (s->state != RM_ARMED && centered && s->neutral_tracking &&
        (uint32_t)(now - s->neutral_since) >= c->neutral_hold_ms &&
        rising && s->release_seen) {
        s->state = RM_ARMED;
        s->release_seen = false;
    }
    return s->state == RM_ARMED;
}
