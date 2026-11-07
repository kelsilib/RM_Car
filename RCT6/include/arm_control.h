/* S1升降台、S2铲斗、S3肩、S4肘、S5腕、S6夹爪、S7预留。逐关节计算目标脉宽。 */
#ifndef RM_ARM_CONTROL_H
#define RM_ARM_CONTROL_H
#include "board_config.h"
#include "remote_ps2.h"

enum { RM_SERVOS = 7, RM_SERVO_ALL = 0x7f };
typedef struct {
    bool enabled, limits_verified, start_pose_verified;
    uint16_t min_us, max_us, start_us, max_rate_us_s;
    int8_t direction; /* +1/-1：右摇杆上推时，脉宽增加/减少；不能从CAD猜出升降正向。 */
    /* 比赛单键切换的两种姿态。S1的A/B为高/低位，S6为张开/闭合。 */
    bool toggle_poses_verified;
    uint16_t pose_a_us, pose_b_us;
} rm_servo_config_t;
typedef struct {
    bool enabled, wiring_verified, power_verified, base_safe_verified, fault_hold_verified;
    uint32_t period_us; /* 所有舵机共同的PWM周期；按本批手册填写，不在这里默认50Hz。 */
    /* S2两姿态要先用手动模式标定。0表示未填写，按钮不会触发自动定位。 */
    bool shovel_poses_verified;
    uint16_t shovel_scoop_us, shovel_store_us;
    rm_servo_config_t servo[RM_SERVOS];
} rm_arm_config_t;
enum {
    RM_ARM_CFG_DISABLED=1u, RM_ARM_CFG_PERIOD=2u, RM_ARM_CFG_WIRING=4u,
    RM_ARM_CFG_POWER=8u, RM_ARM_CFG_BASE=16u, RM_ARM_CFG_HOLD=32u,
    RM_ARM_CFG_CHANNELS=64u, RM_ARM_CFG_LIMITS=128u
};
enum {
    RM_ARM_INHIBIT_CONFIG=1u, RM_ARM_INHIBIT_REMOTE=2u, RM_ARM_INHIBIT_TRANSPORT=4u,
    RM_ARM_INHIBIT_ESTOP=8u, RM_ARM_INHIBIT_SCHEDULER=16u,
    RM_ARM_INHIBIT_IO=32u, RM_ARM_INHIBIT_MODE=64u, RM_ARM_INHIBIT_CHANNEL=128u,
    RM_ARM_INHIBIT_STOP=256u, RM_ARM_INHIBIT_POSE=512u
};
typedef struct {
    rm_arm_config_t config;
    uint16_t pulse_us[RM_SERVOS], applied_us[RM_SERVOS];
    int32_t milli_us[RM_SERVOS], applied_milli_us[RM_SERVOS]; /* 保留小数，慢速调整不会被取整吃掉。 */
    uint32_t inhibit, neutral_since, last_step_ms;
    uint16_t previous_buttons;
    uint8_t selected, output_mask, applied_mask;
    uint8_t automatic_mask, next_pose_b_mask; /* 正在走预设的位置，以及下次按键去B位的通道。 */
    uint16_t automatic_target_us[RM_SERVOS];
    bool mode, armed, io_fault, neutral_tracking, release_seen, previous_start;
} rm_arm_t;

rm_arm_config_t rm_arm_config_default(void);
uint32_t rm_arm_config_issues(const rm_arm_config_t *c);
bool rm_arm_channel_ready(const rm_arm_config_t *c, unsigned channel);
bool rm_arm_shovel_poses_ready(const rm_arm_config_t *c);
bool rm_arm_toggle_poses(const rm_arm_config_t *c, unsigned channel,
                         uint16_t *pose_a_us, uint16_t *pose_b_us);
const char *rm_arm_channel_name(unsigned channel);
void rm_arm_init(rm_arm_t *a, const rm_arm_config_t *c, uint32_t now);
/* L2进入机构控制模式，△/○/□/L1/R1/×选S1..S6，方向左右逐路选。
 * 选路后松键回中、新按START启用，再按START停用，按住R2用右Y调整。
 * 方向上/下也可慢调，S2对应已标定的铲矿/储矿姿态。松R2冻结，保持已写入脉宽。
 * 不依赖未知轮径/轮序，但要求PS2电气、无线失联和机械臂的独立验收项成立。 */
void rm_arm_step(rm_arm_t *a, const rm_config_t *shared, const rm_remote_t *r,
                 uint32_t now, bool estop, bool transport_ok, bool hardware_ready,
                 bool scheduler_ok);
/* 比赛入口由整车START统一启用。△升降、○铲斗、×夹爪各自单键切换。
 * L2加右Y手动调肩/肘/腕，□/L1/R1选路，换路不用再按START。 */
void rm_arm_match_step(rm_arm_t *a, const rm_config_t *shared, const rm_remote_t *r,
                       uint32_t now, bool enabled, bool estop, bool transport_ok,
                       bool hardware_ready, bool scheduler_ok);
/* 停止改变目标，保持最后真正写入的脉宽，避免受力机械臂突然卸力；不是动力切断。 */
void rm_arm_stop(rm_arm_t *a);
void rm_arm_fault(rm_arm_t *a); /* IO/电脑通道出错后锁住，复位才能清除。 */
/* 只有硬件写入成功才能提交；发送前故障会撤销尚未写入的新脉宽。 */
void rm_arm_commit(rm_arm_t *a);
#endif
