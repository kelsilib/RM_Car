/* 按官方附件检查PS2协议，而不是只给程序喂一串预先排好的位。
 * 模拟接收器会检查：片选是否有效、CMD是否先准备好、DAT是否在CLK低时读取。
 * 依据和资料内部的差异见PS2_REFERENCE.md；这里只测软件，不代表实物波形合格。 */
#include "remote_ps2.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(value) do { ++checks; if (!(value)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); return false; \
} } while (0)
#define NEAR(a,b) (fabsf((a)-(b)) < 0.00001f)

/* 最多记录一次配置的四包，分别是进入、设置、退出、确认轮询。
 * 21字节只用于检查底层接口边界，不代表应用接受压力模式。 */
typedef struct {
    uint8_t reply[4][21], sent[4][21];
    size_t length[4];
    uint32_t begin_us[4], end_us[4], now_us, fall_us, rise_us;
    unsigned frames, bits, samples, callbacks;
    bool selected, clock_high, command_high, phase_ok, timing_ok;
} wire_t;

static void wire_init(wire_t *w)
{
    memset(w, 0, sizeof *w);
    memset(w->reply, 0xff, sizeof w->reply);
    w->clock_high = w->command_high = w->phase_ok = w->timing_ok = true;
}

static void select_line(void *ctx, bool active)
{
    wire_t *w = ctx;
    ++w->callbacks;
    if (w->frames >= 4) { w->phase_ok = false; return; }
    if (active) {
        if (w->selected || !w->clock_high || !w->command_high) w->phase_ok = false;
        w->bits = w->samples = 0;
        w->begin_us[w->frames] = w->now_us;
    } else {
        if (!w->selected || !w->clock_high || w->bits % 8 || w->samples != w->bits)
            w->phase_ok = false;
        w->length[w->frames] = w->bits / 8;
        w->end_us[w->frames] = w->now_us;
        ++w->frames;
    }
    w->selected = active;
}

static void clock_line(void *ctx, bool high)
{
    wire_t *w = ctx;
    ++w->callbacks;
    if (!high) {
        if (!w->selected || !w->clock_high || w->frames >= 4 || w->bits >= 21 * 8) {
            w->phase_ok = false;
            return;
        }
        if (w->bits && w->now_us - w->rise_us < 16) w->timing_ok = false;
        /* 下降沿记录主控送来的位；先bit0，再bit1……，不能倒过来。 */
        if (w->command_high) w->sent[w->frames][w->bits / 8] |= (uint8_t)(1u << (w->bits % 8));
        ++w->bits;
        w->fall_us = w->now_us;
    } else if (w->selected && !w->clock_high) {
        /* 每个低电平期间必须已经读过一次DAT，才允许把CLK拉高。 */
        if (w->samples != w->bits) w->phase_ok = false;
        if (w->now_us - w->fall_us < 16) w->timing_ok = false;
        w->rise_us = w->now_us;
    }
    w->clock_high = high;
}

static void command_line(void *ctx, bool high)
{
    wire_t *w = ctx;
    ++w->callbacks;
    if (w->selected && !w->clock_high) w->phase_ok = false;
    w->command_high = high;
}

static bool data_line(void *ctx)
{
    wire_t *w = ctx;
    ++w->callbacks;
    if (!w->selected || w->clock_high || w->samples + 1 != w->bits) w->phase_ok = false;
    if (w->now_us - w->fall_us != 16) w->timing_ok = false;
    unsigned bit = w->samples++;
    return w->frames < 4 && bit < 21 * 8 &&
           (w->reply[w->frames][bit / 8] & (1u << (bit % 8))) != 0;
}

static void delay_line(void *ctx, uint32_t us)
{
    wire_t *w = ctx;
    ++w->callbacks;
    w->now_us += us;
}

static rm_ps2_bus_t make_bus(wire_t *w)
{
    return (rm_ps2_bus_t){w, select_line, clock_line, command_line, data_line, delay_line};
}

static bool cleared(const rm_remote_t *r, uint32_t now)
{
    return !r->valid && !r->arm && !r->stop && r->forward == 0 &&
           r->left == 0 && r->yaw == 0 && r->at_ms == now;
}

static bool test_timing(void)
{
    wire_t w;
    wire_init(&w);
    rm_ps2_bus_t b = make_bus(&w);
    /* 非对称的位型能发现最低位/最高位次序写反、发送和接收互相串位。 */
    const uint8_t tx[] = {0x01,0x42,0x96,0x69,0xa5,0x5a,0x80,0x01,0x00};
    const uint8_t expected[] = {0x00,0x73,0x5a,0xf7,0xff,0x03,0x81,0xfe,0x40};
    uint8_t rx[sizeof tx];
    memcpy(w.reply[0], expected, sizeof expected);
    CHECK(rm_ps2_transfer(&b, tx, rx, sizeof tx));
    CHECK(w.phase_ok);
    CHECK(w.timing_ok);
    CHECK(w.frames == 1 && w.length[0] == 9 && w.samples == 72);
    CHECK(!w.selected && w.clock_high && w.command_high);
    CHECK(!memcmp(w.sent[0], tx, sizeof tx) && !memcmp(rx, expected, sizeof rx));
    CHECK(w.now_us == 9u * 8u * 32u); /* STM32附件：每位低16us、高16us。 */

    /* 正常轮询必须只发01 42和补零，不能意外打开震动或发送配置。 */
    wire_init(&w);
    memcpy(w.reply[0], expected, sizeof expected);
    rm_remote_t r;
    const uint8_t poll[] = {1,0x42,0,0,0,0,0,0,0};
    CHECK(rm_ps2_poll(&b, 123, &r) && r.valid && r.at_ms == 123);
    CHECK(w.phase_ok && w.timing_ok && w.frames == 1 && w.length[0] == sizeof poll);
    CHECK(!memcmp(w.sent[0], poll, sizeof poll));
    return true;
}

static bool test_configuration(void)
{
    /* 数组长度也是协议的一部分；不能把5字节进入命令自动补到9字节。 */
    const uint8_t enter[] = {1,0x43,0,1,0};
    const uint8_t mode[] = {1,0x44,0,1,3,0,0,0,0};
    const uint8_t leave[] = {1,0x43,0,0,0x5a,0x5a,0x5a,0x5a,0x5a};
    const uint8_t poll[] = {1,0x42,0,0,0,0,0,0,0};
    const uint8_t analog[] = {0xff,0x73,0x5a,0xff,0xff,128,128,128,128};
    wire_t w;
    wire_init(&w);
    rm_ps2_bus_t b = make_bus(&w);
    memcpy(w.reply[3], analog, sizeof analog);
    CHECK(rm_ps2_configure_analog(&b));
    CHECK(w.frames == 4 && w.length[0] == sizeof enter && w.length[1] == sizeof mode &&
          w.length[2] == sizeof leave && w.length[3] == sizeof poll);
    CHECK(!memcmp(w.sent[0], enter, sizeof enter));
    CHECK(!memcmp(w.sent[1], mode, sizeof mode));
    CHECK(!memcmp(w.sent[2], leave, sizeof leave));
    CHECK(!memcmp(w.sent[3], poll, sizeof poll));
    for (unsigned i = 1; i < 4; ++i) CHECK(w.begin_us[i] - w.end_us[i-1] == 1000);
    CHECK(w.phase_ok && w.timing_ok && !w.selected && w.clock_high && w.command_high);
    CHECK(w.now_us <= 20000); /* 一次设置只尝试一遍，不在此阻塞重试。 */

    /* 发出配置不等于成功。确认包缺失、数字/压力/配置模式或标记错误都要失败。 */
    const uint8_t wrong_modes[] = {0xff,0x41,0x79,0xf3};
    for (size_t i = 0; i < sizeof wrong_modes; ++i) {
        wire_init(&w);
        memcpy(w.reply[3], analog, sizeof analog);
        w.reply[3][1] = wrong_modes[i];
        CHECK(!rm_ps2_configure_analog(&b) && w.frames == 4);
    }
    wire_init(&w);
    CHECK(!rm_ps2_configure_analog(&b)); /* 未接收器：一直读到上拉高电平。 */
    wire_init(&w);
    memcpy(w.reply[3], analog, sizeof analog);
    w.reply[3][2] = 0;
    CHECK(!rm_ps2_configure_analog(&b));
    return true;
}

static bool test_decode(void)
{
    uint8_t frame[21] = {0xff,0x73,0x5a,0xff,0xff,128,128,128,128};
    rm_remote_t r;
    /* 附件把第一个返回字节写作idle；它不是必须匹配FF的帧头。 */
    for (unsigned idle = 0; idle < 256; ++idle) {
        frame[0] = (uint8_t)idle;
        CHECK(rm_ps2_decode(frame, 9, 42, &r) && r.valid && r.at_ms == 42);
    }
    CHECK(!r.arm && !r.stop && NEAR(r.forward,0) && NEAR(r.left,0) && NEAR(r.yaw,0));
    /* 逐轴扫过0～255，检查端点、中心、方向，另外三轴不应串到该轴上。 */
    const unsigned positions[] = {8,7,5};
    for (unsigned a = 0; a < 3; ++a) {
        float previous = 2;
        for (unsigned raw = 0; raw < 256; ++raw) {
            frame[5] = frame[7] = frame[8] = 128;
            frame[positions[a]] = (uint8_t)raw;
            CHECK(rm_ps2_decode(frame, 9, 43, &r));
            const float axes[] = {r.forward,r.left,r.yaw};
            float value = axes[a];
            CHECK(value >= -1 && value <= 1 && value < previous);
            CHECK(NEAR(axes[(a+1)%3],0) && NEAR(axes[(a+2)%3],0));
            if (raw == 0) CHECK(NEAR(value,1));
            if (raw == 128) CHECK(NEAR(value,0));
            if (raw == 255) CHECK(NEAR(value,-1));
            previous = value;
        }
    }
    frame[5] = frame[7] = frame[8] = 128;
    for (unsigned bit = 0; bit < 16; ++bit) {
        uint16_t buttons = (uint16_t)~(1u << bit);
        frame[3] = (uint8_t)buttons; frame[4] = (uint8_t)(buttons >> 8);
        frame[6] = (uint8_t)(bit * 17u); /* 右Y未分配，不应影响运动或按键。 */
        CHECK(rm_ps2_decode(frame, 9, 44, &r));
        CHECK(r.arm == (bit == 3) && r.stop == (bit == 0));
        CHECK(NEAR(r.forward,0) && NEAR(r.left,0) && NEAR(r.yaw,0));
    }
    frame[3] = 0xf6; frame[4] = 0xff;
    CHECK(rm_ps2_decode(frame, 9, 45, &r) && r.arm && r.stop);

    /* 一个合法包之后再来坏包，不能留下之前的速度、START或SELECT。 */
    for (unsigned mode = 0; mode < 256; ++mode) {
        frame[1] = (uint8_t)mode;
        bool ok = rm_ps2_decode(frame, 9, 46, &r);
        CHECK(ok == (mode == 0x73));
        if (!ok) CHECK(cleared(&r,46));
    }
    frame[1] = 0x73;
    for (unsigned marker = 0; marker < 256; ++marker) {
        frame[2] = (uint8_t)marker;
        bool ok = rm_ps2_decode(frame, 9, 47, &r);
        CHECK(ok == (marker == 0x5a));
        if (!ok) CHECK(cleared(&r,47));
    }
    frame[2] = 0x5a;
    for (size_t n = 0; n <= sizeof frame; ++n) {
        bool ok = rm_ps2_decode(frame, n, 48, &r);
        CHECK(ok == (n == 9));
        if (!ok) CHECK(cleared(&r,48));
    }
    memset(frame, 0, sizeof frame);
    CHECK(!rm_ps2_decode(frame,9,49,&r) && cleared(&r,49));
    memset(frame, 0xff, sizeof frame);
    CHECK(!rm_ps2_decode(frame,9,50,&r) && cleared(&r,50));
    return true;
}

static bool test_arguments(void)
{
    wire_t w;
    wire_init(&w);
    rm_ps2_bus_t b = make_bus(&w);
    uint8_t tx[22] = {0}, rx[22];
    rm_remote_t r = {.valid=true,.arm=true,.stop=true,.forward=1,.left=1,.yaw=1};
    CHECK(!rm_ps2_decode(NULL,9,51,&r) && cleared(&r,51));
    CHECK(!rm_ps2_decode(tx,9,51,NULL));
    CHECK(!rm_ps2_poll(&b,52,NULL) && w.callbacks == 0);
    CHECK(!rm_ps2_poll(NULL,52,&r) && cleared(&r,52));
    CHECK(!rm_ps2_configure_analog(NULL));
    CHECK(!rm_ps2_transfer(NULL,tx,rx,9));
    CHECK(!rm_ps2_transfer(&b,NULL,rx,9));
    CHECK(!rm_ps2_transfer(&b,tx,NULL,9));
    CHECK(!rm_ps2_transfer(&b,tx,rx,0));
    CHECK(!rm_ps2_transfer(&b,tx,rx,22));
    CHECK(!rm_ps2_transfer(&b,tx,rx,SIZE_MAX));
    for (unsigned i = 0; i < 6; ++i) {
        rm_ps2_bus_t broken = b;
        switch (i) {
        case 0: broken.select=NULL; break;
        case 1: broken.clock=NULL; break;
        case 2: broken.command=NULL; break;
        case 3: broken.data=NULL; break;
        case 4: broken.delay_us=NULL; break;
        default: broken=(rm_ps2_bus_t){0}; break;
        }
        CHECK(!rm_ps2_transfer(&broken,tx,rx,9));
        CHECK(!rm_ps2_configure_analog(&broken));
        r = (rm_remote_t){.valid=true,.arm=true,.forward=1};
        CHECK(!rm_ps2_poll(&broken,53,&r) && cleared(&r,53));
    }
    CHECK(w.callbacks == 0); /* 参数不对时不得碰GPIO，也不得解引用空函数指针。 */
    CHECK(rm_ps2_transfer(&b,tx,rx,21) && w.length[0] == 21 && w.phase_ok && w.timing_ok);
    wire_init(&w);
    r = (rm_remote_t){.valid=true,.arm=true,.stop=true,.forward=1,.left=1,.yaw=1};
    CHECK(!rm_ps2_poll(&b,54,&r) && cleared(&r,54));
    return true;
}

int main(int argc, char **argv)
{
    const struct { const char *name; bool (*run)(void); } cases[] = {
        {"timing",test_timing}, {"configuration",test_configuration},
        {"decode",test_decode}, {"arguments",test_arguments}
    };
    unsigned ran = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        if (argc > 1 && strcmp(argv[1],cases[i].name)) continue;
        ++ran;
        if (!cases[i].run()) return 1;
        printf("PASS PS2 %s\n",cases[i].name);
    }
    if (!ran) return 2;
    printf("PS2: %u checks passed (software model only)\n",checks);
    return 0;
}
