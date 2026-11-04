/* 全部脉宽/验收项只存在于PC模拟，不能作为本批MG995的实物规格。 */
#include "arm_control.h"
#include "chassis_app.h"
#include "console.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL arm line %d: %s\n",__LINE__,#x); return false; } } while (0)
typedef struct { rm_arm_t a; rm_config_t shared; rm_remote_t r; uint32_t now; } rig_t;

static rm_arm_config_t fixture(void)
{
    rm_arm_config_t c = {.enabled=true,.wiring_verified=true,.power_verified=true,
        .base_safe_verified=true,.fault_hold_verified=true,.period_us=20000};
    for (unsigned i=0;i<RM_SERVOS;++i) c.servo[i]=(rm_servo_config_t){
        .enabled=true,.limits_verified=true,.start_pose_verified=true,
        .min_us=1000,.max_us=2000,.start_us=1500,.max_rate_us_s=1000,.direction=1};
    return c;
}
static void init(rig_t *t, uint32_t at)
{
    memset(t,0,sizeof *t);
    t->now=at; t->shared=rm_config_default();
    t->shared.ps2_wiring_verified=t->shared.remote_failsafe_verified=true;
    rm_arm_config_t c=fixture(); rm_arm_init(&t->a,&c,at);
}
static void step(rig_t *t, uint32_t dt, uint16_t buttons, float y, bool commit)
{
    t->now+=dt;
    t->r=(rm_remote_t){.valid=true,.at_ms=t->now,.buttons=buttons,.right_y=y,
        .arm=(buttons&RM_PS2_START)!=0,.stop=(buttons&RM_PS2_SELECT)!=0};
    rm_arm_step(&t->a,&t->shared,&t->r,t->now,false,true,true,true);
    if (commit) rm_arm_commit(&t->a);
}
static void arm(rig_t *t)
{
    for (unsigned i=0;i<17;++i) step(t,20,RM_PS2_L2,0,true);
    step(t,20,RM_PS2_L2|RM_PS2_START,0,true);
}

static bool configuration(void)
{
    rm_arm_config_t c=rm_arm_config_default();
    CHECK(rm_arm_config_issues(&c)==0x7fu && !rm_arm_channel_ready(&c,0));
    CHECK(rm_arm_config_issues(NULL)==0xffu && !rm_arm_channel_ready(NULL,0));
    c=fixture(); CHECK(!rm_arm_config_issues(&c) && !rm_arm_channel_ready(&c,7));
    for (unsigned i=0;i<RM_SERVOS;++i) CHECK(rm_arm_channel_ready(&c,i));
    const uint32_t periods[]={0,999,65536,UINT32_MAX};
    for (unsigned i=0;i<4;++i) { c=fixture(); c.period_us=periods[i]; CHECK(rm_arm_config_issues(&c)&RM_ARM_CFG_PERIOD); }
    for (unsigned which=0;which<7;++which) {
        c=fixture(); rm_servo_config_t *s=&c.servo[3];
        if(which==0)s->min_us=0;
        if(which==1)s->max_us=s->min_us;
        if(which==2)s->start_us=999;
        if(which==3)s->max_rate_us_s=10001;
        if(which==4)s->direction=0;
        if(which==5)s->limits_verified=false;
        if(which==6)s->start_pose_verified=false;
        CHECK(!rm_arm_channel_ready(&c,3) && (rm_arm_config_issues(&c)&RM_ARM_CFG_LIMITS));
    }
    c=fixture();c.servo[3].enabled=false;c.servo[3].min_us=0;
    CHECK(!rm_arm_config_issues(&c) && !rm_arm_channel_ready(&c,3));
    rig_t t;init(&t,0); t.a.config=rm_arm_config_default(); arm(&t);
    CHECK(!t.a.armed && t.a.output_mask==0 && t.a.pulse_us[0]==0);
    return true;
}

static bool lifecycle(void)
{
    rig_t t;init(&t,0);
    /* 底盘尺寸/轮向/控制单位仍未知，不用它们伪造机械臂配置；独立底盘安全项在fixture明确给出。 */
    CHECK(!rm_config_motion_ready(&t.shared));
    for(unsigned i=0;i<20;++i)step(&t,20,0,0,true);
    CHECK(t.a.output_mask==0);
    for(unsigned i=0;i<25;++i)step(&t,20,RM_PS2_L2|RM_PS2_START,0,true);
    CHECK(!t.a.armed && !t.a.output_mask); /* 一直按START，不能等时间到后突然初始化。 */
    step(&t,20,RM_PS2_L2,0,true);step(&t,20,RM_PS2_L2|RM_PS2_START,0,true);
    CHECK(t.a.armed && t.a.output_mask==1 && t.a.pulse_us[0]==1500);
    for(unsigned i=1;i<RM_SERVOS;++i)CHECK(t.a.pulse_us[i]==0);
    step(&t,20,RM_PS2_L2,1,true);CHECK(t.a.pulse_us[0]==1500); /* 无R2不移动。 */
    step(&t,20,RM_PS2_L2|RM_PS2_R2,1,true);CHECK(t.a.pulse_us[0]==1520);
    step(&t,20,RM_PS2_L2|RM_PS2_R2,-1,true);CHECK(t.a.pulse_us[0]==1500);
    step(&t,20,RM_PS2_L2|RM_PS2_R2,1,false);CHECK(t.a.pulse_us[0]==1520);
    rm_arm_stop(&t.a);CHECK(t.a.pulse_us[0]==1500 && !t.a.armed); /* 未写入目标必须撤销。 */
    arm(&t);CHECK(t.a.armed && t.a.pulse_us[0]==1500);
    step(&t,20,RM_PS2_L2|RM_PS2_R2,1,true);
    step(&t,20,RM_PS2_L2|RM_PS2_SELECT|RM_PS2_START,0,true);
    CHECK(!t.a.armed && t.a.pulse_us[0]==1520 && t.a.output_mask==1);
    step(&t,20,0,0,true);CHECK(t.a.pulse_us[0]==1520 && !t.a.armed);
    return true;
}

static bool selection_and_limits(void)
{
    rig_t t;init(&t,0);arm(&t);
    for(unsigned i=0;i<100;++i)step(&t,20,RM_PS2_L2|RM_PS2_R2,1,true);
    CHECK(t.a.pulse_us[0]==2000);
    for(unsigned i=0;i<100;++i)step(&t,20,RM_PS2_L2|RM_PS2_R2,-1,true);
    CHECK(t.a.pulse_us[0]==1000);
    step(&t,20,RM_PS2_L2|RM_PS2_RIGHT,0,true);
    CHECK(t.a.selected==1 && !t.a.armed && t.a.output_mask==1 && !t.a.pulse_us[1]);
    for(unsigned i=0;i<30;++i)step(&t,20,RM_PS2_L2|RM_PS2_RIGHT,0,true);
    CHECK(t.a.selected==1); /* 长按不连跳，只有新按一下才切换。 */
    arm(&t);CHECK(t.a.armed && t.a.output_mask==3 && t.a.pulse_us[1]==1500);
    step(&t,20,RM_PS2_L2|RM_PS2_R2|RM_PS2_RIGHT,1,true);
    CHECK(t.a.selected==1 && t.a.pulse_us[1]==1500); /* 使能中不能换路，也不误移动。 */
    t.a.config.servo[2].enabled=false;
    step(&t,20,RM_PS2_L2,0,true);step(&t,20,RM_PS2_L2|RM_PS2_RIGHT,0,true);
    CHECK(t.a.selected==2 && !t.a.armed);
    step(&t,20,RM_PS2_L2,0,true);step(&t,20,RM_PS2_L2|RM_PS2_RIGHT,0,true);
    CHECK(t.a.selected==3); /* 选到关闭通道后仍能继续选走。 */
    t.a.config.servo[3].direction=-1;arm(&t);
    step(&t,20,RM_PS2_L2|RM_PS2_R2,1,true);CHECK(t.a.pulse_us[3]==1480);
    t.a.config.servo[3].max_rate_us_s=1;
    for(unsigned i=0;i<50;++i)step(&t,20,RM_PS2_L2|RM_PS2_R2,1,true);
    CHECK(t.a.pulse_us[3]==1479); /* 每次不足1us也能积累，不会永远停在原值。 */
    return true;
}

static bool guards(void)
{
    for(unsigned which=0;which<11;++which) {
        rig_t t;init(&t,UINT32_MAX-180u);arm(&t);CHECK(t.a.armed);
        uint16_t saved=t.a.pulse_us[0];
        t.now+=20;t.r=(rm_remote_t){.valid=true,.at_ms=t.now,.buttons=RM_PS2_L2|RM_PS2_R2,.right_y=1};
        if(which==0)t.r.valid=false;
        if(which==1)t.r.at_ms-=201;
        if(which==2)t.r.right_y=NAN;
        if(which==3)t.r.right_y=1.01f;
        if(which==4)t.shared.remote_failsafe_verified=false;
        if(which==5)t.a.config.power_verified=false;
        if(which==10)t.now+=101;
        rm_arm_step(&t.a,&t.shared,&t.r,t.now,which==6,which!=7,which!=8,which!=9);
        CHECK(!t.a.armed && t.a.pulse_us[0]==saved && t.a.output_mask==1 && t.a.inhibit);
        if(which==8)rm_arm_fault(&t.a);
        /* 错误恢复后保持START不能自动重新许可。 */
        for(unsigned i=0;i<20;++i)step(&t,20,RM_PS2_L2|RM_PS2_START,0,true);
        CHECK(!t.a.armed && t.a.pulse_us[0]==saved);
    }
    rig_t t;init(&t,0);
    for(unsigned i=0;i<30;++i)step(&t,20,RM_PS2_L2|RM_PS2_R2|RM_PS2_START,0,true);
    CHECK(!t.a.armed && !t.a.output_mask);
    return true;
}

static bool lift_shortcut(void)
{
    rig_t t;
    init(&t, 0);
    t.a.selected = 5;
    arm(&t);
    CHECK(t.a.armed && t.a.applied_mask == (1u << 5));

    /* 从机械臂通道直达升降台，原通道保持，S1等明确许可后再输出。 */
    step(&t, 20, RM_PS2_L2 | RM_PS2_TRIANGLE, 0, true);
    CHECK(t.a.selected == 0 && !t.a.armed && t.a.applied_mask == (1u << 5));
    CHECK(t.a.applied_us[0] == 0 && t.a.applied_us[5] == 1500);
    for (unsigned i = 0; i < 30; ++i) {
        step(&t, 20, RM_PS2_L2 | RM_PS2_TRIANGLE, 0, true);
    }
    step(&t, 20, RM_PS2_L2 | RM_PS2_TRIANGLE | RM_PS2_START, 0, true);
    CHECK(!t.a.armed && t.a.applied_us[0] == 0);
    step(&t, 20, RM_PS2_L2, 0, true);
    step(&t, 20, RM_PS2_L2 | RM_PS2_START, 0, true);
    CHECK(!t.a.armed); /* 松开△之后，回中等待从头计算。 */
    arm(&t);
    CHECK(t.a.armed && t.a.applied_mask == ((1u << 5) | 1u));
    step(&t, 20, RM_PS2_L2 | RM_PS2_R2, 1, true);
    CHECK(t.a.applied_us[0] == 1520 && t.a.applied_us[5] == 1500);

    step(&t, 20, RM_PS2_L2 | RM_PS2_RIGHT, 0, true);
    arm(&t);
    CHECK(t.a.armed && t.a.selected == 1);
    step(&t, 20, RM_PS2_L2 | RM_PS2_R2 | RM_PS2_TRIANGLE, 1, true);
    CHECK(t.a.selected == 1 && t.a.applied_us[1] == 1500);
    step(&t, 20, RM_PS2_L2, 0, true);
    step(&t, 20, RM_PS2_L2 | RM_PS2_TRIANGLE | RM_PS2_LEFT, 0, true);
    CHECK(t.a.selected == 1); /* 同时按两个选路键时，保留当前选择。 */

    init(&t, 0);
    t.a.selected = 3;
    for (unsigned i = 0; i < 20; ++i) {
        step(&t, 20, RM_PS2_L2 | RM_PS2_TRIANGLE, 0, true);
    }
    CHECK(t.a.selected == 3 && !t.a.armed && !t.a.applied_mask);
    step(&t, 20, RM_PS2_L2, 0, true);
    t.a.config.servo[0].enabled = false;
    step(&t, 20, RM_PS2_L2 | RM_PS2_TRIANGLE, 0, true);
    CHECK(t.a.selected == 0 && !t.a.armed && !t.a.applied_mask);
    CHECK(t.a.inhibit == RM_ARM_INHIBIT_CHANNEL);
    step(&t, 20, RM_PS2_L2, 0, true);
    step(&t, 20, RM_PS2_L2 | RM_PS2_RIGHT, 0, true);
    CHECK(t.a.selected == 1);
    arm(&t);
    CHECK(t.a.armed && t.a.applied_mask == (1u << 1));
    step(&t, 20, RM_PS2_L2 | RM_PS2_SELECT | RM_PS2_TRIANGLE, 0, true);
    CHECK(t.a.selected == 1 && !t.a.armed && t.a.applied_mask == (1u << 1));
    return true;
}

static bool app_and_console(void)
{
    rm_config_t c=rm_config_default();
    c.geometry_verified=c.mapping_verified=c.motor_parameters_verified=true;
    c.ps2_wiring_verified=c.remote_failsafe_verified=c.software_stop_verified=true;
    c.speed_unit=RM_SPEED_MM_S;
    for(unsigned i=0;i<RM_WHEELS;++i)c.direction[i]=1;
    rm_app_t a;rm_app_init(&a,&c,0);rm_arm_config_t ac=fixture();rm_arm_init(&a.arm,&ac,0);
    a.servo_hardware_ready=true;
    char frame[48];
    for(uint32_t at=20;at<=360;at+=20) {
        a.remote=(rm_remote_t){.valid=true,.at_ms=at,.buttons=RM_PS2_L2,.arm=at==360};
        a.motor_seen=true;a.motor_at_ms=at;
        CHECK(rm_app_step(&a,at,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
        rm_arm_commit(&a.arm);
    }
    CHECK(a.arm.armed && a.safety.state!=RM_ARMED && !rm_app_cal_arm(&a,360));
    a.remote.right_y=1;a.remote.buttons|=RM_PS2_R2;a.remote.arm=false;a.remote.at_ms=380;
    CHECK(rm_app_step(&a,380,false,frame,sizeof frame));
    CHECK(a.arm.pulse_us[0]==1520 && a.arm.applied_us[0]==1500);
    rm_app_transport_fault(&a);CHECK(a.arm.pulse_us[0]==1500 && !a.arm.armed);
    rm_console_t console;rm_console_init(&console);
    char reply[RM_CONSOLE_REPLY_CAP];rm_console_action_t action;
    const char *queries[]={"arm\n","servo 1\n","servo 7\n","ps2\n","help\n","servo 0\n","servo 8\n"};
    for(unsigned i=0;i<7;++i) {
        bool ready=false;
        for(const char *s=queries[i];*s;++s)ready=rm_console_feed(&console,&a,(uint8_t)*s,400,400,reply,sizeof reply,&action);
        CHECK(ready && strlen(reply)<sizeof reply && strstr(reply,"\r\n"));
        CHECK((strstr(reply,"ERR")!=NULL)==(i>=5));
    }
    return true;
}

static bool perturbations(void)
{
    rig_t t;init(&t,UINT32_MAX-600u);arm(&t);
    uint32_t seed=17;
    for(unsigned n=0;n<5000;++n) {
        seed=seed*1664525u+1013904223u;
        uint16_t buttons=(uint16_t)(seed>>16);
        if(n%37==0)buttons=RM_PS2_L2|RM_PS2_START;
        float y=(float)((int)(seed%2001u)-1000)/1000.0f;
        step(&t,20,buttons,y,true);
        CHECK(t.a.selected<RM_SERVOS && !(t.a.output_mask & ~RM_SERVO_ALL));
        for(unsigned i=0;i<RM_SERVOS;++i) {
            if(t.a.output_mask&(1u<<i))CHECK(t.a.pulse_us[i]>=1000 && t.a.pulse_us[i]<=2000);
            else CHECK(t.a.pulse_us[i]==0);
        }
        if(!(buttons&RM_PS2_L2))CHECK(!t.a.armed);
    }
    return true;
}

static bool mode_reunlock(void)
{
    rm_config_t c=rm_config_default();
    c.geometry_verified=c.mapping_verified=c.motor_parameters_verified=true;
    c.ps2_wiring_verified=c.remote_failsafe_verified=c.software_stop_verified=true;
    c.speed_unit=RM_SPEED_MM_S;c.calibration_enabled=true;
    for(unsigned i=0;i<RM_WHEELS;++i)c.direction[i]=1;
    rm_app_t a;rm_app_init(&a,&c,0);rm_arm_config_t ac=fixture();rm_arm_init(&a.arm,&ac,0);
    a.driver_info.fields=RM_DRIVER_DIAMETER;a.driver_info.wheel_diameter_mm=80.0f; /* 只给PC夹具模拟轮径回读 */
    a.servo_hardware_ready=true;char frame[48];
    for(uint32_t at=0;at<=360;at+=20) {
        a.remote=(rm_remote_t){.valid=true,.at_ms=at,.arm=at==340,.forward=at==360?1.0f:0.0f};
        a.motor_seen=true;a.motor_at_ms=at;
        CHECK(rm_app_step(&a,at,false,frame,sizeof frame));
    }
    CHECK(a.safety.state==RM_ARMED && strcmp(frame,"$spd:0,0,0,0#"));
    a.remote=(rm_remote_t){.valid=true,.at_ms=380,.buttons=RM_PS2_L2|RM_PS2_START,.arm=true};
    a.motor_at_ms=380;
    CHECK(rm_app_step(&a,380,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
    CHECK(a.safety.state!=RM_ARMED && !a.arm.armed);
    for(uint32_t at=400;at<=740;at+=20) {
        a.remote=(rm_remote_t){.valid=true,.at_ms=at,.buttons=RM_PS2_L2,.arm=at==740};
        a.motor_at_ms=at;
        CHECK(rm_app_step(&a,at,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
        rm_arm_commit(&a.arm);
    }
    CHECK(a.arm.armed);
    for(uint32_t at=760;at<=1180;at+=20) {
        a.remote=(rm_remote_t){.valid=true,.at_ms=at,.arm=true};a.motor_at_ms=at;
        CHECK(rm_app_step(&a,at,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
        CHECK(!a.arm.armed && a.safety.state!=RM_ARMED && a.arm.pulse_us[0]==1500);
    }
    a.remote.arm=false;a.remote.at_ms=a.motor_at_ms=1200;
    CHECK(rm_app_step(&a,1200,false,frame,sizeof frame));
    a.remote.arm=true;a.remote.at_ms=a.motor_at_ms=1220;
    CHECK(rm_app_step(&a,1220,false,frame,sizeof frame) && a.safety.state==RM_ARMED);
    rm_app_stop(&a);
    for(uint32_t at=1240;at<=1580;at+=20) {
        a.remote=(rm_remote_t){.valid=true,.at_ms=at};a.motor_at_ms=at;
        CHECK(rm_app_step(&a,at,false,frame,sizeof frame));
    }
    CHECK(rm_app_cal_arm(&a,1580));
    a.remote.buttons=RM_PS2_L2;a.remote.at_ms=a.motor_at_ms=1600;
    CHECK(rm_app_step(&a,1600,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
    CHECK(a.calibration.state==RM_CAL_OFF && !rm_app_cal_arm(&a,1600));
    return true;
}

int main(void)
{
    const struct { const char *name; bool (*run)(void); } groups[]={
        {"configuration gates",configuration},{"explicit start and hold",lifecycle},
        {"selection limits and fractional rate",selection_and_limits},{"faults and timer wrap",guards},
        {"chassis interlock and queries",app_and_console},{"input perturbations",perturbations},
        {"mode transition requires separate unlock",mode_reunlock},
        {"dedicated K210 lift selection",lift_shortcut}};
    for(unsigned i=0;i<sizeof groups/sizeof groups[0];++i) {
        if(!groups[i].run())return 1;
        printf("PASS arm %s\n",groups[i].name);
    }
    printf("PASS arm: %u assertions (PC only)\n",checks);
    return 0;
}
