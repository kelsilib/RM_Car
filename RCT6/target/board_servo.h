/* 接线核实前保持0；即使改1，未填写合格arm_config也不会初始化舵机引脚。 */
#ifndef RM_BOARD_SERVO_H
#define RM_BOARD_SERVO_H
#include "arm_control.h"
#ifndef RM_ARM_WIRED
#define RM_ARM_WIRED 0
#endif
bool board_servo_init(const rm_arm_config_t *c);
/* 先整组检查，再写CCR预装载寄存器。返回true后应用才可rm_arm_commit。 */
bool board_servo_apply(const uint16_t pulse_us[RM_SERVOS], uint8_t mask);
#endif
