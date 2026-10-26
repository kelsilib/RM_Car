/* PC上的纯逻辑测试，不操作实际电机；运行方式：本目录build.ps1 -Test。
 * fixture()中的尺寸和verified=true只用于模拟，不会编入STM32目标。
 * 六组测试：报文解析、轮序/命令、运动学、安全状态机、PS2、应用完整流程。
 * FAIL会打印所在行号和表达式；看失败组名回到对应src文件定位。
 */
#include "chassis_app.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return false; } } while (0)
#define NEAR(a,b) (fabsf((a)-(b)) < 0.0001f)

/* 给PC测试准备一辆“参数都已确认”的模拟车；这些数值不会进入实车固件。 */
static rm_config_t fixture(void)
{
    rm_config_t c = rm_config_default();
    c.radius_m = 0.05f; c.half_length_m = 0.10f; c.half_width_m = 0.10f;
    c.geometry_verified = c.mapping_verified = c.ps2_wiring_verified = true;
    c.motor_parameters_verified = true;
    c.remote_failsafe_verified = c.driver_failsafe_verified = true;
    c.speed_unit = RM_SPEED_MM_S;
    for (unsigned i = 0; i < RM_WHEELS; ++i) c.direction[i] = 1;
    return c;
}

static unsigned feed(rm_motor_parser_t *p, const char *s, uint32_t now,
                     rm_motor_packet_t *pkt)
{
    unsigned accepted = 0;
    while (*s) if (rm_motor_feed(p, (uint8_t)*s++, now, pkt)) ++accepted;
    return accepted;
}

static bool test_parser(void)
{
    rm_motor_parser_t p;
    rm_motor_packet_t pkt = {0};
    rm_motor_parser_init(&p);
    CHECK(feed(&p, "noise\r\n$MAll:1,-2", 10, &pkt) == 0);
    CHECK(feed(&p, ",3,4#$Battery:12.60V#$MSPD:1.5,-2,0,1000#", 20, &pkt) == 3);
    CHECK(pkt.kind == RM_PACKET_SPEED && pkt.at_ms == 20 && NEAR(pkt.speeds[0],1.5f));
    CHECK(feed(&p, "$MAll:-2147483648,2147483647,0,0#", 30, &pkt) == 1);
    CHECK(pkt.counts[0] == INT32_MIN && pkt.counts[1] == INT32_MAX);
    const char *bad[] = {
        "$MAll:2147483648,0,0,0#", "$MAll:-2147483649,0,0,0#",
        "$MAll:1,2,3#", "$MAll:1,2,3,4,5#", "$MAll:1,2,,4#",
        "$MAll:1,2,3,4garbage#", "$MAll:+1,2,3,4#", "$MAll:1, 2,3,4#",
        "$MAll:1.0,2,3,4#", "$MSPD:nan,0,0,0#", "$MSPD:inf,0,0,0#",
        "$MSPD:1e2,0,0,0#", "$MSPD:1001,0,0,0#", "$MSPD:1.,0,0,0#",
        "$Battery:31.00V#", "$Battery:-1V#", "$Battery:12.6Vjunk#"
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        uint32_t errors = p.malformed;
        CHECK(feed(&p, bad[i], 40, &pkt) == 0);
        CHECK(p.malformed == errors + 1);
    }
    CHECK(feed(&p, "$config_ok#$random#", 50, &pkt) == 0 && p.unknown == 2);
    CHECK(feed(&p, "$MAll:1,2$MAll:4,3,2,1#", 60, &pkt) == 1);
    CHECK(pkt.counts[0] == 4);
    char long_frame[300];
    memset(long_frame, '1', sizeof long_frame);
    long_frame[0] = '$'; long_frame[sizeof long_frame-1] = 0;
    CHECK(feed(&p, long_frame, 70, &pkt) == 0);
    CHECK(feed(&p, "$MTEP:4,3,2,1#", 80, &pkt) == 1 && pkt.kind == RM_PACKET_DELTA);
    CHECK(feed(&p, "$MAll:1,", 90, &pkt) == 0);
    CHECK(feed(&p, "2,3,4#", 200, &pkt) == 0); /* 旧半包已经等太久，不能继续拼接 */
    CHECK(feed(&p, "$MAll:0,0,0,0#", 201, &pkt) == 1);
    /* 每次用同一组规则生成乱码，再放一包正确数据，检查解析器能否重新找到包头。 */
    uint32_t rng = 42;
    for (unsigned i = 0; i < 1000; ++i) {
        for (unsigned j = 0; j < 128; ++j) {
            rng = rng * 1664525u + 1013904223u;
            (void)rm_motor_feed(&p, (uint8_t)(rng >> 24), 300+i, &pkt);
            CHECK(p.length < RM_FRAME_CAP);
        }
        CHECK(feed(&p, "$MAll:9,8,7,6#", 300+i, &pkt) == 1);
    }
    return true;
}

static bool test_mapping_and_commands(void)
{
    rm_config_t c = fixture();
    int32_t wheel[4] = {10,20,30,40}, channel[4], back[4];
    CHECK(rm_motor_map_command(&c, wheel, channel));
    CHECK(channel[0]==10 && channel[1]==30 && channel[2]==20 && channel[3]==40);
    CHECK(rm_motor_unmap_counts(&c, channel, back) && !memcmp(wheel,back,sizeof wheel));
    c.direction[RM_FR] = -1;
    CHECK(rm_motor_map_command(&c,wheel,channel) && channel[2]==-20);
    CHECK(rm_motor_unmap_counts(&c,channel,back) && back[RM_FR]==20);
    wheel[RM_FR] = INT32_MIN;
    CHECK(!rm_motor_map_command(&c,wheel,channel));
    channel[2] = INT32_MIN;
    CHECK(!rm_motor_unmap_counts(&c,channel,back));
    c.channel[1] = c.channel[0];
    CHECK(!rm_motor_map_command(&c,wheel,channel));
    char out[112];
    int32_t v[] = {-1000,1000,0,1};
    CHECK(rm_motor_command(out,sizeof out,true,v) && !strcmp(out,"$spd:-1000,1000,0,1#"));
    v[0] = 1001;
    CHECK(!rm_motor_command(out,sizeof out,true,v) && out[0]==0);
    v[0] = -3600;
    CHECK(rm_motor_command(out,sizeof out,false,v));
    v[0] = 3601;
    CHECK(!rm_motor_command(out,sizeof out,false,v));
    v[0] = 0;
    CHECK(!rm_motor_command(out,4,false,v) && out[0]==0);
    return true;
}

static bool test_kinematics(void)
{
    rm_config_t c = fixture();
    float w[4]; int32_t mm[4];
    CHECK(rm_mecanum_angular(&c,(rm_twist_t){.vx=0.1f},w));
    for (unsigned i=0;i<4;++i) CHECK(NEAR(w[i],2.0f));
    CHECK(rm_mecanum_angular(&c,(rm_twist_t){.vy=0.1f},w));
    CHECK(NEAR(w[0],-2.0f) && NEAR(w[1],2.0f) && NEAR(w[2],2.0f) && NEAR(w[3],-2.0f));
    CHECK(rm_mecanum_angular(&c,(rm_twist_t){.wz=1.0f},w));
    CHECK(NEAR(w[0],-4.0f) && NEAR(w[1],4.0f) && NEAR(w[2],-4.0f) && NEAR(w[3],4.0f));
    CHECK(rm_mecanum_mm_s(&c,(rm_twist_t){.vx=0.4f,.vy=0.2f},mm));
    CHECK(mm[0]==67 && mm[1]==200 && mm[2]==200 && mm[3]==67); /* 四轮一起按比例慢下来 */
    CHECK(!rm_mecanum_mm_s(&c,(rm_twist_t){.vx=NAN},mm));
    c.speed_unit = RM_SPEED_UNKNOWN;
    CHECK(!rm_mecanum_mm_s(&c,(rm_twist_t){0},mm));
    CHECK(NEAR(rm_deadzone(0.04f,0.08f),0.0f));
    CHECK(NEAR(rm_deadzone(1.0f,0.08f),1.0f));
    c = fixture();
    rm_twist_t v = rm_slew((rm_twist_t){0},(rm_twist_t){1,1,1},&c,0.02f);
    CHECK(NEAR(v.vx,0.006f) && NEAR(v.wz,0.02f));
    /* 用当前60mm名义轮径和CAD轮心距核对真实默认参数的换算。
     * 上面的通用fixture使用50mm半径，不能替这一组检查；这里只在PC计算，不控制实物。 */
    c = rm_config_default();
    CHECK(!rm_config_motion_ready(&c) && !rm_config_calibration_ready(&c));
    CHECK(!c.geometry_verified && !c.mapping_verified && c.speed_unit == RM_SPEED_UNKNOWN);
    CHECK(rm_mecanum_angular(&c,(rm_twist_t){.vx=0.1f},w));
    for (unsigned i=0;i<RM_WHEELS;++i) CHECK(NEAR(w[i],3.33333333f));
    CHECK(rm_mecanum_angular(&c,(rm_twist_t){.vy=0.1f},w));
    CHECK(NEAR(w[0],-3.33333333f) && NEAR(w[1],3.33333333f) &&
          NEAR(w[2],3.33333333f) && NEAR(w[3],-3.33333333f));
    CHECK(rm_mecanum_angular(&c,(rm_twist_t){.wz=0.5f},w));
    CHECK(NEAR(w[0],-2.79898112f) && NEAR(w[1],2.79898112f) &&
          NEAR(w[2],-2.79898112f) && NEAR(w[3],2.79898112f));
    /* 已填尺寸也不能绕过未知速度单位。单位仅在这份PC局部副本中设为mm/s。 */
    CHECK(!rm_mecanum_mm_s(&c,(rm_twist_t){.vx=0.1f},mm));
    c.speed_unit = RM_SPEED_MM_S;
    CHECK(rm_mecanum_mm_s(&c,(rm_twist_t){.vx=0.1f,.vy=0.02f,.wz=0.2f},mm));
    CHECK(mm[0]==46 && mm[1]==154 && mm[2]==86 && mm[3]==114);
    CHECK(!rm_config_motion_ready(&c));
    return true;
}

static void fresh(rm_safety_input_t *in, uint32_t now)
{
    in->remote.at_ms = in->motor_at_ms = now;
}

static bool test_safety(void)
{
    rm_config_t c = fixture();
    CHECK(rm_config_motion_ready(&c));
    rm_config_t unverified = rm_config_default();
    CHECK(!rm_config_motion_ready(&unverified));
    c.radius_m = NAN; CHECK(!rm_config_motion_ready(&c)); c=fixture();
    c.channel[1] = 0; CHECK(!rm_config_motion_ready(&c)); c=fixture();
    rm_safety_t s; rm_safety_init(&s);
    rm_safety_input_t in = {.remote={.valid=true},.motor_seen=true,
        .transport_ok=true,.scheduler_ok=true};
    fresh(&in,0); CHECK(!rm_safety_step(&s,&c,&in,0));
    fresh(&in,300); in.remote.arm=true;
    CHECK(rm_safety_step(&s,&c,&in,300) && s.state==RM_ARMED);
    in.remote.forward=0.5f; fresh(&in,320);
    CHECK(rm_safety_step(&s,&c,&in,320));
    CHECK(!rm_safety_step(&s,&c,&in,521) && s.state==RM_FAULT);
    /* 断线恢复时仍按着START：即使摇杆回中，也不能自己重新解锁。 */
    in.remote.forward=0; fresh(&in,600);
    CHECK(!rm_safety_step(&s,&c,&in,600));
    fresh(&in,1000); CHECK(!rm_safety_step(&s,&c,&in,1000));
    in.remote.arm=false; fresh(&in,1020); CHECK(!rm_safety_step(&s,&c,&in,1020));
    in.remote.arm=true; fresh(&in,1040); CHECK(rm_safety_step(&s,&c,&in,1040));
    in.estop=true; fresh(&in,1060);
    CHECK(!rm_safety_step(&s,&c,&in,1060) && s.state==RM_ESTOP);
    in.estop=false; fresh(&in,1080); CHECK(!rm_safety_step(&s,&c,&in,1080));
    fresh(&in,1500); CHECK(!rm_safety_step(&s,&c,&in,1500));
    in.remote.arm=false; fresh(&in,1520); CHECK(!rm_safety_step(&s,&c,&in,1520));
    in.remote.arm=true; fresh(&in,1540); CHECK(rm_safety_step(&s,&c,&in,1540));
    in.remote.forward=NAN; fresh(&in,1560);
    CHECK(!rm_safety_step(&s,&c,&in,1560) && (s.inhibit & RM_INHIBIT_REMOTE));
    in.remote.forward=0; in.remote.arm=false; in.remote.stop=true; fresh(&in,1580);
    CHECK(!rm_safety_step(&s,&c,&in,1580) && s.state==RM_DISARMED);
    /* 毫秒计数从最大值回到0：回中计时和反馈时间差仍应正确，约32ms前的包仍新鲜。 */
    rm_safety_init(&s); in.remote.stop=false; in.remote.arm=false;
    fresh(&in,UINT32_MAX-150u);
    CHECK(!rm_safety_step(&s,&c,&in,UINT32_MAX-150u));
    fresh(&in,200); in.remote.arm=true;
    CHECK(rm_safety_step(&s,&c,&in,200));
    fresh(&in,UINT32_MAX-10u); CHECK(rm_safety_step(&s,&c,&in,20));
    in.motor_seen=false; fresh(&in,40);
    CHECK(!rm_safety_step(&s,&c,&in,40) && (s.inhibit & RM_INHIBIT_MOTOR));
    /* 手柄正常但电机反馈过期，也必须禁止运动；两条通信分开检查。 */
    in.motor_seen=true; fresh(&in,1000); in.motor_at_ms=799;
    CHECK(!rm_safety_step(&s,&c,&in,1000) && (s.inhibit & RM_INHIBIT_MOTOR));
    CHECK(!(s.inhibit & RM_INHIBIT_REMOTE));
    /* 提前按住START，等回中时间够了也不能算“新按一次”。 */
    rm_safety_init(&s); fresh(&in,2000); in.remote.arm=false;
    CHECK(!rm_safety_step(&s,&c,&in,2000));
    fresh(&in,2100); in.remote.arm=true;
    CHECK(!rm_safety_step(&s,&c,&in,2100));
    fresh(&in,2400); CHECK(!rm_safety_step(&s,&c,&in,2400));
    in.remote.arm=false; fresh(&in,2420);
    CHECK(!rm_safety_step(&s,&c,&in,2420));
    in.remote.arm=true; fresh(&in,2440); CHECK(rm_safety_step(&s,&c,&in,2440));
    in.scheduler_ok=false; fresh(&in,2460);
    CHECK(!rm_safety_step(&s,&c,&in,2460) && (s.inhibit & RM_INHIBIT_SCHEDULER));
    in.scheduler_ok=true;
    /* 即使包格式正确，没实测过无线断联停车，也不能解锁。 */
    rm_safety_init(&s); c.remote_failsafe_verified=false;
    in.motor_seen=true; fresh(&in,1000); in.remote.arm=false;
    CHECK(!rm_safety_step(&s,&c,&in,1000));
    fresh(&in,1400); in.remote.arm=true;
    CHECK(!rm_safety_step(&s,&c,&in,1400) && (s.inhibit & RM_INHIBIT_CONFIG));
    return true;
}

static bool test_ps2(void)
{
    uint8_t f[9]={0xff,0x73,0x5a,0xff,0xff,128,128,128,128};
    rm_remote_t r;
    CHECK(rm_ps2_decode(f,9,42,&r) && r.valid && r.at_ms==42 && !r.arm && !r.stop);
    CHECK(NEAR(r.forward,0) && NEAR(r.left,0) && NEAR(r.yaw,0));
    f[3] &= (uint8_t)~(1u<<3); f[8]=0; f[7]=0; f[5]=255;
    CHECK(rm_ps2_decode(f,9,43,&r) && r.arm && NEAR(r.forward,1) && NEAR(r.left,1) && NEAR(r.yaw,-1));
    CHECK(!rm_ps2_decode(f,8,44,&r) && !r.valid);
    f[1]=0x41; CHECK(!rm_ps2_decode(f,9,45,&r));
    f[1]=0x79; CHECK(!rm_ps2_decode(f,9,46,&r));
    memset(f,0xff,sizeof f); CHECK(!rm_ps2_decode(f,9,47,&r));
    return true;
}

static void app_fresh(rm_app_t *a, uint32_t now)
{
    const char *s="$MAll:0,0,0,0#";
    while (*s) rm_app_receive(a,(uint8_t)*s++,now);
    a->remote.at_ms=now; a->remote.valid=true;
}

static bool test_app(void)
{
    rm_config_t c=fixture(); rm_app_t a; char frame[112];
    rm_app_init(&a,&c,0);
    for (uint32_t now=0;now<=320;now+=20) {
        app_fresh(&a,now); a.remote.arm=now==320;
        CHECK(rm_app_step(&a,now,false,frame,sizeof frame));
    }
    CHECK(a.safety.state==RM_ARMED && !strcmp(frame,"$spd:0,0,0,0#"));
    a.remote.arm=false; a.remote.forward=1;
    for (uint32_t now=340;now<=540;now+=20) {
        app_fresh(&a,now); CHECK(rm_app_step(&a,now,false,frame,sizeof frame));
    }
    CHECK(a.ramp.vx>0.05f && !strncmp(frame,"$spd:",5));
    a.remote.forward=0; app_fresh(&a,560);
    CHECK(rm_app_step(&a,560,false,frame,sizeof frame) && !strcmp(frame,"$spd:0,0,0,0#"));
    a.remote.forward=1; app_fresh(&a,580);
    CHECK(rm_app_step(&a,580,false,frame,sizeof frame));
    a.ramp=(rm_twist_t){c.max_v_m_s*0.70710678f,c.max_v_m_s*0.70710678f,0};
    a.remote.left=0; app_fresh(&a,600);
    CHECK(rm_app_step(&a,600,false,frame,sizeof frame));
    CHECK(hypotf(a.ramp.vx,a.ramp.vy)<=c.max_v_m_s+0.000001f);
    CHECK(rm_app_step(&a,800,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
    CHECK(a.safety.state==RM_FAULT && NEAR(a.ramp.vx,0));
    app_fresh(&a,820);
    CHECK(rm_app_step(&a,820,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
    /* 电压、乱码和不认识的回复，都不能冒充新的电机编码器反馈。 */
    uint32_t previous=a.motor_at_ms;
    const char *q="$Battery:12.3V#$ok#noise";
    while (*q) rm_app_receive(&a,(uint8_t)*q++,840);
    CHECK(a.motor_at_ms==previous);
    q="$MAll:1,2,3,4,5#";
    while (*q) rm_app_receive(&a,(uint8_t)*q++,850);
    CHECK(a.transport_fault && a.motor_at_ms==previous);
    CHECK(rm_app_step(&a,860,false,frame,sizeof frame) && !strcmp(frame,"$pwm:0,0,0,0#"));
    /* 换回实车默认参数后，即使手柄和反馈正常、按了START，也必须保持禁动。 */
    c=rm_config_default(); rm_app_init(&a,&c,0);
    for (uint32_t now=0;now<=600;now+=20) {
        app_fresh(&a,now); a.remote.arm=now>=320;
        CHECK(rm_app_step(&a,now,false,frame,sizeof frame));
        CHECK(!strcmp(frame,"$pwm:0,0,0,0#"));
    }
    return true;
}

int main(void)
{
    const struct { const char *name; bool (*run)(void); } tests[] = {
        {"parser",test_parser}, {"mapping/commands",test_mapping_and_commands},
        {"kinematics",test_kinematics}, {"safety",test_safety},
        {"PS2",test_ps2}, {"application",test_app}
    };
    for (unsigned i=0;i<sizeof tests/sizeof tests[0];++i) {
        if (!tests[i].run()) return 1;
        printf("PASS %s\n",tests[i].name);
    }
    printf("PASS all: %u assertions (includes deterministic noise recovery)\n",checks);
    return 0;
}
