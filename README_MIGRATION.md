# RM 底盘开发 — 当前聊天接续工程

2026-10-21 从 Codex 聊天「安装 embed-ai-tool 的 skill」接续到本聊天。

## 最新进度：RCT6第一阶段已完成

**当前开发入口为 [RCT6/README.md](RCT6/README.md)。** 电机串口、麦轮运动学、PS2协议、安全状态机、RCT6外设和构建入口已实现；六组主机测试与实际ARM固件构建通过，已生成ELF/HEX/BIN。默认禁止运动，尚未烧录或实物验收。接线/烧录/验证步骤见 [RCT6/BRINGUP.md](RCT6/BRINGUP.md)。

以下保留迁移时的接手记录；上级旧F103C8构建入口不用于本阶段。

## 位置与迁移结果

当前开发副本：本文件所在目录。
原工程：D:\stm32\RM_Car，原目录未修改。
原交接文档：C:\Users\fakel\Documents\Codex\RM_CODEX_HANDOFF.md，已复制到本目录。
开发板全部原始资料：C:\Users\fakel\Downloads\rct6。
CubeIDE：D:\stm32\STM32CubeIDE_1.19.0\STM32CubeIDE。

159 个原工程源文件完整复制并通过逐文件 SHA-256 一致性校验，见 references/migration-manifest.json。
旧 build 目录和旧编译异常产生的 -MT 文件不作为源码迁入。
开发板原理图及官方 UART 示例 .ioc 已复制到 references/；示例不是本车已配置好的工程。

## 最新用户指示（覆盖交接文档相关旧范围）

- 使用 STM32F103RCT6、亚博 AT8236 四路智能串口驱动、四个 JGB37-520 编码电机、四轮麦克纳姆和 PS2 遥控。
- 先完成电机串口、麦克纳姆运动学、遥控安全状态机和必要测试。
- K210 和全部视觉内容不用做；机械臂舵机暂不做。
- 使用已安装的适用 skill；每完成一步汇报，允许用户随时修正。
- 保留现有代码，不虚构引脚、硬件参数或实测结果。
- 用户已有 ST-Link 与开发板 C 口。ST-Link 可作为下载方式；C 口用途须核对板卡资料。
- 不写入用户记忆。

## 迁移时接手状态（历史）

原聊天在资料检查阶段被中断，尚未写入新版 RCT6 底盘实现。
现有源码是旧 F103C8/TB6612/主控轮速 PID 架构；与最新硬件不同。
源码中没有本车 CubeIDE .project/.cproject 或 .ioc；不能把 Makefile 工程说成已建好的 CubeIDE 工程。

已用 build-makefile skill 进行环境探测、Makefile 解析和一次真实固件构建：

- GNU Arm GCC 13.3.1 与 CubeIDE 自带 Make 可用。
- 构建失败，首个错误为 Core/Inc/app_config.h 第 156 行：C 预处理器的 #if 使用浮点常量。
- Makefile 的 CFLAGS 用 := 展开 $@，导致 -MF/-MT 目标为空，也需在后续构建目标中修正。
- 此次未修改旧代码来掩盖架构差异，未产生可用 RCT6 固件，未烧录或进行实车测试。
- 构建记录见 references/baseline-build.log。

## 迁移时确定的工作路线

在此开发副本中新增独立 RCT6 串口驱动目标，保留旧工程作参考。
先实现与 HAL 解耦的 motor_uart、mecanum、safety、board_config 与必要主机测试；随后对照原理图与官方示例建立正确的 RCT6 启动、链接和外设适配。
不在新主控重做轮速 PID 或四组编码器驱动。
未知轮几何、方向、速度单位、遥控失联标识及驱动断通信停车行为保持待验证，默认禁止运动。
需输出 HARDWARE.md、PROTOCOL.md、CONFIG_PENDING.md、BRINGUP.md。

上述软件路线已在RCT6子目录完成第一阶段，实物配置及硬件验证仍按待核实清单推进。
