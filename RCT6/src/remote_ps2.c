/**
 * @file remote_ps2.c
 * @brief 读出PS2接收器的字节，再翻译成“前后、左右、旋转、解锁、停车”。
 *
 * 看代码顺序：rm_ps2_poll读一包 -> rm_ps2_decode解释按键/摇杆 ->
 * chassis_app.c先请safety.c检查能否运动，通过后才换成速度。
 * 改手柄对应关系主要看rm_ps2_decode；改STM32接线看target/board_hal.c。
 * 这里不直接控制电机。有些接收器会重复旧数据，所以读到正确格式不等于无线正常。
 * 测试先看g_app.remote。PS2接线开关默认关闭，此时valid=false是正常的。
 */
#include "remote_ps2.h"
#include <string.h>

/* 手柄原始值是0～255，中间128。这里换成-1～+1，让后面容易乘速度上限。
 * 左右两边的格数不同，所以分别除128和127。忽略中间小偏移的处理在mecanum.c。 */
static float axis(uint8_t raw)
{
    int n = (int)raw - 128;
    return n < 0 ? (float)n / 128.0f : (float)n / 127.0f;
}

bool rm_ps2_decode(const uint8_t *frame, size_t n, uint32_t now, rm_remote_t *out)
{
    /* 先清空上次结果。如果这次包坏了，就不能继续使用上次的摇杆和按键。 */
    *out = (rm_remote_t){.at_ms = now};
    /* 目前只认9字节的模拟包，前三个字节必须是FF 73 5A。
     * 0x41是数字模式，0x79是压力模式。要支持其他模式，必须完整实现对应协议。 */
    if (!frame || n != 9 || frame[0] != 0xff || frame[1] != 0x73 ||
        frame[2] != 0x5a) return false;
    /* frame[3]/[4]各存一组按键，注意：按下是0，松开反而是1。
     * frame[5]=右X，[6]=右Y（暂不用），[7]=左X，[8]=左Y。
     * 想交换摇杆功能，在下面三行改；轴值正负须与车体系一致。 */
    uint16_t buttons = (uint16_t)((uint16_t)frame[3] | ((uint16_t)frame[4] << 8));
    out->forward = -axis(frame[8]); /* 左摇杆上/下 -> 前进(+)/后退(-) */
    out->left = -axis(frame[7]);    /* 左摇杆左/右 -> 左横移(+)/右横移(-) */
    out->yaw = -axis(frame[5]);     /* 右摇杆左/右 -> 逆时针(+)/顺时针(-) */
    out->arm = (buttons & (1u << 3)) == 0;  /* START：只是申请解锁，最终由safety.c批准 */
    out->stop = (buttons & (1u << 0)) == 0; /* SELECT：软件禁动，不是硬件断动力急停 */
    /* valid只表示这包格式正确；接收器若重发旧值，关手柄后它也可能一直是true。 */
    out->valid = true;
    return true;
}

bool rm_ps2_transfer(const rm_ps2_bus_t *b, const uint8_t *tx, uint8_t *rx, size_t n)
{
    /* b里放着“拉高时钟、读取数据”等函数。这里安排顺序，board_hal.c决定具体引脚。 */
    if (!b || !b->select || !b->clock || !b->command || !b->data ||
        !b->delay_us || !tx || !rx || !n || n > 21) return false;
    /* 平时CLK为高电平；把ATT拉低表示开始通信，再等20微秒让接收器准备。 */
    b->clock(b->ctx, true);
    b->command(b->ctx, true);
    b->select(b->ctx, true);
    b->delay_us(b->ctx, 20);
    for (size_t i = 0; i < n; ++i) {
        rx[i] = 0;
        /* 一个字节有8位。PS2先发最低位bit0，再发bit1，一直发到bit7。 */
        for (unsigned bit = 0; bit < 8; ++bit) {
            b->command(b->ctx, (tx[i] & (1u << bit)) != 0);
            b->clock(b->ctx, false);
            b->delay_us(b->ctx, 20);
            b->clock(b->ctx, true);
            b->delay_us(b->ctx, 20);
            /* 时钟拉高后读这一位。这里的延时取20微秒，接实物后仍要验证能否稳定通信。 */
            if (b->data(b->ctx)) rx[i] |= (uint8_t)(1u << bit);
        }
        b->delay_us(b->ctx, 20);
    }
    /* 一包结束，把ATT恢复高电平。查通信波形时，观察ATT、CLK、CMD、DAT四根线。 */
    b->select(b->ctx, false);
    b->command(b->ctx, true);
    b->delay_us(b->ctx, 20);
    return true;
}

bool rm_ps2_configure_analog(const rm_ps2_bus_t *b)
{
    /* 先告诉接收器“我要设置”，再要求它用带摇杆数值的模拟模式，最后退出设置。
     * main只在车已禁动时调用这里；这一串操作约需16ms，不应插进正常行驶过程。 */
    static const uint8_t commands[][9] = {
        {1, 0x43, 0, 1, 0, 0, 0, 0, 0},
        {1, 0x44, 0, 1, 3, 0, 0, 0, 0},
        {1, 0x43, 0, 0, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a}
    };
    uint8_t rx[9];
    for (unsigned i = 0; i < 3; ++i) {
        if (!rm_ps2_transfer(b, commands[i], rx, sizeof rx)) return false;
        b->delay_us(b->ctx, 1000);
    }
    /* 发完设置还不算成功：再读一整包，确实收到FF 73 5A才返回true。
     * 接收器没上电、仍是数字模式或包不完整时返回false，主循环过一会儿会重试。
     * 这包只用来确认模式；正常读手柄时会重新读包并记录真实时间。 */
    rm_remote_t probe;
    return rm_ps2_poll(b, 0, &probe);
}

bool rm_ps2_poll(const rm_ps2_bus_t *b, uint32_t now, rm_remote_t *out)
{
    /* 0x42=轮询按键和摇杆；其余发送值为0，不在此控制震动电机。 */
    static const uint8_t tx[9] = {1, 0x42, 0, 0, 0, 0, 0, 0, 0};
    uint8_t rx[9];
    *out = (rm_remote_t){.at_ms = now};
    return rm_ps2_transfer(b, tx, rx, sizeof rx) &&
           rm_ps2_decode(rx, sizeof rx, now, out);
}
