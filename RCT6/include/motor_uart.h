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
    float speeds[RM_WHEELS], battery_v; /* SPEED反馈单位mm/s，依据官方USART示例；电压单位V */
    uint32_t at_ms; /* 正确收到整包时的时间，取最后一个字节到达的毫秒数 */
} rm_motor_packet_t;
typedef struct {
    char frame[RM_FRAME_CAP]; /* 正在接收的内容；没收齐时不能直接当字符串打印 */
    size_t length; /* 已经收了多少个内容字节 */
    bool collecting; /* 是否已经看到$、正在等待# */
    uint32_t malformed, unknown, last_byte_ms; /* 坏帧数 / 未知帧数 / 最近字节时间ms */
} rm_motor_parser_t;

/* 驱动read_flash还会返回不带$和#的普通文字行。这里只记录收到的字段，
 * fields表示“收到了哪些项”，不表示参数正确，更不会自动放开运动许可。
 * 字段名称来自用户资料包FourWay_Motor_STM32.hex的输出格式，见DRIVER_REFERENCE.md。 */
enum {
    RM_DRIVER_VERSION=1u, RM_DRIVER_TYPE=2u, RM_DRIVER_DEADZONE=4u,
    RM_DRIVER_LINE=8u, RM_DRIVER_PHASE=16u, RM_DRIVER_DIAMETER=32u,
    RM_DRIVER_ALL_FIELDS=63u, RM_DRIVER_LINE_CAP=64u, RM_DRIVER_VERSION_CAP=17u
};
typedef struct {
    uint32_t fields, at_ms, last_byte_ms;
    uint32_t motor_type, dead_zone, pulse_line, pulse_phase;
    float wheel_diameter_mm;
    char version[RM_DRIVER_VERSION_CAP], line[RM_DRIVER_LINE_CAP];
    size_t length;
    bool dropping;
} rm_driver_info_t;

void rm_driver_info_init(rm_driver_info_t *info);
/* framed=true表示本字节属于$...#报文：不把报文里的文字误当配置行。
 * 本函数不更新电机链路健康时间；过长、截断和异常配置行也不会自动填写verified。 */
void rm_driver_info_feed(rm_driver_info_t *info, uint8_t byte, uint32_t now, bool framed);
/* wdiameter影响SPD对应的实际轮速。必须收到轮径字段，并与主控半径换算出的轮径
 * 按厂商回读的三位小数一致；只比对配置，不代表实际滚动尺寸已经标定。 */
bool rm_driver_diameter_matches(const rm_driver_info_t *info, const rm_config_t *config);

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
