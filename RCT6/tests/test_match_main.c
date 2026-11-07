/* 真实main的比赛模式模拟，检查先停车再动机构及写入前故障撤销。 */
#include "board_hal.h"
#include "board_servo.h"
#include "chassis_app.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

extern int rm_firmware_main(void);
extern rm_app_t g_app;
static jmp_buf done;
static unsigned now, scenario, next_feedback, flash_position, feedback_position;
static bool subscribed, unsafe, energized, moved_lift, moved_shoulder, moved_elbow, moved_car, stopped;
static uint16_t hardware[RM_SERVOS], frozen[RM_SERVOS];
static char feedback[64];
static const char flash[] = "Motor_Version:fixture\nMotor_type:1\nDead_Zone:1600\n"
                            "Pulse_Line:11\nPulse_Phase:30\nwheel_diameter:80\n";

static void mark_stop(void)
{
    if (!stopped) memcpy(frozen, hardware, sizeof frozen);
    stopped = true;
}

bool board_init(void) { return true; }
uint32_t board_millis(void)
{
    if (++now >= 3100) longjmp(done, 1);
    return now;
}
void board_panic(void) { longjmp(done, 2); }
bool board_ps2_start(void) { return true; }
bool board_ps2_read(rm_remote_t *remote)
{
    *remote = (rm_remote_t){.valid=true,.at_ms=now};
    remote->arm = now >= 500 && now < 540;
    if ((now >= 620 && now < 660) || (now >= 1000 && now < 1040) ||
        (now >= 2450 && now < 2490)) remote->buttons = RM_PS2_TRIANGLE;
    if (now >= 1650 && now < 1850) {
        remote->buttons = RM_PS2_L2;
        remote->right_y = 1;
    }
    if (now >= 1850 && now < 1890) remote->buttons = RM_PS2_L2 | RM_PS2_L1;
    if (now >= 1890 && now < 2100) {
        remote->buttons = RM_PS2_L2;
        remote->right_y = 1;
    }
    if (now >= 2200 && now < 2400) {
        remote->buttons = RM_PS2_R2;
        remote->forward = 1;
    }
    if (now >= 2700) {
        remote->stop = true;
        mark_stop();
    }
    if (scenario == 3 && now >= 700) { remote->valid = false; mark_stop(); }
    return true;
}
bool board_motor_send(const char *frame)
{
    ++now;
    if (!strcmp(frame, "$read_flash#")) flash_position = 0;
    if (!strcmp(frame, "$upload:1,0,1#")) subscribed = true;
    int values[4];
    if (sscanf(frame, "$spd:%d,%d,%d,%d#", &values[0], &values[1], &values[2], &values[3]) == 4 &&
        (values[0] || values[1] || values[2] || values[3])) {
        moved_car = true;
        if (stopped || g_app.arm.mode || g_app.arm.automatic_mask || now < 2200 || now >= 2430) unsafe = true;
    }
    if (scenario == 1 && now >= 700) { mark_stop(); return false; }
    return true;
}
bool board_motor_read(uint8_t *byte, uint32_t *at)
{
    if (flash_position < sizeof flash-1) *byte = (uint8_t)flash[flash_position++];
    else {
        if (!feedback[feedback_position]) {
            if (!subscribed || now < next_feedback) return false;
            if (scenario == 5 && now >= 700) return false;
            next_feedback = now+20;
            (void)snprintf(feedback, sizeof feedback, "$MAll:1,2,3,4#$MSPD:0,0,0,0#");
            feedback_position = 0;
        }
        *byte = (uint8_t)feedback[feedback_position++];
    }
    *at = now;
    return true;
}
bool board_uart_fault(void) { return false; }
bool board_debug_fault(void)
{
    if (scenario == 4 && now >= 700) { mark_stop(); return true; }
    return false;
}
bool board_debug_read(uint8_t *byte, uint32_t *at) { (void)byte; (void)at; return false; }
void board_debug(const char *text) { (void)text; ++now; }
bool board_servo_init(const rm_arm_config_t *config)
{
    (void)config;
    rm_arm_config_t arm = {.enabled=true,.wiring_verified=true,.power_verified=true,
        .base_safe_verified=true,.fault_hold_verified=true,.period_us=20000};
    for (unsigned i = 0; i < RM_SERVOS; ++i) {
        arm.servo[i] = (rm_servo_config_t){.enabled=true,.limits_verified=true,
            .start_pose_verified=true,.min_us=1000,.max_us=2000,.start_us=1500,
            .max_rate_us_s=1000,.direction=1};
    }
    arm.servo[0].toggle_poses_verified = true;
    arm.servo[0].pose_a_us = 1800;
    arm.servo[0].pose_b_us = 1200;
    rm_arm_init(&g_app.arm, &arm, now);
    g_app.config.ps2_wiring_verified = g_app.config.remote_failsafe_verified = true;
    return true;
}
bool board_servo_apply(const uint16_t pulse[RM_SERVOS], uint8_t mask)
{
    if (scenario == 2 && now >= 700) { mark_stop(); return false; }
    if (scenario == 5 && now >= 930) mark_stop();
    for (unsigned i = 0; i < RM_SERVOS; ++i) {
        if (!(mask & (1u << i))) continue;
        if (now < 620 || (stopped && pulse[i] != frozen[i])) unsafe = true;
        if (pulse[i] < 1000 || pulse[i] > 2000) unsafe = true;
        if (pulse[i] != hardware[i]) {
            if (i == 0) moved_lift = true;
            if (i == 2) moved_shoulder = true;
            if (i == 3) moved_elbow = true;
        }
        hardware[i] = pulse[i];
        energized = true;
    }
    return true;
}

int main(void)
{
    for (scenario = 0; scenario < 6; ++scenario) {
        now = next_feedback = feedback_position = 0;
        flash_position = sizeof flash-1;
        feedback[0] = '\0';
        subscribed = unsafe = energized = moved_lift = moved_shoulder = moved_elbow = moved_car = stopped = false;
        memset(hardware, 0, sizeof hardware);
        memset(frozen, 0, sizeof frozen);
        int result = setjmp(done);
        if (!result) (void)rm_firmware_main();
        if (result != 1 || unsafe || !energized || !moved_lift || !subscribed || !stopped ||
            (scenario == 0 && (!moved_shoulder || !moved_elbow || !moved_car)) ||
            (scenario != 0 && moved_car) || g_app.arm.automatic_mask) {
            fprintf(stderr,"FAIL match main scenario=%u result=%d unsafe=%u energized=%u shoulder=%u elbow=%u car=%u\n",
                scenario,result,unsafe,energized,moved_shoulder,moved_elbow,moved_car);
            return 1;
        }
        printf("PASS match main scenario=%u\n",scenario);
    }
    return 0;
}
