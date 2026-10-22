/**
 * @file    imu.h
 * @brief   MPU6050（I2C2: PB10=SCL, PB11=SDA）。
 *
 * 没有 IMU 也能跑：imu_init 返回 false 时，底盘照常工作，只是关掉偏航保持。
 */
#ifndef IMU_H
#define IMU_H

#include <stdbool.h>

typedef struct {
    float ax, ay, az;   /* g */
    float gx, gy, gz;   /* rad/s */
    float temp_c;
} imu_sample_t;

/** 初始化 + 零偏标定（约 0.6 秒，上电时车必须静止）。 */
bool imu_init(void);

bool imu_read(imu_sample_t *out);
bool imu_present(void);

/** 标定得到的 Z 轴零偏，rad/s。 */
float imu_gyro_bias_z(void);

#endif /* IMU_H */
