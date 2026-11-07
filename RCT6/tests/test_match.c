/* 比赛操作回归，参数全部是PC夹具。这里只验证软件请求，实车端点仍待标定。 */
#include "console.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do { ++checks; if (!(test)) { \
    fprintf(stderr,"FAIL match line %d: %s\n",__LINE__,#test); return false; } } while (0)

typedef struct {
    rm_app_t app;
    rm_console_t console;
    uint32_t now;
    char frame[48];
} rig_t;

static void packet(rig_t *rig, const char *text)
{
    while (*text) rm_app_receive(&rig->app, (uint8_t)*text++, rig->now);
}

static void init(rig_t *rig, uint32_t now)
{
    memset(rig, 0, sizeof *rig);
    rig->now = now;
    rm_config_t config = rm_config_default();
    config.ps2_wiring_verified = config.remote_failsafe_verified = true;
    rm_app_init(&rig->app, &config, now);
    rm_console_init(&rig->console);
    packet(rig, "Motor_Version:fixture\nMotor_type:1\nDead_Zone:1600\n"
                "Pulse_Line:11\nPulse_Phase:30\nwheel_diameter:80\n");
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
    arm.shovel_poses_verified = true;
    arm.shovel_scoop_us = 1700;
    arm.shovel_store_us = 1300;
    arm.servo[5].toggle_poses_verified = true;
    arm.servo[5].pose_a_us = 1750;
    arm.servo[5].pose_b_us = 1250;
    rm_arm_init(&rig->app.arm, &arm, now);
    rig->app.servo_hardware_ready = true;
}

static bool tick(rig_t *rig)
{
    rig->now += 20;
    rig->app.remote.at_ms = rig->now;
    rig->app.remote.valid = true;
    packet(rig, "$MAll:1,2,3,4#");
    CHECK(rm_app_step(&rig->app, rig->now, false, rig->frame, sizeof rig->frame));
    if (rig->app.arm.mode || rig->app.arm.automatic_mask)
        CHECK(!strcmp(rig->frame, "$pwm:0,0,0,0#"));
    rm_arm_commit(&rig->app.arm);
    return true;
}

static bool enable(rig_t *rig)
{
    rig->app.remote = (rm_remote_t){0};
    for (unsigned i = 0; i < 18; ++i) CHECK(tick(rig));
    rig->app.remote.arm = true;
    CHECK(tick(rig));
    rig->app.remote.arm = false;
    CHECK(tick(rig));
    CHECK(rig->app.bench.state == RM_BENCH_DRIVE_ACTIVE);
    return true;
}

static bool pose_toggle_and_hold(void)
{
    rig_t rig;
    init(&rig, UINT_MAX-160u);
    rig.app.remote.buttons = RM_PS2_TRIANGLE;
    for (unsigned i = 0; i < 20; ++i) CHECK(tick(&rig));
    CHECK(!rig.app.arm.output_mask); /* 未启用时按动作键，不能产生输出。 */
    CHECK(enable(&rig));
    CHECK(!rig.app.arm.output_mask); /* 整车START只启用，舵机不会一起回中。 */
    rig.app.remote.buttons = RM_PS2_TRIANGLE;
    for (unsigned i = 0; i < 4; ++i) CHECK(tick(&rig));
    CHECK(rig.app.arm.pulse_us[0] == 1580 && rig.app.arm.automatic_target_us[0] == 1800);
    rig.app.remote.buttons = 0;
    for (unsigned i = 0; i < 20; ++i) CHECK(tick(&rig));
    CHECK(rig.app.arm.pulse_us[0] == 1800 && !rig.app.arm.automatic_mask);
    rig.app.remote.buttons = RM_PS2_TRIANGLE;
    CHECK(tick(&rig));
    CHECK(rig.app.arm.automatic_target_us[0] == 1200 && rig.app.arm.pulse_us[0] == 1780);
    for (unsigned i = 0; i < 40; ++i) CHECK(tick(&rig));
    CHECK(rig.app.arm.pulse_us[0] == 1200 && !rig.app.arm.automatic_mask);
    rig.app.remote.buttons = 0;
    CHECK(tick(&rig));
    rig.app.remote.buttons = RM_PS2_CIRCLE;
    CHECK(tick(&rig));
    CHECK(rig.app.arm.automatic_target_us[1] == 1700);
    rig.app.remote.buttons = 0;
    CHECK(tick(&rig));
    rig.app.remote.buttons = RM_PS2_CROSS;
    CHECK(tick(&rig));
    CHECK(rig.app.arm.automatic_mask == (1u << 5));
    CHECK(rig.app.arm.automatic_target_us[5] == 1750 && rig.app.arm.pulse_us[1] == 1540);
    rig.app.remote.buttons = 0;
    CHECK(tick(&rig));
    rig.app.remote.buttons = RM_PS2_CROSS;
    CHECK(tick(&rig));
    CHECK(rig.app.arm.automatic_target_us[5] == 1250); /* 行程中再按，立即反向。 */
    return true;
}

static bool manual_joints_and_drive(void)
{
    rig_t rig;
    init(&rig, 0);
    CHECK(enable(&rig));
    rig.app.remote.buttons = RM_PS2_L2;
    rig.app.remote.right_y = 1;
    CHECK(tick(&rig));
    CHECK(rig.app.arm.selected == 2 && rig.app.arm.pulse_us[2] == 1520);
    rig.app.remote.buttons |= RM_PS2_L1;
    CHECK(tick(&rig));
    CHECK(rig.app.arm.selected == 3 && !rig.app.arm.pulse_us[3]);
    rig.app.remote.buttons = RM_PS2_L2;
    CHECK(tick(&rig));
    CHECK(rig.app.arm.pulse_us[3] == 1520 && rig.app.bench.state == RM_BENCH_DRIVE_ACTIVE);
    rig.app.remote.buttons |= RM_PS2_R1;
    CHECK(tick(&rig));
    rig.app.remote.buttons = RM_PS2_L2;
    CHECK(tick(&rig));
    CHECK(rig.app.arm.selected == 4 && rig.app.arm.pulse_us[4] == 1520);
    rig.app.remote.buttons = 0;
    rig.app.remote.right_y = 0;
    CHECK(tick(&rig));
    CHECK(rig.app.arm.pulse_us[4] == 1520);
    rig.app.remote.buttons = RM_PS2_R2;
    rig.app.remote.forward = 1;
    CHECK(tick(&rig) && rig.app.last_speed_mode);
    rig.app.remote.arm = true;
    CHECK(tick(&rig) && !rig.app.last_speed_mode && !rig.app.arm.armed);
    for (unsigned i = 0; i < 30; ++i) CHECK(tick(&rig) && !rig.app.last_speed_mode);
    return true;
}

static bool pose_guards_and_throttle(void)
{
    rig_t rig;
    init(&rig, 0);
    CHECK(enable(&rig));
    rig.app.remote.buttons = RM_PS2_R2 | RM_PS2_TRIANGLE;
    CHECK(tick(&rig) && !rig.app.arm.output_mask);
    rig.app.remote.buttons = RM_PS2_TRIANGLE;
    CHECK(tick(&rig) && !rig.app.arm.output_mask); /* 松油门时仍按住旧动作键，不会突然启动。 */
    rig.app.remote.buttons = 0;
    CHECK(tick(&rig));
    rig.app.remote.buttons = RM_PS2_TRIANGLE;
    CHECK(tick(&rig) && rig.app.arm.automatic_mask == 1);
    rig.app.remote.buttons = RM_PS2_R2;
    rig.app.remote.forward = 1;
    CHECK(tick(&rig) && !rig.app.arm.automatic_mask && !rig.app.last_speed_mode);
    for (unsigned i = 0; i < 20; ++i) CHECK(tick(&rig) && !rig.app.last_speed_mode);
    rig.app.remote.buttons = 0;
    CHECK(tick(&rig));
    rig.app.remote.buttons = RM_PS2_R2;
    CHECK(tick(&rig) && rig.app.last_speed_mode);
    rig.app.remote.buttons = 0;
    rig.app.remote.forward = 0;
    CHECK(tick(&rig));
    rig.app.arm.config.servo[0].pose_a_us = 2200;
    rig.app.remote.buttons = RM_PS2_TRIANGLE;
    CHECK(tick(&rig) && rig.app.arm.inhibit == RM_ARM_INHIBIT_POSE);
    CHECK(!rig.app.arm.automatic_mask);
    uint16_t a, b;
    CHECK(!rm_arm_toggle_poses(NULL, 0, &a, &b));
    CHECK(!rm_arm_toggle_poses(&rig.app.arm.config, 5, NULL, &b));
    CHECK(!rm_arm_toggle_poses(&rig.app.arm.config, 2, &a, &b));
    rig.app.arm.config.servo[5].pose_b_us = rig.app.arm.config.servo[5].pose_a_us;
    CHECK(!rm_arm_toggle_poses(&rig.app.arm.config, 5, &a, &b));
    return true;
}

static bool faults_cancel_and_queries(void)
{
    for (unsigned fault = 0; fault < 11; ++fault) {
        rig_t rig;
        init(&rig, 0);
        CHECK(enable(&rig));
        rig.app.remote.buttons = RM_PS2_TRIANGLE;
        CHECK(tick(&rig));
        uint16_t frozen = rig.app.arm.applied_us[0];
        uint32_t now = rig.now+20;
        rig.app.remote.at_ms = rig.app.motor_at_ms = now;
        rig.app.remote.buttons = 0;
        if (fault == 0) rig.app.remote.valid = false;
        if (fault == 1) rig.app.remote.at_ms = now-201;
        if (fault == 2) rig.app.motor_at_ms = now-201;
        if (fault == 3) rm_app_transport_fault(&rig.app);
        if (fault == 4) rig.app.last_step_ms = now-101;
        if (fault == 5) rig.app.arm.config.power_verified = false;
        if (fault == 6) rig.app.servo_hardware_ready = false;
        if (fault == 7) rm_arm_fault(&rig.app.arm);
        if (fault == 8) rig.app.remote.stop = true;
        if (fault == 10) rig.app.remote.right_y = NAN;
        CHECK(rm_app_step(&rig.app, now, fault == 9, rig.frame, sizeof rig.frame));
        CHECK(!strcmp(rig.frame, "$pwm:0,0,0,0#"));
        CHECK(!rig.app.arm.automatic_mask && rig.app.arm.pulse_us[0] == frozen);
    }
    rig_t rig;
    init(&rig, 0);
    CHECK(enable(&rig));
    rig.app.remote.buttons = RM_PS2_TRIANGLE;
    CHECK(tick(&rig));
    char reply[RM_CONSOLE_REPLY_CAP];
    rm_console_action_t action;
    const char *queries[] = {"poses 1\n", "poses 2\n", "poses 6\n", "poses 7\n", "arm\n", "stop\n"};
    for (unsigned i = 0; i < 6; ++i) {
        bool replied = false;
        for (const char *cursor = queries[i]; *cursor; ++cursor)
            replied = rm_console_feed(&rig.console, &rig.app, (uint8_t)*cursor,
                rig.now, rig.now, reply, sizeof reply, &action) || replied;
        CHECK(replied && strstr(reply, "\r\n"));
        if (i < 3) CHECK(strstr(reply, "ready=1"));
        if (i == 3) CHECK(strstr(reply, "ready=0"));
        if (i == 4) CHECK(strstr(reply, "auto=0x01"));
    }
    CHECK(!rig.app.arm.automatic_mask && !rig.app.arm.armed);
    uint16_t frozen = rig.app.arm.applied_us[0];
    rig.app.remote.buttons = 0;
    for (unsigned i = 0; i < 20; ++i) CHECK(tick(&rig));
    CHECK(rig.app.arm.pulse_us[0] == frozen);
    return true;
}

int main(void)
{
    if (!pose_toggle_and_hold() || !manual_joints_and_drive() ||
        !pose_guards_and_throttle() || !faults_cancel_and_queries()) return 1;
    printf("PASS match: %u checks (PC only)\n", checks);
    return 0;
}
