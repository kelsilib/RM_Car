/* CH340串口的文字指令。这里只解释指令，不直接碰UART或透传原始电机命令。 */
#ifndef RM_CONSOLE_H
#define RM_CONSOLE_H
#include "chassis_app.h"
#define RM_CONSOLE_LINE_CAP 64u
/* 每条指令回复都带reply前缀，和自动状态/开机日志区分；留足空间保留完整驱动原文。 */
#define RM_CONSOLE_REPLY_PREFIX "reply "
#define RM_CONSOLE_REPLY_CAP 192u
typedef enum { RM_CONSOLE_NONE, RM_CONSOLE_STOP, RM_CONSOLE_READ_DRIVER, RM_CONSOLE_READ_VOLTAGE } rm_console_action_t;
typedef struct {
    char line[RM_CONSOLE_LINE_CAP];
    size_t length;
    uint32_t started_at;
    bool dropping, swallow_lf, failed;
} rm_console_t;
void rm_console_init(rm_console_t *c);
/* 调试串口出错后丢掉旧指令并禁止继续执行；修好并复位后恢复。 */
void rm_console_fault(rm_console_t *c);
/* true=一行已处理，out中有回复；每轮主循环最多处理一条，避免回复占用太久。
 * out必须至少RM_CONSOLE_REPLY_CAP字节。received_ms是实际接收时间，now是处理时间。 */
bool rm_console_feed(rm_console_t *c, rm_app_t *a, uint8_t byte, uint32_t received_ms,
                     uint32_t now, char *out, size_t cap, rm_console_action_t *action);
#endif
