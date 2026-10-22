/**
 * @file chassis_app.c
 * @brief 把各个模块串起来：读手柄结果 -> 检查能否动 -> 慢慢加速 -> 算四轮速度 -> 拼命令。
 *
 * 正常行驶命令统一从rm_app_step生成；主循环另外可以发停车和查询/重连命令。
 * 调试时顺着看：remote是手柄，target是期望速度，ramp是缓慢调整后的速度，
 * wheel是各轮速度，channel是按驱动接口重新排列的速度，frame是最终串口文字。
 * 默认参数还没确认，只会发PWM0。想先验证计算过程，可以运行PC测试。
 */
#include "chassis_app.h"
#include <math.h>
#include <string.h>

void rm_app_init(rm_app_t *a, const rm_config_t *c, uint32_t now)
{
    /* 把配置复制进应用。改源码里的默认值后，需要重新编译、下载、启动才生效。 */
    memset(a, 0, sizeof *a);
    a->config = *c;
    a->last_step_ms = now;
    rm_motor_parser_init(&a->parser);
    rm_safety_init(&a->safety);
}

void rm_app_receive(rm_app_t *a, uint8_t byte, uint32_t now)
{
    rm_motor_packet_t packet;
    uint32_t malformed = a->parser.malformed;
    uint32_t unknown = a->parser.unknown;
    if (rm_motor_feed(&a->parser, byte, now, &packet)) {
        a->last_packet = packet;
        /* 只有完整、正确的MAll编码器包才算“最近收到过电机反馈”。
         * 电压包、配置回复和乱码不会更新这个时间。 */
        if (packet.kind == RM_PACKET_COUNT) {
            a->motor_seen = true;
            a->motor_at_ms = now;
            memcpy(a->motor_counts, packet.counts, sizeof a->motor_counts);
        } else if (packet.kind == RM_PACKET_BATTERY) {
            a->battery_seen = true;
            a->battery_v = packet.battery_v;
            a->battery_at_ms = now;
        }
    }
    /* 厂商配置回复的字段格式还未实测，因此先保留完整原文，不能猜字段再自动放行。 */
    if (a->parser.unknown != unknown) {
        size_t length = a->parser.length;
        a->driver_reply[0] = '$';
        memcpy(a->driver_reply+1, a->parser.frame, length);
        a->driver_reply[length+1] = '#';
        a->driver_reply[length+2] = '\0';
        a->driver_reply_at_ms = now;
        a->driver_reply_seen = true;
    }
    /* 如果发现新坏包，就记住通信故障。后面来了好包也不自动清除，需排查后复位。 */
    if (a->parser.malformed != malformed) rm_app_transport_fault(a);
}

void rm_app_transport_fault(rm_app_t *a)
{
    a->transport_fault = true;
    rm_cal_cancel(&a->calibration);
    a->safety.state = a->estop_active ? RM_ESTOP : RM_FAULT;
    a->safety.inhibit |= RM_INHIBIT_TRANSPORT;
    a->ramp = (rm_twist_t){0};
    memset(a->last_channel, 0, sizeof a->last_channel);
    memset(a->last_wheel, 0, sizeof a->last_wheel);
    a->last_speed_mode = false;
}

void rm_app_stop(rm_app_t *a)
{
    rm_cal_cancel(&a->calibration);
    rm_safety_init(&a->safety);
    a->safety.previous_arm = a->remote.arm;
    a->safety.inhibit = rm_config_motion_ready(&a->config) ? 0 : RM_INHIBIT_CONFIG;
    if (a->transport_fault) a->safety.inhibit |= RM_INHIBIT_TRANSPORT;
    if (a->estop_active) a->safety.inhibit |= RM_INHIBIT_ESTOP;
    if (a->estop_active) a->safety.state = RM_ESTOP;
    else if (a->transport_fault) a->safety.state = RM_FAULT;
    a->ramp = (rm_twist_t){0};
    memset(a->last_channel, 0, sizeof a->last_channel);
    memset(a->last_wheel, 0, sizeof a->last_wheel);
    a->last_speed_mode = false;
}

bool rm_app_output_active(const rm_app_t *a)
{
    return a->safety.state == RM_ARMED || a->safety.state == RM_CALIBRATING ||
        a->calibration.state != RM_CAL_OFF;
}

static rm_safety_input_t safety_input(const rm_app_t *a, uint32_t now, bool estop)
{
    return (rm_safety_input_t){
        .remote=a->remote, .motor_seen=a->motor_seen, .motor_at_ms=a->motor_at_ms,
        .transport_ok=!a->transport_fault, .estop=estop,
        .scheduler_ok=(uint32_t)(now-a->last_step_ms) <= 100
    };
}

bool rm_app_cal_arm(rm_app_t *a, uint32_t now)
{
    if (a->safety.state == RM_ARMED) return false;
    rm_safety_input_t input = safety_input(a, now, a->estop_active);
    if (!rm_cal_arm(&a->calibration, &a->config, &input, now)) return false;
    rm_safety_init(&a->safety);
    a->safety.state = RM_CALIBRATING;
    a->safety.previous_arm = a->remote.arm;
    a->ramp = (rm_twist_t){0};
    return true;
}

bool rm_app_cal_pulse(rm_app_t *a, uint32_t now, unsigned channel, int pwm, unsigned duration_ms)
{
    if (a->safety.state != RM_CALIBRATING) return false;
    rm_safety_input_t input = safety_input(a, now, a->estop_active);
    return rm_cal_pulse(&a->calibration, &a->config, &input, now, channel, pwm, duration_ms);
}

/* 无论行驶、标定还是停车，都从这里拼最终命令，并记录最近输出供output指令查看。 */
static bool output(rm_app_t *a, bool speed, const int32_t channel[RM_WHEELS], char *frame, size_t cap)
{
    if (!rm_motor_command(frame, cap, speed, channel)) {
        rm_app_transport_fault(a);
        a->safety.state = RM_FAULT;
        a->safety.inhibit = RM_INHIBIT_TRANSPORT;
        memset(a->last_channel, 0, sizeof a->last_channel);
        memset(a->last_wheel, 0, sizeof a->last_wheel);
        a->last_speed_mode = false;
        return false;
    }
    memcpy(a->last_channel, channel, sizeof a->last_channel);
    a->last_speed_mode = speed;
    return true;
}

bool rm_app_step(rm_app_t *a, uint32_t now, bool estop, char *frame, size_t cap)
{
    /* 算出距离上次检查过了多久。正常约20ms，超过100ms就按控制停顿处理。
     * 断点和单步也会造成停顿，所以调试前要断开电机动力。 */
    uint32_t elapsed = now - a->last_step_ms;
    a->estop_active = estop;
    rm_safety_input_t input = safety_input(a, now, estop);
    a->last_step_ms = now;
    /* 不允许动就直接发PWM0，并忘掉上次速度；停车指令不等待缓慢减速。 */
    const int32_t zero[RM_WHEELS] = {0};
    /* 单轮标定也走同一输出口。结束、取消或丢失保护条件时，本轮直接归零。
     * state=4专门表示标定，不能把它误认为普通禁动，也不能自动转进整车行驶。 */
    bool was_calibrating = a->safety.state == RM_CALIBRATING || a->calibration.state != RM_CAL_OFF;
    rm_cal_update(&a->calibration, &a->config, &input, now);
    if (was_calibrating) {
        int32_t channel[RM_WHEELS] = {0};
        a->ramp = (rm_twist_t){0};
        memset(a->last_wheel, 0, sizeof a->last_wheel);
        if (a->calibration.state == RM_CAL_OFF) {
            rm_app_stop(a);
            a->safety.inhibit |= a->calibration.inhibit;
            if (input.estop) a->safety.state = RM_ESTOP;
            else if (input.remote.stop) {
                a->safety.inhibit &= ~(uint32_t)RM_INHIBIT_ESTOP;
                a->safety.state = (a->safety.inhibit & ~(uint32_t)RM_INHIBIT_CONFIG) ? RM_FAULT : RM_DISARMED;
            }
            else if (a->calibration.inhibit) a->safety.state = RM_FAULT;
        } else {
            a->safety.state = RM_CALIBRATING;
            a->safety.inhibit = a->calibration.inhibit;
            if (a->calibration.state == RM_CAL_PULSE) {
                /* 从首次生成非零输出时计时，避免20ms短脉冲在等下一轮控制时就用完。 */
                if (!a->calibration.pulse_started) {
                    a->calibration.pulse_started = true;
                    a->calibration.pulse_at = now;
                }
                channel[a->calibration.channel] = a->calibration.pwm;
            }
        }
        return output(a, false, channel, frame, cap);
    }
    if (!rm_safety_step(&a->safety, &a->config, &input, now)) {
        a->ramp = (rm_twist_t){0};
        memset(a->last_wheel, 0, sizeof a->last_wheel);
        return output(a, false, zero, frame, cap);
    }
    /* 手柄数值为-1到+1，乘以配置上限才是车体速度。
     * 例如前推到1、上限0.15m/s，就表示希望车以0.15m/s前进。
     * 旋转单位是rad/s（弧度/秒），不是度/秒，也不是电机PWM。 */
    rm_twist_t target = {
        rm_deadzone(a->remote.forward, a->config.deadzone) * a->config.max_v_m_s,
        rm_deadzone(a->remote.left, a->config.deadzone) * a->config.max_v_m_s,
        rm_deadzone(a->remote.yaw, a->config.deadzone) * a->config.max_w_rad_s
    };
    /* 斜着推时，前后速度和左右速度要一起算；合成速度也不能超过平移上限。 */
    float norm = hypotf(target.vx, target.vy);
    if (norm > a->config.max_v_m_s) {
        target.vx *= a->config.max_v_m_s / norm;
        target.vy *= a->config.max_v_m_s / norm;
    }
    /* 所有摇杆方向都回中，马上把速度设为0，不等加减速过程慢慢降到0。
     * 此时仍已解锁，发spd:0让驱动维持零速；按SELECT或故障时则发pwm:0退出控速。 */
    if (target.vx == 0.0f && target.vy == 0.0f && target.wz == 0.0f)
        a->ramp = (rm_twist_t){0};
    else if (elapsed)
        a->ramp = rm_slew(a->ramp, target, &a->config, (float)elapsed * 0.001f);
    /* 前后、左右分别缓慢改速度后，再检查一次合成速度，防止换方向时超限。 */
    norm = hypotf(a->ramp.vx, a->ramp.vy);
    if (norm > a->config.max_v_m_s) {
        a->ramp.vx *= a->config.max_v_m_s / norm;
        a->ramp.vy *= a->config.max_v_m_s / norm;
    }
    /* wheel按左前/右前/左后/右后排列；channel按驱动的M1/M2/M3/M4排列。
     * 先算轮缘mm/s，再按接线换顺序、调整正反号，最后拼成驱动认识的文字命令。 */
    int32_t wheel[RM_WHEELS], channel[RM_WHEELS];
    if (!rm_mecanum_mm_s(&a->config, a->ramp, wheel) ||
        !rm_motor_map_command(&a->config, wheel, channel)) {
        /* 轮速算不出来或接线对应有问题，也发PWM0，不能继续用上一条行驶命令。 */
        rm_app_transport_fault(a);
        a->safety.state = RM_FAULT;
        a->safety.inhibit = RM_INHIBIT_TRANSPORT;
        a->ramp = (rm_twist_t){0};
        memset(a->last_wheel, 0, sizeof a->last_wheel);
        return output(a, false, zero, frame, cap);
    }
    memcpy(a->last_wheel, wheel, sizeof wheel);
    return output(a, true, channel, frame, cap);
}
