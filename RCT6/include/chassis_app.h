/* 把手柄、安全检查、轮速计算和驱动命令接起来；这里不直接操作STM32硬件。 */
#ifndef RM_CHASSIS_APP_H
#define RM_CHASSIS_APP_H
#include "mecanum.h"
#include "motor_uart.h"
#include "safety.h"
#include "calibration.h"
#include "arm_control.h"
#include "bench_control.h"
#include "servo_cal.h"
#include "shovel_control.h"
#include "joint_pair.h"

typedef struct {
#if RM_SHOVEL_CONTROL
    rm_shovel_t shovel;
#endif
#if RM_ARM_PAIR_CONTROL
    rm_joint_pair_t joint_pair;
#endif
#if RM_SERVO_CAL_CHANNEL
    rm_servo_cal_t servo_cal;       /* 单路标定专用，普通比赛构建不占用这段状态。 */
#endif
    rm_config_t config;             /* 启动时复制的配置；默认值在board_config.c */
    rm_motor_parser_t parser;       /* 收包进度和计数；malformed/unknown对应日志bad/unknown */
    rm_driver_info_t driver_info;   /* 普通配置文本的回读结果；不代替接线/保护验收 */
    rm_safety_t safety;             /* 最先观察state/inhibit，定位为什么不能解锁 */
    rm_remote_t remote;             /* PS2调试重点：valid、forward/left/yaw、arm/stop */
    rm_arm_t arm;                   /* S1升降、S2铲斗、S3肩、S4肘、S5腕、S6夹爪、S7预留。 */
    bool servo_hardware_ready;      /* 主循环初始化TIM3/TIM4成功后才为true；PC测试可模拟 */
    rm_motor_packet_t last_packet;  /* 最近一份合法电机报文；先看kind，再看对应字段 */
    rm_twist_t ramp;                /* 缓慢调整后的车体速度，单位m/s、rad/s；禁动时清零 */
    uint32_t motor_at_ms, last_step_ms; /* 最后合法累计帧时间 / 最后控制检查时间ms */
    bool motor_seen, transport_fault; /* 收到过累计编码器包吗 / 记住的通信故障，修好后需复位 */
    rm_calibration_t calibration;
    rm_bench_t bench;               /* 专用联调构建的临时许可与方向，普通构建不开放。 */
    int32_t motor_counts[RM_WHEELS]; /* 最近MAll原始计数；电压回复不会覆盖它 */
    float motor_speeds[RM_WHEELS]; /* 驱动MSPD实测轮缘mm/s，按M1～M4保留各自正负号。 */
    uint32_t motor_speed_at_ms;
    bool motor_speed_seen; /* 速度包只用于诊断；健康检查继续要求累计编码器回包。 */
    int32_t last_wheel[RM_WHEELS], last_channel[RM_WHEELS];
    bool last_speed_mode, battery_seen, estop_active;
    float battery_v;
    uint32_t battery_at_ms;
    char driver_reply[RM_FRAME_CAP+2]; /* 最近一条完整未知回复；保留原文供人工核对，不据此判定参数正确 */
    uint32_t driver_reply_at_ms;
    bool driver_reply_seen;
} rm_app_t;
void rm_app_init(rm_app_t *a, const rm_config_t *c, uint32_t now);
/* 每收到一个字节就调用；now填字节实际到达的时间，不能把处理旧包的时间填进去。 */
void rm_app_receive(rm_app_t *a, uint8_t byte, uint32_t now);
/* 串口出错、接收缓存满或发送失败时调用。这个故障会一直记住，直到复位重新初始化。 */
void rm_app_transport_fault(rm_app_t *a);
/* 串口stop和标定取消都用这里；只取消许可，不清除已记录的通信故障。 */
void rm_app_stop(rm_app_t *a);
bool rm_app_cal_arm(rm_app_t *a, uint32_t now);
bool rm_app_cal_pulse(rm_app_t *a, uint32_t now, unsigned channel, int pwm, unsigned duration_ms);
bool rm_app_output_active(const rm_app_t *a);
rm_safety_input_t rm_app_safety_input(const rm_app_t *a, uint32_t now, bool estop);
/* 主循环约每20ms调用：检查能不能动，再拼出spd或PWM0命令。
 * 返回true只说明命令拼好了；是否解锁看safety.state，是否发出还要看串口发送结果。
 * 两次调用间隔超过100ms会禁止运动；禁动时的连接重试可能让某一轮略超过20ms。 */
bool rm_app_step(rm_app_t *a, uint32_t now, bool estop, char *frame, size_t cap);
#endif
