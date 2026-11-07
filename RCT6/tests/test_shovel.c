/* S2按键与停车的必要回归，硬件和时间均为PC模拟。 */
#include "console.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(t) do { ++checks; if (!(t)) { fprintf(stderr,"FAIL shovel line %d: %s\n",__LINE__,#t); return false; } } while (0)
static void tick(rm_shovel_t *s, rm_remote_t *r, uint32_t now)
{
    r->at_ms=now;
    rm_shovel_step(s,r,now,true);
    rm_shovel_commit(s);
}
static bool enable(rm_shovel_t *s, rm_remote_t *r, uint32_t *now)
{
    rm_shovel_init(s,*now);
    *r=(rm_remote_t){.valid=true};
    for (unsigned i=0; i<27; ++i) {
        *now+=20; tick(s,r,*now); CHECK(!s->pulse_us && !s->enabled);
    }
    r->arm=true; r->buttons=RM_PS2_START;
    *now+=20; tick(s,r,*now); CHECK(s->enabled && !s->pulse_us);
    r->arm=false; r->buttons=0;
    *now+=20; tick(s,r,*now);
    return true;
}
static bool buttons(void)
{
    rm_shovel_t s; rm_remote_t r; uint32_t now=UINT_MAX-800u;
    CHECK(enable(&s,&r,&now));
    CHECK(s.next_direction==-1);
    r.buttons=RM_PS2_CIRCLE; now+=20; tick(&s,&r,now);
    CHECK(!s.pulse_us && s.next_direction==-1);
    r.buttons=RM_PS2_SQUARE; now+=20; tick(&s,&r,now);
    CHECK(s.pulse_us==1470 && s.direction==-1 && s.next_direction==1);
    now+=20; tick(&s,&r,now); CHECK(s.pulse_us==1470 && s.next_direction==1);
    r.buttons=0; now+=20; tick(&s,&r,now); CHECK(!s.pulse_us && s.enabled);
    r.buttons=RM_PS2_SQUARE; now+=20; tick(&s,&r,now);
    CHECK(s.pulse_us==1530 && s.direction==1 && s.next_direction==-1);
    r.buttons=0; now+=20; tick(&s,&r,now); CHECK(!s.pulse_us);
    r.buttons=RM_PS2_SQUARE; now+=20; tick(&s,&r,now);
    CHECK(s.pulse_us==1470 && s.next_direction==1);
    r.buttons=RM_PS2_CIRCLE|RM_PS2_SQUARE; now+=20; tick(&s,&r,now);
    CHECK(s.pulse_us==1470 && s.next_direction==1);
    r.stop=true; now+=20; tick(&s,&r,now); CHECK(!s.enabled && !s.pulse_us);
    r.stop=false; now+=20; tick(&s,&r,now); CHECK(!s.enabled && !s.pulse_us && s.next_direction==1);
    return true;
}
static bool faults_and_limit(void)
{
    for (unsigned reason=0; reason<11; ++reason) {
        rm_shovel_t s; rm_remote_t r; uint32_t now=0;
        CHECK(enable(&s,&r,&now));
        r.buttons=RM_PS2_SQUARE; now+=20; tick(&s,&r,now); CHECK(s.pulse_us==1470);
        now+=20; r.at_ms=now;
        if (reason==0) r.valid=false;
        if (reason==1) r.at_ms=now-201u;
        if (reason==2) r.forward=NAN;
        if (reason==3) r.right_y=1.1f;
        if (reason==4) now+=101;
        if (reason==5) rm_shovel_fault(&s);
        if (reason==6) rm_shovel_stop(&s);
        if (reason==7) { r.arm=true; r.buttons|=RM_PS2_START; }
        if (reason==9) r.buttons|=RM_PS2_L2;
        if (reason==10) r.buttons|=RM_PS2_R2;
        rm_shovel_step(&s,&r,now,reason!=8);
        CHECK(!s.pulse_us);
        if (reason<9) CHECK(!s.enabled);
    }
    rm_shovel_t s; rm_remote_t r; uint32_t now=UINT_MAX-800u;
    CHECK(enable(&s,&r,&now));
    r.buttons=RM_PS2_SQUARE; now+=20; tick(&s,&r,now);
    for (unsigned i=1; i<=251; ++i) {
        now+=20; tick(&s,&r,now); CHECK((s.pulse_us!=0)==(i<250));
    }
    CHECK(s.release_required && s.enabled);
    r.buttons=0; now+=20; tick(&s,&r,now);
    r.buttons=RM_PS2_SQUARE; now+=20; tick(&s,&r,now); CHECK(s.pulse_us==1530);
    return true;
}
static bool pending_write(void)
{
    rm_shovel_t s; rm_remote_t r; uint32_t now=0;
    CHECK(enable(&s,&r,&now));
    r.buttons=RM_PS2_SQUARE; now+=20; r.at_ms=now;
    rm_shovel_step(&s,&r,now,true);
    CHECK(s.pulse_us==1470 && s.next_direction==-1 && !s.direction_committed);
    rm_shovel_fault(&s);
    rm_shovel_commit(&s);
    CHECK(!s.applied_us && s.next_direction==-1 && s.fault);
    return true;
}
static bool startup_and_config(void)
{
    rm_shovel_t s; rm_remote_t r={.valid=true,.arm=true,.buttons=RM_PS2_START};
    rm_shovel_init(&s,0);
    for (unsigned i=1; i<=40; ++i) { tick(&s,&r,i*20); CHECK(!s.enabled && !s.pulse_us); }
    CHECK(!rm_shovel_set_pulses(NULL,1500,1490));
    CHECK(!rm_shovel_set_pulses(&s,1399,1470) && !rm_shovel_set_pulses(&s,1530,1601));
    CHECK(!rm_shovel_set_pulses(&s,1500,1500));
    CHECK(rm_shovel_set_pulses(&s,1540,1460) && s.up_us==1540 && s.down_us==1460);
    s.applied_us=1530; CHECK(!rm_shovel_set_pulses(&s,1530,1470));
    s.applied_us=0; s.enabled=true; CHECK(!rm_shovel_set_pulses(&s,1530,1470));
    rm_shovel_fault(&s); CHECK(!rm_shovel_set_pulses(&s,1530,1470));
    return true;
}
static bool console_entry(void)
{
    rm_app_t app; rm_config_t cfg=rm_config_default(); rm_app_init(&app,&cfg,0);
    rm_console_t console; rm_console_init(&console);
    const char *commands[]={"version\n","shovel\n","shovel pulses 1540 1460\n","stop\n",
        "shovel pulses 1700 1460\n","scal center\n","driver read\n","shovel wrong\n"};
    char reply[RM_CONSOLE_REPLY_CAP]; rm_console_action_t action;
    for (unsigned i=0; i<sizeof commands/sizeof commands[0]; ++i) {
        bool replied=false;
        for (const char *p=commands[i]; *p; ++p)
            replied=rm_console_feed(&console,&app,(uint8_t)*p,0,0,reply,sizeof reply,&action)||replied;
        CHECK(replied && (strstr(reply,"ERR")!=NULL)==(i>=4));
        if (i==0) CHECK(strstr(reply,"shovel-20261106-r18-S2"));
        if (i==1) CHECK(strstr(reply,"shovel enabled=0 active=0 output=0") && strstr(reply,"next=down"));
    }
    CHECK(app.shovel.up_us==1540 && app.shovel.down_us==1460);
    CHECK(!app.config.ps2_wiring_verified && !app.arm.config.enabled);
    return true;
}
int main(void)
{
    if (!buttons() || !faults_and_limit() || !pending_write() || !startup_and_config() || !console_entry()) return 1;
    printf("PASS shovel: %u checks (PC only)\n",checks);
    return 0;
}
