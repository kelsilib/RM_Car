/* 单轮标定和电脑指令的PC测试。下面的已验收标志只存在于模拟车，不会编入固件。 */
#include "console.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do { ++checks; if (!(test)) { \
    fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#test); return false; } } while (0)

static rm_config_t fixture(bool geometry_ready)
{
    rm_config_t c = rm_config_default();
    c.calibration_enabled = true;
    c.ps2_wiring_verified = c.remote_failsafe_verified = c.driver_failsafe_verified = true;
    if (geometry_ready) {
        c.radius_m=0.05f; c.half_length_m=c.half_width_m=0.1f;
        c.geometry_verified=c.mapping_verified=c.motor_parameters_verified=true;
        c.speed_unit=RM_SPEED_MM_S;
        for (unsigned i=0;i<RM_WHEELS;++i) c.direction[i]=1;
    }
    return c;
}

static void packet(rm_app_t *a, const char *text, uint32_t now)
{
    while (*text) rm_app_receive(a,(uint8_t)*text++,now);
}
static void fresh(rm_app_t *a, uint32_t now)
{
    packet(a,"$MAll:1,-2,3,-4#",now);
    a->remote.valid=true;
    a->remote.at_ms=now;
}
static void prepare(rm_app_t *a, bool geometry_ready, uint32_t start)
{
    rm_config_t c=fixture(geometry_ready);
    rm_app_init(a,&c,start);
    char frame[48];
    for(unsigned elapsed=0;elapsed<=320;elapsed+=20) {
        fresh(a,start+elapsed);
        (void)rm_app_step(a,start+elapsed,false,frame,sizeof frame);
    }
}

static bool test_calibration_lifecycle(void)
{
    rm_app_t a; char frame[48];
    prepare(&a,false,0);
    CHECK(!rm_config_motion_ready(&a.config) && rm_config_calibration_ready(&a.config));
    CHECK(rm_app_cal_arm(&a,320) && a.safety.state==RM_CALIBRATING);
    fresh(&a,340);
    CHECK(rm_app_step(&a,340,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
    CHECK(!rm_app_cal_arm(&a,340)); /* 不能靠重复arm延长这次许可 */
    CHECK(!rm_app_cal_pulse(&a,340,4,50,200));
    CHECK(!rm_app_cal_pulse(&a,340,0,101,200));
    CHECK(!rm_app_cal_pulse(&a,340,0,0,200));
    CHECK(!rm_app_cal_pulse(&a,340,0,50,201));
    CHECK(!rm_app_cal_pulse(&a,340,0,50,19));
    CHECK(rm_app_cal_pulse(&a,340,2,-50,200));
    CHECK(!rm_app_cal_pulse(&a,340,0,50,200)); /* 一次许可只执行一次 */
    for(uint32_t now=360;now<560;now+=20) {
        fresh(&a,now);
        CHECK(rm_app_step(&a,now,false,frame,sizeof frame));
        CHECK(!strcmp(frame,"$pwm:0,0,-50,0#"));
        CHECK(a.last_channel[2]==-50 && !a.last_speed_mode);
    }
    fresh(&a,560);
    CHECK(rm_app_step(&a,560,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
    CHECK(a.calibration.state==RM_CAL_OFF && a.safety.state!=RM_ARMED);
    CHECK(!rm_app_cal_pulse(&a,560,2,-50,200));
    CHECK(!rm_app_cal_arm(&a,560));
    /* 最短20ms指令也必须先输出一次，不能在等待控制循环时悄悄过期。 */
    prepare(&a,false,0); CHECK(rm_app_cal_arm(&a,320));
    CHECK(rm_app_cal_pulse(&a,320,0,50,20));
    fresh(&a,340); CHECK(rm_app_step(&a,340,false,frame,sizeof frame));
    CHECK(!strcmp(frame,"$pwm:50,0,0,0#"));
    fresh(&a,360); CHECK(rm_app_step(&a,360,false,frame,sizeof frame));
    CHECK(!strcmp(frame,"$pwm:0,0,0,0#"));
    prepare(&a,false,0); CHECK(rm_app_cal_arm(&a,320));
    CHECK(rm_app_cal_pulse(&a,320,0,50,200));
    fresh(&a,420); CHECK(rm_app_step(&a,420,false,frame,sizeof frame));
    CHECK(!strcmp(frame,"$pwm:0,0,0,0#") && a.calibration.state==RM_CAL_OFF);
    /* 等待5秒没有执行，也会收回准备许可。 */
    prepare(&a,false,0); CHECK(rm_app_cal_arm(&a,320));
    for(uint32_t now=340;now<=5320;now+=20) {
        fresh(&a,now); CHECK(rm_app_step(&a,now,false,frame,sizeof frame));
    }
    CHECK(a.calibration.state==RM_CAL_OFF && !rm_app_cal_pulse(&a,5320,0,50,200));
    return true;
}

static bool test_calibration_guards(void)
{
    rm_config_t defaults=rm_config_default();
    CHECK(!defaults.calibration_enabled && !rm_config_calibration_ready(&defaults));
    for(unsigned fault=0;fault<12;++fault) {
        rm_app_t a; char frame[48]; prepare(&a,true,0);
        CHECK(rm_app_cal_arm(&a,320));
        CHECK(rm_app_cal_pulse(&a,320,0,50,200));
        fresh(&a,340);
        bool estop=false;
        switch(fault) {
            case 0: a.remote.valid=false; break;
            case 1: a.motor_seen=false; break;
            case 2: a.transport_fault=true; break;
            case 3: a.remote.forward=0.3f; break;
            case 4: a.remote.arm=true; break;
            case 5: a.remote.stop=true; break;
            case 6: estop=true; break;
            case 7: a.config.driver_failsafe_verified=false; break;
            case 8: a.last_step_ms=200; break;
            case 9: a.calibration.channel=4; break;
            case 10: a.config.calibration_max_pwm=10; break;
            default: a.calibration.state=(rm_cal_state_t)-1; break;
        }
        CHECK(rm_app_step(&a,340,estop,frame,sizeof frame));
        CHECK(!strcmp(frame,"$pwm:0,0,0,0#") && a.calibration.state==RM_CAL_OFF);
        CHECK(a.safety.state!=RM_ARMED);
        if(fault==5) CHECK(a.safety.state==RM_DISARMED);
        if(fault==6) CHECK(a.safety.state==RM_ESTOP);
    }
    rm_app_t a; prepare(&a,false,0);
    a.config.calibration_enabled=false; CHECK(!rm_app_cal_arm(&a,320));
    a.config=fixture(false); a.config.remote_failsafe_verified=false;
    CHECK(!rm_app_cal_arm(&a,320));
    a.config=fixture(false); a.config.calibration_max_pwm=301;
    CHECK(!rm_config_calibration_ready(&a.config));
    a.config=fixture(false); a.config.calibration_max_ms=501;
    CHECK(!rm_config_calibration_ready(&a.config));
    /* 未完成回中等待、没有新鲜数据，都不能获得准备许可。 */
    rm_config_t c=fixture(false); rm_app_init(&a,&c,0); fresh(&a,0);
    CHECK(!rm_app_cal_arm(&a,0));
    prepare(&a,false,0); CHECK(!rm_app_cal_arm(&a,600));
    prepare(&a,true,0); fresh(&a,340); char frame[48];
    CHECK(rm_app_step(&a,340,true,frame,sizeof frame));
    rm_app_stop(&a);
    CHECK(a.safety.state==RM_ESTOP && !rm_app_cal_arm(&a,340));
    return true;
}

static bool test_mode_switch_and_wrap(void)
{
    rm_app_t a; char frame[48]; prepare(&a,true,0);
    a.remote.arm=true; fresh(&a,340);
    CHECK(rm_app_step(&a,340,false,frame,sizeof frame) && a.safety.state==RM_ARMED);
    CHECK(!rm_app_cal_arm(&a,340));
    rm_app_stop(&a);
    for(uint32_t now=360;now<=760;now+=20) {
        fresh(&a,now); CHECK(rm_app_step(&a,now,false,frame,sizeof frame));
        CHECK(a.safety.state!=RM_ARMED);
    }
    a.remote.arm=false; fresh(&a,780); CHECK(rm_app_step(&a,780,false,frame,sizeof frame));
    a.remote.arm=true; fresh(&a,800); CHECK(rm_app_step(&a,800,false,frame,sizeof frame));
    CHECK(a.safety.state==RM_ARMED);
    uint32_t start=UINT32_MAX-400u;
    prepare(&a,false,start);
    CHECK(rm_app_cal_arm(&a,start+320u));
    CHECK(rm_app_cal_pulse(&a,start+320u,3,50,200));
    fresh(&a,start+340u);
    CHECK(rm_app_step(&a,start+340u,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,50#"));
    fresh(&a,start+440u);
    CHECK(rm_app_step(&a,start+440u,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,50#"));
    fresh(&a,start+540u);
    CHECK(rm_app_step(&a,start+540u,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
    return true;
}

static unsigned command(rm_console_t *c, rm_app_t *a, const char *text, uint32_t received,
                        uint32_t now, char reply[RM_CONSOLE_REPLY_CAP], rm_console_action_t *action)
{
    unsigned replies=0;
    while(*text) {
        char current[RM_CONSOLE_REPLY_CAP]; rm_console_action_t current_action;
        if(rm_console_feed(c,a,(uint8_t)*text++,received,now,current,sizeof current,&current_action)) {
            memcpy(reply,current,sizeof current);
            *action=current_action;
            ++replies;
        }
    }
    return replies;
}

static bool test_console_queries_and_framing(void)
{
    rm_app_t a; prepare(&a,false,0); rm_console_t c; rm_console_init(&c);
    char reply[RM_CONSOLE_REPLY_CAP]; rm_console_action_t action;
    CHECK(command(&c,&a,"help\r\n",320,320,reply,&action)==1 && strstr(reply,"cal pulse"));
    CHECK(command(&c,&a,"  ps2  \n",320,320,reply,&action)==1 && strstr(reply,"fwd=0"));
    CHECK(command(&c,&a,"config\n",320,320,reply,&action)==1 && strstr(reply,"issues=0xC7"));
    packet(&a,"$Battery:12.60V#",321);
    CHECK(command(&c,&a,"counts\n",322,322,reply,&action)==1 && strstr(reply,"M2=-2"));
    CHECK(command(&c,&a,"battery\n",322,322,reply,&action)==1 && strstr(reply,"millivolts=12600"));
    packet(&a,"$sample_config_reply#",322);
    packet(&a,"$MAll:1,-2,3,-4#",323);
    CHECK(command(&c,&a,"driver reply\n",323,323,reply,&action)==1 && strstr(reply,"$sample_config_reply#"));
    CHECK(command(&c,&a,"driver voltage\n",322,322,reply,&action)==1 && action==RM_CONSOLE_READ_VOLTAGE);
    CHECK(command(&c,&a,"$pwm:100,0,0,0#\n",322,322,reply,&action)==1 && strstr(reply,"ERR"));
    CHECK(command(&c,&a,"set verified 1\n",322,322,reply,&action)==1 && strstr(reply,"ERR"));
    CHECK(command(&c,&a,"ps2 extra\n",322,322,reply,&action)==1 && strstr(reply,"ERR"));
    char long_line[100]; memset(long_line,'x',sizeof long_line); long_line[98]='\n'; long_line[99]=0;
    CHECK(command(&c,&a,long_line,322,322,reply,&action)==1 && strstr(reply,"long_line"));
    CHECK(command(&c,&a,"status\n",322,322,reply,&action)==1 && strstr(reply,"state="));
    CHECK(command(&c,&a,"he\x01lp\n",322,322,reply,&action)==1 && strstr(reply,"ERR"));
    CHECK(command(&c,&a,"cal pulse 1 -999999999999999999999 200\n",322,322,reply,&action)==1 && strstr(reply,"ERR"));
    CHECK(a.calibration.state==RM_CAL_OFF && a.last_channel[0]==0);
    return true;
}

static bool test_console_actions_and_stale_commands(void)
{
    rm_app_t a; prepare(&a,false,0); rm_console_t c; rm_console_init(&c);
    char reply[RM_CONSOLE_REPLY_CAP]; rm_console_action_t action;
    CHECK(command(&c,&a,"cal arm\n",0,320,reply,&action)==1 && strstr(reply,"stale"));
    CHECK(a.calibration.state==RM_CAL_OFF);
    CHECK(command(&c,&a,"cal arm\n",320,320,reply,&action)==1 && strstr(reply,"OK"));
    CHECK(command(&c,&a,"driver read\n",320,320,reply,&action)==1 && strstr(reply,"ERR"));
    /* 固件允许数字前面带0；电脑回复判断也必须认作同一条合法标定指令。 */
    CHECK(command(&c,&a,"cal pulse 01 050 0200\n",320,320,reply,&action)==1 && strstr(reply,"OK"));
    fresh(&a,340); char frame[48]; CHECK(rm_app_step(&a,340,false,frame,sizeof frame));
    CHECK(!strcmp(frame,"$pwm:50,0,0,0#"));
    CHECK(command(&c,&a,"output\n",340,340,reply,&action)==1 && strstr(reply,"M1=50"));
    CHECK(command(&c,&a,"stop\n",340,340,reply,&action)==1 && action==RM_CONSOLE_STOP);
    CHECK(a.calibration.state==RM_CAL_OFF && a.last_channel[0]==0);
    fresh(&a,360); CHECK(rm_app_step(&a,360,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
    rm_console_fault(&c);
    CHECK(command(&c,&a,"cal arm\n",360,360,reply,&action)==0 && a.calibration.state==RM_CAL_OFF);
    rm_console_init(&c);
    CHECK(command(&c,&a,"cal ",0,0,reply,&action)==0);
    CHECK(command(&c,&a,"arm\n",2100,2100,reply,&action)==1 && strstr(reply,"stale"));
    /* 小于约定的回复空间时，不能先执行动作再报告输出失败。 */
    CHECK(!rm_console_feed(&c,&a,'s',360,360,reply,4,&action));
    rm_app_transport_fault(&a);
    CHECK(a.safety.state==RM_FAULT && a.last_channel[0]==0);
    rm_app_stop(&a);
    CHECK(a.transport_fault && a.safety.state==RM_FAULT && !rm_app_cal_arm(&a,360));
    return true;
}

static bool test_reply_markers_and_capacity(void)
{
    rm_app_t a; prepare(&a,false,0); rm_console_t c; rm_console_init(&c);
    char reply[RM_CONSOLE_REPLY_CAP]; rm_console_action_t action;
    const char *queries[] = {"help\n", "version\n", "status\n", "ps2\n", "counts\n",
        "config\n", "output\n", "battery\n", "driver read\n", "driver voltage\n",
        "stop\n", "cal cancel\n", "cal arm\n", "cal pulse 1 9999 200\n", "unknown\n"};
    for (unsigned i=0;i<sizeof queries/sizeof queries[0];++i) {
        CHECK(command(&c,&a,queries[i],320,320,reply,&action)==1);
        CHECK(!strncmp(reply,RM_CONSOLE_REPLY_PREFIX,sizeof RM_CONSOLE_REPLY_PREFIX-1));
        size_t n=strlen(reply);
        CHECK(n<sizeof reply && n>=2 && !strcmp(reply+n-2,"\r\n"));
    }
    /* 最长的未知驱动包，加上10位年龄和reply标识，仍应原样显示，不能被前缀挤掉。 */
    char raw[RM_FRAME_CAP+2];
    raw[0]='$'; memset(raw+1,'x',RM_FRAME_CAP-1);
    raw[RM_FRAME_CAP]='#'; raw[RM_FRAME_CAP+1]='\0';
    packet(&a,raw,0);
    CHECK(command(&c,&a,"driver reply\n",UINT32_MAX,UINT32_MAX,reply,&action)==1);
    CHECK(strstr(reply,raw) && strstr(reply,"age_ms=4294967295"));
    CHECK(!strncmp(reply,"reply driver_reply ",19) && strlen(reply)<sizeof reply);
    return true;
}

int main(void)
{
    const struct { const char *name; bool (*run)(void); } groups[]={
        {"calibration lifecycle",test_calibration_lifecycle}, {"calibration guards",test_calibration_guards},
        {"mode switch and timer wrap",test_mode_switch_and_wrap},
        {"console queries and framing",test_console_queries_and_framing},
        {"console actions and stale commands",test_console_actions_and_stale_commands},
        {"reply markers and longest payload",test_reply_markers_and_capacity}
    };
    for(unsigned i=0;i<sizeof groups/sizeof groups[0];++i) {
        if(!groups[i].run()) return 1;
        printf("PASS %s\n",groups[i].name);
    }
    printf("PASS service: %u assertions\n",checks);
    return 0;
}
