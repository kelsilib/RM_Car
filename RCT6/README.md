# RM 底盘 RCT6 第一阶段

当前开发目标：STM32F103RCT6 + 亚博四路智能串口驱动 + PS2遥控 + 四轮麦克纳姆。K210、视觉、机械臂均未纳入本阶段。

初次读代码和准备测试，先看 [代码阅读与测试索引](TEST_INDEX.md)。应用、接口和板级代码使用白话注释，说明“这段做什么、为什么这样做、测试时看哪里”。

当前底盘范围的软件开发已补齐，硬件调试按用户要求留到主板就绪后。完整功能与边界见 [软件交付范围](SOFTWARE_SCOPE.md)。

## 已实现与验证

- `src/motor_uart.c`：厂商ASCII命令构造、四轮通道映射、严格增量解析、半包/粘包/过长/坏包处理和重同步。
- `src/mecanum.c`：标准机体系运动学、死区、车体速度和加速度限制、四轮同比例限幅。
- `src/safety.c`：上电禁动、遥控和电机反馈独立超时、故障锁存、稳定回中后新按键解锁、计时回绕。
- `src/remote_ps2.c`：PS2九字节模拟模式读包、GPIO时序和模拟模式配置；电气适配默认不启用。
- `src/chassis_app.c`：统一安全出口，回中立即速度0，故障PWM0，清理旧运动斜坡，不在主控重复轮速PID。
- `target/`：RCT6启动文件、256KB Flash/48KB RAM链接、72MHz时钟、USART2中断收包和USART1调试输出。
- `target/main.c`：在禁动状态恢复晚启动/重启设备，重试至少间隔500ms；恢复连接后仍需回中并新按START。
- `src/calibration.c`：有许可、幅度和时限限制的单轮PWM标定，默认关闭。
- `src/console.c`：CH340文字终端，可查看手柄、编码器、配置缺项、输出和电压，支持停车及受限标定。

测试入口包含原六组逻辑测试、27个模拟主循环场景和六组标定/指令测试，另有17个日志工具测试和33个电脑回复检查。最新结果见 [修复验证记录](reports/REVIEW_FIXES_2026-10-22.md)。**尚未烧录或做实车测试。**

## 构建

```powershell
.\build.ps1 -Test
.\build.ps1
.\build.ps1 -Rebuild
```

第三条命令会强制重新编译，已有build目录也能使用；也可以组合 `-Test -Rebuild` 强制重编译PC测试。

产物：`build/rm_rct6.elf`、`build/rm_rct6.hex`、`build/rm_rct6.bin`。`tests/`里的模拟参数不会成为实物配置。

默认 `rm_config_default()` 缺尺寸、方向、单位和实测确认项，始终禁止运动；周期输出 `$pwm:0,0,0,0#`。初始化查询驱动配置并开启累计反馈，缺反馈时会在禁动状态重发上传开关；没有自动写持久参数或自动运动演示。坏帧/UART错误等通信故障不会被重试清除，仍需排查后复位。

## 下一步

现在可在PC运行全部软件测试和构建。主板就绪后再按 `BRINGUP.md` 下载和联调；需要测量的参数集中列在 `CONFIG_PENDING.md`。

| 文档 | 用途 |
|---|---|
| `TEST_INDEX.md` | 从哪里读代码、改什么去哪里、测试看哪些变量 |
| `CONSOLE.md` | 电脑终端指令、参数缺项、单轮标定条件和操作 |
| `SOFTWARE_SCOPE.md` | 当前已完成的软件和留到上板后验证的部分 |
| `HARDWARE.md` | 已核对的板卡引脚、供电与接线 |
| `PROTOCOL.md` | 电机与PS2协议、安全语义及解析边界 |
| `CONFIG_PENDING.md` | 需要测量/验证的参数，及禁止解锁原因 |
| `BRINGUP.md` | 编译、下载、观察日志、逐步联调、CubeMX配置清单 |
| `reports/VERIFICATION.md` | 实际执行的检查、产物与限制 |

当前是可实际构建的Make工程，尚未生成本车的CubeMX/CubeIDE受管工程。上级 `Makefile` 与 `build.ps1` 保留旧F103C8/TB6612版本作参考，**本阶段只使用本目录入口**。

## 文件来源

ST启动文件原样取自用户的RCT6资料包中CMSIS GCC模板。链接脚本依据现有ST CubeIDE模板调整为RCT6存储器大小，删除整库DISCARD并预留4KB栈。HAL/CMSIS复用上级原工程的驱动文件，未修改它们。厂商协议依据固定commit及本次重读的官方PDF，见PROTOCOL.md。

build-makefile skill已完成构建和产物扫描；其profile自动保存路径遇到已安装脚本的`info`未初始化错误，本次使用官方脚本参数 `--no-save-profile` 绕过缓存写入，构建本身返回成功。工程的可复现参数记在 `PROJECT_PROFILE.json`。
