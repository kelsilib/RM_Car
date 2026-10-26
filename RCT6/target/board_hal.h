/* 应用通过这些函数使用硬件；具体的引脚和STM32设置在board_hal.c。 */
#ifndef RM_BOARD_HAL_H
#define RM_BOARD_HAL_H
#include "remote_ps2.h"
#include <stddef.h>

/* PS2接线开关：0=不操作PS2引脚，读包一直无效；1=启用这四根线。
 * 先核对供电、电压和接线，再改1并编译下载。启用读手柄并不会自动解锁电机。
 * 候选PB12 ATT、PB13 CLK、PB14 DAT、PB15 CMD；定义集中在board_hal.c的PS2_*_PIN。
 * 原理图和官方示例的适用范围见PS2_REFERENCE.md，不能把示例板接线当成本板实测。 */
#ifndef RM_PS2_WIRED
#define RM_PS2_WIRED 0
#endif

bool board_init(void); /* 时钟、GPIO、USART1/2；不初始化电机PWM/主控编码器 */
bool board_motor_send(const char *frame); /* 从USART2发一条文字命令，最多等待约5ms；没有运动命令回执 */
bool board_motor_read(uint8_t *byte, uint32_t *received_ms); /* 取一个已收到的字节和它到达的时间 */
bool board_uart_fault(void); /* 是否记住了串口错误、缓存满或发送失败；复位后才清除 */
uint32_t board_millis(void); /* HAL毫秒计时，会按uint32_t回绕 */
enum { BOARD_DEBUG_TX_CAP = 192 }; /* 一次调试输出的上限，主循环会检查能否装下指令回复 */
void board_debug(const char *s); /* USART1->CH340，最长192字节、最多等待约20ms */
bool board_debug_read(uint8_t *byte, uint32_t *received_ms); /* 读取CH340发来的一字节指令 */
bool board_debug_fault(void); /* 调试串口出错；不和电机串口故障混在一起 */
bool board_ps2_start(void); /* 启用接线后请求模拟模式并读包确认；失败可在禁动时隔500ms重试 */
bool board_ps2_read(rm_remote_t *remote); /* 读取一帧；失败时valid=false */
void board_panic(void) __attribute__((noreturn)); /* 严重异常时尽力发PWM0，然后一直停在这里，不返回 */
#endif
