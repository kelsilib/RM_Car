/* 所有运动都先经过这里检查。这里只决定能否动，命令由chassis_app拼好后交给串口发送。 */
#ifndef RM_SAFETY_H
#define RM_SAFETY_H
#include "board_config.h"
#include "remote_ps2.h"

/* 串口state数值：0禁动、1已解锁、2故障、3软件急停、4受限单轮标定。 */
typedef enum { RM_DISARMED, RM_ARMED, RM_FAULT, RM_ESTOP, RM_CALIBRATING } rm_motion_state_t;
/* inhibit每一位表示一个原因，可同时出现。例如0x07=配置、手柄、电机反馈都有问题。 */
enum {
    RM_INHIBIT_CONFIG = 1u << 0, RM_INHIBIT_REMOTE = 1u << 1, /* 0x01配置 / 0x02遥控 */
    RM_INHIBIT_MOTOR = 1u << 2, RM_INHIBIT_TRANSPORT = 1u << 3, /* 0x04反馈 / 0x08传输 */
    RM_INHIBIT_ESTOP = 1u << 4, RM_INHIBIT_SCHEDULER = 1u << 5 /* 0x10急停 / 0x20调度 */
};
typedef struct {
    rm_motion_state_t state; /* 测试先看：是否仍为FAULT/DISARMED，而不是强行改成ARMED */
    uint32_t inhibit, neutral_since; /* 不能动的原因 / 这次摇杆回中从哪一毫秒开始 */
    bool neutral_tracking, release_seen, previous_arm; /* 是否正在计回中时间 / 通信正常时见过松开START / 上次是否按下 */
} rm_safety_t;
typedef struct {
    rm_remote_t remote; /* 本次要检查的手柄结果 */
    bool motor_seen, transport_ok, estop, scheduler_ok; /* 收到过累计包 / 没有通信故障 / 要求急停 / 控制间隔正常 */
    uint32_t motor_at_ms; /* 最后一包正确的MAll编码器数据是在什么时候收到的，单位ms */
} rm_safety_input_t;
void rm_safety_init(rm_safety_t *s);
/* 正常行驶和单轮标定共同检查通信、数值、急停、控制间隔；不在这里检查轮子尺寸。 */
uint32_t rm_safety_health(const rm_config_t *c, const rm_safety_input_t *in, uint32_t now);
/* 每轮控制都检查一次，now单位毫秒。只有返回true，调用者才可以生成行驶命令。
 * 原因消失后inhibit可以变0，但故障/急停后仍要重新回中，并松开再按START。 */
bool rm_safety_step(rm_safety_t *s, const rm_config_t *c,
                    const rm_safety_input_t *in, uint32_t now);
#endif
