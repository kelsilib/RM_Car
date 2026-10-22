/**
 * @file    main.c
 * @brief   入口：初始化外设 -> 建立任务表 -> 超级循环。
 *
 * 时间片划分：
 *   100Hz  速度环（编码器 -> PID -> PWM）
 *   200Hz  IMU
 *    500Hz 串口发送泵
 *    500Hz 指令解析
 *     50Hz 遥测（可通过协议改）
 *     20Hz LED
 */
#include "app_config.h"
#include "board.h"
#include "commander.h"
#include "crc16.h"
#include "encoder.h"
#include "imu.h"
#include "led.h"
#include "link.h"
#include "motor.h"
#include "robot.h"
#include "scheduler.h"
#include "stm32f1xx_hal.h"

static void task_control(float dt)   { robot_control_step(dt); }
static void task_imu(float dt)       { robot_imu_step(dt); }
static void task_comm(float dt)      { (void)dt; commander_poll(); }
static void task_link(float dt)      { (void)dt; link_poll(); }
static void task_telem(float dt)     { (void)dt; robot_telemetry_step(board_millis()); }
static void task_led(float dt)       { (void)dt; led_task(board_millis()); }

static task_t s_tasks[] = {
    {"ctrl",  task_control, CONTROL_PERIOD_US, 0u},
    {"imu",   task_imu,     IMU_PERIOD_US,     0u},
    {"link",  task_link,    2000u,             0u},
    {"comm",  task_comm,    2000u,             0u},
    {"telem", task_telem,   5000u,             0u},
    {"led",   task_led,     20000u,            0u},
};

#define TASK_COUNT (sizeof(s_tasks) / sizeof(s_tasks[0]))

int main(void)
{
    board_init();

    led_init();
    motor_init();
    encoder_init();
    link_init();

    /* 上电自检：CRC 算法算错的话，后面所有通信都是废的 */
    if (crc16_ccitt((const uint8_t *)"123456789", 9u) != 0x29B1u) {
        board_error("crc");
    }

    link_write_str("\r\nRM_Car v1.0 boot\r\n");

    const bool imu_ok = imu_init();

    robot_init(imu_ok);
    commander_init(robot_hooks());

    link_write_str(imu_ok ? "IMU: OK\r\n" : "IMU: not found, yaw hold disabled\r\n");

    sched_init(s_tasks, TASK_COUNT, board_micros());

    for (;;) {
        sched_run(s_tasks, TASK_COUNT, board_micros());

        /* 状态灯：FAULT 快闪 / ARMED 常亮 / 其余慢闪 */
        switch (robot_state()) {
        case ROBOT_FAULT:
            led_set(LED_MODE_FAST);
            break;
        case ROBOT_ARMED:
            led_set(LED_MODE_ON);
            break;
        default:
            led_set(LED_MODE_SLOW);
            break;
        }
    }
}
