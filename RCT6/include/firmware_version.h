/* 串口version指令用这个编号确认当前运行的是哪一版代码。改动发布内容后同步更新。 */
#ifndef RM_FIRMWARE_VERSION_H
#define RM_FIRMWARE_VERSION_H
#if defined(RM_PS2_WIRED) && RM_PS2_WIRED
#define RM_FIRMWARE_ID "rm_rct6-bench-20261104-r9-ps2"
#else
#define RM_FIRMWARE_ID "rm_rct6-bench-20261104-r9-safe"
#endif
#endif
