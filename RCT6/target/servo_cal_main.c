/* 单路标定主循环。比赛main在此构建中排除，只允许车轮PWM0及单路小步调整。 */
#include "servo_cal.h"
#include "shovel_control.h"
#if RM_SERVO_CAL_CHANNEL && !RM_SHOVEL_CONTROL
#include "board_hal.h"
#include "board_servo.h"
#include "console.h"
#include "firmware_version.h"

#if RM_BENCH_TEST || RM_FIELD_DRIVE || !RM_ARM_WIRED
#error "Servo calibration must use its own build with RM_ARM_WIRED=1"
#endif

rm_app_t g_app;
rm_console_t g_console;

static bool check_links(void)
{
    if (board_debug_fault() || g_console.failed) {
        if (!g_console.failed) rm_console_fault(&g_console);
        rm_servo_cal_fault(&g_app.servo_cal);
    }
    if (board_uart_fault()) rm_servo_cal_fault(&g_app.servo_cal);
    return !g_app.servo_cal.fault;
}

static void poll_console(void)
{
    (void)check_links();
    if (g_console.failed) return; /* 舵机/电机故障后仍能查询原因，电脑通道本身故障才停收。 */
    uint8_t byte;
    uint32_t at;
    unsigned budget = 32;
    while (budget-- && board_debug_read(&byte,&at)) {
        char response[RM_CONSOLE_REPLY_CAP];
        rm_console_action_t action;
        if (!rm_console_feed(&g_console,&g_app,byte,at,board_millis(),response,sizeof response,&action)) continue;
        if (action == RM_CONSOLE_STOP && !board_motor_send("$pwm:0,0,0,0#"))
            rm_servo_cal_fault(&g_app.servo_cal);
        (void)check_links();
        if (!g_console.failed) board_debug(response);
        (void)check_links(); /* 回复发送出错时，尚未写入的定位/步进马上撤销。 */
        break;
    }
}

int main(void)
{
    if (!board_init()) board_panic();
    if (!board_motor_send("$pwm:0,0,0,0#")) board_panic();
    rm_config_t config = rm_config_default();
    rm_app_init(&g_app,&config,board_millis());
    rm_console_init(&g_console);
    g_app.servo_hardware_ready = board_servo_cal_init();
    if (!g_app.servo_hardware_ready) rm_servo_cal_fault(&g_app.servo_cal);
    board_debug("\r\nversion=" RM_FIRMWARE_ID "\r\nSingle unloaded servo calibration; PWM starts only after confirm, arm and center.\r\n");
    (void)board_ps2_start(); /* 手柄只保留SELECT停车入口，PC标定不依赖手柄在线。 */
    uint32_t control_at = board_millis();
    g_app.servo_cal.last_step_ms = control_at;
    for (;;) {
        uint8_t byte;
        uint32_t at;
        unsigned budget = 128;
        while (budget-- && board_motor_read(&byte,&at)) { /* 清空驱动输入，不接受其运动指令。 */ }
        (void)check_links();
        poll_console();
        uint32_t now = board_millis();
        if ((uint32_t)(now-control_at) < 20u) continue;
        control_at = now;
        (void)board_ps2_read(&g_app.remote);
        if (g_app.remote.valid && g_app.remote.stop) rm_servo_cal_stop(&g_app.servo_cal);
        bool links_ok = check_links();
        rm_servo_cal_step(&g_app.servo_cal,board_millis(),links_ok);
        /* 写PWM前先发车轮零命令，再查一遍发送期间是否发生了故障。 */
        if (!board_motor_send("$pwm:0,0,0,0#")) rm_servo_cal_fault(&g_app.servo_cal);
        (void)check_links();
        uint32_t apply_at = board_millis();
        if ((uint32_t)(apply_at-g_app.servo_cal.last_step_ms) > 100u)
            rm_servo_cal_fault(&g_app.servo_cal);
        if (!board_servo_cal_apply(g_app.servo_cal.pulse_us,g_app.servo_cal.output_enabled))
            rm_servo_cal_fault(&g_app.servo_cal);
        else rm_servo_cal_commit(&g_app.servo_cal);
        g_app.safety.state = g_app.servo_cal.fault ? RM_FAULT :
            (g_app.servo_cal.armed ? RM_CALIBRATING : RM_DISARMED);
    }
}
#endif
