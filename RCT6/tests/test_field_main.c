/* 运行真实main，用PC假设备检查上电禁动、速度订阅、连续R2和故障停车。 */
#include "board_hal.h"
#include "board_servo.h"
#include "chassis_app.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

extern int rm_firmware_main(void);
extern rm_app_t g_app;
static jmp_buf finish;
static unsigned now, scenario, next_feedback, flash_position, feedback_position;
static bool subscribed, unsafe, continuous_seen, arm_mode_seen, moving_seen;
static char feedback[96];
static const char flash[] = "Motor_Version:fixture\nMotor_type:1\nDead_Zone:1600\n"
                            "Pulse_Line:11\nPulse_Phase:30\nwheel_diameter:80.000\n";

bool board_init(void) { return true; }
uint32_t board_millis(void)
{
    if (++now >= 3500) longjmp(finish, 1);
    return now;
}
void board_panic(void) { longjmp(finish, 2); }
bool board_ps2_start(void) { return true; }
bool board_ps2_read(rm_remote_t *remote)
{
    *remote = (rm_remote_t){.valid=true,.at_ms=now};
    remote->arm = (now >= 500 && now < 540) || (now >= 2100 && now < 2140);
    if ((now >= 540 && now < 1800) || (now >= 2140 && now < 2900)) {
        remote->forward = 1;
        remote->buttons = RM_PS2_R2;
    }
    if (now >= 2400 && now < 2500) remote->buttons |= RM_PS2_L2;
    return true;
}
bool board_motor_send(const char *frame)
{
    ++now;
    if (!strcmp(frame, "$read_flash#")) flash_position = 0;
    if (!strcmp(frame, "$upload:1,0,1#")) subscribed = true;
    int channel[4];
    if (sscanf(frame, "$spd:%d,%d,%d,%d#", &channel[0], &channel[1], &channel[2], &channel[3]) == 4 &&
        (channel[0] || channel[1] || channel[2] || channel[3])) {
        moving_seen = true;
        if (now < 500 || (now >= 1830 && now < 2100) || now >= 2430) unsafe = true;
        if ((scenario == 1 || scenario == 3) && now >= 1030) unsafe = true;
        if (scenario == 2 && now >= 1240) unsafe = true;
        if (now > 1600 && now < 1790) continuous_seen = true;
        if (channel[0] < 0 || channel[1] > 0 || channel[2] > 0 || channel[3] < 0) unsafe = true;
    }
    return true;
}
bool board_motor_read(uint8_t *byte, uint32_t *at)
{
    if (flash_position < sizeof flash-1) {
        *byte = (uint8_t)flash[flash_position++];
    } else {
        if (!feedback[feedback_position]) {
            if (!subscribed || now < next_feedback) return false;
            next_feedback = now+20;
            const char *counts = scenario == 2 && now >= 1000 ? "" : "$MAll:1,2,3,4#";
            (void)snprintf(feedback, sizeof feedback, "%s$MSPD:50,-50,-50,50#", counts);
            feedback_position = 0;
        }
        *byte = (uint8_t)feedback[feedback_position++];
    }
    *at = now;
    return true;
}
bool board_uart_fault(void) { return scenario == 1 && now >= 1000; }
bool board_debug_fault(void) { return scenario == 3 && now >= 1000; }
bool board_debug_read(uint8_t *byte, uint32_t *at) { (void)byte; (void)at; return false; }
void board_debug(const char *text) { (void)text; ++now; }
bool board_servo_init(const rm_arm_config_t *config) { (void)config; return false; }
bool board_servo_apply(const uint16_t pulse[RM_SERVOS], uint8_t mask)
{
    (void)pulse;
    if (mask) unsafe = true;
    if (g_app.arm.mode) arm_mode_seen = true;
    return mask == 0;
}

int main(void)
{
    for (scenario = 0; scenario < 4; ++scenario) {
        now = next_feedback = feedback_position = 0;
        flash_position = sizeof flash-1;
        feedback[0] = '\0';
        subscribed = unsafe = continuous_seen = arm_mode_seen = moving_seen = false;
        int result = setjmp(finish);
        if (!result) (void)rm_firmware_main();
        if (result != 1 || unsafe || !subscribed || !moving_seen || !g_app.motor_speed_seen ||
            (scenario == 0 && (!continuous_seen || !arm_mode_seen)) ||
            ((scenario == 1 || scenario == 3) && !g_app.transport_fault)) {
            fprintf(stderr, "FAIL field main scenario=%u result=%d unsafe=%u continuous=%u arm=%u\n",
                scenario, result, unsafe, continuous_seen, arm_mode_seen);
            return 1;
        }
        printf("PASS field main scenario=%u\n", scenario);
    }
    return 0;
}
