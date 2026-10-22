/* 这里说明每个参数的含义。想改默认数值，去src/board_config.c。 */
#ifndef RM_BOARD_CONFIG_H
#define RM_BOARD_CONFIG_H
#include <stdbool.h>
#include <stdint.h>

/* 程序内部的四轮顺序始终是左前、右前、左后、右后；发送给驱动前才按接线换顺序。 */
enum { RM_FL, RM_FR, RM_RL, RM_RR, RM_WHEELS };
/* UNKNOWN=还不知道速度单位，禁止解锁；MM_S=已经确认单位是毫米/秒。 */
typedef enum { RM_SPEED_UNKNOWN, RM_SPEED_MM_S } rm_speed_unit_t;
typedef struct {
    float radius_m, half_length_m, half_width_m; /* 轮半径、半轴距、半轮距，全部m */
    float max_v_m_s, max_w_rad_s, accel_m_s2, accel_rad_s2; /* 最大平移/旋转速度，平移各轴/旋转的加速度 */
    float wheel_limit_mm_s, deadzone; /* 每轮轮缘限速mm/s；手柄中间忽略的小范围，0<=deadzone<1 */
    uint8_t channel[RM_WHEELS]; /* 应用轮->驱动通道：0=M1、1=M2、2=M3、3=M4 */
    int8_t direction[RM_WHEELS]; /* 每轮方向：+1/-1；0表示尚未标定，禁止运动 */
    uint32_t remote_timeout_ms, motor_timeout_ms, neutral_hold_ms; /* 遥控超时、驱动反馈超时、回中等待ms */
    rm_speed_unit_t speed_unit; /* 厂商spd命令的真实速度单位 */
    /* verified表示“这一项已核实”。哪项真正测过了才填true，不能为了让车动全勾上。 */
    bool geometry_verified, mapping_verified, motor_parameters_verified, ps2_wiring_verified;
    bool remote_failsafe_verified, driver_failsafe_verified;
    /* 单轮标定是单独的受限模式，默认关闭。幅度是厂商pwm参数，不是百分比。
     * 驱动自身还可能添加死区补偿，所以不能把100理解成很小的实际占空比。 */
    bool calibration_enabled;
    uint16_t calibration_max_pwm, calibration_max_ms;
} rm_config_t;

/* config指令显示的缺项：尺寸、轮序、电机参数、PS2接线、无线保护、驱动保护、速度单位、数值。 */
enum {
    RM_CFG_GEOMETRY=1u, RM_CFG_MAPPING=2u, RM_CFG_MOTOR=4u, RM_CFG_PS2=8u,
    RM_CFG_RADIO=16u, RM_CFG_DRIVER=32u, RM_CFG_UNIT=64u, RM_CFG_VALUES=128u
};

/* 返回一份默认参数。默认有未确认项，所以实物不能直接解锁。 */
rm_config_t rm_config_default(void);
/* 检查参数有没有填全且合法。返回true后，还要由safety检查当前通信和按键。 */
bool rm_config_motion_ready(const rm_config_t *c);
uint32_t rm_config_issues(const rm_config_t *c);
/* 标定不需要已知轮径/轮序/速度单位，但必须确认遥控和独立停车保护。 */
bool rm_config_calibration_ready(const rm_config_t *c);
#endif
