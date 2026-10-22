/* 麦轮计算函数：只算数，不操作串口和引脚；每个电机的控速仍交给智能驱动。 */
#ifndef RM_MECANUM_H
#define RM_MECANUM_H
#include "board_config.h"
/* 车体系：vx向前m/s，vy向左m/s，wz逆时针rad/s。角度不能填“度/秒”。 */
typedef struct { float vx, vy, wz; } rm_twist_t;
/* 车体速度 -> 四轮角速度rad/s；数组顺序FL/FR/RL/RR。 */
bool rm_mecanum_angular(const rm_config_t *c, rm_twist_t t, float wheel[RM_WHEELS]);
/* 先算四轮角速度，再换成轮缘mm/s；太快时四轮一起按比例降低。需先确认驱动单位。 */
bool rm_mecanum_mm_s(const rm_config_t *c, rm_twist_t t, int32_t wheel[RM_WHEELS]);
/* 输入-1～1：中间的小偏移忽略掉；离开这段范围后逐渐增加，推到底仍是±1。 */
float rm_deadzone(float v, float zone);
/* 前后、左右、旋转各自缓慢改变：每次最多变化“各自加速度×间隔秒数”。
 * 这是单轴限制，不是平移合成加速度限制；故障停车不走这段缓慢减速。 */
rm_twist_t rm_slew(rm_twist_t current, rm_twist_t target,
                   const rm_config_t *c, float dt_s);
#endif
