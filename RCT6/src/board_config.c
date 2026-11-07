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
        /* 用户更正实物轮径为80mm，所以轮半径=80/2000=0.040m。
         * 前后轮心距141.776179mm、左右轮心距194.101555mm来自装配体/工程图，
         * 各除以2000得到半轴距和半轮距；这些距离继续沿用，装配完成后再核对实物。
         * 车头是底盘凹口端，已核对为CAD的-Z；车体向左为CAD的-X。
         * 这些数值已可用于计算，但不代表验收完成；geometry_verified仍为false。
         * 来源、照片布局和复核方法见GEOMETRY_REFERENCE.md。 */
        .radius_m = 0.040f,
        .half_length_m = 0.070888089544f, .half_width_m = 0.097050777636f,
        /* 手柄推到底时：平移最多0.15m/s，旋转最多0.5rad/s。先从低速测试。 */
        .max_v_m_s = 0.15f, .max_w_rad_s = 0.5f,
        /* 起步/换方向不要一下跳到目标速度。前后、左右各自每秒最多改变0.3m/s。
         * 旋转每秒最多改变1rad/s；所有轴回中或故障停车时直接归零，不等缓慢减速。 */
        .accel_m_s2 = 0.3f, .accel_rad_s2 = 1.0f,
        /* 每个轮子的轮缘速度上限200mm/s；摇杆中间附近8%的行程当作没推。 */
        .wheel_limit_mm_s = 200.0f, .deadzone = 0.08f,
        /* 数组依次是左前、右前、左后、右后。2026-11-05用户最新确认接M4、M3、M2、M1。
         * 即M1右后、M2左后、M3右前、M4左前；0～3分别表示驱动接口M1～M4。
         * direction的0表示还不知道方向；逐个轮子确认后，分别填+1或-1。 */
        .channel = {3, 2, 1, 0}, .direction = {0, 0, 0, 0},
        /* 超过200ms没收到正确的手柄包或累计编码器包，就禁止运动。 */
        .remote_timeout_ms = 200, .motor_timeout_ms = 200,
        /* 2026-11-02用户补充确认：SPD是轮缘mm/s，受驱动wdiameter影响，不是轴转速。
         * 单位已知不等于允许行驶，其他验收和驱动轮径回读仍要通过。 */
        .neutral_hold_ms = 300, .speed_unit = RM_SPEED_MM_S,
        /* 轮径已由用户确认，轮心距目前是CAD初值；实车尺寸和接线逐项核对后再填true。
         * 照片中的X布局只证明摆放方向，不能代替轮序、电机正反号和失联停车实测。 */
        .geometry_verified = false, .mapping_verified = false,
        /* 驱动里的电机类型、编码器线数、减速比、轮径和反馈方向都核对过才填true。 */
        .motor_parameters_verified = false,
        /* 第一项确认接收器供电和接线；第二项确认关手柄、超距后能可靠停止。 */
        .ps2_wiring_verified = false, .remote_failsafe_verified = false,
        /* 用户本轮选择SELECT/电脑stop软件急停，今晚由人在旁看护、四轮架空。
         * 先用断开电机负载的终端检查确认两条路径都撤销许可并输出PWM0，再填true。
         * 驱动会保持末值，主控停机或TX断线时软件停车命令可能无法送达。 */
        .software_stop_verified = false,
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
    if (!c->software_stop_verified) result |= RM_CFG_SOFTWARE_STOP;
    if (c->speed_unit != RM_SPEED_MM_S) result |= RM_CFG_UNIT;
    if (!values_valid(c)) result |= RM_CFG_VALUES;
    return result;
}

bool rm_config_motion_ready(const rm_config_t *c) { return rm_config_issues(c) == 0; }

bool rm_config_calibration_ready(const rm_config_t *c)
{
    return c && c->calibration_enabled && c->ps2_wiring_verified &&
        c->remote_failsafe_verified && c->software_stop_verified &&
        isfinite(c->deadzone) && c->deadzone >= 0.0f && c->deadzone < 1.0f &&
        c->remote_timeout_ms > 0 && c->remote_timeout_ms <= 1000 &&
        c->motor_timeout_ms > 0 && c->motor_timeout_ms <= 1000 &&
        c->neutral_hold_ms > 0 && c->neutral_hold_ms <= 5000 &&
        c->calibration_max_pwm > 0 && c->calibration_max_pwm <= 300 &&
        c->calibration_max_ms >= 20 && c->calibration_max_ms <= 500;
}
