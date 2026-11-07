/* 行走构建回归。这里的驱动回复和舵机许可是PC夹具，不能作为实物验收。 */
#include "console.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do { ++checks; if (!(test)) { \
    fprintf(stderr, "FAIL field line %d: %s\n", __LINE__, #test); return false; } } while (0)

typedef struct {
    rm_app_t app;
    rm_console_t console;
    rm_console_action_t action;
    uint32_t now;
    char frame[48], reply[RM_CONSOLE_REPLY_CAP];
} fixture_t;

static void packet(fixture_t *f, const char *text)
{
    while (*text) rm_app_receive(&f->app, (uint8_t)*text++, f->now);
}

static bool command(fixture_t *f, const char *text)
{
    bool replied = false;
    while (*text) replied = rm_console_feed(&f->console, &f->app, (uint8_t)*text++,
        f->now, f->now, f->reply, sizeof f->reply, &f->action) || replied;
    return replied;
}

static void init(fixture_t *f, uint32_t now)
{
    memset(f, 0, sizeof *f);
    f->now = now;
    rm_config_t config = rm_config_default();
    rm_app_init(&f->app, &config, now);
    rm_console_init(&f->console);
    packet(f, "Motor_Version:fixture\nMotor_type:1\nDead_Zone:1600\n"
              "Pulse_Line:11\nPulse_Phase:30\nwheel_diameter:80.000\n");
}

static bool tick(fixture_t *f)
{
    f->now += 20;
    packet(f, "$MAll:1,2,3,4#");
    f->app.remote.valid = true;
    f->app.remote.at_ms = f->now;
    return rm_app_step(&f->app, f->now, false, f->frame, sizeof f->frame);
}

static bool arm(fixture_t *f)
{
    f->app.remote.forward = f->app.remote.left = f->app.remote.yaw = 0;
    f->app.remote.buttons = 0;
    f->app.remote.arm = f->app.remote.stop = false;
    for (unsigned i = 0; i < 18; ++i) CHECK(tick(f));
    f->app.remote.arm = true;
    CHECK(tick(f));
    CHECK(f->app.bench.state == RM_BENCH_DRIVE_ACTIVE);
    CHECK(!strcmp(f->frame, "$pwm:0,0,0,0#"));
    f->app.remote.arm = false;
    f->app.remote.buttons = RM_PS2_R2;
    f->app.remote.forward = 1;
    return true;
}

static bool startup_and_continuous(void)
{
    fixture_t f;
    init(&f, UINT_MAX-200u);
    CHECK(f.app.bench.state == RM_BENCH_IDLE && f.app.bench.stop_checks == 0);
    CHECK(f.app.bench.direction[0] == 1 && f.app.bench.direction[1] == -1 &&
          f.app.bench.direction[2] == -1 && f.app.bench.direction[3] == 1);
    CHECK(rm_config_issues(&f.app.config) == 0xBF);
    CHECK(!f.app.config.remote_failsafe_verified && !f.app.config.geometry_verified);
    f.app.remote.arm = true;
    f.app.remote.buttons = RM_PS2_R2;
    f.app.remote.forward = 1;
    for (unsigned i = 0; i < 40; ++i) {
        CHECK(tick(&f));
        CHECK(!strcmp(f.frame, "$pwm:0,0,0,0#"));
    }
    CHECK(arm(&f));
    for (unsigned i = 0; i < 250; ++i) {
        CHECK(tick(&f));
        CHECK(f.app.bench.state == RM_BENCH_DRIVE_ACTIVE);
        if (i >= 20) CHECK(!strcmp(f.frame, "$spd:50,-50,-50,50#"));
    }
    f.app.remote.buttons = 0;
    CHECK(tick(&f) && !strcmp(f.frame, "$pwm:0,0,0,0#"));
    CHECK(f.app.bench.state == RM_BENCH_DRIVE_ACTIVE);
    f.app.remote.buttons = RM_PS2_R2;
    for (unsigned i = 0; i < 20; ++i) CHECK(tick(&f) && f.app.last_speed_mode);
    /* 正在给油且摇杆未回中时，第二次START也必须立即停用。 */
    f.app.remote.arm = true;
    CHECK(tick(&f) && !f.app.last_speed_mode);
    CHECK(f.app.bench.state == RM_BENCH_DRIVE_READY);
    for (unsigned i = 0; i < 30; ++i) CHECK(tick(&f) && !f.app.last_speed_mode);
    CHECK(arm(&f) && tick(&f) && f.app.last_speed_mode);
    f.app.remote.stop = true;
    CHECK(tick(&f) && !strcmp(f.frame, "$pwm:0,0,0,0#"));
    CHECK(f.app.bench.stop_checks == RM_BENCH_SELECT_SEEN);
    CHECK(command(&f, "stop\n") && f.action == RM_CONSOLE_STOP);
    CHECK(f.app.bench.stop_checks == 3);
    CHECK(command(&f, "bench\n") && strstr(f.reply, "drive_ms=0 field=1"));
    CHECK(command(&f, "bench off\n"));
    f.app.remote.stop = false;
    for (unsigned i = 0; i < 20; ++i) CHECK(tick(&f) && !f.app.last_speed_mode);
    CHECK(f.app.bench.state == RM_BENCH_OFF);
    return true;
}

static bool faults_and_interlock(void)
{
    for (unsigned fault = 0; fault < 9; ++fault) {
        fixture_t f;
        init(&f, 0);
        CHECK(arm(&f) && tick(&f) && f.app.last_speed_mode);
        uint32_t now = f.now + 20;
        switch (fault) {
            case 0: f.app.remote.valid = false; break;
            case 1: f.app.remote.at_ms = now-201; break;
            case 2: f.app.motor_at_ms = now-201; break;
            case 3: rm_app_transport_fault(&f.app); break;
            case 4: f.app.last_step_ms = now-101; break;
            case 5: f.app.driver_info.wheel_diameter_mm = 67; break;
            case 6: f.app.driver_info.pulse_line = 13; break;
            case 7: f.app.remote.stop = true; break;
            case 8: f.app.remote.buttons |= RM_PS2_L2; break;
        }
        CHECK(rm_app_step(&f.app, now, false, f.frame, sizeof f.frame));
        CHECK(!strcmp(f.frame, "$pwm:0,0,0,0#"));
        CHECK(!f.app.arm.output_mask);
        if (fault == 8) {
            CHECK(f.app.arm.mode);
            f.app.remote.buttons = RM_PS2_R2;
            CHECK(tick(&f) && !f.app.arm.mode && !f.app.last_speed_mode);
            CHECK(tick(&f) && !f.app.last_speed_mode);
            CHECK(f.app.bench.state == RM_BENCH_DRIVE_ACTIVE);
            f.app.remote.buttons = 0;
            CHECK(tick(&f));
            f.app.remote.buttons = RM_PS2_R2;
            CHECK(tick(&f) && f.app.last_speed_mode); /* 退出L2不需要重新按START，先松一次R2。 */
        }
    }
    fixture_t f;
    init(&f, 0);
    CHECK(arm(&f));
    f.app.remote.buttons = 0;
    f.app.remote.forward = 0;
    for (unsigned i = 0; i < 251; ++i) CHECK(tick(&f));
    CHECK(f.app.bench.state == RM_BENCH_DRIVE_ACTIVE);
    f.app.remote.buttons = RM_PS2_R2;
    f.app.remote.forward = 1;
    CHECK(tick(&f) && f.app.last_speed_mode);
    return true;
}

static bool speed_diagnostics_and_servo_candidates(void)
{
    fixture_t f;
    init(&f, 0);
    CHECK(!f.app.motor_seen && !f.app.motor_speed_seen);
    packet(&f, "$MSPD:49.6,-47.2,-51.1,48.7#");
    CHECK(f.app.motor_speed_seen && !f.app.motor_seen);
    CHECK(command(&f, "wheels\n") && strstr(f.reply, "actual_mm_s=50,-47,-51,49"));
    CHECK(strlen(f.reply) < sizeof f.reply-1);
    CHECK(command(&f, "wheels read\n") && f.action == RM_CONSOLE_READ_SPEED);
    CHECK(arm(&f) && tick(&f));
    CHECK(command(&f, "wheels read\n") && strstr(f.reply, "ERR stop_first"));
    CHECK(command(&f, "wheels\n") && strstr(f.reply, "target_mm_s=3,-3,-3,3"));
    CHECK(command(&f, "stop\n"));
    rm_app_transport_fault(&f.app);
    CHECK(command(&f, "wheels read\n") && strstr(f.reply, "ERR stop_first"));
    rm_arm_config_t config = rm_arm_config_default();
    CHECK(config.period_us == 20000 && !config.enabled && rm_arm_config_issues(&config));
    for (unsigned i = 0; i < 6; ++i) {
        CHECK(config.servo[i].min_us == 1400 && config.servo[i].max_us == 1600);
        CHECK(config.servo[i].start_us == 1500 && config.servo[i].max_rate_us_s == 100);
        CHECK(!config.servo[i].enabled && !config.servo[i].limits_verified &&
              !config.servo[i].start_pose_verified);
    }
    CHECK(config.servo[6].max_us == 0);
    CHECK(!rm_arm_shovel_poses_ready(&config) && !config.shovel_poses_verified);
    return true;
}

int main(void)
{
    if (!startup_and_continuous() || !faults_and_interlock() ||
        !speed_diagnostics_and_servo_candidates()) return 1;
    printf("PASS field: %u checks (PC only)\n", checks);
    return 0;
}
