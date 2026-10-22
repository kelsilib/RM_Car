/**
 * @file    commander.h
 * @brief   指令解析：二进制帧 + 纯文本命令，两套语法共用一个串口。
 */
#ifndef COMMANDER_H
#define COMMANDER_H

#include <stdint.h>

typedef struct {
    void (*on_vel)(float vx, float vy, float wz);
    void (*on_arm)(void);
    void (*on_disarm)(void);
    void (*on_stop)(void);
    void (*on_pid)(uint8_t wheel, float kp, float ki, float kd);
    void (*on_rate)(uint16_t period_ms);
    void (*on_ping)(void);
    void (*on_yaw_zero)(void);
} commander_hooks_t;

void commander_init(const commander_hooks_t *hooks);
void commander_poll(void);
uint32_t commander_frame_errors(void);

#endif /* COMMANDER_H */
