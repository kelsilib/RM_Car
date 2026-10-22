/**
 * @file    chassis.h
 * @brief   麦轮底盘运动学：车体速度 <-> 四个轮子的线速度。
 *
 * 坐标约定（右手系，车头朝 +x）：
 *   vx > 0  前进
 *   vy > 0  向左平移
 *   wz > 0  逆时针（从上往下看，车头向左转）
 *
 * 轮序固定为 FL, FR, RL, RR，和 motor/encoder 的索引一一对应。
 */
#ifndef CHASSIS_H
#define CHASSIS_H

typedef enum {
    WHEEL_FL = 0,
    WHEEL_FR,
    WHEEL_RL,
    WHEEL_RR,
    WHEEL_COUNT
} wheel_index_t;

typedef struct {
    float vx;  /* m/s   */
    float vy;  /* m/s   */
    float wz;  /* rad/s */
} chassis_vel_t;

typedef struct {
    float wheel_radius;    /* m */
    float half_wheelbase;  /* m */
    float half_track;      /* m */
} chassis_geom_t;

/** 正运动学：车体速度 -> 四轮线速度 (m/s)。 */
void chassis_ik(const chassis_vel_t *body, const chassis_geom_t *geom,
                float wheel_mps[WHEEL_COUNT]);

/**
 * 带限幅的正运动学：如果某个轮子超速，所有轮子等比缩小，
 * 保证方向不变、不出现"车体速度对了但轮子互相打架"。
 * @return 缩放系数 0..1。
 */
float chassis_ik_limited(const chassis_vel_t *body, const chassis_geom_t *geom,
                         float max_wheel_mps, float wheel_mps[WHEEL_COUNT]);

/** 逆运动学（里程计）：四轮线速度 -> 车体速度。 */
void chassis_odom(const float wheel_mps[WHEEL_COUNT], const chassis_geom_t *geom,
                  chassis_vel_t *body);

/** 车体速度限幅（模长），方向不变。 */
void chassis_limit_body_velocity(chassis_vel_t *v, float max_linear, float max_wz);

#endif /* CHASSIS_H */
