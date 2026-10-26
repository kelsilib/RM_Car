/**
 * @file remote_ps2.c
 * @brief 读出PS2接收器的字节，再翻译成“前后、左右、旋转、解锁、停车”。
 *
 * 看代码顺序：rm_ps2_poll读一包 -> rm_ps2_decode解释按键/摇杆 ->
 * chassis_app.c先请safety.c检查能否运动，通过后才换成速度。
 * 改手柄对应关系主要看rm_ps2_decode；改STM32接线看target/board_hal.c。
 * 这里不直接控制电机。有些接收器会重复旧数据，所以读到正确格式不等于无线正常。
 * 测试先看g_app.remote。PS2接线开关默认关闭，此时valid=false是正常的。
 * 协议依据：官方资料页的STM32F1和PS2X附件，具体位置和差异见PS2_REFERENCE.md。
 */
#include "remote_ps2.h"

/* 这里按C数组从0编号，对照资料时要加1。例如资料“第2字节”就是frame[1]。 */
enum {
    PS2_MODE_BYTE = 1, PS2_MARKER_BYTE = 2,
    PS2_BUTTONS_LOW_BYTE = 3, PS2_BUTTONS_HIGH_BYTE = 4,
    PS2_RIGHT_X_BYTE = 5, PS2_LEFT_X_BYTE = 7, PS2_LEFT_Y_BYTE = 8,
    PS2_ANALOG_MODE = 0x73, PS2_REPLY_MARKER = 0x5a,
    PS2_SELECT_MASK = 1u << 0, PS2_START_MASK = 1u << 3,
    PS2_HALF_CLOCK_US = 16, /* STM32附件ax_ps2.c：低16us读DAT，再高16us。 */
    PS2_CONFIG_GAP_US = 1000, /* PS2X库首次配置时read_delay=1ms。 */
    PS2_MAX_TRANSFER_BYTES = 21 /* 底层收发上限；解码仍只接受9字节模拟包。 */
};

/* 手柄原始值是0～255，中间128。这里换成-1～+1，让后面容易乘速度上限。
 * 左右两边的格数不同，所以分别除128和127。忽略中间小偏移的处理在mecanum.c。 */
static float axis(uint8_t raw)
{
    int n = (int)raw - 128;
    return n < 0 ? (float)n / 128.0f : (float)n / 127.0f;
}

bool rm_ps2_decode(const uint8_t *frame, size_t n, uint32_t now, rm_remote_t *out)
{
    if (!out) return false;
    /* 先清空上次结果。如果这次包坏了，就不能继续使用上次的摇杆和按键。 */
    *out = (rm_remote_t){.at_ms = now};
    /* 只认完整9字节模拟包。资料把第1字节定义为idle，不能硬要求它等于FF。
     * 第2字节必须是73，第3字节必须是5A；首字节不参与判断不等于放行其他坏包。
     * 0x41是数字模式，0x79是压力模式。要支持其他模式，必须完整实现对应协议。 */
    if (!frame || n != RM_PS2_ANALOG_FRAME_BYTES || frame[PS2_MODE_BYTE] != PS2_ANALOG_MODE ||
        frame[PS2_MARKER_BYTE] != PS2_REPLY_MARKER) return false;
    /* frame[3]/[4]各存一组按键，注意：按下是0，松开反而是1。
     * frame[5]=右X，[6]=右Y（暂不用），[7]=左X，[8]=左Y。
     * 想交换摇杆功能，在下面三行改；轴值正负须与车体系一致。 */
    uint16_t buttons = (uint16_t)((uint16_t)frame[PS2_BUTTONS_LOW_BYTE] |
                                 ((uint16_t)frame[PS2_BUTTONS_HIGH_BYTE] << 8));
    out->forward = -axis(frame[PS2_LEFT_Y_BYTE]); /* 左摇杆上/下 -> 前进(+)/后退(-) */
    out->left = -axis(frame[PS2_LEFT_X_BYTE]);    /* 左摇杆左/右 -> 左横移(+)/右横移(-) */
    out->yaw = -axis(frame[PS2_RIGHT_X_BYTE]);    /* 右摇杆左/右 -> 逆时针(+)/顺时针(-) */
    out->arm = (buttons & PS2_START_MASK) == 0;  /* START：申请解锁，由safety.c批准 */
    out->stop = (buttons & PS2_SELECT_MASK) == 0; /* SELECT：软件禁动，不是硬件断动力 */
    /* valid只表示这包格式正确；接收器若重发旧值，关手柄后它也可能一直是true。 */
    out->valid = true;
    return true;
}

/* 一个字节先发bit0，再到bit7。严格保留附件中的“低时钟 -> 等待 -> 读 -> 高时钟”。
 * 不要把data()移到拉高CLK之后；tests/test_ps2.c会专门检查这个顺序。 */
static uint8_t transfer_byte(const rm_ps2_bus_t *b, uint8_t command)
{
    uint8_t received = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        b->command(b->ctx, (command & (1u << bit)) != 0);
        b->clock(b->ctx, false);
        b->delay_us(b->ctx, PS2_HALF_CLOCK_US);
        if (b->data(b->ctx)) received |= (uint8_t)(1u << bit);
        b->clock(b->ctx, true);
        b->delay_us(b->ctx, PS2_HALF_CLOCK_US);
    }
    return received;
}

bool rm_ps2_transfer(const rm_ps2_bus_t *b, const uint8_t *tx, uint8_t *rx, size_t n)
{
    /* b里放着“拉高时钟、读取数据”等函数。这里安排顺序，board_hal.c决定具体引脚。 */
    if (!b || !b->select || !b->clock || !b->command || !b->data ||
        !b->delay_us || !tx || !rx || !n || n > PS2_MAX_TRANSFER_BYTES) return false;
    /* 空闲时CLK/CMD为高。整包期间一直选中，不能每读完一个字节就松开ATT。
     * 按STM32附件连续传字节，每位已留16us高电平；没有额外插入字节间延时。 */
    b->clock(b->ctx, true);
    b->command(b->ctx, true);
    b->select(b->ctx, true);
    for (size_t i = 0; i < n; ++i) rx[i] = transfer_byte(b, tx[i]);
    /* 一包结束，把ATT恢复高电平。查通信波形时，观察ATT、CLK、CMD、DAT四根线。 */
    b->select(b->ctx, false);
    b->command(b->ctx, true);
    return true;
}

bool rm_ps2_configure_analog(const rm_ps2_bus_t *b)
{
    /* 同页PS2X库：进入设置用5字节，设置模拟模式和退出各9字节。
     * 每包设置后等1ms，再读包确认。本函数只试一次，失败由main隔500ms重试。
     * 所有显式延时合计约11.2ms，另有GPIO调用和中断耗时；只能在已禁动时调用。 */
    static const uint8_t enter[] = {1, 0x43, 0, 1, 0};
    static const uint8_t analog[] = {1, 0x44, 0, 1, 3, 0, 0, 0, 0};
    static const uint8_t leave[] = {1, 0x43, 0, 0, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a};
    const struct { const uint8_t *data; size_t length; } commands[] = {
        {enter, sizeof enter}, {analog, sizeof analog}, {leave, sizeof leave}
    };
    uint8_t rx[RM_PS2_ANALOG_FRAME_BYTES];
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        if (!rm_ps2_transfer(b, commands[i].data, rx, commands[i].length)) return false;
        b->delay_us(b->ctx, PS2_CONFIG_GAP_US);
    }
    /* 发完设置还不算成功：再读完整9字节，模式73和标记5A都正确才返回true。
     * 接收器没上电、仍是数字模式或包不完整时返回false，主循环过一会儿会重试。
     * 这包只用来确认模式；正常读手柄时会重新读包并记录真实时间。 */
    rm_remote_t probe;
    return rm_ps2_poll(b, 0, &probe);
}

bool rm_ps2_poll(const rm_ps2_bus_t *b, uint32_t now, rm_remote_t *out)
{
    if (!out) return false;
    /* 0x42=轮询按键和摇杆；其余发送值为0，不在此控制震动电机。 */
    static const uint8_t tx[RM_PS2_ANALOG_FRAME_BYTES] = {1, 0x42, 0, 0, 0, 0, 0, 0, 0};
    uint8_t rx[RM_PS2_ANALOG_FRAME_BYTES];
    *out = (rm_remote_t){.at_ms = now};
    return rm_ps2_transfer(b, tx, rx, sizeof rx) &&
           rm_ps2_decode(rx, sizeof rx, now, out);
}
