/* 接线核实前保持0；即使改1，未填写合格arm_config也不会初始化舵机引脚。 */
#ifndef RM_BOARD_SERVO_H
#define RM_BOARD_SERVO_H
#include "arm_control.h"
#include "servo_cal.h"
#include "shovel_control.h"
#include "joint_pair.h"
#ifndef RM_ARM_WIRED
#define RM_ARM_WIRED 0
#endif
bool board_servo_init(const rm_arm_config_t *c);
/* 先整组检查，再写CCR预装载寄存器。返回true后应用才可rm_arm_commit。 */
bool board_servo_apply(const uint16_t pulse_us[RM_SERVOS], uint8_t mask);
#if RM_SERVO_CAL_CHANNEL
bool board_servo_cal_init(void);
bool board_servo_cal_apply(uint16_t pulse_us, bool enabled);
#if RM_ARM_PAIR_CONTROL
/* 一次检查全部通道。先撤销不用的通道，再写本轮要动的一台。 */
bool board_servo_pair_init(void);
bool board_servo_pair_apply(const uint16_t pulse_us[3]);
#endif
#endif
#endif
