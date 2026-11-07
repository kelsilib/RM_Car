/* 联调专用回归。所有驱动参数和计数都来自这里的PC夹具，不能作为实物验收。 */
#include "console.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do { ++checks; if (!(test)) { \
    fprintf(stderr, "FAIL bench line %d: %s\n", __LINE__, #test); return false; } } while (0)

typedef struct {
    rm_app_t app;
    rm_console_t console;
    uint32_t now;
    char frame[48], reply[RM_CONSOLE_REPLY_CAP];
    rm_console_action_t action;
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

static bool tick(fixture_t *f)
{
    f->now += 20;
    packet(f, "$MAll:1,2,3,4#");
    f->app.remote.valid = true;
    f->app.remote.at_ms = f->now;
    return rm_app_step(&f->app, f->now, false, f->frame, sizeof f->frame);
}

static bool init(fixture_t *f, uint32_t start)
{
    memset(f, 0, sizeof *f);
    f->now = start;
    rm_config_t c = rm_config_default();
    rm_app_init(&f->app, &c, start);
    rm_console_init(&f->console);
    CHECK(command(f, "bench on\n"));
    if (!RM_BENCH_TEST) {
        CHECK(strstr(f->reply, "ERR bench_not_in_this_build"));
        CHECK(f->app.bench.state == RM_BENCH_OFF);
        return true;
    }
    CHECK(strstr(f->reply, "OK bench state=1"));
    f->app.remote.stop = true;
    CHECK(tick(f));
    f->app.remote.stop = false;
    CHECK(command(f, "stop\n") && f->action == RM_CONSOLE_STOP);
    for (unsigned i = 0; i < 18; ++i) CHECK(tick(f));
    CHECK(f->app.bench.stop_checks == 3);
    CHECK(rm_config_issues(&f->app.config) == 0xBF);
    CHECK(!f->app.config.calibration_enabled && !f->app.config.remote_failsafe_verified);
    return true;
}

static bool setup_drive(fixture_t *f, uint32_t start)
{
    CHECK(init(f, start));
    CHECK(command(f, "bench dir 1 1\n"));
    CHECK(command(f, "bench dir 2 -1\n"));
    CHECK(command(f, "bench dir 3 -1\n"));
    CHECK(command(f, "bench dir 4 1\n"));
    packet(f, "Motor_Version:fixture\r\nMotor_type:1\r\nDead_Zone:0\r\n"
              "Pulse_Line:11\r\nPulse_Phase:30\r\nwheel_diameter:80.000\r\n");
    for (unsigned i = 0; i < 18; ++i) CHECK(tick(f));
    CHECK(command(f, "bench drive\n") && strstr(f->reply, "OK bench state=4"));
    for (unsigned i = 0; i < 18; ++i) CHECK(tick(f));
    f->app.remote.arm = true;
    CHECK(tick(f));
    CHECK(f->app.bench.state == RM_BENCH_DRIVE_ACTIVE);
    CHECK(!strcmp(f->frame, "$pwm:0,0,0,0#"));
    f->app.remote.arm = false;
    return true;
}

static bool test_default_closed(void)
{
    fixture_t f;
    CHECK(init(&f, 0));
    CHECK(command(&f, "bench\n") && strstr(f.reply, "bench built="));
    CHECK(strlen(f.reply) < sizeof f.reply-1);
    if (!RM_BENCH_TEST) {
        for (unsigned i = 0; i < 50; ++i) {
            f.app.remote.arm = (i & 1u) != 0;
            f.app.remote.forward = 1.0f;
            CHECK(tick(&f) && !strcmp(f.frame, "$pwm:0,0,0,0#"));
        }
    }
    return true;
}

static bool test_pulse(void)
{
    fixture_t f; CHECK(init(&f, UINT_MAX-100u));
    CHECK(!rm_app_cal_arm(&f.app, f.now));
    CHECK(command(&f, "bench pulse 1 50 200\n") && strstr(f.reply, "ERR"));
    CHECK(command(&f, "bench arm\n") && strstr(f.reply, "OK bench state=2"));
    CHECK(command(&f, "bench pulse 5 50 200\n") && strstr(f.reply, "ERR"));
    CHECK(command(&f, "bench pulse 1 101 200\n") && strstr(f.reply, "ERR"));
    CHECK(command(&f, "bench pulse 1 50 201\n") && strstr(f.reply, "ERR"));
    CHECK(command(&f, "bench pulse 1 0 200\n") && strstr(f.reply, "ERR"));
    CHECK(command(&f, "bench pulse 2 -50 20\n") && strstr(f.reply, "OK bench state=3"));
    CHECK(tick(&f) && !strcmp(f.frame, "$pwm:0,-50,0,0#"));
    CHECK(tick(&f) && !strcmp(f.frame, "$pwm:0,0,0,0#"));
    CHECK(f.app.bench.state == RM_BENCH_IDLE);
    CHECK(command(&f, "bench pulse 2 -50 20\n") && strstr(f.reply, "ERR"));
    for (unsigned i = 0; i < 18; ++i) CHECK(tick(&f));
    CHECK(command(&f, "bench arm\n") && strstr(f.reply, "OK"));
    for (unsigned i = 0; i < 250; ++i) CHECK(tick(&f));
    CHECK(f.app.bench.state == RM_BENCH_IDLE);
    return true;
}

static bool test_setup_gates(void)
{
    fixture_t f; CHECK(init(&f, 0));
    CHECK(command(&f, "bench drive\n") && strstr(f.reply, "ERR"));
    CHECK(command(&f, "bench dir 1 0\n") && strstr(f.reply, "ERR"));
    CHECK(f.app.bench.direction[0] == 0);
    CHECK(command(&f, "bench off\n") && f.app.bench.state == RM_BENCH_OFF);
    CHECK(command(&f, "bench on\n"));
    for (unsigned i = 0; i < 18; ++i) CHECK(tick(&f));
    CHECK(command(&f, "bench arm\n") && strstr(f.reply, "ERR")); /* 本次还没检查两条stop入口。 */
    CHECK(setup_drive(&f, 0));
    CHECK(command(&f, "driver read\n") && strstr(f.reply, "ERR"));
    CHECK(command(&f, "stop\n"));
    f.app.driver_info.wheel_diameter_mm = 67;
    for (unsigned i = 0; i < 18; ++i) CHECK(tick(&f));
    CHECK(command(&f, "bench drive\n") && strstr(f.reply, "ERR"));
    f.app.driver_info.wheel_diameter_mm = 80;
    f.app.driver_info.pulse_line = 0;
    CHECK(command(&f, "bench drive\n") && strstr(f.reply, "ERR"));
    CHECK(!f.app.config.mapping_verified && !f.app.config.motor_parameters_verified);
    return true;
}

static bool test_movement_patterns(void)
{
    /* 按M1/M2/M3/M4观察，套用本夹具实测方向后核对三种机体系运动。 */
    const int expected[3][RM_WHEELS] = {{1,-1,-1,1},{-1,-1,-1,-1},{1,1,-1,-1}};
    for (unsigned axis = 0; axis < 4; ++axis) {
        fixture_t f; CHECK(setup_drive(&f, 0));
        f.app.remote.buttons = RM_PS2_R2;
        if (axis == 0) f.app.remote.forward = 1;
        if (axis == 1) f.app.remote.left = 1;
        if (axis == 2) f.app.remote.yaw = 1;
        if (axis == 3) f.app.remote.forward = f.app.remote.left = f.app.remote.yaw = 1;
        for (unsigned i = 0; i < 25; ++i) CHECK(tick(&f));
        CHECK(f.app.last_speed_mode);
        for (unsigned i = 0; i < RM_WHEELS; ++i) {
            if (axis < 3) CHECK(f.app.last_channel[i]*expected[axis][i] > 0);
            CHECK(f.app.last_channel[i] <= 80 && f.app.last_channel[i] >= -80);
        }
        CHECK(hypotf(f.app.ramp.vx, f.app.ramp.vy) <= 0.05001f);
        CHECK(fabsf(f.app.ramp.wz) <= 0.20001f);
        /* 松R2必须在本轮输出PWM0，并撤销许可。 */
        f.app.remote.buttons = 0;
        CHECK(tick(&f) && !strcmp(f.frame, "$pwm:0,0,0,0#"));
        CHECK(f.app.bench.state == RM_BENCH_DRIVE_READY);
    }
    return true;
}

static bool test_pulse_guards(void)
{
    for (unsigned fault = 0; fault < 12; ++fault) {
        fixture_t f; CHECK(init(&f, 0));
        CHECK(command(&f, "bench arm\n") && strstr(f.reply, "OK"));
        CHECK(command(&f, "bench pulse 1 50 200\n") && strstr(f.reply, "OK"));
        f.now += 20;
        f.app.remote.at_ms = f.now; f.app.motor_at_ms = f.now;
        switch (fault) {
            case 0: f.app.remote.valid = false; break;
            case 1: f.app.remote.forward = 1; break;
            case 2: f.app.remote.arm = true; break;
            case 3: f.app.remote.stop = true; break;
            case 4: f.app.remote.buttons = RM_PS2_R2; break;
            case 5: f.app.remote.buttons = RM_PS2_L2; break;
            case 6: f.app.bench.channel = RM_WHEELS; break;
            case 7: f.app.bench.duration_ms = 201; break;
            case 8: f.app.bench.pwm = 101; break;
            case 9: f.app.last_step_ms = f.now-101; break;
            case 10: f.app.motor_at_ms -= 201; break;
            case 11: f.app.config.deadzone = NAN; break;
        }
        CHECK(rm_app_step(&f.app, f.now, false, f.frame, sizeof f.frame));
        CHECK(!strcmp(f.frame, "$pwm:0,0,0,0#") && f.app.bench.state == RM_BENCH_IDLE);
    }
    return true;
}

static bool test_window_and_rearm(void)
{
    fixture_t f; CHECK(setup_drive(&f, UINT_MAX-1000u));
    f.app.remote.forward = 1;
    f.app.remote.buttons = RM_PS2_R2;
    CHECK(tick(&f));
    uint32_t began = f.now;
    while ((uint32_t)(f.now-began) < RM_BENCH_DRIVE_MS) {
        CHECK(f.app.bench.state == RM_BENCH_DRIVE_ACTIVE);
        CHECK(tick(&f));
    }
    CHECK(!strcmp(f.frame, "$pwm:0,0,0,0#"));
    CHECK(f.app.bench.state == RM_BENCH_DRIVE_READY);
    f.app.remote.arm = true;
    for (unsigned i = 0; i < 80; ++i) CHECK(tick(&f) && !strcmp(f.frame, "$pwm:0,0,0,0#"));
    f.app.remote.forward = 0; f.app.remote.buttons = 0;
    for (unsigned i = 0; i < 18; ++i) CHECK(tick(&f));
    CHECK(f.app.bench.state == RM_BENCH_DRIVE_READY); /* START一直按着仍不解锁。 */
    f.app.remote.arm = false; CHECK(tick(&f));
    f.app.remote.arm = true; CHECK(tick(&f));
    CHECK(f.app.bench.state == RM_BENCH_DRIVE_ACTIVE);
    CHECK(command(&f, "stop\n") && f.app.bench.state == RM_BENCH_IDLE);
    CHECK(tick(&f) && !strcmp(f.frame, "$pwm:0,0,0,0#"));
    return true;
}

static bool test_faults(void)
{
    for (unsigned fault = 0; fault < 12; ++fault) {
        fixture_t f; CHECK(setup_drive(&f, 0));
        f.app.remote.buttons = RM_PS2_R2;
        f.app.remote.forward = 1;
        CHECK(tick(&f) && f.app.last_speed_mode);
        f.now += 20;
        f.app.remote.at_ms = f.now; f.app.motor_at_ms = f.now;
        bool estop = false;
        switch (fault) {
            case 0: f.app.remote.valid = false; break;
            case 1: f.app.remote.at_ms -= 201; break;
            case 2: f.app.motor_at_ms -= 201; break;
            case 3: rm_app_transport_fault(&f.app); break;
            case 4: f.app.last_step_ms = f.now-101; break;
            case 5: f.app.remote.stop = true; break;
            case 6: f.app.remote.buttons |= RM_PS2_L2; break;
            case 7: f.app.remote.forward = NAN; break;
            case 8: f.app.driver_info.wheel_diameter_mm = 67; break;
            case 9: f.app.bench.direction[2] = 0; break;
            case 10: f.app.config.channel[1] = f.app.config.channel[0]; break;
            case 11: estop = true; break;
        }
        CHECK(rm_app_step(&f.app, f.now, estop, f.frame, sizeof f.frame));
        CHECK(!strcmp(f.frame, "$pwm:0,0,0,0#"));
        CHECK(f.app.bench.state == RM_BENCH_IDLE);
    }
    return true;
}

int main(void)
{
    if (!test_default_closed()) return 1;
    if (RM_BENCH_TEST && (!test_pulse() || !test_setup_gates() || !test_movement_patterns() ||
        !test_window_and_rearm() || !test_faults() || !test_pulse_guards())) return 1;
    printf("PASS bench built=%u checks=%u\n", (unsigned)RM_BENCH_TEST, checks);
    return 0;
}
