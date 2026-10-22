/**
 * @file    robot.h
 * @brief   整车控制：状态机、速度闭环、里程计、遥测。
 */
#ifndef ROBOT_H
#define ROBOT_H

#include <stdbool.h>
#include <stdint.h>

#include "chassis.h"
#include "commander.h"

typedef enum {
    ROBOT_BOOT = 0,
    ROBOT_DISARMED,
    ROBOT_ARMED,
    ROBOT_FAULT
} robot_state_t;

/** 上电初始化。imu_ok 决定是否启用偏航保持。 */
void robot_init(bool imu_ok);

/** 100Hz 速度环，必须在主循环按固定周期调用。 */
void robot_control_step(float dt);

/** 200Hz IMU 更新。 */
void robot_imu_step(float dt);

/** 按设定周期发送遥测。 */
void robot_telemetry_step(uint32_t now_ms);

bool robot_arm(void);
bool robot_disarm(void);
void robot_stop(void);
void robot_set_velocity(float vx, float vy, float wz);
bool robot_set_pid(uint8_t wheel, float kp, float ki, float kd);
void robot_set_telem_period(uint16_t period_ms);
void robot_yaw_zero(void);

robot_state_t robot_state(void);
const char   *robot_state_name(robot_state_t st);
bool          robot_is_armed(void);
bool          robot_imu_ok(void);
uint16_t      robot_telem_period_ms(void);
uint32_t      robot_fault_code(void);

/** 给 commander 用的回调表。 */
const commander_hooks_t *robot_hooks(void);

#endif /* ROBOT_H */
