/* 真实主循环模拟，重点检查三路互斥和STOP/故障后清零。 */
#include "board_hal.h"
#include "board_servo.h"
#include "chassis_app.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

extern int rm_firmware_main(void);
extern rm_app_t g_app;
static jmp_buf done;
static unsigned now, scenario, command_position;
static uint16_t hardware[3];
static bool unsafe, jumped, s2_down, s2_up, s3_up, s3_down, s4_up, s4_down;

bool board_init(void) { return true; }
uint32_t board_millis(void)
{
    ++now;
    if (scenario==9 && now>=1140 && !jumped) { now+=150; jumped=true; }
    if (now>=2200) longjmp(done,1);
    return now;
}
void board_panic(void) { longjmp(done,2); }
bool board_ps2_start(void) { return true; }
bool board_ps2_read(rm_remote_t *r)
{
    *r=(rm_remote_t){.valid=true,.at_ms=now};
    if (now>=600 && now<660) { r->arm=true; r->buttons=RM_PS2_START; }
    if ((now>=700 && now<780) || (now>=850 && now<930)) r->buttons=RM_PS2_SQUARE;
    if (now>=1000 && now<1040) r->buttons=RM_PS2_TRIANGLE;
    if (now>=1500 && now<1540) r->buttons=RM_PS2_CIRCLE;
    if ((now>=1100 && now<1200) || (now>=1600 && now<1700)) r->buttons=RM_PS2_UP;
    if ((now>=1300 && now<1400) || (now>=1800 && now<1900)) r->buttons=RM_PS2_DOWN;
    if (scenario==1 && now>=1150) r->stop=true;
    if (scenario==5 && now>=1150) r->valid=false;
    if (scenario==6 && now>=1150 && now<1200) r->buttons=RM_PS2_SQUARE | RM_PS2_UP;
    if (scenario==6 && now>=1200 && now<1250) r->buttons=RM_PS2_UP;
    if (scenario==7 && now>=1000 && now<1040) r->buttons=RM_PS2_TRIANGLE | RM_PS2_UP;
    if (scenario==7 && now>=1040 && now<1160) r->buttons=RM_PS2_UP;
    if (scenario==7 && now>=1160 && now<1250) r->buttons=0;
    if (scenario==11 && now>=1150 && now<1200) r->buttons=RM_PS2_DOWN;
    return true;
}
bool board_motor_send(const char *frame)
{
    ++now;
    if (strcmp(frame,"$pwm:0,0,0,0#")) unsafe=true;
    return true;
}
bool board_motor_read(uint8_t *byte, uint32_t *at) { (void)byte; (void)at; return false; }
bool board_uart_fault(void) { return scenario==2 && now>=1150; }
bool board_debug_fault(void) { return scenario==3 && now>=1150; }
bool board_debug_read(uint8_t *byte, uint32_t *at)
{
    const char *command="stop\n";
    if (scenario!=8 || now<1150 || !command[command_position]) return false;
    *byte=(uint8_t)command[command_position++]; *at=now;
    return true;
}
void board_debug(const char *text) { (void)text; ++now; }
bool board_servo_pair_init(void) { return scenario!=10; }
bool board_servo_pair_apply(const uint16_t pulse[3])
{
    if (scenario==4 && now>=1150 && pulse[1]) return false;
    unsigned active=(pulse[0]!=0)+(pulse[1]!=0)+(pulse[2]!=0);
    if (active>1 || (now<700 && active) || (scenario==10 && active)) unsafe=true;
    if ((scenario==1 || scenario==2 || scenario==3 || scenario==4 ||
         scenario==5 || scenario==8 || scenario==9) && now>=1200 && active) unsafe=true;
    if (scenario==6 && now>=1210 && now<1250 && pulse[1]) unsafe=true;
    if (scenario==7 && now<1160 && (pulse[1] || pulse[2])) unsafe=true;
    for (unsigned i=0; i<3; ++i) {
        if (pulse[i] && pulse[i]!=1470 && pulse[i]!=1530) unsafe=true;
        hardware[i]=pulse[i];
    }
    if (pulse[0]==1470) s2_down=true;
    if (pulse[0]==1530) s2_up=true;
    if (pulse[1]==1470) s3_up=true;
    if (pulse[1]==1530) s3_down=true;
    if (pulse[2]==1530) s4_up=true;
    if (pulse[2]==1470) s4_down=true;
    return true;
}
int main(void)
{
    for (scenario=0; scenario<12; ++scenario) {
        now=command_position=0; memset(hardware,0,sizeof hardware);
        unsafe=jumped=s2_down=s2_up=s3_up=s3_down=s4_up=s4_down=false;
        int result=setjmp(done);
        if (!result) (void)rm_firmware_main();
        bool expect_fault=scenario==2 || scenario==3 || scenario==4 || scenario==10;
        bool complete=scenario==0 || scenario==6 || scenario==7 || scenario==11;
        if (result!=1 || unsafe || hardware[0] || hardware[1] || hardware[2] ||
            g_app.shovel.fault!=expect_fault ||
            (scenario!=10 && (!s2_down || !s2_up)) ||
            (scenario!=7 && scenario!=10 && !s3_up) ||
            (complete && (!s3_down || !s4_up || !s4_down))) {
            fprintf(stderr,"FAIL arm pair main scenario=%u result=%d unsafe=%u fault=%u S2=%u/%u S3=%u/%u S4=%u/%u\n",
                scenario,result,unsafe,g_app.shovel.fault,s2_down,s2_up,s3_up,s3_down,s4_up,s4_down);
            return 1;
        }
        printf("PASS arm pair main scenario=%u\n",scenario);
    }
    return 0;
}
