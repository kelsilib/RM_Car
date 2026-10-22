/**
 * @file mecanum.c
 * @brief 算出“车想往哪里走时，四个麦轮各该转多快”，也处理摇杆死区和加减速。
 *
 * 测试时按“左前、右前、左后、右后”看结果：前进四个同号，左移[-,+,+,-]，
 * 逆时针转[-,+,-,+]。这是机械方向；接线导致的电机正反号由motor_uart.c另外处理。
 * 四个麦轮的滚子安装方向必须符合这套公式，装反时不能只改正负号凑结果。
 */
#include "mecanum.h"
#include <math.h>

bool rm_mecanum_angular(const rm_config_t *c, rm_twist_t t, float wheel[RM_WHEELS])
{
    /* 先把输出清零，再检查尺寸和速度是否有效。返回false时，调用者不能使用输出。 */
    for (unsigned i = 0; i < RM_WHEELS; ++i) wheel[i] = 0.0f;
    if (!c || !isfinite(c->radius_m) || c->radius_m <= 0.0f ||
        !isfinite(c->half_length_m) || c->half_length_m <= 0.0f ||
        !isfinite(c->half_width_m) || c->half_width_m <= 0.0f ||
        !isfinite(t.vx) || !isfinite(t.vy) || !isfinite(t.wz)) return false;
    /* 转弯时，车越宽、越长，轮子需要转得越快。这里先算转弯需要的轮缘速度。
     * 再加上前进和横移的需要，最后除以轮半径，得到每个轮子的角速度rad/s。 */
    float yaw = (c->half_length_m + c->half_width_m) * t.wz;
    wheel[RM_FL] = (t.vx - t.vy - yaw) / c->radius_m;
    wheel[RM_FR] = (t.vx + t.vy + yaw) / c->radius_m;
    wheel[RM_RL] = (t.vx + t.vy - yaw) / c->radius_m;
    wheel[RM_RR] = (t.vx - t.vy + yaw) / c->radius_m;
    for (unsigned i = 0; i < RM_WHEELS; ++i)
        if (!isfinite(wheel[i])) return false;
    return true;
}

bool rm_mecanum_mm_s(const rm_config_t *c, rm_twist_t t, int32_t wheel[RM_WHEELS])
{
    for (unsigned i = 0; i < RM_WHEELS; ++i) wheel[i] = 0;
    /* 必须先确认驱动速度命令用的是mm/s。还不知道单位就返回失败，不能猜。 */
    if (!c || c->speed_unit != RM_SPEED_MM_S ||
        !isfinite(c->wheel_limit_mm_s) || c->wheel_limit_mm_s <= 0.0f ||
        c->wheel_limit_mm_s > 1000.0f) return false;
    float angular[RM_WHEELS], mm[RM_WHEELS], peak = 0.0f;
    if (!rm_mecanum_angular(c, t, angular)) return false;
    for (unsigned i = 0; i < RM_WHEELS; ++i) {
        mm[i] = angular[i] * c->radius_m * 1000.0f; /* rad/s × 半径m -> m/s -> mm/s */
        if (!isfinite(mm[i])) return false;
        if (fabsf(mm[i]) > peak) peak = fabsf(mm[i]);
    }
    /* 只要有一个轮子太快，就让四个轮子一起按相同比例慢下来。
     * 这样车的运动方向不容易变；最后取整数会有约±0.5mm/s的误差。 */
    float scale = peak > c->wheel_limit_mm_s ? c->wheel_limit_mm_s / peak : 1.0f;
    for (unsigned i = 0; i < RM_WHEELS; ++i)
        wheel[i] = (int32_t)lroundf(mm[i] * scale);
    return true;
}

float rm_deadzone(float v, float zone)
{
    /* 例如zone=0.08，中间附近8%的轻微偏移忽略掉，避免手没推车却慢慢爬。
     * 离开这段范围后平滑增加，推到底仍是±1。设太大会觉得摇杆不灵敏。 */
    if (!isfinite(v) || !isfinite(zone) || zone < 0.0f || zone >= 1.0f) return 0.0f;
    if (fabsf(v) <= zone) return 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return copysignf((fabsf(v) - zone) / (1.0f - zone), v);
}

/* 当前值a朝目标b走一小步，每次最多变化step；到目标就停，不越过目标。 */
static float approach(float a, float b, float step)
{
    if (b > a + step) return a + step;
    if (b < a - step) return a - step;
    return b;
}

rm_twist_t rm_slew(rm_twist_t current, rm_twist_t target,
                   const rm_config_t *c, float dt_s)
{
    /* dt_s是两次控制之间的秒数：20ms应传0.020。间隔异常时不一下跳一大步。
     * 前后、左右分别限制变化量；两者合起来的加速度可能大于单轴设置。 */
    if (!c || !isfinite(dt_s) || dt_s <= 0.0f || dt_s > 0.1f)
        return (rm_twist_t){0};
    current.vx = approach(current.vx, target.vx, c->accel_m_s2 * dt_s);
    current.vy = approach(current.vy, target.vy, c->accel_m_s2 * dt_s);
    current.wz = approach(current.wz, target.wz, c->accel_rad_s2 * dt_s);
    return current;
}
