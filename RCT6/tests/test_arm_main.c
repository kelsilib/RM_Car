/* 跑真正的main，模拟“算好新PWM后、写硬件之前”发生故障；不连接任何设备。 */
#include "board_hal.h"
#include "board_servo.h"
#include "chassis_app.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

extern int rm_firmware_main(void);
extern rm_app_t g_app;
typedef enum { NORMAL, DEFAULT_DISABLED, TX_INITIAL, TX_MOVING, UART_DURING_SEND,
    UART_DURING_POLL, CONSOLE_DURING_POLL, SERVO_WRITE_FAIL, STALE_REMOTE, MODE_EXIT, SELECT_STOP } fault_t;
static fault_t fault;
static jmp_buf done;
static uint32_t clock_ms;
static bool uart_failed, debug_failed, faulted, unsafe, energized, adjusted, motor_zero;
static uint16_t hardware_us[RM_SERVOS], frozen_us;

static rm_arm_config_t fixture(void)
{
    rm_arm_config_t c={.enabled=true,.wiring_verified=true,.power_verified=true,
        .base_safe_verified=true,.fault_hold_verified=true,.period_us=20000};
    c.servo[0]=(rm_servo_config_t){.enabled=true,.limits_verified=true,.start_pose_verified=true,
        .min_us=1000,.max_us=2000,.start_us=1500,.max_rate_us_s=1000,.direction=1};
    return c;
}
bool board_init(void) { return true; }
uint32_t board_millis(void)
{
    if(++clock_ms>1250)longjmp(done,1);
    return clock_ms;
}
void board_panic(void) { longjmp(done,2); }
bool board_servo_init(const rm_arm_config_t *config)
{
    (void)config;
    if(fault==DEFAULT_DISABLED)return false;
    rm_arm_config_t c=fixture();rm_arm_init(&g_app.arm,&c,clock_ms);
    g_app.config.ps2_wiring_verified=g_app.config.remote_failsafe_verified=true;
    return true;
}
static void mark_fault(void)
{
    if(!faulted)frozen_us=hardware_us[0];
    faulted=true;
}
bool board_motor_send(const char *frame)
{
    motor_zero=!strcmp(frame,"$pwm:0,0,0,0#");
    if(!motor_zero && strcmp(frame,"$read_flash#") && strcmp(frame,"$upload:1,0,0#"))unsafe=true;
    if(fault==TX_INITIAL && g_app.arm.output_mask && !g_app.arm.applied_mask) {
        mark_fault();return false;
    }
    if(clock_ms>=650 && (fault==TX_MOVING || fault==UART_DURING_SEND)) {
        mark_fault();
        if(fault==TX_MOVING)return false;
        uart_failed=true;
    }
    return true;
}
bool board_motor_read(uint8_t *byte,uint32_t *at) { (void)byte;(void)at;return false; }
bool board_uart_fault(void) { return uart_failed; }
void board_debug(const char *s) { (void)s; }
bool board_debug_fault(void) { return debug_failed; }
bool board_debug_read(uint8_t *byte,uint32_t *at) { (void)byte;(void)at;return false; }
bool board_ps2_start(void) { return false; }
bool board_ps2_read(rm_remote_t *remote)
{
    uint16_t buttons=RM_PS2_L2;
    if(clock_ms>=420 && clock_ms<470)buttons|=RM_PS2_START;
    if(clock_ms>=500 && clock_ms<1000)buttons|=RM_PS2_R2;
    if(fault==MODE_EXIT && clock_ms>=650) {
        buttons=RM_PS2_START;mark_fault();
    }
    if(fault==SELECT_STOP && clock_ms>=650) { buttons|=RM_PS2_SELECT;mark_fault(); }
    *remote=(rm_remote_t){.valid=true,.at_ms=clock_ms,.buttons=buttons,
        .arm=(buttons&RM_PS2_START)!=0,.stop=(buttons&RM_PS2_SELECT)!=0,
        .right_y=(buttons&RM_PS2_R2)?1.0f:0.0f};
    if(clock_ms>=650) {
        if(fault==UART_DURING_POLL) { mark_fault();uart_failed=true; }
        if(fault==CONSOLE_DURING_POLL) { mark_fault();debug_failed=true; }
        if(fault==STALE_REMOTE) { mark_fault();remote->at_ms-=201; }
    }
    return true;
}
bool board_servo_apply(const uint16_t pulse_us[RM_SERVOS],uint8_t mask)
{
    if(fault==SERVO_WRITE_FAIL && mask && !g_app.arm.applied_mask) { mark_fault();return false; }
    if(mask && !motor_zero)unsafe=true;
    if(faulted && pulse_us[0]!=frozen_us)unsafe=true;
    for(unsigned i=1;i<RM_SERVOS;++i)if(pulse_us[i])unsafe=true;
    if(mask & ~1u)unsafe=true;
    if(mask) {
        if(pulse_us[0]<1000 || pulse_us[0]>2000)unsafe=true;
        if(hardware_us[0] && pulse_us[0]!=hardware_us[0]) {
            if(!g_app.arm.armed || !(g_app.remote.buttons&RM_PS2_R2))unsafe=true;
            adjusted=true;
        }
        energized=true;
    }
    memcpy(hardware_us,pulse_us,sizeof hardware_us);
    return true;
}

int main(void)
{
    const char *names[]={"normal one-channel control","default disabled",
        "motor TX fails before first pulse","motor TX fails before new pulse",
        "UART fault during motor send","UART fault while polling PS2",
        "console fault while polling PS2","servo write fails before first pulse",
        "stale remote cannot update pulse","mode exit holds position","SELECT overrides movement"};
    /* 测试用longjmp跳出无限主循环；跨越setjmp保留的循环编号必须有明确存储。 */
    for(volatile unsigned i=0;i<sizeof names/sizeof names[0];++i) {
        fault=(fault_t)i;clock_ms=0;
        uart_failed=debug_failed=faulted=unsafe=energized=adjusted=motor_zero=false;
        memset(hardware_us,0,sizeof hardware_us);frozen_us=0;
        int status=setjmp(done);
        if(!status)(void)rm_firmware_main();
        bool expect_output=fault!=DEFAULT_DISABLED && fault!=TX_INITIAL && fault!=SERVO_WRITE_FAIL;
        if(status!=1 || unsafe || energized!=expect_output || adjusted!=expect_output ||
            (faulted && hardware_us[0]!=frozen_us) ||
            (fault!=NORMAL && g_app.arm.armed)) {
            fprintf(stderr,"FAIL arm main %s: status=%d unsafe=%u energized=%u adjusted=%u pulse=%u frozen=%u\n",
                names[i],status,(unsigned)unsafe,(unsigned)energized,(unsigned)adjusted,
                (unsigned)hardware_us[0],(unsigned)frozen_us);
            return 1;
        }
        printf("PASS arm main %s\n",names[i]);
    }
    puts("PASS arm main scenarios: 11");
    return 0;
}
