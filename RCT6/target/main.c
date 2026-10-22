/**
 * @file main.c
 * @brief 整个程序从这里开始。先准备硬件，再反复“收消息、读手柄、检查、发命令”。
 *
 * 想改参数：src/board_config.c；想改按键：src/remote_ps2.c；想改接线：board_hal.c。
 * 这份main是本工程自己的入口，不能和CubeMX生成的另一个main一起编译。
 */
#include "board_hal.h"
#include "chassis_app.h"
#include "console.h"
#include "firmware_version.h"
#include <stdio.h>

/* 时间单位都是毫秒。控制目标是约20ms一次；日志和连接重试不用这么频繁。 */
enum { CONTROL_MS = 20, DEBUG_MS = 500, RETRY_MS = 500 };

/* 在调试器的Expressions/Watch中加g_app，就能看到这些信息：
 * remote：摇杆数值和按键；safety.state/inhibit：现在能不能动、为什么不能动；
 * motor_seen/motor_at_ms：收到过电机反馈吗、最后一包是什么时候；
 * parser.malformed：坏包数量；ramp：最后用于算轮速的车体速度。
 * 断点会暂停控制，观察这些变量前要断开电机动力。不要在调试器里强改解锁状态。 */
rm_app_t g_app;
rm_console_t g_console;
_Static_assert(RM_CONSOLE_REPLY_CAP <= BOARD_DEBUG_TX_CAP, "console reply exceeds debug UART limit");

/* 电脑通道出错后，撤销尚未开始或正在执行的标定，旧许可不能再使用。
 * 返回true表示这一次刚取消了标定；这里只改状态，由调用处负责发送PWM0。
 * 普通行驶仍由PS2安全流程判断，电脑通道故障不会替它清除或设置运动许可。 */
static bool cancel_calibration_on_console_fault(void)
{
    if (!board_debug_fault() && !g_console.failed) return false;
    if (!g_console.failed) rm_console_fault(&g_console);
    if (g_app.safety.state != RM_CALIBRATING && g_app.calibration.state == RM_CAL_OFF) return false;
    rm_app_stop(&g_app);
    return true;
}

/* 发送失败要记住：即使下一次成功，也要复位主控后才能重新解锁。 */
static bool send_motor(const char *frame)
{
    /* 算完输出后，中断仍可能刚报告电脑串口错误。发送前再检查一次；
     * 取消许可时必须同时替换手里的旧PWM文字，不能只把应用状态清零。 */
    if (cancel_calibration_on_console_fault()) frame = "$pwm:0,0,0,0#";
    if (board_motor_send(frame)) return true;
    rm_app_transport_fault(&g_app);
    return false;
}

/* 只负责恢复通信，不负责解锁。
 * 调用前，本轮的停车指令必须已经发出。ARMED表示仍允许运动，此时不做配置。
 * 普通的“暂时没数据”可以重试；坏包/UART错误等已记住的故障，仍需查线后复位。 */
static void retry_links(uint32_t *motor_retry_at, uint32_t *ps2_retry_at)
{
    if (rm_app_output_active(&g_app) || g_app.transport_fault) return;
    uint32_t now = board_millis();
    bool motor_fresh = g_app.motor_seen &&
        (uint32_t)(now - g_app.motor_at_ms) <= g_app.config.motor_timeout_ms;

    /* 驱动复位会忘记“上传编码器数据”的设置。没反馈时隔500ms重发一次。
     * 不改电机型号、PID或Flash参数；也不把“发出成功”当成“已经连接”。
     * 只有后面真正收到合法MAll包，motor_at_ms才会更新。 */
    if (!motor_fresh && (uint32_t)(now - *motor_retry_at) >= RETRY_MS) {
        if (!send_motor("$upload:1,0,0#")) return;
        *motor_retry_at = board_millis();
    }

    now = board_millis();
    if (!g_app.remote.valid && (uint32_t)(now - *ps2_retry_at) >= RETRY_MS) {
        /* 接收器晚启动或回到数字模式时，重新请求模拟模式。
         * start会读包确认模式；成功后再读一次，把带真实时间的结果放进g_app。
         * 即使读到了手柄，这里也不改变safety，下一轮仍按正常解锁规则检查。 */
        if (board_ps2_start()) (void)board_ps2_read(&g_app.remote);
        *ps2_retry_at = board_millis();
    }
}

/* 每轮最多处理一条电脑指令，避免大量查询回复拖延停车检查。 */
static void poll_console(void)
{
    if (cancel_calibration_on_console_fault()) (void)send_motor("$pwm:0,0,0,0#");
    if (g_console.failed) return;
    uint8_t byte;
    uint32_t at;
    unsigned budget = 32;
    while (budget-- && board_debug_read(&byte, &at)) {
        char response[RM_CONSOLE_REPLY_CAP];
        rm_console_action_t action;
        if (!rm_console_feed(&g_console, &g_app, byte, at, board_millis(),
                             response, sizeof response, &action)) continue;
        const char *request = action == RM_CONSOLE_STOP ? "$pwm:0,0,0,0#" :
            (action == RM_CONSOLE_READ_DRIVER ? "$read_flash#" :
             (action == RM_CONSOLE_READ_VOLTAGE ? "$read_vol#" : NULL));
        if (request && !send_motor(request)) board_debug(RM_CONSOLE_REPLY_PREFIX "ERR motor_uart_send_failed\r\n");
        else board_debug(response);
        /* 刚接受cal pulse也可能没能把回复发回电脑；现在就取消，不等下一轮控制。 */
        if (cancel_calibration_on_console_fault()) (void)send_motor("$pwm:0,0,0,0#");
        break;
    }
}

int main(void)
{
    /* 1. 设置时钟和串口，然后先发PWM0。硬件初始化失败就进入异常停车函数。 */
    if (!board_init()) board_panic();
    if (!board_motor_send("$pwm:0,0,0,0#")) board_panic();
    board_debug("\r\nRM RCT6 stage 1: default motion INHIBITED\r\n");
    board_debug("version=" RM_FIRMWARE_ID "\r\n");

    /* 2. 读取默认配置。未测量、未核实的项目会让安全检查拒绝解锁。 */
    rm_config_t config = rm_config_default();
    rm_app_init(&g_app, &config, board_millis());
    rm_console_init(&g_console);

    /* 3. 查询驱动的已有参数，并请求上传累计编码器数据。
     * 查询只发一次；上传开关如果没生效，下面的重试流程会再发。 */
    if (send_motor("$read_flash#")) (void)send_motor("$upload:1,0,0#");
    uint32_t motor_retry_at = board_millis();

    /* 4. 尝试读手柄。RM_PS2_WIRED默认是0，此时不会操作PS2引脚。
     * 启用接线后，只有确认读到模拟包，start才返回true；失败可以稍后再试。 */
    if (board_ps2_start()) (void)board_ps2_read(&g_app.remote);
    uint32_t ps2_retry_at = board_millis();
    uint32_t control_at = board_millis(), debug_at = control_at;

    for (;;) {
        uint8_t byte;
        uint32_t at;
        /* A. 处理电机发来的字节，每次最多128个，给后面的控制检查留时间。
         * at是字节到达的时间，不能换成现在，否则缓存里的旧包会被误当成新包。 */
        unsigned budget = 128;
        while (budget-- && board_motor_read(&byte, &at)) rm_app_receive(&g_app, byte, at);
        if (board_uart_fault()) rm_app_transport_fault(&g_app);

        /* B. 约每20ms读一次手柄。先做安全检查，再生成本轮要发给驱动的命令。 */
        uint32_t now = board_millis();
        if ((uint32_t)(now - control_at) >= CONTROL_MS) {
            control_at = now;
            (void)board_ps2_read(&g_app.remote);
            /* 读手柄要花几毫秒，其间电脑RX可能出错。先撤销标定，再生成本轮输出。 */
            (void)cancel_calibration_on_console_fault();
            char frame[48];
            /* false表示还没接物理急停状态输入。独立切断动力的开关仍需另外落实。 */
            if (!rm_app_step(&g_app, board_millis(), false, frame, sizeof frame) ||
                !send_motor(frame)) {
                rm_app_transport_fault(&g_app);
                (void)send_motor("$pwm:0,0,0,0#");
            } else {
                /* 先发本轮命令，再考虑重连。retry_links内部还会确认已处于禁动状态。 */
                retry_links(&motor_retry_at, &ps2_retry_at);
            }
        }

        poll_console();

        /* C. 每500ms通过板载CH340打印状态，串口设置115200、8N1。
         * 发送ps2指令可直接看摇杆，counts看编码器，help查看全部指令。
         * 重试可能让禁动期间的某一轮超过20ms；不要再加长时间HAL_Delay。 */
        now = board_millis();
        if ((uint32_t)(now - debug_at) >= DEBUG_MS) {
            debug_at = now;
            char log[160];
            /* state：0禁动、1已解锁、2故障、3软件急停、4单轮标定。
             * inhibit按十六进制显示，可同时有多项原因：
             * 01配置、02手柄、04电机反馈、08传输错误、10急停、20控制间隔过长。
             * motor_seen=0时还没收到过反馈，age_ms这时没有“反馈年龄”的意义。 */
            (void)snprintf(log, sizeof log,
                "state=%u inhibit=0x%02lX motor_seen=%u age_ms=%lu bad=%lu unknown=%lu cal=%u console_fault=%u\r\n",
                (unsigned)g_app.safety.state, (unsigned long)g_app.safety.inhibit,
                (unsigned)g_app.motor_seen, (unsigned long)(now - g_app.motor_at_ms),
                (unsigned long)g_app.parser.malformed, (unsigned long)g_app.parser.unknown,
                (unsigned)g_app.calibration.state, (unsigned)g_console.failed);
            board_debug(log);
        }
    }
}
