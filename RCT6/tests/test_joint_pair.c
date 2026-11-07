/* 电脑模拟：选路、上下、相反方向、S2互锁、故障和超时；不代表实物停转。 */
#include "console.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return false; } } while (0)
static void tick(rm_joint_pair_t *j, rm_shovel_t *s, rm_remote_t *r, uint32_t *now)
{
    *now += 20; r->at_ms = *now;
    rm_joint_pair_step(j, s, r, *now); rm_joint_pair_commit(j);
}

static bool mapping_and_interlocks(void)
{
    rm_joint_pair_t j; rm_shovel_t s; rm_remote_t r={.valid=true}; uint32_t now=0;
    rm_joint_pair_init(&j); rm_shovel_init(&s,now); s.enabled=true;
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(!j.selected && !j.pulse_us[0]);
    r.buttons=RM_PS2_TRIANGLE; tick(&j,&s,&r,&now); CHECK(j.selected==3 && !j.pulse_us[0]);
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(j.pulse_us[0]==1470 && !j.pulse_us[1]);
    r.buttons=RM_PS2_DOWN; tick(&j,&s,&r,&now); CHECK(j.pulse_us[0]==1530);
    r.buttons=0; tick(&j,&s,&r,&now); CHECK(!j.pulse_us[0]);
    r.buttons=RM_PS2_CIRCLE; tick(&j,&s,&r,&now); CHECK(j.selected==4 && !j.pulse_us[1]);
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(j.pulse_us[1]==1530 && !j.pulse_us[0]);
    r.buttons=RM_PS2_DOWN; tick(&j,&s,&r,&now); CHECK(j.pulse_us[1]==1470);
    r.buttons=RM_PS2_TRIANGLE | RM_PS2_UP; tick(&j,&s,&r,&now);
    CHECK(j.selected==3 && j.center_required && !j.pulse_us[0] && !j.pulse_us[1]);
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(!j.pulse_us[0]);
    r.buttons=0; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(j.pulse_us[0]==1470);
    r.buttons=RM_PS2_UP | RM_PS2_DOWN; tick(&j,&s,&r,&now); CHECK(!j.pulse_us[0] && j.center_required);
    r.buttons=RM_PS2_DOWN; tick(&j,&s,&r,&now); CHECK(!j.pulse_us[0]);
    r.buttons=0; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_TRIANGLE | RM_PS2_CIRCLE; tick(&j,&s,&r,&now); CHECK(j.selected==3 && j.center_required);
    r.buttons=0; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(j.pulse_us[0]);
    s.active=true; r.buttons=RM_PS2_SQUARE | RM_PS2_UP; tick(&j,&s,&r,&now);
    CHECK(!j.pulse_us[0] && j.center_required);
    s.active=false; r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(!j.pulse_us[0]);
    r.buttons=0; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(j.pulse_us[0]==1470);
    r.buttons=RM_PS2_R2 | RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(!j.pulse_us[0] && j.center_required);
    r.buttons=0; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(j.pulse_us[0]==1470);
    r.stop=true; tick(&j,&s,&r,&now); CHECK(!j.selected && !j.pulse_us[0]);
    return true;
}

static bool timeout_faults_and_pulses(void)
{
    rm_joint_pair_t j; rm_shovel_t s; rm_remote_t r={.valid=true}; uint32_t now=UINT_MAX-500u;
    rm_joint_pair_init(&j); rm_shovel_init(&s,now);
    CHECK(rm_shovel_set_pulses(&s,1550,1450)); s.enabled=true;
    r.buttons=RM_PS2_TRIANGLE; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(j.pulse_us[0]==1450);
    for (unsigned i=1; i<=251; ++i) {
        r.buttons=(i&1u)?RM_PS2_DOWN:RM_PS2_UP;
        tick(&j,&s,&r,&now); CHECK((j.pulse_us[0]!=0)==(i<250));
    }
    CHECK(j.center_required);
    r.buttons=0; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_DOWN; tick(&j,&s,&r,&now); CHECK(j.pulse_us[0]==1550);
    r.valid=false; tick(&j,&s,&r,&now); CHECK(!j.selected && !j.pulse_us[0]);
    r.valid=true; r.buttons=RM_PS2_CIRCLE; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(j.pulse_us[1]==1550);
    r.right_y=NAN; tick(&j,&s,&r,&now); CHECK(!j.selected && !j.pulse_us[1]);
    r.right_y=0; r.buttons=RM_PS2_TRIANGLE; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_UP; now+=221; r.at_ms=now-201;
    rm_joint_pair_step(&j,&s,&r,now); CHECK(!j.selected && !j.pulse_us[0]);
    r.buttons=0; tick(&j,&s,&r,&now);
    r.buttons=RM_PS2_CIRCLE; tick(&j,&s,&r,&now);
    s.enabled=false; r.buttons=RM_PS2_UP; tick(&j,&s,&r,&now); CHECK(!j.selected && !j.pulse_us[1]);
    return true;
}

static bool console_directions(void)
{
    rm_app_t app; rm_config_t config=rm_config_default(); rm_app_init(&app,&config,0);
    rm_console_t console; rm_console_init(&console);
    char out[RM_CONSOLE_REPLY_CAP]; rm_console_action_t action;
    const char *command="joints\n"; bool replied=false;
    for (const char *p=command; *p; ++p)
        replied=rm_console_feed(&console,&app,(uint8_t)*p,0,0,out,sizeof out,&action)||replied;
    CHECK(replied && strstr(out,"s3_up=1470 s3_down=1530 s4_up=1530 s4_down=1470"));
    CHECK(strstr(out,"max_hold_ms=5000\r\n"));
    return true;
}

int main(void)
{
    if (!mapping_and_interlocks() || !timeout_faults_and_pulses() || !console_directions()) return 1;
    printf("PASS joint dpad: %u checks (PC only)\n", checks);
    return 0;
}
