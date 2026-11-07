/* 标定状态机和终端回归，所有许可、时间及舵机写入都是PC模拟。 */
#include "console.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do { ++checks; if (!(test)) { \
    fprintf(stderr,"FAIL servo cal line %d: %s\n",__LINE__,#test); return false; } } while (0)

static bool arm_and_center(rm_servo_cal_t *cal, unsigned channel, uint32_t now)
{
    rm_servo_cal_init(cal,channel,now);
    CHECK(!cal->output_enabled && !cal->pulse_us);
    CHECK(!rm_servo_cal_arm(cal,now,true) && !rm_servo_cal_center(cal,now));
    CHECK(rm_servo_cal_confirm(cal));
    CHECK(!rm_servo_cal_arm(cal,now,false));
    CHECK(rm_servo_cal_arm(cal,now,true));
    CHECK(rm_servo_cal_center(cal,now));
    CHECK(!rm_servo_cal_move(cal,now,10)); /* 尚未写入首次定位时不能接着加步进。 */
    rm_servo_cal_commit(cal);
    CHECK(cal->applied_enabled && cal->applied_us == 1500);
    return true;
}

static bool lifecycle(void)
{
    for (unsigned channel = 1; channel <= 6; ++channel) {
        rm_servo_cal_t cal;
        uint32_t now = UINT_MAX-40u;
        CHECK(arm_and_center(&cal,channel,now));
        CHECK(!rm_servo_cal_center(&cal,now));
        CHECK(!rm_servo_cal_move(&cal,now,0) && !rm_servo_cal_move(&cal,now,21));
        CHECK(rm_servo_cal_move(&cal,now,10));
        CHECK(!rm_servo_cal_move(&cal,now,10));
        for (unsigned i = 0; i < 5; ++i) {
            now += 20;
            rm_servo_cal_step(&cal,now,true);
            CHECK(cal.pulse_us == 1502+i*2);
            rm_servo_cal_commit(&cal);
        }
        CHECK(cal.applied_us == 1510);
        CHECK(rm_servo_cal_move(&cal,now,-20));
        now += 20;
        rm_servo_cal_step(&cal,now,true);
        CHECK(cal.pulse_us == 1508);
        rm_servo_cal_stop(&cal);
        CHECK(cal.pulse_us == 1510 && !cal.armed && cal.output_enabled);
        CHECK(rm_servo_cal_arm(&cal,now,true));
        now += 20;
        rm_servo_cal_step(&cal,now,true);
        CHECK(cal.pulse_us == 1510); /* 取消后不能补跑尚未写入的目标。 */
        rm_servo_cal_fault(&cal);
        CHECK(cal.fault && !cal.setup_acknowledged && !rm_servo_cal_confirm(&cal));
        CHECK(cal.output_enabled && cal.pulse_us == 1510);
    }
    return true;
}

static bool records_and_limits(void)
{
    rm_servo_cal_t cal;
    uint32_t now = 0;
    CHECK(arm_and_center(&cal,1,now));
    CHECK(rm_servo_cal_mark(&cal,RM_SCAL_MARK_START));
    CHECK(!rm_servo_cal_mark(&cal,5) && !rm_servo_cal_direction(&cal,0));
    CHECK(rm_servo_cal_direction(&cal,-1));
    for (unsigned i = 0; i < 5; ++i) {
        CHECK(rm_servo_cal_move(&cal,now,-20));
        for (unsigned tick = 0; tick < 10; ++tick) {
            now += 20; rm_servo_cal_step(&cal,now,true); rm_servo_cal_commit(&cal);
        }
    }
    CHECK(cal.applied_us == 1400 && !rm_servo_cal_move(&cal,now,-1));
    CHECK(rm_servo_cal_mark(&cal,RM_SCAL_MARK_MIN));
    CHECK(rm_servo_cal_mark(&cal,RM_SCAL_MARK_B));
    for (unsigned i = 0; i < 10; ++i) {
        CHECK(rm_servo_cal_move(&cal,now,20));
        CHECK(!rm_servo_cal_mark(&cal,RM_SCAL_MARK_MAX));
        for (unsigned tick = 0; tick < 10; ++tick) {
            now += 20; rm_servo_cal_step(&cal,now,true); rm_servo_cal_commit(&cal);
        }
    }
    CHECK(cal.applied_us == 1600 && !rm_servo_cal_move(&cal,now,1));
    CHECK(rm_servo_cal_mark(&cal,RM_SCAL_MARK_MAX));
    CHECK(!rm_servo_cal_result_ready(&cal));
    CHECK(rm_servo_cal_mark(&cal,RM_SCAL_MARK_A) && rm_servo_cal_result_ready(&cal));
    cal.marked_us[RM_SCAL_MARK_START] = 1800;
    CHECK(!rm_servo_cal_result_ready(&cal));
    rm_servo_cal_init(&cal,0,now);
    CHECK(!rm_servo_cal_confirm(&cal) && !rm_servo_cal_result_ready(NULL));
    return true;
}

static bool faults_and_expiry(void)
{
    for (unsigned fault = 0; fault < 3; ++fault) {
        rm_servo_cal_t cal;
        CHECK(arm_and_center(&cal,2,0));
        CHECK(rm_servo_cal_move(&cal,0,20));
        uint32_t now = fault == 0 ? RM_SCAL_ARM_MS : (fault == 1 ? 101u : 20u);
        if (fault == 0) cal.last_step_ms = now-20; /* 单独测试许可过期，不混入调度超时。 */
        rm_servo_cal_step(&cal,now,fault != 2);
        CHECK(!cal.armed && cal.pulse_us == 1500 && cal.target_milli_us == 1500000);
        CHECK(cal.fault == (fault != 0));
    }
    return true;
}

static bool console_entry(void)
{
    rm_config_t config = rm_config_default();
    rm_app_t app;
    rm_app_init(&app,&config,0);
    app.servo_hardware_ready = true;
    rm_console_t console;
    rm_console_init(&console);
    char reply[RM_CONSOLE_REPLY_CAP];
    rm_console_action_t action;
    const char *commands[] = {"version\n","help\n","scal\n","scal step 10\n",
        "scal confirm\n","scal arm\n","scal center\n","stop\n","scal result\n",
        "bench on\n","driver read\n","scal mark wrong\n"};
    for (unsigned i = 0; i < sizeof commands/sizeof commands[0]; ++i) {
        bool replied = false;
        for (const char *p = commands[i]; *p; ++p)
            replied = rm_console_feed(&console,&app,(uint8_t)*p,0,0,reply,sizeof reply,&action) || replied;
        CHECK(replied && strstr(reply,"\r\n") && strlen(reply) < sizeof reply-1);
        bool error = i == 3 || i >= 9;
        CHECK((strstr(reply,"ERR") != NULL) == error);
        if (i == 0) CHECK(strstr(reply,"servo-cal-20261106-r16-S2"));
        if (i == 7) CHECK(!app.servo_cal.output_enabled && !app.servo_cal.armed);
    }
    CHECK(!app.config.geometry_verified && !app.arm.config.enabled);
    CHECK(!app.config.ps2_wiring_verified && !app.arm.config.power_verified);
    return true;
}

int main(void)
{
    if (!lifecycle() || !records_and_limits() || !faults_and_expiry() || !console_entry()) return 1;
    printf("PASS servo cal: %u checks (PC only)\n",checks);
    return 0;
}
