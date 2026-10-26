# RM 底盘 RCT6 第一阶段

当前底盘开发目标：STM32F103RCT6 + 亚博四路智能串口驱动 + PS2遥控 + 四轮麦克纳姆。K210回传后点亮三条红绿蓝灯带、机械臂控制已另列待办，尚未实现，见[软件范围与待补资料](SOFTWARE_SCOPE.md)。

初次读代码和准备测试，先看 [代码阅读与测试索引](TEST_INDEX.md)。应用、接口和板级代码使用白话注释，说明“这段做什么、为什么这样做、测试时看哪里”。

当前本地固件编号`rm_rct6-chassis-60mm-r2`：按用户确认的60mm轮径、凹口朝前补齐几何配置，轮心距采用CAD初值。PC回归、默认ARM构建和产物检查通过，**新版本尚未烧录，继续默认禁动**。尺寸、坐标与布局见[几何说明](GEOMETRY_REFERENCE.md)，本次结果见[60mm配置验证](reports/GEOMETRY_60MM_CONFIG.md)。

2026-10-23曾按用户提供的官方PS2附件核对并重构，旧编号`rm_rct6-chassis-20261023-r1`也是最后确认在主板上运行的版本。接线、按键、时序来源和资料冲突见[PS2资料依据](PS2_REFERENCE.md)，历史检查见[PS2重构报告](reports/PS2_REFACTOR_2026-10-23.md)。

当前底盘范围的软件开发已补齐。2026-10-25已完成主控单板启动、日志与version/config查询检查，其他模块实测仍待进行。完整功能与边界见 [软件交付范围](SOFTWARE_SCOPE.md)。

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

本次PC回归通过：核心129233条断言、29个模拟主循环场景、标定/指令510条断言、PS2专项3745项检查，见[60mm配置验证](reports/GEOMETRY_60MM_CONFIG.md)。另有17个日志工具测试和33个电脑回复检查，本次未重跑；其历史结果见[PS2重构报告](reports/PS2_REFACTOR_2026-10-23.md)，[2026-10-22修复记录](reports/REVIEW_FIXES_2026-10-22.md)保留。**2026-10-25旧版r1已捕获真实主板启动信息和59行连续禁动日志，并取得version/config查询回复；新r2未烧录，PS2和实车功能仍待验证。** 详见[单板实测记录](reports/USB_ISP_2026-10-25.md)。

## 构建

```powershell
.\build.ps1 -Test
.\build.ps1
.\build.ps1 -Rebuild
```

第三条命令会强制重新编译，已有build目录也能使用；也可以组合 `-Test -Rebuild` 强制重编译PC测试。

产物：`build/rm_rct6.elf`、`build/rm_rct6.hex`、`build/rm_rct6.bin`。`tests/`里的模拟参数不会成为实物配置。

默认 `rm_config_default()` 已填60mm轮径和CAD轮心距，仍缺实际轮心距复核、方向、单位及各项验收，始终禁止运动；周期输出 `$pwm:0,0,0,0#`。初始化查询驱动配置并开启累计反馈，缺反馈时会在禁动状态重发上传开关；没有自动写持久参数或自动运动演示。坏帧/UART错误等通信故障不会被重试清除，仍需排查后复位。

## 下一步

现在可在PC运行全部软件测试和构建。首次下载按[USB_ISP.md](USB_ISP.md)，使用支持数据的A转C线连接板载CH340对应端口；随后按[BRINGUP.md](BRINGUP.md)联调。需要测量的参数集中列在[CONFIG_PENDING.md](CONFIG_PENDING.md)，不必等整车参数齐全才烧录默认禁动固件。

| 文档 | 用途 |
|---|---|
| `TEST_INDEX.md` | 从哪里读代码、改什么去哪里、测试看哪些变量 |
| `CONSOLE.md` | 电脑终端指令、参数缺项、单轮标定条件和操作 |
| `SOFTWARE_SCOPE.md` | 当前已完成的软件和留到上板后验证的部分 |
| `HARDWARE.md` | 已核对的板卡引脚、供电与接线 |
| `PROTOCOL.md` | 电机与PS2协议、安全语义及解析边界 |
| `PS2_REFERENCE.md` | 官方附件来源、信号/按键对应、时序、资料冲突和待实测项 |
| `CONFIG_PENDING.md` | 需要测量/验证的参数，及禁止解锁原因 |
| `GEOMETRY_REFERENCE.md` | 60mm实物轮径、CAD轮心距、凹口车头、四轮名称和装配复核方法 |
| `BRINGUP.md` | 编译、下载、观察日志、逐步联调、CubeMX配置清单 |
| `USB_ISP.md` | A转C线连接CH340、FlyMCU串口ISP下载、校验和故障排查 |
| `reports/VERIFICATION.md` | 实际执行的检查、产物与限制 |
| `reports/GEOMETRY_60MM_CONFIG.md` | 当前60mm配置的测试、构建、固件哈希和未烧录状态 |

当前是可实际构建的Make工程，尚未生成本车的CubeMX/CubeIDE受管工程。上级 `Makefile` 与 `build.ps1` 保留旧F103C8/TB6612版本作参考，**本阶段只使用本目录入口**。

## 文件来源

ST启动文件原样取自用户的RCT6资料包中CMSIS GCC模板。链接脚本依据现有ST CubeIDE模板调整为RCT6存储器大小，删除整库DISCARD并预留4KB栈。HAL/CMSIS复用上级原工程的驱动文件，未修改它们。厂商协议依据固定commit及本次重读的官方PDF，见PROTOCOL.md。

build-makefile skill已完成构建和产物扫描；其profile自动保存路径遇到已安装脚本的`info`未初始化错误，本次使用官方脚本参数 `--no-save-profile` 绕过缓存写入，构建本身返回成功。工程的可复现参数记在 `PROJECT_PROFILE.json`。
