/* 亚博驱动使用文字命令。这里负责检查格式和四轮对应关系，真正的串口发送在board_hal.c。 */
#ifndef RM_MOTOR_UART_H
#define RM_MOTOR_UART_H
#include "board_config.h"
#include <stddef.h>

#define RM_FRAME_CAP 112u /* 最多存111个内容字符，再留1字节表示结束；$和#不存进来 */
/* last_packet.kind决定下面哪个字段有意义，不要把电压帧的counts当新计数。 */
typedef enum { RM_PACKET_NONE, RM_PACKET_COUNT, RM_PACKET_DELTA,
               RM_PACKET_SPEED, RM_PACKET_BATTERY } rm_packet_kind_t;
typedef struct {
    rm_packet_kind_t kind;
    int32_t counts[RM_WHEELS]; /* 驱动M1/M2/M3/M4原始计数，还没按车轮顺序和正反方向调整 */
    float speeds[RM_WHEELS], battery_v; /* SPEED帧的四轮速度 / BATTERY帧的电压V */
    uint32_t at_ms; /* 正确收到整包时的时间，取最后一个字节到达的毫秒数 */
} rm_motor_packet_t;
typedef struct {
    char frame[RM_FRAME_CAP]; /* 正在接收的内容；没收齐时不能直接当字符串打印 */
    size_t length; /* 已经收了多少个内容字节 */
    bool collecting; /* 是否已经看到$、正在等待# */
    uint32_t malformed, unknown, last_byte_ms; /* 坏帧数 / 未知帧数 / 最近字节时间ms */
} rm_motor_parser_t;

void rm_motor_parser_init(rm_motor_parser_t *p);
/* 每次送进1个字节。整包收齐且检查通过时才返回true，并把结果写进packet。
 * false也可能只是“还没收齐”，是否有坏包要看malformed计数。 */
bool rm_motor_feed(rm_motor_parser_t *p, uint8_t byte, uint32_t now,
                   rm_motor_packet_t *packet);
/* 把四个数拼成文字，不在这里发送。true选spd(±1000)，false选pwm(±3600)。
 * 输入按M1～M4排列；数字超限或输出空间不够就返回false，不能发送不完整命令。 */
bool rm_motor_command(char *out, size_t cap, bool speed_mode,
                      const int32_t channel[RM_WHEELS]);
/* 应用FL/FR/RL/RR -> 驱动M1/M2/M3/M4，同时乘每轮±1方向。 */
bool rm_motor_map_command(const rm_config_t *c, const int32_t wheel[RM_WHEELS],
                          int32_t channel[RM_WHEELS]);
/* 把驱动计数排回程序内部的四轮顺序，供后续校准使用；控速仍由智能驱动完成。 */
bool rm_motor_unmap_counts(const rm_config_t *c,
                           const int32_t channel[RM_WHEELS],
                           int32_t wheel[RM_WHEELS]);
#endif
