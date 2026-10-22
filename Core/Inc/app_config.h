/**
 * @file    app_config.h
 * @brief   整车参数总表。
 *
 * 换车、换电机、换轮子之后，基本原则：
 *   1. 先量机械参数（轮半径、轮距、轴距）；
 *   2. 再标定编码器每圈计数（手转一圈看计数）；
 *   3. 最后调 PID（先内环后外环，先 P 后 I 后 D）。
 *
 * 本文件里所有量都是"物理量"，代码内部不做单位换算的隐式假设。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/* =========================================================================
 * 一、机械参数
 * ========================================================================= */

/** 轮子半径，米。76.5mm 是 152mm 麦轮的标准值。 */
#define WHEEL_RADIUS_M            0.0765f

/** 半轴距 = 前后轮中心距 / 2，米。 */
#define HALF_WHEELBASE_M          0.1500f

/** 半轮距 = 左右轮中心距 / 2，米。 */
#define HALF_TRACK_M              0.1500f

/** 减速电机输出轴空载转速，rpm（看电机铭牌，不是电机内部转速）。 */
#define WHEEL_MAX_RPM             330.0f

/**
 * 编码器计数：轮子转一圈，定时器/软件计数器增加多少。
 * 霍尔 11 线 × 4 倍频 × 减速比 30 = 1320。
 * 标定方法见 docs/tuning-guide.md。
 */
#define ENCODER_COUNTS_PER_REV    1320.0f

/** 每个轮子的最大线速度 (m/s)，由转速和轮半径推出来。 */
#define WHEEL_MAX_MPS             (WHEEL_MAX_RPM / 60.0f * 6.28318530718f * WHEEL_RADIUS_M)

/* =========================================================================
 * 二、装配方向修正
 * 装完之后如果某个轮子转向反了、或者编码器计数方向反了，只改这里，
 * 不要去改接线（改接线容易在比赛前夜接错）。
 * 0 = 正常，1 = 取反
 * ========================================================================= */
#define MOTOR_INVERT_FL           0
#define MOTOR_INVERT_FR           0
#define MOTOR_INVERT_RL           0
#define MOTOR_INVERT_RR           0

#define ENCODER_INVERT_FL         0
#define ENCODER_INVERT_FR         0
#define ENCODER_INVERT_RL         0
#define ENCODER_INVERT_RR         0

/* =========================================================================
 * 三、控制周期与滤波
 * ========================================================================= */

/** 速度环周期，微秒。10000us = 100Hz，对直流减速电机足够。 */
#define CONTROL_PERIOD_US         10000u

/** IMU 采样周期，微秒。 */
#define IMU_PERIOD_US             5000u

/** 默认遥测周期，毫秒（上位机可通过协议修改）。 */
#define TELEMETRY_PERIOD_MS       20u

/** 轮速低通滤波截止频率，Hz。越低越平滑但相位滞后越大。 */
#define SPEED_LPF_HZ              40.0f

/** 陀螺仪零偏在线估计的静止阈值，rad/s。 */
#define GYRO_STILL_THRESHOLD      0.03f

/* =========================================================================
 * 四、速度环 PID（输出量纲：归一化占空比 -1..1）
 * ========================================================================= */

#define SPEED_PID_KP              0.55f
#define SPEED_PID_KI              4.00f
#define SPEED_PID_KD              0.00f

/** 积分项限幅，防止长时间堵转后积分饱和。 */
#define SPEED_PID_I_MIN          (-0.60f)
#define SPEED_PID_I_MAX          ( 0.60f)

/** 输出限幅。留一点余量给死区补偿，所以不是 1.0。 */
#define SPEED_PID_OUT_MIN        (-1.00f)
#define SPEED_PID_OUT_MAX        ( 1.00f)

/** 微分项低通系数 0..1，1 表示不滤波。 */
#define SPEED_PID_D_ALPHA         0.20f

/** 前馈：占空比 = ff_gain * 目标速度 + ff_offset * sign(目标速度)。 */
#define SPEED_FF_GAIN             0.10f
#define SPEED_FF_OFFSET           0.00f

/** 电机死区补偿：小于该占空比电机基本不转，补偿到这个值。 */
#define MOTOR_DEADZONE            0.08f
/** 只有 PID 输出大于这个值才做死区补偿，避免静止时蠕动。 */
#define MOTOR_DEADZONE_MIN_CMD    0.02f

/** 轮速目标斜率限制，m/s²。防止突然给满占空比导致电流冲击。 */
#define WHEEL_SETPOINT_ACCEL      12.0f

/* =========================================================================
 * 五、整车级限幅
 * ========================================================================= */

/** 平移加速度上限，m/s²。 */
#define BODY_ACCEL_LIMIT          2.5f

/** 旋转角加速度上限，rad/s²。 */
#define BODY_ALPHA_LIMIT          8.0f

/** 遥控/上位机指令超时，毫秒。超时自动 disarm。 */
#define CMD_TIMEOUT_MS            300u

/** 认为"摇杆回中"的阈值，用于安全上电。 */
#define ARM_CMD_EPS               0.05f

/* =========================================================================
 * 六、偏航保持（IMU 辅助）
 * ========================================================================= */

#define ENABLE_YAW_HOLD           1
#define YAW_HOLD_KP               1.20f
#define YAW_HOLD_KI               0.00f
#define YAW_HOLD_KD               0.05f
#define YAW_HOLD_MAX_WZ           1.50f
/** 目标速度低于该值时不启用偏航保持（原地不动时不要瞎纠偏）。 */
#define YAW_HOLD_MIN_SPEED        0.05f
/** 手动转向指令超过该值时不启用偏航保持（人在打方向就听人的）。 */
#define YAW_HOLD_WZ_DEADBAND      0.10f

/* =========================================================================
 * 七、通信
 * ========================================================================= */

#define LINK_BAUDRATE             115200u
#define LINK_RX_BUFFER_SIZE       512u

/** 一帧最大负载字节数。 */
#define PROTO_MAX_PAYLOAD         48u

/** 上电默认遥测频率上限，Hz。 */
#define TELEMETRY_MAX_HZ          100u

/* =========================================================================
 * 八、编译期自检（参数写错时直接编译不过，比烧进去再查快得多）
 * ========================================================================= */

#if WHEEL_RADIUS_M <= 0.0f
#error "WHEEL_RADIUS_M 必须大于 0"
#endif

#if ENCODER_COUNTS_PER_REV <= 0.0f
#error "ENCODER_COUNTS_PER_REV 必须大于 0"
#endif

#if CONTROL_PERIOD_US < 1000u
#error "控制周期太快，F103 扛不住"
#endif

#endif /* APP_CONFIG_H */
