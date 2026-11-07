/* 串口version指令用这个编号确认当前运行的是哪一版代码。改动发布内容后同步更新。 */
#ifndef RM_FIRMWARE_VERSION_H
#define RM_FIRMWARE_VERSION_H
#include "servo_cal.h"
#include "shovel_control.h"
#include "joint_pair.h"
#if RM_ARM_PAIR_CONTROL
#define RM_FIRMWARE_ID "rm_rct6-arm-dpad-20261106-r20"
#elif RM_SHOVEL_CONTROL
#define RM_FIRMWARE_ID "rm_rct6-shovel-20261106-r18-S2"
#elif RM_SERVO_CAL_CHANNEL
#define RM_SCAL_STRING_(value) #value
#define RM_SCAL_STRING(value) RM_SCAL_STRING_(value)
#define RM_FIRMWARE_ID "rm_rct6-servo-cal-20261106-r16-S" RM_SCAL_STRING(RM_SERVO_CAL_CHANNEL)
#elif defined(RM_FIELD_DRIVE) && RM_FIELD_DRIVE
#define RM_FIRMWARE_ID "rm_rct6-match-20261106-r15-field"
#elif defined(RM_BENCH_TEST) && RM_BENCH_TEST
#define RM_FIRMWARE_ID "rm_rct6-match-20261106-r15-test"
#elif defined(RM_PS2_WIRED) && RM_PS2_WIRED
#define RM_FIRMWARE_ID "rm_rct6-match-20261106-r15-ps2"
#else
#define RM_FIRMWARE_ID "rm_rct6-match-20261106-r15-safe"
#endif
#endif
