/**
 * @file    scheduler.h
 * @brief   极简协作式时间片调度器（超级循环 + 时间戳）。
 *
 * 不做抢占、不用 RTOS：对一辆 100Hz 速度环的小车足够，
 * 而且调试时调用栈永远是平的，出了问题一眼能看出来。
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

typedef struct {
    const char *name;
    void (*fn)(float dt);   /* 回调，dt 为距上次执行的秒数 */
    uint32_t period_us;
    uint32_t last_us;
} task_t;

void sched_init(task_t *tasks, uint32_t count, uint32_t now_us);
void sched_run(task_t *tasks, uint32_t count, uint32_t now_us);

#endif /* SCHEDULER_H */
