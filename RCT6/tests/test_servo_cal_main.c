/* 真实单路标定main，检查定位前后故障和SELECT优先，模拟硬件没有真实舵机。 */
#include "board_hal.h"
#include "board_servo.h"
#include "chassis_app.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

extern int rm_firmware_main(void);
extern rm_app_t g_app;
static jmp_buf done;
static unsigned now, scenario, command_position, command_index;
static bool unsafe, energized, adjusted, stopped, status_seen;
static uint16_t hardware_us, frozen_us;
static const unsigned command_times[] = {100,200,300,500,800,900};
static const char *commands[] = {"scal confirm\n","scal arm\n","scal center\n","scal step 20\n","stop\n","scal\n"};

static void mark_stop(void)
{
    if (!stopped) frozen_us = hardware_us;
    stopped = true;
}
bool board_init(void) { return true; }
uint32_t board_millis(void) { if (++now >= 1100) longjmp(done,1); return now; }
void board_panic(void) { longjmp(done,2); }
bool board_ps2_start(void) { return false; }
bool board_ps2_read(rm_remote_t *remote)
{
    *remote = (rm_remote_t){.valid=false,.at_ms=now};
    if (scenario == 6 && now >= 550) { remote->valid = remote->stop = true; mark_stop(); }
    return true;
}
bool board_motor_send(const char *frame)
{
    ++now;
    if (strcmp(frame,"$pwm:0,0,0,0#")) unsafe = true;
    if (scenario == 1 && now >= 530) { mark_stop(); return false; }
    return true;
}
bool board_motor_read(uint8_t *byte, uint32_t *at) { (void)byte; (void)at; return false; }
bool board_uart_fault(void)
{
    if (scenario == 2 && now >= 530) { mark_stop(); return true; }
    return false;
}
bool board_debug_fault(void)
{
    if (scenario == 3 && now >= 301) { mark_stop(); return true; }
    return false;
}
bool board_debug_read(uint8_t *byte, uint32_t *at)
{
    if (command_index >= 6 || now < command_times[command_index]) return false;
    *byte = (uint8_t)commands[command_index][command_position++];
    *at = now;
    if (!commands[command_index][command_position]) { ++command_index; command_position = 0; }
    return true;
}
void board_debug(const char *text)
{
    if (now >= 900 && strstr(text,"reply scal channel=")) status_seen = true;
    if (scenario == 4 && command_index == 3 && now < 400) { now = 530; mark_stop(); }
    else ++now;
}
bool board_servo_cal_init(void) { return scenario != 7; }
bool board_servo_cal_apply(uint16_t pulse, bool enabled)
{
    if (scenario == 5 && now >= 530) { mark_stop(); return false; }
    if (!enabled) {
        if (hardware_us || pulse) unsafe = true;
        return true;
    }
    if (now < 300 || pulse < 1400 || pulse > 1600 || (stopped && pulse != frozen_us)) unsafe = true;
    if (pulse > 1500) adjusted = true;
    hardware_us = pulse;
    energized = true;
    return true;
}
int main(void)
{
    for (scenario = 0; scenario < 8; ++scenario) {
        now = command_position = command_index = hardware_us = frozen_us = 0;
        unsafe = energized = adjusted = stopped = status_seen = false;
        int result = setjmp(done);
        if (!result) (void)rm_firmware_main();
        bool expect_output = scenario != 3 && scenario != 4 && scenario != 7;
        if (result != 1 || unsafe || energized != expect_output || (scenario == 0 && !adjusted) ||
            (scenario != 0 && scenario != 6 && !g_app.servo_cal.fault) ||
            (g_app.servo_cal.armed && now >= 1000) || status_seen != (scenario != 3)) {
            fprintf(stderr,"FAIL servo cal main scenario=%u result=%d unsafe=%u output=%u adjusted=%u fault=%u\n",
                scenario,result,unsafe,energized,adjusted,g_app.servo_cal.fault);
            return 1;
        }
        printf("PASS servo cal main scenario=%u\n",scenario);
    }
    return 0;
}
