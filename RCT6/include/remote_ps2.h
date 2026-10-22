/* PS2只负责读手柄。读到了按键，也必须经过安全检查才能让车动。 */
#ifndef RM_REMOTE_PS2_H
#define RM_REMOTE_PS2_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float forward, left, yaw; /* -1～+1：正值分别是向前、向左、逆时针 */
    uint32_t at_ms;           /* 这次读取接收器的时间ms；不代表无线手柄刚发了新数据 */
    bool valid, arm, stop;    /* 包是否合法 / START是否按下 / SELECT是否按下 */
} rm_remote_t;
/* 这些函数负责操作引脚。换主控板时改这些函数，按键和摇杆的解释可以继续用。 */
typedef struct {
    void *ctx; /* 给这些函数传递额外数据的位置；本板用不到，所以传NULL */
    void (*select)(void *ctx, bool active); /* ATT片选：active=true表示选中 */
    void (*clock)(void *ctx, bool high);    /* 输出CLK高/低 */
    void (*command)(void *ctx, bool high);  /* 输出CMD数据位 */
    bool (*data)(void *ctx);               /* 读取DAT数据位 */
    void (*delay_us)(void *ctx, uint32_t us); /* 微秒延时 */
} rm_ps2_bus_t;

/* 解释9字节模拟包。true仅说明格式正确，不能用它代替“关手柄能否停车”的实测。
 * frame放原始字节，now放毫秒时间，out得到-1～+1的摇杆数值和按键状态。 */
bool rm_ps2_decode(const uint8_t *frame, size_t n, uint32_t now, rm_remote_t *out);
/* 一次收发：tx是要发的字节，rx存收到的字节，n是字节数。这段时间函数会等通信完成。 */
bool rm_ps2_transfer(const rm_ps2_bus_t *b, const uint8_t *tx, uint8_t *rx, size_t n);
/* 请求模拟模式并读一包确认；true=读到合法模拟包，仍不能证明无线手柄在线。
 * 只能在已禁动时调用；失败后间隔至少500ms再试，避免频繁设置影响读手柄。 */
bool rm_ps2_configure_analog(const rm_ps2_bus_t *b);
/* 控制周期调用的入口：先收原始帧，再调用decode；失败时out.valid=false。 */
bool rm_ps2_poll(const rm_ps2_bus_t *b, uint32_t now, rm_remote_t *out);
#endif
