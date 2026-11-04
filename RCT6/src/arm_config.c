/* 日后只在这里填写舵机实测值。模型只支持S1为升降候选，不能给出脉宽和机械限位。 */
#include "arm_control.h"

rm_arm_config_t rm_arm_config_default(void)
{
    /* S1..S7接线见ARM_CONTROL.md。0不是舵机中位，而是“尚未填写、禁止输出”。
     * 每路分别启用：先核实该路上下限、起始姿态和方向，其他路可继续保持关闭。
     * 不把PC测试里的1000/1500/2000us等模拟值抄到这里。 */
    const rm_arm_config_t c = {0};
    return c;
}
