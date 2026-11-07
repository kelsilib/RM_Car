/* 真实S2 main的必要回归，检查松键、SELECT、stop和IO故障后的输出顺序。 */
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
static uint16_t hardware_us, first_pulse;
static bool unsafe, up_seen, down_seen, released, jumped;

bool board_init(void) { return true; }
uint32_t board_millis(void)
{
    ++now;
    if (scenario==4 && now>=740 && !jumped) { now+=150; jumped=true; }
    if (now>=(scenario==9 ? 6900u : 1300u)) longjmp(done,1);
    return now;
}
void board_panic(void) { longjmp(done,2); }
bool board_ps2_start(void) { return true; }
bool board_ps2_read(rm_remote_t *r)
{
    *r=(rm_remote_t){.valid=true,.at_ms=now};
    if (scenario!=8 && now>=600 && now<640) { r->arm=true; r->buttons=RM_PS2_START; }
    if (scenario==8) r->buttons=RM_PS2_SQUARE;
    else if (scenario==9) {
        if ((now>=700 && now<6700) || (now>=6750 && now<6800)) r->buttons=RM_PS2_SQUARE;
    } else {
        if (now>=700 && now<800) r->buttons=RM_PS2_SQUARE;
        if (now>=900 && now<1000) r->buttons=RM_PS2_SQUARE;
    }
    if (scenario==1 && now>=750) r->stop=true;
    if (scenario==7 && now>=750) r->at_ms=now-201;
    if (scenario==10 && now>=750 && now<800) r->buttons=RM_PS2_CIRCLE|RM_PS2_SQUARE;
    if (scenario==10 && now>=800 && now<850) r->buttons=RM_PS2_CIRCLE;
    return true;
}
bool board_motor_send(const char *frame)
{
    ++now;
    if (strcmp(frame,"$pwm:0,0,0,0#")) unsafe=true;
    return true;
}
bool board_motor_read(uint8_t *byte, uint32_t *at) { (void)byte; (void)at; return false; }
bool board_uart_fault(void) { return scenario==2 && now>=750; }
bool board_debug_fault(void) { return scenario==3 && now>=750; }
bool board_debug_read(uint8_t *byte, uint32_t *at)
{
    const char *command="stop\n";
    if (scenario!=6 || now<750 || !command[command_position]) return false;
    *byte=(uint8_t)command[command_position++]; *at=now;
    return true;
}
void board_debug(const char *text) { (void)text; ++now; }
bool board_servo_cal_init(void) { return scenario!=11; }
bool board_servo_cal_apply(uint16_t pulse, bool enabled)
{
    if (scenario==5 && now>=750 && enabled) return false;
    if (!enabled) {
        if (pulse) unsafe=true;
        if (hardware_us) released=true;
        hardware_us=0;
        return true;
    }
    if (now<700 || (pulse!=1530 && pulse!=1470)) unsafe=true;
    if (scenario>=1 && scenario<=7 && now>=790) unsafe=true;
    if (scenario==8 || scenario==11) unsafe=true;
    if (scenario==9 && now>=5740 && now<6700) unsafe=true;
    if (scenario==10 && now>=840 && now<900) unsafe=true;
    if (scenario==0 && now>=840 && now<900) unsafe=true;
    if (pulse==1530) up_seen=true;
    if (pulse==1470) down_seen=true;
    if (!first_pulse) first_pulse=pulse;
    hardware_us=pulse;
    return true;
}
int main(void)
{
    for (scenario=0; scenario<12; ++scenario) {
        now=command_position=hardware_us=first_pulse=0;
        unsafe=up_seen=down_seen=released=jumped=false;
        int result=setjmp(done);
        if (!result) (void)rm_firmware_main();
        bool expect_output=scenario!=8 && scenario!=11;
        bool expect_fault=scenario==2 || scenario==3 || scenario==5 || scenario==11;
        if (result!=1 || unsafe || hardware_us || down_seen!=expect_output ||
            (expect_output && !released) || g_app.shovel.fault!=expect_fault ||
            (expect_output && first_pulse!=1470) || ((scenario==0 || scenario==10) && !up_seen)) {
            fprintf(stderr,"FAIL shovel main scenario=%u result=%d unsafe=%u hw=%u up=%u down=%u released=%u fault=%u\n",
                scenario,result,unsafe,hardware_us,up_seen,down_seen,released,g_app.shovel.fault);
            return 1;
        }
        printf("PASS shovel main scenario=%u\n",scenario);
    }
    return 0;
}
