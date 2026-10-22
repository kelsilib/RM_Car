/* 把手柄、安全检查、轮速计算和驱动命令接起来；这里不直接操作STM32硬件。 */
#ifndef RM_CHASSIS_APP_H
#define RM_CHASSIS_APP_H
#include "mecanum.h"
#include "motor_uart.h"
#include "safety.h"
#include "calibration.h"

typedef struct {
    rm_config_t config;             /* 启动时复制的配置；默认值在board_config.c */
    rm_motor_parser_t parser;       /* 收包进度和计数；malformed/unknown对应日志bad/unknown */
    rm_safety_t safety;             /* 最先观察state/inhibit，定位为什么不能解锁 */
    rm_remote_t remote;             /* PS2调试重点：valid、forward/left/yaw、arm/stop */
    rm_motor_packet_t last_packet;  /* 最近一份合法电机报文；先看kind，再看对应字段 */
    rm_twist_t ramp;                /* 缓慢调整后的车体速度，单位m/s、rad/s；禁动时清零 */
    uint32_t motor_at_ms, last_step_ms; /* 最后合法累计帧时间 / 最后控制检查时间ms */
    bool motor_seen, transport_fault; /* 收到过累计编码器包吗 / 记住的通信故障，修好后需复位 */
    rm_calibration_t calibration;
    int32_t motor_counts[RM_WHEELS]; /* 最近MAll原始计数；电压回复不会覆盖它 */
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
/* 主循环约每20ms调用：检查能不能动，再拼出spd或PWM0命令。
 * 返回true只说明命令拼好了；是否解锁看safety.state，是否发出还要看串口发送结果。
 * 两次调用间隔超过100ms会禁止运动；禁动时的连接重试可能让某一轮略超过20ms。 */
bool rm_app_step(rm_app_t *a, uint32_t now, bool estop, char *frame, size_t cap);
#endif
