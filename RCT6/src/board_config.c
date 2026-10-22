/**
 * @file board_config.c
 * @brief 主要改参数的地方：车轮尺寸、接线顺序、正反方向、速度和等待时间。
 *
 * 改完要重新编译、下载。tests里的fixture/test_config只是PC模拟用的，不能抄来上车。
 * 下面的0/false/UNKNOWN表示“还没确认”，因此当前默认配置不会让车动。
 * 只想看看手柄有没有读对，不需要把这些确认项改成true。
 */
#include "board_config.h"
#include <math.h>

rm_config_t rm_config_default(void)
{
    const rm_config_t c = {
        /* 先用毫米测量，再除以2000填这里。例如轮径100mm，半径就填0.05m。
         * 前后轮中心距离、左右轮中心距离也各除以2000，得到一半的距离，单位米。 */
        .radius_m = 0.0f, .half_length_m = 0.0f, .half_width_m = 0.0f,
        /* 手柄推到底时：平移最多0.15m/s，旋转最多0.5rad/s。先从低速测试。 */
        .max_v_m_s = 0.15f, .max_w_rad_s = 0.5f,
        /* 起步/换方向不要一下跳到目标速度。前后、左右各自每秒最多改变0.3m/s。
         * 旋转每秒最多改变1rad/s；所有轴回中或故障停车时直接归零，不等缓慢减速。 */
        .accel_m_s2 = 0.3f, .accel_rad_s2 = 1.0f,
        /* 每个轮子的轮缘速度上限200mm/s；摇杆中间附近8%的行程当作没推。 */
        .wheel_limit_mm_s = 200.0f, .deadzone = 0.08f,
        /* 数组依次是左前、右前、左后、右后。暂按M1、M3、M2、M4接线。
         * direction的0表示还不知道方向；逐个轮子确认后，分别填+1或-1。 */
        .channel = {0, 2, 1, 3}, .direction = {0, 0, 0, 0},
        /* 超过200ms没收到正确的手柄包或累计编码器包，就禁止运动。 */
        .remote_timeout_ms = 200, .motor_timeout_ms = 200,
        /* 摇杆放回中间至少300ms后，再新按START。速度单位没确认时仍不能解锁。 */
        .neutral_hold_ms = 300, .speed_unit = RM_SPEED_UNKNOWN,
        /* 确实量过尺寸、核对过轮子接线后，才把对应项改成true。 */
        .geometry_verified = false, .mapping_verified = false,
        /* 驱动里的电机类型、编码器线数、减速比、轮径和反馈方向都核对过才填true。 */
        .motor_parameters_verified = false,
        /* 第一项确认接收器供电和接线；第二项确认关手柄、超距后能可靠停止。 */
        .ps2_wiring_verified = false, .remote_failsafe_verified = false,
        /* 主控不再发命令、TX线断开或主控复位时，驱动仍能自行停车，实测后才填true。 */
        .driver_failsafe_verified = false,
        /* 标定默认关闭。启用后也只能一次动一个驱动接口，最多100参数单位、200ms。
         * 原始PWM会受驱动死区设置影响，实测前不能当成占空比或功率保证。 */
        .calibration_enabled = false, .calibration_max_pwm = 100, .calibration_max_ms = 200
    };
    return c;
}

static bool values_valid(const rm_config_t *c)
{
    if (!c) return false;
    /* 第2关：尺寸、速度和加速度必须是大于0的正常数值。 */
    const float positive[] = {c->radius_m, c->half_length_m, c->half_width_m,
        c->max_v_m_s, c->max_w_rad_s, c->accel_m_s2, c->accel_rad_s2,
        c->wheel_limit_mm_s};
    for (unsigned i = 0; i < sizeof positive / sizeof positive[0]; ++i)
        if (!isfinite(positive[i]) || positive[i] <= 0.0f) return false;
    /* 第3关：限速不能超过协议范围；手柄死区和等待时间也要在允许范围内。 */
    if (c->wheel_limit_mm_s > 1000.0f || !isfinite(c->deadzone) ||
        c->deadzone < 0.0f || c->deadzone >= 1.0f) return false;
    if (!c->remote_timeout_ms || c->remote_timeout_ms > 1000 ||
        !c->motor_timeout_ms || c->motor_timeout_ms > 1000 ||
        !c->neutral_hold_ms || c->neutral_hold_ms > 5000) return false;
    /* 第4关：不能把两个轮子填成同一个接口；每轮方向必须已经填成+1或-1。 */
    unsigned seen = 0;
    for (unsigned i = 0; i < RM_WHEELS; ++i) {
        if (c->channel[i] >= RM_WHEELS ||
            (c->direction[i] != 1 && c->direction[i] != -1)) return false;
        unsigned bit = 1u << c->channel[i];
        if (seen & bit) return false;
        seen |= bit;
    }
    return true;
}

uint32_t rm_config_issues(const rm_config_t *c)
{
    if (!c) return 0xffu;
    uint32_t result = 0;
    if (!c->geometry_verified) result |= RM_CFG_GEOMETRY;
    if (!c->mapping_verified) result |= RM_CFG_MAPPING;
    if (!c->motor_parameters_verified) result |= RM_CFG_MOTOR;
    if (!c->ps2_wiring_verified) result |= RM_CFG_PS2;
    if (!c->remote_failsafe_verified) result |= RM_CFG_RADIO;
    if (!c->driver_failsafe_verified) result |= RM_CFG_DRIVER;
    if (c->speed_unit != RM_SPEED_MM_S) result |= RM_CFG_UNIT;
    if (!values_valid(c)) result |= RM_CFG_VALUES;
    return result;
}

bool rm_config_motion_ready(const rm_config_t *c) { return rm_config_issues(c) == 0; }

bool rm_config_calibration_ready(const rm_config_t *c)
{
    return c && c->calibration_enabled && c->ps2_wiring_verified &&
        c->remote_failsafe_verified && c->driver_failsafe_verified &&
        isfinite(c->deadzone) && c->deadzone >= 0.0f && c->deadzone < 1.0f &&
        c->remote_timeout_ms > 0 && c->remote_timeout_ms <= 1000 &&
        c->motor_timeout_ms > 0 && c->motor_timeout_ms <= 1000 &&
        c->neutral_hold_ms > 0 && c->neutral_hold_ms <= 5000 &&
        c->calibration_max_pwm > 0 && c->calibration_max_pwm <= 300 &&
        c->calibration_max_ms >= 20 && c->calibration_max_ms <= 500;
}
