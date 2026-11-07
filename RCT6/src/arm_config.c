/* 按用户图片从底座到夹爪安排S3～S6，S1升降、S2铲斗、S7预留。 */
#include "arm_control.h"

rm_arm_config_t rm_arm_config_default(void)
{
    /* S1..S7接线见ARM_CONTROL.md。0不是舵机中位，而是“尚未填写、禁止输出”。
     * 每路分别启用，先核实该路上下限、起始姿态和方向。
     * 下方候选值来自用户授权的通用MG995初试安排，实物标定后在这里填写许可。 */
    rm_arm_config_t c = {0};
#if defined(RM_FIELD_DRIVE) && RM_FIELD_DRIVE
    /* 用户于2026-11-06授权按通用MG995保守准备候选值。周期50Hz、中心1500us，
     * 初试窗口1400～1600us，变化上限100us/s。这是开发选择，实物机械端点待标定。
     * 首次1500us对应的姿态未知，因此通道/验收标志仍待填写，上电无PWM。 */
    c.period_us = 20000;
    for (unsigned i = 0; i < 6; ++i) {
        c.servo[i].min_us = 1400;
        c.servo[i].max_us = 1600;
        c.servo[i].start_us = 1500;
        c.servo[i].max_rate_us_s = 100;
        c.servo[i].direction = 1; /* 仅约定上推右Y增加脉宽，实际升降/铲斗方向待测。 */
    }
    /* S2铲斗的两姿态由现场手动标定填写。先读取servo 2，再设置下列三项。
     * 不从1400/1600us端点推断哪边铲矿、哪边储矿。 */
    c.shovel_scoop_us = 0;
    c.shovel_store_us = 0;
    c.shovel_poses_verified = false;
    /* 比赛动作的两端点由标定填写。S1的A=升起/B=降下，S6的A=张开/B=闭合。
     * 整车START只启用操作，不会自动把这些舵机移到中位。 */
    c.servo[0].toggle_poses_verified = false;
    c.servo[0].pose_a_us = c.servo[0].pose_b_us = 0;
    c.servo[5].toggle_poses_verified = false;
    c.servo[5].pose_a_us = c.servo[5].pose_b_us = 0;
#endif
    return c;
}
