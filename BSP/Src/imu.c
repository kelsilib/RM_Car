#include "imu.h"

#include <math.h>

#include "stm32f1xx_hal.h"

#define MPU_ADDR        (0x68u << 1)
#define MPU_REG_SMPLRT  0x19u
#define MPU_REG_CONFIG  0x1Au
#define MPU_REG_GYRO    0x1Bu
#define MPU_REG_ACCEL   0x1Cu
#define MPU_REG_ACCEL_X 0x3Bu
#define MPU_REG_PWR     0x6Bu
#define MPU_REG_WHOAMI  0x75u

#define GYRO_LSB_PER_DPS  65.5f     /* ±500 dps */
#define ACCEL_LSB_PER_G   8192.0f   /* ±4g */
#define DEG_TO_RAD        0.01745329252f

static I2C_HandleTypeDef s_hi2c;
static bool  s_present;
static float s_bias_z;
static uint32_t s_errors;

/* -------------------------------------------------------------------------- */

static void imu_gpio_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void imu_i2c_init(void)
{
    __HAL_RCC_I2C2_CLK_ENABLE();

    s_hi2c.Instance = I2C2;
    s_hi2c.Init.ClockSpeed = 400000u;
    s_hi2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_hi2c.Init.OwnAddress1 = 0u;
    s_hi2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_hi2c.Init.OwnAddress2 = 0u;
    s_hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_hi2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    (void)HAL_I2C_Init(&s_hi2c);
}

/**
 * 总线卡死恢复：把 SCL 当普通 IO 手动打 9 个时钟，
 * 让从机把没发完的字节吐完。这是 I2C 现场调试的必备手段。
 */
static void imu_bus_recovery(void)
{
    (void)HAL_I2C_DeInit(&s_hi2c);

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
        for (volatile int d = 0; d < 50; d++) {
            __NOP();
        }
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
        for (volatile int d = 0; d < 50; d++) {
            __NOP();
        }
    }

    imu_gpio_init();
    imu_i2c_init();
}

static bool imu_write_reg(uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(&s_hi2c, MPU_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                             &value, 1u, 20u) == HAL_OK;
}

static bool imu_read_raw(uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (HAL_I2C_Mem_Read(&s_hi2c, MPU_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                         buf, len, 20u) != HAL_OK) {
        s_errors++;
        if (s_errors > 20u) {
            imu_bus_recovery();
            s_errors = 0u;
        }
        return false;
    }
    s_errors = 0u;
    return true;
}

/* -------------------------------------------------------------------------- */

bool imu_init(void)
{
    s_present = false;
    s_bias_z = 0.0f;
    s_errors = 0u;

    imu_i2c_init();

    /* 上电后 MPU6050 需要一点时间 */
    HAL_Delay(50);

    if (HAL_I2C_IsDeviceReady(&s_hi2c, MPU_ADDR, 3u, 50u) != HAL_OK) {
        return false;
    }

    uint8_t who = 0u;
    if (!imu_read_raw(MPU_REG_WHOAMI, &who, 1u)) {
        return false;
    }

    /* 0x68 是 MPU6050，0x70 是 MPU6500 系列，都接受 */
    if (who != 0x68u && who != 0x70u) {
        return false;
    }

    (void)imu_write_reg(MPU_REG_PWR, 0x01u);      /* 唤醒，PLL 时钟 */
    HAL_Delay(20);
    (void)imu_write_reg(MPU_REG_CONFIG, 0x03u);   /* DLPF 44Hz */
    (void)imu_write_reg(MPU_REG_SMPLRT, 0x04u);   /* 1kHz/(1+4) = 200Hz */
    (void)imu_write_reg(MPU_REG_GYRO, 0x08u);     /* ±500 dps */
    (void)imu_write_reg(MPU_REG_ACCEL, 0x08u);    /* ±4g */

    s_present = true;

    /* 零偏标定：车必须静止，取 300 个样本平均 */
    imu_sample_t s;
    float sum = 0.0f;
    uint32_t valid = 0u;
    for (int i = 0; i < 300; i++) {
        if (imu_read(&s)) {
            sum += s.gz;
            valid++;
        }
        HAL_Delay(2);
    }

    if (valid > 100u) {
        s_bias_z = sum / (float)valid;
    }

    return true;
}

bool imu_read(imu_sample_t *out)
{
    if (!s_present || out == NULL) {
        return false;
    }

    uint8_t buf[14];
    if (!imu_read_raw(MPU_REG_ACCEL_X, buf, sizeof(buf))) {
        return false;
    }

    const int16_t ax = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    const int16_t ay = (int16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    const int16_t az = (int16_t)(((uint16_t)buf[4] << 8) | buf[5]);
    const int16_t temp = (int16_t)(((uint16_t)buf[6] << 8) | buf[7]);
    const int16_t gx = (int16_t)(((uint16_t)buf[8] << 8) | buf[9]);
    const int16_t gy = (int16_t)(((uint16_t)buf[10] << 8) | buf[11]);
    const int16_t gz = (int16_t)(((uint16_t)buf[12] << 8) | buf[13]);

    out->ax = (float)ax / ACCEL_LSB_PER_G;
    out->ay = (float)ay / ACCEL_LSB_PER_G;
    out->az = (float)az / ACCEL_LSB_PER_G;
    out->gx = (float)gx / GYRO_LSB_PER_DPS * DEG_TO_RAD;
    out->gy = (float)gy / GYRO_LSB_PER_DPS * DEG_TO_RAD;
    out->gz = (float)gz / GYRO_LSB_PER_DPS * DEG_TO_RAD;
    out->temp_c = (float)temp / 340.0f + 36.53f;

    return true;
}

bool imu_present(void)
{
    return s_present;
}

float imu_gyro_bias_z(void)
{
    return s_bias_z;
}
