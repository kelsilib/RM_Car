/* 人工操作入口。车轮只发送零命令，S2/S3/S4按对应输入请求输出。 */
#include "board_hal.h"
#include "board_servo.h"
#include "console.h"
#include "firmware_version.h"
#if RM_SHOVEL_CONTROL
#if RM_SERVO_CAL_CHANNEL != 2 || !RM_ARM_WIRED || RM_BENCH_TEST || RM_FIELD_DRIVE
#error "S2 control requires its isolated S2 build"
#endif

rm_app_t g_app;
rm_console_t g_console;

static bool links_ok(void)
{
    if (board_debug_fault() || g_console.failed || board_uart_fault()) {
        if (board_debug_fault() && !g_console.failed) rm_console_fault(&g_console);
        rm_shovel_fault(&g_app.shovel);
    }
    return !g_app.shovel.fault;
}

static void poll_console(void)
{
    (void)links_ok();
    if (g_console.failed) return;
    uint8_t byte;
    uint32_t at;
    unsigned budget = 32;
    while (budget-- && board_debug_read(&byte,&at)) {
        char response[RM_CONSOLE_REPLY_CAP];
        rm_console_action_t action;
        if (!rm_console_feed(&g_console,&g_app,byte,at,board_millis(),response,sizeof response,&action)) continue;
        board_debug(response);
        (void)links_ok();
        break;
    }
}

int main(void)
{
    if (!board_init()) board_panic();
    rm_config_t config = rm_config_default();
    rm_app_init(&g_app,&config,board_millis());
    rm_console_init(&g_console);
#if RM_ARM_PAIR_CONTROL
    if (!board_servo_pair_init()) rm_shovel_fault(&g_app.shovel);
#else
    if (!board_servo_cal_init()) rm_shovel_fault(&g_app.shovel);
#endif
    if (!board_motor_send("$pwm:0,0,0,0#")) rm_shovel_fault(&g_app.shovel);
#if RM_ARM_PAIR_CONTROL
    board_debug("\r\nversion=" RM_FIRMWARE_ID "\r\nSTART enable; SQUARE S2; TRIANGLE S3; CIRCLE S4; D-pad UP/DOWN; SELECT stop.\r\n");
#else
    board_debug("\r\nversion=" RM_FIRMWARE_ID "\r\nSTART enable; SQUARE alternates down/up per press; release cancels PWM; SELECT stop.\r\n");
#endif
    (void)board_ps2_start();
    uint32_t control_at = board_millis();
    g_app.shovel.last_step_ms = control_at;
    for (;;) {
        uint8_t byte;
        uint32_t at;
        unsigned budget = 128;
        while (budget-- && board_motor_read(&byte,&at)) { /* 本固件只发送车轮PWM0。 */ }
        poll_console();
        uint32_t now = board_millis();
        if ((uint32_t)(now-control_at) < 20u) continue;
        control_at = now;
        if (!board_ps2_read(&g_app.remote)) g_app.remote.valid = false;
        rm_shovel_step(&g_app.shovel,&g_app.remote,board_millis(),links_ok());
#if RM_ARM_PAIR_CONTROL
        rm_joint_pair_step(&g_app.joint_pair,&g_app.shovel,&g_app.remote,board_millis());
#endif
        if (!board_motor_send("$pwm:0,0,0,0#")) rm_shovel_fault(&g_app.shovel);
        (void)links_ok();
        if ((uint32_t)(board_millis()-g_app.shovel.last_step_ms) > 100u)
            rm_shovel_fault(&g_app.shovel);
#if RM_ARM_PAIR_CONTROL
        if (!g_app.shovel.enabled) rm_joint_pair_stop(&g_app.joint_pair);
        const uint16_t pulses[3] = {g_app.shovel.pulse_us,
            g_app.joint_pair.pulse_us[0],g_app.joint_pair.pulse_us[1]};
        if (!board_servo_pair_apply(pulses)) {
            rm_shovel_fault(&g_app.shovel);
            rm_joint_pair_stop(&g_app.joint_pair);
            const uint16_t zero[3] = {0};
            if (board_servo_pair_apply(zero)) {
                rm_shovel_commit(&g_app.shovel);
                rm_joint_pair_commit(&g_app.joint_pair);
            }
        } else {
            rm_shovel_commit(&g_app.shovel);
            rm_joint_pair_commit(&g_app.joint_pair);
        }
#else
        uint16_t pulse = g_app.shovel.pulse_us;
        if (!board_servo_cal_apply(pulse,pulse != 0)) {
            rm_shovel_fault(&g_app.shovel);
            if (board_servo_cal_apply(0,false)) rm_shovel_commit(&g_app.shovel);
        } else rm_shovel_commit(&g_app.shovel);
#endif
        g_app.safety.state = g_app.shovel.fault ? RM_FAULT :
            (g_app.shovel.enabled ? RM_ARMED : RM_DISARMED);
    }
}
#endif
