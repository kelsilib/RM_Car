#include "scheduler.h"

#include <stddef.h>

void sched_init(task_t *tasks, uint32_t count, uint32_t now_us)
{
    for (uint32_t i = 0; i < count; i++) {
        tasks[i].last_us = now_us;
    }
}

void sched_run(task_t *tasks, uint32_t count, uint32_t now_us)
{
    for (uint32_t i = 0; i < count; i++) {
        task_t *t = &tasks[i];
        /* 用有符号差值比较，天然处理 32 位计数器回绕 */
        if ((int32_t)(now_us - t->last_us) < (int32_t)t->period_us) {
            continue;
        }

        uint32_t elapsed = now_us - t->last_us;
        t->last_us += t->period_us;

        /* 如果落后超过一个周期（例如被阻塞），丢掉积压的节拍，
         * 避免"追赶式"连发把总线打爆。 */
        if ((int32_t)(now_us - t->last_us) >= (int32_t)t->period_us) {
            t->last_us = now_us;
            elapsed = t->period_us;
        }

        if (t->fn != NULL) {
            float dt = (float)elapsed * 1.0e-6f;
            if (dt > 0.2f) {
                dt = 0.2f;
            }
            t->fn(dt);
        }
    }
}
