# RM 底盘 RCT6 第一阶段

2026-11-04追加r9，用户确认S1为承载K210的升降台，S2～S7供机械臂编号控制。△可在L2模式下直接选择S1，随后回中、新按START许可，按R2加右Y调整。当前r9默认、PS2只读及接口启用三个构建通过，默认下载文件在`build/rm_rct6.hex`，按键与验证见[机构控制说明](ARM_CONTROL.md)。下方r8及更早记录按日期保留。

当前开发目标：STM32F103RCT6 + 亚博四路智能串口驱动 + PS2遥控 + 四轮麦克纳姆，并新增七路MG995逐路手动控制。机械臂软件已接入、默认关闭且尚未实测；K210回传后三条红绿蓝灯带亮灯仍待开发，见[软件范围与待补资料](SOFTWARE_SCOPE.md)。

初次读代码和准备测试，先看 [代码阅读与测试索引](TEST_INDEX.md)。应用、接口和板级代码使用白话注释，说明“这段做什么、为什么这样做、测试时看哪里”。

2026-11-04已迁入当前聊天工作区，分支test/mecanum-bringup-20261104。默认r8-safe与PS2只读r8-ps2已构建核对。用户本轮明确以SELECT/电脑stop作为人工看护、四轮架空测试的急停，验收字段现为software_stop_verified。当前实测项保持false、标定关闭。服务745条断言、其他全部PC回归、7项工具回归及15次路径分析通过，见[本次报告](reports/SOFTWARE_STOP_2026-11-04.md)，今晚按[联调入口](TONIGHT_BRINGUP.md)进行。下方r6及更早结果为历史。

2026-10-26整理的[整车排线表](WIRING_PLAN.md)继续沿用。2026-11-01按用户提供的模型完成[机械臂控制与按键表](ARM_CONTROL.md)：S1升降候选，S2～S7按编号；K210已按照片定位为创乐博CanMV K210系列，但回传程序及舵机电源能力仍待核对。

此前r6编号`rm_rct6-driver-final-20261102-r6`：确认SPD为受wdiameter影响的轮缘mm/s，增加驱动轮径一致性门禁、只读driver safety，并将保护记录改为外部independent_stop_verified。驱动无超时停车，断命令保持末值，因此底盘和标定继续禁用；七路MG995功能完整保留、默认关闭。两种ARM构建、PC测试和静态分析通过，**r6未烧录**，见[收尾报告](reports/DRIVER_FINALIZATION_2026-11-02.md)。此前[机械臂记录](reports/ARM_CONTROL_2026-11-01.md)、[驱动修正](reports/DRIVER_ALIGNMENT_2026-10-26.md)及80mm几何保持。

2026-10-23曾按用户提供的官方PS2附件核对并重构，旧编号`rm_rct6-chassis-20261023-r1`也是最后确认在主板上运行的版本。接线、按键、时序来源和资料冲突见[PS2资料依据](PS2_REFERENCE.md)，历史检查见[PS2重构报告](reports/PS2_REFACTOR_2026-10-23.md)。

当前底盘范围的软件开发已补齐。2026-10-25已完成主控单板启动、日志与version/config查询检查，其他模块实测仍待进行。完整功能与边界见 [软件交付范围](SOFTWARE_SCOPE.md)。

## 已实现与验证

- `src/motor_uart.c`：厂商ASCII命令构造、四轮通道映射、严格增量解析、半包/粘包/过长/坏包处理和重同步；普通配置文本单独接收，只读记录六项参数。
- `src/mecanum.c`：标准机体系运动学、死区、车体速度和加速度限制、四轮同比例限幅。
- `src/safety.c`：上电禁动、遥控和电机反馈独立超时、故障锁存、稳定回中后新按键解锁、计时回绕。
- `src/remote_ps2.c`：PS2九字节模拟模式读包、GPIO时序和模拟模式配置；电气适配默认不启用。
- `src/chassis_app.c`：统一安全出口，回中立即速度0，故障PWM0，清理旧运动斜坡，不在主控重复轮速PID。
- `target/`：RCT6启动文件、256KB Flash/48KB RAM链接、72MHz时钟、USART2中断收包和USART1调试输出。
- `target/main.c`：在禁动状态恢复晚启动/重启设备，重试至少间隔500ms；恢复连接后仍需回中并新按START。
- `src/calibration.c`：有许可、幅度和时限限制的单轮PWM标定，默认关闭。
- `src/console.c`：CH340文字终端，可查看手柄、编码器、配置缺项、驱动回读、输出和电压，支持停车及受限标定。
- `src/arm_config.c`、`src/arm_control.c`：七台MG995逐路许可、限位/限速、失联冻结和未写入目标撤销；默认参数未填写，不能输出。
- `target/board_servo.c`：PA6/PA7/PB0/PB1和PB6/PB7/PB8的TIM3/TIM4硬件PWM，`RM_ARM_WIRED=0`时不操作引脚。

r6历史PC回归：核心129334条断言、32个底盘主循环场景、九组服务555条断言、PS2专项6580项、机械臂42651条断言和11个主循环场景、日志17项、电脑回复43项。默认及接口启用版ARM构建、ELF/HEX/BIN核对和七个模块的ARM GCC分析通过，见[本轮报告](reports/DRIVER_FINALIZATION_2026-11-02.md)。新r6未烧录，物理停车、配置回读、PS2、电机与舵机仍未验收；旧r1单板历史证据见[记录](reports/USB_ISP_2026-10-25.md)。

## 构建

```powershell
.\build.ps1 -Test
.\build.ps1
.\build.ps1 -Rebuild
```

第三条命令会强制重新编译，已有build目录也能使用；也可以组合 `-Test -Rebuild` 强制重编译PC测试。

产物：`build/rm_rct6.elf`、`build/rm_rct6.hex`、`build/rm_rct6.bin`。`tests/`里的模拟参数不会成为实物配置。

默认`rm_config_default()`已填80mm轮径、CAD轮心距和确认的MM_S单位，其他未验收项仍禁止行驶与标定。禁动时持续尝试PWM0，启动查询配置并开启累计反馈；缺反馈时重试上传开关，不自动写驱动持久参数。驱动保持末值时，主控发出的PWM0若因停机/TX断线无法送达，就不能保证实际停车，必须由另外的独立路径处理。

配置回读使用driver read、等待、driver info、driver safety。fields=0x3F表示六字段已收到，行驶还要求驱动轮径与主控80mm一致。r8默认issues=0xBF、speed_unit=1、software_stop_verified=false，人工按SELECT/stop的检查按今晚流程记录。

## 下一步

今晚按[联调入口](TONIGHT_BRINGUP.md)完成单板、PS2、无电机负载UART和软件停车检查，再依据实测开放单轮标定。用户已确认12.6V输入可用，实际电压/极性仍需测量。Check默认UART，显式-Interface SWD保留旧入口。

| 文档 | 用途 |
|---|---|
| `TEST_INDEX.md` | 从哪里读代码、改什么去哪里、测试看哪些变量 |
| `CONSOLE.md` | 电脑终端指令、参数缺项、单轮标定条件和操作 |
| `SOFTWARE_SCOPE.md` | 当前已完成的软件和留到上板后验证的部分 |
| `HARDWARE.md` | 已核对的板卡引脚、供电与接线 |
| `WIRING_PLAN.md` | 最新整车排线：供电分支、七路舵机PWM、K210/驱动UART、PS2及排针编号 |
| `ARM_CONTROL.md` | S1～S7引脚、PS2按键、实测参数填写和故障保持策略 |
| `PROTOCOL.md` | 电机与PS2协议、安全语义及解析边界 |
| `DRIVER_REFERENCE.md` | 用户驱动资料包、表格笔误、配置文本六字段与控制/反馈单位区别 |
| `PS2_REFERENCE.md` | 官方附件来源、信号/按键对应、时序、资料冲突和待实测项 |
| `CONFIG_PENDING.md` | 需要测量/验证的参数，及禁止解锁原因 |
| `GEOMETRY_REFERENCE.md` | 80mm实物轮径、CAD轮心距、凹口车头、四轮名称和装配复核方法 |
| `BRINGUP.md` | 编译、下载、观察日志、逐步联调、CubeMX配置清单 |
| `USB_ISP.md` | A转C线连接CH340、FlyMCU串口ISP下载、校验和故障排查 |
| `reports/VERIFICATION.md` | 实际执行的检查、产物与限制 |
| `reports/DRIVER_FINALIZATION_2026-11-02.md` | r6历史单位、保护要求、轮径门禁、测试、两种构建和哈希 |
| `reports/ARM_CONTROL_2026-11-01.md` | r5机械臂新增功能与历史验证，当前继续保留 |
| `reports/DRIVER_ALIGNMENT_2026-10-26.md` | 前一轮r4驱动资料修正与历史验证 |
| `reports/GEOMETRY_80MM_CORRECTION.md` | 前一轮80mm几何修正的测试、构建与哈希，保留历史 |

当前是可实际构建的Make工程，尚未生成本车的CubeMX/CubeIDE受管工程。上级 `Makefile` 与 `build.ps1` 保留旧F103C8/TB6612版本作参考，**本阶段只使用本目录入口**。

## 文件来源

ST启动文件原样取自用户的RCT6资料包中CMSIS GCC模板。链接脚本依据现有ST CubeIDE模板调整为RCT6存储器大小，删除整库DISCARD并预留4KB栈。HAL/CMSIS复用上级原工程的驱动文件，未修改它们。厂商协议依据固定commit、官方PDF及用户驱动资料包，见PROTOCOL.md和DRIVER_REFERENCE.md。

build-makefile skill已完成构建和产物扫描；其profile自动保存路径遇到已安装脚本的`info`未初始化错误，本次使用官方脚本参数 `--no-save-profile` 绕过缓存写入，构建本身返回成功。工程的可复现参数记在 `PROJECT_PROFILE.json`。
