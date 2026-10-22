/* 在PC上运行真正的main循环，把串口、PS2和时钟换成模拟设备。
 * 用来检查晚开机、断线恢复、重试频率和重新解锁，不接触实物。
 * PS2模型假设接收器复位后回到数字模式；实物是否这样仍需上板测试。 */
#include "board_hal.h"
#include "chassis_app.h"
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

extern int rm_firmware_main(void);
extern rm_app_t g_app;

typedef enum { NEUTRAL, DRIVE, HOLD_START, REARM } remote_script_t;
/* 这些错误只注入PC模型：分别模拟回指令失败、读手柄时接收出错、发送前刚出错。 */
typedef enum { DEBUG_OK, DEBUG_ARM_REPLY, DEBUG_PULSE_REPLY,
               DEBUG_POLL_PENDING, DEBUG_POLL_RUNNING, DEBUG_BEFORE_SEND } debug_fault_stage_t;
typedef struct {
    const char *name;
    unsigned motor_ready_ms, ps2_ready_ms, motor_reset_ms, ps2_reset_ms;
    unsigned uart_error_ms, bad_frame_ms, fail_upload_number;
    uint32_t start_tick;
    bool verified_config, reject_analog;
    remote_script_t remote_script;
    bool expect_motor_fresh, expect_remote, expect_fault, expect_armed;
    bool expect_initial_motion, expect_rearmed_motion;
    const char *commands;
    unsigned command_ms, debug_error_ms;
    bool enable_calibration, expect_calibration, stale_commands;
    debug_fault_stage_t debug_fault_stage;
} scenario_t;

static const scenario_t *scenario;
static jmp_buf finish;
static uint64_t elapsed_us;
static unsigned uploads, setups, polls, reads_of_flash;
static uint32_t upload_at, setup_at, last_poll_at;
static unsigned min_upload_gap, min_setup_gap, max_poll_gap;
static bool upload_enabled, analog, config_mode, motor_reset_done, ps2_reset_done;
static bool bad_frame_sent, sending_bad_frame, last_control_was_pwm0;
static bool unsafe_setup, unexpected_motion, premature_rearm;
static bool initial_motion, rearmed_motion, first_setup_ok;
static unsigned feedback_position, next_feedback_ms;
static uint8_t tx_bytes[21], rx_bytes[9];
static unsigned tx_bits, rx_bits;
static bool command_high;
static unsigned console_position, cal_started_ms, cal_stopped_ms;
static bool calibration_moved, cal_running, saw_ps2, saw_counts;
static bool debug_failed;
static unsigned calibration_outputs_after_fault;

static unsigned elapsed_ms(void) { return (unsigned)(elapsed_us / 1000u); }
static uint32_t tick(void) { return scenario->start_tick + elapsed_ms(); }

/* 模拟时间会在GPIO延时和串口发送时前进，避免把重试当成“不花时间”的动作。 */
static void advance(unsigned us)
{
    elapsed_us += us;
    if (scenario->motor_reset_ms && elapsed_ms() >= scenario->motor_reset_ms && !motor_reset_done) {
        upload_enabled = false;
        motor_reset_done = true;
    }
    if (scenario->ps2_reset_ms && elapsed_ms() >= scenario->ps2_reset_ms && !ps2_reset_done) {
        analog = config_mode = false;
        ps2_reset_done = true;
    }
}

/* 只在这个PC测试里临时提供完整参数，正式固件仍使用未验收的默认配置。 */
static rm_config_t test_config(void)
{
    rm_config_t c = rm_config_default();
    c.radius_m = 0.05f; c.half_length_m = c.half_width_m = 0.10f;
    c.geometry_verified = c.mapping_verified = c.motor_parameters_verified = true;
    c.ps2_wiring_verified = c.remote_failsafe_verified = c.driver_failsafe_verified = true;
    c.speed_unit = RM_SPEED_MM_S;
    for (unsigned i = 0; i < RM_WHEELS; ++i) c.direction[i] = 1;
    return c;
}

static void fill_remote(void)
{
    unsigned ms = elapsed_ms();
    bool arm = false, forward = false;
    if (scenario->remote_script == DRIVE) {
        arm = ms >= 400 && ms < 460;
        forward = ms >= 460;
    } else if (scenario->remote_script == HOLD_START || scenario->remote_script == REARM) {
        arm = ms >= 400;
        forward = ms >= 460 && ms < 900;
        if (scenario->remote_script == REARM) {
            arm = (ms >= 400 && ms < 1100) || (ms >= 1500 && ms < 1560);
            if (ms >= 1560) forward = true;
        }
    }
    if (arm) rx_bytes[3] &= (uint8_t)~(1u << 3);
    if (forward) rx_bytes[8] = 0;
}

static void select_bus(void *ctx, bool active)
{
    (void)ctx;
    if (active) {
        memset(tx_bytes, 0, sizeof tx_bytes);
        memset(rx_bytes, 0xff, sizeof rx_bytes);
        tx_bits = rx_bits = 0;
        if (elapsed_ms() >= scenario->ps2_ready_ms) {
            rx_bytes[1] = config_mode ? 0xf3 : (analog ? 0x73 : 0x41);
            rx_bytes[2] = 0x5a;
            for (unsigned i = 5; i < 9; ++i) rx_bytes[i] = 128;
            fill_remote();
        }
    } else if (elapsed_ms() >= scenario->ps2_ready_ms) {
        if (tx_bytes[1] == 0x43) config_mode = tx_bytes[3] == 1;
        if (tx_bytes[1] == 0x44 && config_mode && tx_bytes[3] == 1 && !scenario->reject_analog)
            analog = true;
    }
}
static void clock_bus(void *ctx, bool high)
{
    (void)ctx;
    if (!high && tx_bits < sizeof tx_bytes * 8) {
        if (command_high) tx_bytes[tx_bits / 8] |= (uint8_t)(1u << (tx_bits % 8));
        ++tx_bits;
    }
}
static void command_bus(void *ctx, bool high) { (void)ctx; command_high = high; }
static bool data_bus(void *ctx)
{
    (void)ctx;
    unsigned bit = rx_bits++;
    return bit < sizeof rx_bytes * 8 && (rx_bytes[bit / 8] & (1u << (bit % 8))) != 0;
}
static void delay_bus(void *ctx, uint32_t us) { (void)ctx; advance(us); }
static const rm_ps2_bus_t bus = {NULL, select_bus, clock_bus, command_bus, data_bus, delay_bus};

bool board_init(void) { return true; }
uint32_t board_millis(void)
{
    advance(1000);
    if (elapsed_ms() >= 2200) longjmp(finish, 1);
    return tick();
}
void board_panic(void) { longjmp(finish, 2); }

bool board_motor_send(const char *frame)
{
    unsigned ms = elapsed_ms();
    if (!strcmp(frame, "$upload:1,0,0#")) {
        if (uploads) {
            unsigned gap = tick() - upload_at;
            if (gap < min_upload_gap) min_upload_gap = gap;
        }
        upload_at = tick();
        ++uploads;
        if (!last_control_was_pwm0 || g_app.safety.state == RM_ARMED) unsafe_setup = true;
        if (uploads == scenario->fail_upload_number) return false;
        if (ms >= scenario->motor_ready_ms) upload_enabled = true;
    } else if (!strcmp(frame, "$read_flash#")) {
        ++reads_of_flash;
    } else {
        last_control_was_pwm0 = !strcmp(frame, "$pwm:0,0,0,0#");
        bool speed = !strncmp(frame, "$spd:", 5);
        bool moving = speed && strcmp(frame, "$spd:0,0,0,0#");
        bool cal_pwm = !strcmp(frame,"$pwm:50,0,0,0#");
        if (cal_pwm) {
            if (debug_failed || (scenario->debug_error_ms && ms >= scenario->debug_error_ms))
                ++calibration_outputs_after_fault;
            if (!scenario->enable_calibration || g_app.safety.state!=RM_CALIBRATING) unexpected_motion=true;
            if (!calibration_moved) cal_started_ms=ms;
            calibration_moved=cal_running=true;
        } else if (last_control_was_pwm0 && cal_running) {
            cal_stopped_ms=ms;
            cal_running=false;
        }
        if (!last_control_was_pwm0 && !speed && !cal_pwm) unexpected_motion = true;
        if (moving && !scenario->verified_config) unexpected_motion = true;
        if (moving && ms < 600) initial_motion = true;
        if (moving && ms >= 1560) rearmed_motion = true;
        if ((scenario->motor_reset_ms || scenario->ps2_reset_ms || scenario->uart_error_ms || scenario->bad_frame_ms)
            && ms >= 900 && ms < 1500 && g_app.safety.state == RM_ARMED)
            premature_rearm = true;
    }
    advance((unsigned)strlen(frame) * 87u);
    return true;
}
bool board_motor_read(uint8_t *byte, uint32_t *at)
{
    static const char good[] = "$MAll:0,0,0,0#";
    static const char bad[] = "$MAll:1,2,3#";
    if (!upload_enabled || elapsed_ms() < next_feedback_ms) return false;
    if (!feedback_position && scenario->bad_frame_ms && elapsed_ms() >= scenario->bad_frame_ms && !bad_frame_sent) {
        sending_bad_frame = true;
        bad_frame_sent = true;
    }
    const char *message = sending_bad_frame ? bad : good;
    *at = tick();
    *byte = (uint8_t)message[feedback_position++];
    if (!message[feedback_position]) {
        feedback_position = 0;
        sending_bad_frame = false;
        next_feedback_ms = elapsed_ms() + 20;
    }
    return true;
}
bool board_uart_fault(void)
{
    return scenario->uart_error_ms && elapsed_ms() >= scenario->uart_error_ms;
}
void board_debug(const char *message)
{
    size_t wire_length = strlen(message);
    /* 前缀不影响故障模型；同一组测试可对比修复前后的固件。 */
    if (!strncmp(message,"reply ",6)) message += 6;
    if(!strncmp(message,"ps2 ",4)) saw_ps2=true;
    if(!strncmp(message,"counts ",7)) saw_counts=true;
    if ((scenario->debug_fault_stage == DEBUG_ARM_REPLY && !strncmp(message,"OK cal_ready",12)) ||
        (scenario->debug_fault_stage == DEBUG_PULSE_REPLY && !strncmp(message,"OK one_pulse_queued",19))) {
        advance(20000); /* 对应真实调试串口发送的20ms等待上限。 */
        debug_failed=true;
        return;
    }
    advance((unsigned)wire_length * 87u);
}
bool board_debug_fault(void)
{
    /* pulse_started在生成首条PWM时置位，模拟计算后、发送前刚发生接收错误。 */
    if (scenario->debug_fault_stage == DEBUG_BEFORE_SEND && g_app.calibration.pulse_started)
        debug_failed=true;
    return debug_failed || (scenario->debug_error_ms && elapsed_ms()>=scenario->debug_error_ms);
}
bool board_debug_read(uint8_t *byte, uint32_t *at)
{
    if(!scenario->commands || elapsed_ms()<scenario->command_ms || !scenario->commands[console_position]) return false;
    *byte=(uint8_t)scenario->commands[console_position++];
    *at=tick()-(scenario->stale_commands ? 300u : 0u);
    return true;
}
bool board_ps2_start(void)
{
    if (!setups && scenario->verified_config) {
        g_app.config = test_config();
        g_app.config.calibration_enabled=scenario->enable_calibration;
    }
    if (setups) {
        unsigned gap = tick() - setup_at;
        if (gap < min_setup_gap) min_setup_gap = gap;
    }
    setup_at = tick();
    ++setups;
    if (!last_control_was_pwm0 || g_app.safety.state == RM_ARMED) unsafe_setup = true;
    bool ok = rm_ps2_configure_analog(&bus);
    if (setups == 1) first_setup_ok = ok;
    return ok;
}
bool board_ps2_read(rm_remote_t *remote)
{
    if (polls && tick() - last_poll_at > max_poll_gap) max_poll_gap = tick() - last_poll_at;
    last_poll_at = tick();
    ++polls;
    bool ok = rm_ps2_poll(&bus, tick(), remote);
    if (g_app.calibration.state == RM_CAL_PULSE &&
        ((scenario->debug_fault_stage == DEBUG_POLL_PENDING && !g_app.calibration.pulse_started) ||
         (scenario->debug_fault_stage == DEBUG_POLL_RUNNING && g_app.calibration.pulse_started)))
        debug_failed=true;
    return ok;
}

#define EXPECT(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL %s line %d: %s\n", scenario->name, __LINE__, #value); return false; \
} } while (0)

static bool run_scenario(const scenario_t *test)
{
    scenario = test;
    elapsed_us = uploads = setups = polls = reads_of_flash = 0;
    upload_at = setup_at = last_poll_at = 0;
    min_upload_gap = min_setup_gap = UINT_MAX; max_poll_gap = 0;
    upload_enabled = analog = config_mode = motor_reset_done = ps2_reset_done = false;
    bad_frame_sent = sending_bad_frame = last_control_was_pwm0 = false;
    unsafe_setup = unexpected_motion = premature_rearm = false;
    initial_motion = rearmed_motion = first_setup_ok = false;
    feedback_position = next_feedback_ms = 0;
    console_position=cal_started_ms=cal_stopped_ms=0;
    calibration_moved=cal_running=saw_ps2=saw_counts=false;
    debug_failed=false;
    calibration_outputs_after_fault=0;
    memset(&g_app, 0, sizeof g_app);
    int result = setjmp(finish);
    if (!result) (void)rm_firmware_main();
    EXPECT(result == 1);
    bool fresh = g_app.motor_seen && (uint32_t)(tick() - g_app.motor_at_ms) <= 200;
    EXPECT(fresh == test->expect_motor_fresh);
    EXPECT(g_app.remote.valid == test->expect_remote);
    EXPECT(g_app.transport_fault == test->expect_fault);
    EXPECT((g_app.safety.state == RM_ARMED) == test->expect_armed);
    EXPECT(initial_motion == test->expect_initial_motion);
    EXPECT(rearmed_motion == test->expect_rearmed_motion);
    EXPECT(calibration_moved == test->expect_calibration);
    EXPECT(calibration_outputs_after_fault == 0);
    if (test->debug_fault_stage != DEBUG_OK) EXPECT(debug_failed);
    if(calibration_moved) {
        EXPECT(!cal_running && cal_stopped_ms>=cal_started_ms);
        EXPECT(cal_stopped_ms-cal_started_ms<=240);
    }
    if(test->commands && strstr(test->commands,"ps2\n")) EXPECT(saw_ps2);
    if(test->commands && strstr(test->commands,"counts\n")) EXPECT(saw_counts);
    EXPECT(!unsafe_setup && !unexpected_motion && !premature_rearm);
    EXPECT(min_upload_gap >= 500 && min_setup_gap >= 500);
    EXPECT(max_poll_gap <= 100 && reads_of_flash == 1);
    if (test->ps2_ready_ms >= 300 || test->reject_analog) EXPECT(!first_setup_ok);
    else EXPECT(first_setup_ok);
    if (!test->motor_ready_ms && !test->motor_reset_ms) EXPECT(uploads == 1);
    if (!test->ps2_ready_ms && !test->ps2_reset_ms && !test->reject_analog) EXPECT(setups == 1);
    printf("PASS %s: upload=%u PS2_setup=%u max_poll_gap=%ums state=%u\n",
           test->name, uploads, setups, max_poll_gap, (unsigned)g_app.safety.state);
    return true;
}

int main(void)
{
    const scenario_t cases[] = {
        {.name="production config stays stopped", .remote_script=DRIVE,
         .expect_motor_fresh=true, .expect_remote=true},
        {.name="normal drive has no repeated setup", .verified_config=true, .remote_script=DRIVE,
         .expect_motor_fresh=true, .expect_remote=true, .expect_armed=true,
         .expect_initial_motion=true, .expect_rearmed_motion=true},
        {.name="motor starts late", .motor_ready_ms=300, .expect_motor_fresh=true, .expect_remote=true},
        {.name="PS2 starts late", .ps2_ready_ms=300, .expect_motor_fresh=true, .expect_remote=true},
        {.name="both devices absent", .motor_ready_ms=10000, .ps2_ready_ms=10000},
        {.name="PS2 refuses analog mode", .reject_analog=true, .expect_motor_fresh=true},
        {.name="motor restart with START held", .motor_reset_ms=600, .verified_config=true,
         .remote_script=HOLD_START, .expect_motor_fresh=true, .expect_remote=true, .expect_initial_motion=true},
        {.name="PS2 restart with START held", .ps2_reset_ms=600, .verified_config=true,
         .remote_script=HOLD_START, .expect_motor_fresh=true, .expect_remote=true, .expect_initial_motion=true},
        {.name="motor restart then deliberate rearm", .motor_reset_ms=600, .verified_config=true,
         .remote_script=REARM, .expect_motor_fresh=true, .expect_remote=true, .expect_armed=true,
         .expect_initial_motion=true, .expect_rearmed_motion=true},
        {.name="PS2 restart then deliberate rearm", .ps2_reset_ms=600, .verified_config=true,
         .remote_script=REARM, .expect_motor_fresh=true, .expect_remote=true, .expect_armed=true,
         .expect_initial_motion=true, .expect_rearmed_motion=true},
        {.name="UART error stays latched", .uart_error_ms=600, .verified_config=true,
         .remote_script=REARM, .expect_motor_fresh=true, .expect_remote=true, .expect_fault=true,
         .expect_initial_motion=true},
        {.name="bad frame stays latched", .bad_frame_ms=600, .verified_config=true,
         .remote_script=REARM, .expect_motor_fresh=true, .expect_remote=true, .expect_fault=true,
         .expect_initial_motion=true},
        {.name="initial upload TX failure", .fail_upload_number=1, .expect_remote=true, .expect_fault=true},
        {.name="retry upload TX failure", .motor_ready_ms=300, .fail_upload_number=2,
         .expect_remote=true, .expect_fault=true},
        {.name="late devices across tick wrap", .motor_ready_ms=300, .ps2_ready_ms=300,
         .start_tick=UINT32_MAX-250u, .expect_motor_fresh=true, .expect_remote=true},
        {.name="console queries during drive", .verified_config=true, .remote_script=DRIVE,
         .commands="ps2\ncounts\nconfig\noutput\n", .command_ms=600,
         .expect_motor_fresh=true,.expect_remote=true,.expect_armed=true,
         .expect_initial_motion=true,.expect_rearmed_motion=true},
        {.name="console stop cannot rearm held START", .verified_config=true, .remote_script=HOLD_START,
         .commands="stop\n", .command_ms=600, .expect_motor_fresh=true,.expect_remote=true,
         .expect_initial_motion=true},
        {.name="console one bounded calibration pulse", .verified_config=true,.enable_calibration=true,
         .commands="cal arm\ncal pulse 1 50 200\n",.command_ms=400,
         .expect_motor_fresh=true,.expect_remote=true,.expect_calibration=true},
        {.name="console error cancels calibration", .verified_config=true,.enable_calibration=true,
         .commands="cal arm\ncal pulse 1 50 200\n",.command_ms=400,.debug_error_ms=550,
         .expect_motor_fresh=true,.expect_remote=true,.expect_calibration=true},
        {.name="console stale pulse is rejected", .verified_config=true,.enable_calibration=true,
         .commands="cal arm\ncal pulse 1 50 200\n",.command_ms=400,.stale_commands=true,
         .expect_motor_fresh=true,.expect_remote=true},
        {.name="console queries cannot starve control",.verified_config=true,.remote_script=DRIVE,
         .commands="help\nhelp\nhelp\nhelp\nhelp\nhelp\nhelp\nhelp\nhelp\nhelp\n",.command_ms=600,
         .expect_motor_fresh=true,.expect_remote=true,.expect_armed=true,
          .expect_initial_motion=true,.expect_rearmed_motion=true},
        {.name="failed arm reply revokes calibration permission",.verified_config=true,.enable_calibration=true,
         .commands="cal arm\ncal pulse 1 50 200\n",.command_ms=400,.debug_fault_stage=DEBUG_ARM_REPLY,
         .expect_motor_fresh=true,.expect_remote=true},
        {.name="failed pulse reply cannot start a motor",.verified_config=true,.enable_calibration=true,
         .commands="cal arm\ncal pulse 1 50 200\n",.command_ms=400,.debug_fault_stage=DEBUG_PULSE_REPLY,
         .expect_motor_fresh=true,.expect_remote=true},
        {.name="RX fault during PS2 poll cancels pending pulse",.verified_config=true,.enable_calibration=true,
         .commands="cal arm\ncal pulse 1 50 200\n",.command_ms=400,.debug_fault_stage=DEBUG_POLL_PENDING,
         .expect_motor_fresh=true,.expect_remote=true},
        {.name="RX fault during PS2 poll stops running pulse",.verified_config=true,.enable_calibration=true,
         .commands="cal arm\ncal pulse 1 50 200\n",.command_ms=400,.debug_fault_stage=DEBUG_POLL_RUNNING,
         .expect_motor_fresh=true,.expect_remote=true,.expect_calibration=true},
        {.name="fault after calculation replaces pending PWM with zero",.verified_config=true,.enable_calibration=true,
         .commands="cal arm\ncal pulse 1 50 200\n",.command_ms=400,.debug_fault_stage=DEBUG_BEFORE_SEND,
         .expect_motor_fresh=true,.expect_remote=true},
        {.name="console fault leaves PS2 driving under its own safety checks",.verified_config=true,
         .remote_script=DRIVE,.debug_error_ms=600,.expect_motor_fresh=true,.expect_remote=true,
         .expect_armed=true,.expect_initial_motion=true,.expect_rearmed_motion=true}
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
        if (!run_scenario(&cases[i])) return 1;
    printf("PASS all startup/recovery scenarios: %u\n", (unsigned)(sizeof cases / sizeof cases[0]));
    return 0;
}
