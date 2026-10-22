#include "robot.h"

#include <math.h>
#include <string.h>

#include "app_config.h"
#include "board.h"
#include "encoder.h"
#include "filters.h"
#include "imu.h"
#include "link.h"
#include "motor.h"
#include "protocol.h"
#include "speed_ctrl.h"

#define TWO_PI_F 6.28318530718f

/* 装配方向修正表（见 app_config.h） */
static const bool s_motor_invert[WHEEL_COUNT] = {
    MOTOR_INVERT_FL != 0,
    MOTOR_INVERT_FR != 0,
    MOTOR_INVERT_RL != 0,
    MOTOR_INVERT_RR != 0,
};

static const bool s_encoder_invert[WHEEL_COUNT] = {
    ENCODER_INVERT_FL != 0,
    ENCODER_INVERT_FR != 0,
    ENCODER_INVERT_RL != 0,
    ENCODER_INVERT_RR != 0,
};

static float clampf(float x, float lo, float hi)
{
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

/* -------------------------------------------------------------------------- */

static struct {
    robot_state_t state;

    chassis_geom_t geom;
    wheel_ctrl_t   wheel[WHEEL_COUNT];

    chassis_vel_t cmd_raw;   /* 上位机原始指令 */
    chassis_vel_t cmd;       /* 经过加速度限制后的指令 */
    chassis_vel_t est;       /* 里程计估计 */

    slew_t cmd_slew_x;
    slew_t cmd_slew_y;
    slew_t cmd_slew_w;

    float measured[WHEEL_COUNT];  /* m/s */
    float duty[WHEEL_COUNT];

    float yaw;
    float yaw_rate;
    float yaw_ref;
    bool  yaw_hold_active;

    attitude_t attitude;
    float gyro_bias;
    bool  imu_ok;

    uint32_t last_cmd_ms;
    bool     timeout_flag;

    uint32_t fault_code;

    uint16_t telem_period_ms;
    uint32_t last_telem_ms;
} g;

/* -------------------------------------------------------------------------- */
/* 内部工具                                                                    */
/* -------------------------------------------------------------------------- */

static void wheels_output_zero(void)
{
    for (int i = 0; i < WHEEL_COUNT; i++) {
        g.duty[i] = 0.0f;
        motor_set_duty((uint8_t)i, 0.0f);
        wheel_ctrl_reset(&g.wheel[i]);
    }
}

static void robot_fault(uint32_t code)
{
    wheels_output_zero();
    motor_enable(false);
    g.fault_code = code;
    g.state = ROBOT_FAULT;
}

static void robot_reset_command(void)
{
    g.cmd_raw.vx = 0.0f;
    g.cmd_raw.vy = 0.0f;
    g.cmd_raw.wz = 0.0f;
    g.cmd.vx = 0.0f;
    g.cmd.vy = 0.0f;
    g.cmd.wz = 0.0f;
    slew_reset(&g.cmd_slew_x, 0.0f);
    slew_reset(&g.cmd_slew_y, 0.0f);
    slew_reset(&g.cmd_slew_w, 0.0f);
    g.yaw_hold_active = false;
}

/* -------------------------------------------------------------------------- */
/* 初始化                                                                      */
/* -------------------------------------------------------------------------- */

void robot_init(bool imu_ok)
{
    memset(&g, 0, sizeof(g));

    g.geom.wheel_radius = WHEEL_RADIUS_M;
    g.geom.half_wheelbase = HALF_WHEELBASE_M;
    g.geom.half_track = HALF_TRACK_M;

    wheel_ctrl_cfg_t cfg = {
        .kp = SPEED_PID_KP,
        .ki = SPEED_PID_KI,
        .kd = SPEED_PID_KD,
        .i_min = SPEED_PID_I_MIN,
        .i_max = SPEED_PID_I_MAX,
        .out_min = SPEED_PID_OUT_MIN,
        .out_max = SPEED_PID_OUT_MAX,
        .d_alpha = SPEED_PID_D_ALPHA,
        .ff_gain = SPEED_FF_GAIN,
        .ff_offset = SPEED_FF_OFFSET,
        .deadzone = MOTOR_DEADZONE,
        .deadzone_min_cmd = MOTOR_DEADZONE_MIN_CMD,
        .max_accel = WHEEL_SETPOINT_ACCEL,
        .lpf_hz = SPEED_LPF_HZ,
    };

    for (int i = 0; i < WHEEL_COUNT; i++) {
        wheel_ctrl_init(&g.wheel[i], &cfg, (float)CONTROL_PERIOD_US * 1.0e-6f);
    }

    slew_init(&g.cmd_slew_x, BODY_ACCEL_LIMIT);
    slew_init(&g.cmd_slew_y, BODY_ACCEL_LIMIT);
    slew_init(&g.cmd_slew_w, BODY_ALPHA_LIMIT);
    slew_reset(&g.cmd_slew_x, 0.0f);
    slew_reset(&g.cmd_slew_y, 0.0f);
    slew_reset(&g.cmd_slew_w, 0.0f);

    g.imu_ok = imu_ok;
    g.gyro_bias = imu_ok ? imu_gyro_bias_z() : 0.0f;
    g.telem_period_ms = TELEMETRY_PERIOD_MS;
    g.last_cmd_ms = board_millis();
    g.last_telem_ms = board_millis();

    motor_enable(false);
    wheels_output_zero();

    g.state = ROBOT_DISARMED;
}

/* -------------------------------------------------------------------------- */
/* 状态机                                                                      */
/* -------------------------------------------------------------------------- */

bool robot_arm(void)
{
    if (g.state != ROBOT_DISARMED) {
        return false;
    }

    /* 上电/解锁时摇杆必须回中，防止一上电就窜出去 */
    const float mag = fabsf(g.cmd_raw.vx) + fabsf(g.cmd_raw.vy) + fabsf(g.cmd_raw.wz);
    if (mag > ARM_CMD_EPS) {
        return false;
    }

    robot_reset_command();
    for (int i = 0; i < WHEEL_COUNT; i++) {
        wheel_ctrl_reset(&g.wheel[i]);
        g.duty[i] = 0.0f;
        motor_set_duty((uint8_t)i, 0.0f);
    }

    g.yaw_ref = g.yaw;
    g.timeout_flag = false;
    g.last_cmd_ms = board_millis();

    motor_enable(true);
    g.state = ROBOT_ARMED;
    return true;
}

bool robot_disarm(void)
{
    if (g.state == ROBOT_FAULT) {
        return false;
    }

    wheels_output_zero();
    motor_enable(false);
    robot_reset_command();
    g.state = ROBOT_DISARMED;
    g.yaw_hold_active = false;
    return true;
}

void robot_stop(void)
{
    robot_reset_command();
    g.last_cmd_ms = board_millis();
}

void robot_set_velocity(float vx, float vy, float wz)
{
    /* 未解锁时只记录、不执行：这样上位机可以先发速度再 ARM */
    g.cmd_raw.vx = vx;
    g.cmd_raw.vy = vy;
    g.cmd_raw.wz = wz;

    if (g.state == ROBOT_ARMED) {
        g.last_cmd_ms = board_millis();
        g.timeout_flag = false;
    }
}

bool robot_set_pid(uint8_t wheel, float kp, float ki, float kd)
{
    if (kp < 0.0f || kp > 50.0f || ki < 0.0f || ki > 200.0f ||
        kd < 0.0f || kd > 20.0f) {
        return false;
    }

    if (wheel == 0xFFu) {
        for (int i = 0; i < WHEEL_COUNT; i++) {
            wheel_ctrl_set_gains(&g.wheel[i], kp, ki, kd);
        }
        return true;
    }

    if (wheel >= WHEEL_COUNT) {
        return false;
    }

    wheel_ctrl_set_gains(&g.wheel[wheel], kp, ki, kd);
    return true;
}

void robot_set_telem_period(uint16_t period_ms)
{
    if (period_ms < 10u) {
        period_ms = 10u;
    }
    if (period_ms > 1000u) {
        period_ms = 1000u;
    }
    g.telem_period_ms = period_ms;
}

void robot_yaw_zero(void)
{
    g.yaw = 0.0f;
    g.yaw_ref = 0.0f;
}

/* -------------------------------------------------------------------------- */
/* IMU                                                                         */
/* -------------------------------------------------------------------------- */

void robot_imu_step(float dt)
{
    imu_sample_t s;

    if (!imu_read(&s)) {
        g.imu_ok = false;
        return;
    }
    g.imu_ok = true;

    const float gz = s.gz - g.gyro_bias;
    g.yaw_rate = gz;
    g.yaw = wrap_pi(g.yaw + gz * dt);

    attitude_update(&g.attitude, s.ax, s.ay, s.az, s.gx, s.gy, dt);

    /* 电机停转、车体静止时缓慢跟踪零偏，抑制长时间漂移 */
    if (g.state != ROBOT_ARMED && fabsf(s.gz - g.gyro_bias) < GYRO_STILL_THRESHOLD) {
        g.gyro_bias += 0.0005f * (s.gz - g.gyro_bias);
    }
}

/* -------------------------------------------------------------------------- */
/* 速度环                                                                      */
/* -------------------------------------------------------------------------- */

void robot_control_step(float dt)
{
    if (dt <= 0.0f) {
        return;
    }

    /* ---- 1. 编码器测速 ---- */
    for (int i = 0; i < WHEEL_COUNT; i++) {
        int16_t delta = encoder_read_delta((uint8_t)i);
        if (s_encoder_invert[i]) {
            delta = (int16_t)(-delta);
        }
        const float revs = (float)delta / ENCODER_COUNTS_PER_REV;
        g.measured[i] = revs * (TWO_PI_F * WHEEL_RADIUS_M) / dt;
    }

    /* ---- 2. 安全：指令超时 ---- */
    if (g.state == ROBOT_ARMED) {
        const uint32_t now = board_millis();
        if ((now - g.last_cmd_ms) > CMD_TIMEOUT_MS) {
            g.timeout_flag = true;
            wheels_output_zero();
            motor_enable(false);
            robot_reset_command();
            g.state = ROBOT_DISARMED;
        }
    }

    /* ---- 3. 目标速度 ---- */
    if (g.state == ROBOT_ARMED) {
        g.cmd.vx = slew_update(&g.cmd_slew_x, g.cmd_raw.vx, dt);
        g.cmd.vy = slew_update(&g.cmd_slew_y, g.cmd_raw.vy, dt);
        g.cmd.wz = slew_update(&g.cmd_slew_w, g.cmd_raw.wz, dt);

        chassis_limit_body_velocity(&g.cmd, WHEEL_MAX_MPS, 8.0f);

#if ENABLE_YAW_HOLD
        if (g.imu_ok) {
            const float speed = sqrtf(g.cmd.vx * g.cmd.vx + g.cmd.vy * g.cmd.vy);
            if (fabsf(g.cmd_raw.wz) > YAW_HOLD_WZ_DEADBAND) {
                /* 驾驶员在主动转向：以当前航向为新的参考 */
                g.yaw_ref = g.yaw;
                g.yaw_hold_active = false;
            } else if (speed > YAW_HOLD_MIN_SPEED) {
                const float err = wrap_pi(g.yaw_ref - g.yaw);
                const float hold = YAW_HOLD_KP * err - YAW_HOLD_KD * g.yaw_rate;
                g.cmd.wz = clampf(hold, -YAW_HOLD_MAX_WZ, YAW_HOLD_MAX_WZ);
                g.yaw_hold_active = true;
            } else {
                g.yaw_hold_active = false;
            }
        }
#endif
    } else {
        g.cmd.vx = 0.0f;
        g.cmd.vy = 0.0f;
        g.cmd.wz = 0.0f;
        g.yaw_hold_active = false;
    }

    /* ---- 4. 运动学 + 四轮闭环 ---- */
    float wheel_setpoint[WHEEL_COUNT] = {0};
    if (g.state == ROBOT_ARMED) {
        (void)chassis_ik_limited(&g.cmd, &g.geom, WHEEL_MAX_MPS, wheel_setpoint);
    }

    for (int i = 0; i < WHEEL_COUNT; i++) {
        const float duty = wheel_ctrl_step(&g.wheel[i], wheel_setpoint[i],
                                           g.measured[i], dt);
        g.duty[i] = duty;
        motor_set_duty((uint8_t)i, s_motor_invert[i] ? -duty : duty);
    }

    /* ---- 5. 里程计 ---- */
    chassis_odom(g.measured, &g.geom, &g.est);
}

/* -------------------------------------------------------------------------- */
/* 遥测                                                                        */
/* -------------------------------------------------------------------------- */

void robot_telemetry_step(uint32_t now_ms)
{
    if ((now_ms - g.last_telem_ms) < g.telem_period_ms) {
        return;
    }
    g.last_telem_ms = now_ms;

    uint8_t p[TELEM_LEN];
    memset(p, 0, sizeof(p));

    proto_put_u32(&p[TELEM_OFF_TICK], now_ms);
    p[TELEM_OFF_STATE] = (uint8_t)g.state;

    uint8_t flags = 0u;
    if (g.state == ROBOT_ARMED) {
        flags |= TELEM_FLAG_ARMED;
    }
    if (g.timeout_flag) {
        flags |= TELEM_FLAG_TIMEOUT;
    }
    if (g.yaw_hold_active) {
        flags |= TELEM_FLAG_YAW_HOLD;
    }
    if (g.imu_ok) {
        flags |= TELEM_FLAG_IMU_OK;
    }
    p[TELEM_OFF_FLAGS] = flags;

    proto_put_f32(&p[TELEM_OFF_VX], g.est.vx);
    proto_put_f32(&p[TELEM_OFF_VY], g.est.vy);
    proto_put_f32(&p[TELEM_OFF_WZ], g.est.wz);

    proto_put_f32(&p[TELEM_OFF_CMD_VX], g.cmd.vx);
    proto_put_f32(&p[TELEM_OFF_CMD_VY], g.cmd.vy);
    proto_put_f32(&p[TELEM_OFF_CMD_WZ], g.cmd.wz);

    proto_put_f32(&p[TELEM_OFF_YAW], g.yaw);

    for (int i = 0; i < WHEEL_COUNT; i++) {
        proto_put_f32(&p[TELEM_OFF_WHEEL + 4 * i], g.measured[i]);
        proto_put_f32(&p[TELEM_OFF_DUTY + 4 * i], g.duty[i]);
    }

    p[TELEM_OFF_IMU_OK] = g.imu_ok ? 1u : 0u;

    uint8_t frame[PROTO_MAX_FRAME];
    const size_t n = proto_encode(MSG_TELEMETRY, p, (uint8_t)TELEM_LEN, frame);
    link_write(frame, (uint32_t)n);
}

/* -------------------------------------------------------------------------- */
/* 查询接口                                                                    */
/* -------------------------------------------------------------------------- */

robot_state_t robot_state(void)
{
    return g.state;
}

const char *robot_state_name(robot_state_t st)
{
    switch (st) {
    case ROBOT_BOOT:     return "BOOT";
    case ROBOT_DISARMED: return "DISARMED";
    case ROBOT_ARMED:    return "ARMED";
    case ROBOT_FAULT:    return "FAULT";
    default:             return "?";
    }
}

bool robot_is_armed(void)
{
    return g.state == ROBOT_ARMED;
}

bool robot_imu_ok(void)
{
    return g.imu_ok;
}

uint16_t robot_telem_period_ms(void)
{
    return g.telem_period_ms;
}

uint32_t robot_fault_code(void)
{
    return g.fault_code;
}

/* -------------------------------------------------------------------------- */
/* 回调表                                                                      */
/* -------------------------------------------------------------------------- */

static void hook_vel(float vx, float vy, float wz)
{
    robot_set_velocity(vx, vy, wz);
}

static void hook_arm(void)
{
    (void)robot_arm();
}

static void hook_disarm(void)
{
    (void)robot_disarm();
}

static void hook_stop(void)
{
    robot_stop();
}

static void hook_pid(uint8_t wheel, float kp, float ki, float kd)
{
    (void)robot_set_pid(wheel, kp, ki, kd);
}

static void hook_rate(uint16_t period_ms)
{
    robot_set_telem_period(period_ms);
}

static void hook_yaw_zero(void)
{
    robot_yaw_zero();
}

static void hook_ping(void)
{
    link_write_str("PONG ");
    char tmp[11];
    uint32_t v = board_millis();
    int i = 10;
    tmp[i] = '\0';
    do {
        tmp[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v > 0u && i > 0);
    link_write_str(&tmp[i]);
    link_write_str("\r\n");
}

static const commander_hooks_t s_hooks = {
    .on_vel = hook_vel,
    .on_arm = hook_arm,
    .on_disarm = hook_disarm,
    .on_stop = hook_stop,
    .on_pid = hook_pid,
    .on_rate = hook_rate,
    .on_ping = hook_ping,
    .on_yaw_zero = hook_yaw_zero,
};

const commander_hooks_t *robot_hooks(void)
{
    return &s_hooks;
}
