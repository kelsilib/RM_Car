# r8 人工架空联调与软件急停

日期为2026-11-04。当前分支`test/mecanum-bringup-20261104`，HEAD为`e6ad2e2dd7707051d71418000becf65ed23a5873`，全部开发工作保留在未暂存工作区。源码输入及产物以[本次清单](software-stop-artifacts-20261104.json)中的171个文件哈希标识。今晚按[联调入口](../TONIGHT_BRINGUP.md)接线和检查。

## 用户最新确认

用户明确要求“能急停就行”，随后指定PS2的SELECT或电脑stop作为急停。本轮按该要求准备人工看护、四轮架空测试，采用软件停车验收。原r6/r7的独立停车硬件要求已被这次用户选择替代。驱动无自动停车、断命令保持末值这一协议事实继续如实保留。

用户确认3S标称11.1V、3000mAh电池，满电12.6V输入可用，计划直接供驱动VIN。主控经原有稳压板固定5V输出供电。供电兼容性来源为用户确认，电压/极性和实物版本在今晚接线时核对。PS2、电机和编码器尚未连接，轮序安排为M1左前、M2左后、M3右前、M4右后，方向待测。

## 实现与验收含义

- 配置字段改为`software_stop_verified`，默认false。普通行驶和单轮标定均使用这项检查，config的0x20含义同步改为软件停车待测。
- SELECT和电脑stop沿用原有统一停车路径，撤销行驶/标定许可、清空速度斜坡、生成四路PWM0。恢复许可继续要求回中、松START再新按，保持按住START无法自动恢复。
- driver safety显示`spd_unit=mm_s command_loss=hold_last timeout_config=unsupported software_stop_verified=0/1 diameter_match=0/1`，字段由只读查询显示，终端没有改验收标志的命令。
- 80mm轮径、CAD轮心距、轮径回读门禁、四轮映射、PS2协议、机械臂及既有故障处理保留。所有生产实测项仍为false，方向为0，标定与机械臂默认关闭。
- 软件停车需要主控运行和命令TX畅通，驱动端没有命令ACK。人工架空测试中保持通信线接好，断线/复位排查先隔离电机动力，具体步骤及验收记录见联调入口。

此次字段变化适用于本工程源码配置，配置在启动时由函数创建，项目中没有旧字段的持久化配置迁移。源码、测试夹具、控制台和现行文档已同步更新。r6/r7报告按当时语义保留，r7源码及固件另已备份在`work/preflight-20261104/before-software-stop/`。

## 本次实际检查

| 项目 | 结果 |
|---|---|
| 核心C逻辑 | 129334条断言通过，含软件停车缺项禁动、轮径一致性及恢复解锁 |
| 底盘主循环 | 32场景通过，保留默认禁动、电脑stop和发送/接收故障检查 |
| 服务 | 10组745条断言通过。新增行驶SELECT、行驶stop、标定SELECT、标定stop四种路径，包含START同时按和停车后一直按START |
| PS2 | 6580项通过 |
| 机械臂 | 42651条断言及11个主循环场景通过 |
| 日志/回复/联调工具 | 17项、43项、7项通过 |
| 默认ARM与PS2只读ARM | 最终源码全量重编译通过 |
| ARM产物 | 两版版本行、架构、关键向量、栈/内存、HEX校验和及HEX/BIN内容通过 |
| GCC分析 | 13个C模块及两条接口启用分支，共15次`-fanalyzer -c`无诊断，警告视错误，见[命令与结果](software-stop-static-20261104.json) |
| 正常工具入口 | Windows PowerShell 5.1下Test、Verify和默认UART Check通过，Check确认默认BIN与新清单一致 |
| 设备与运动 | 烧录、接线、PS2电气/无线行为、实物停轮及运动均待执行 |

输出摘录见[软件检查原文](software-stop-tests-20261104.log)。原始日志保存于本地`work/preflight-20261104/logs/r8-*.log`，哈希记录在清单。当前电脑枚举见[UART检查](host-check-bff335ba.json)，只有Microsoft的COM3/COM4，今晚接板后重新确定CH340端口。

测试使用本机现有ARM GCC13.3.1、PC GCC16.2.0、Make4.4.1、Python3.12.14和Windows PowerShell5.1。Makefile构建技能实际用于两种全量构建，静态分析实际使用完整`-fanalyzer -c`。沙箱623问题按前阶段记录，在自动审核允许的本地运行环境完成，系统配置保持原设置。

## 当前固件

| 构建 | 版本 | Flash/RAM含预留 | 检查报告 |
|---|---|---|---|
| 默认禁动 | `rm_rct6-bench-20261104-r8-safe` | 26056 / 9376字节 | [默认](software-stop-safe-final-check-20261104.json) |
| PS2只读 | `rm_rct6-bench-20261104-r8-ps2` | 27016 / 9376字节 | [PS2](software-stop-ps2-final-check-20261104.json) |

HEX路径分别为`RCT6/build/rm_rct6.hex`、`RCT6/build-ps2-readonly/rm_rct6.hex`。默认PS2为0，只读版PS2为1，机械臂接口均为0。哈希及配置见[清单](software-stop-artifacts-20261104.json)。默认config仍为`issues=0xBF speed_unit=1 cal_enabled=0`，其中0x20现在是软件停车检查。

今晚先完成默认单板、PS2及无电机负载查询，再按实际结果填写PS2、无线行为和软件停车项，仅开放单轮标定。逐轮得到方向和映射后再填写普通行驶所需参数。每次许可配置变更另分配版本号、重编译并记录哈希。

## 分支整理与交付状态

origin/main已包含历史feat/rct6-ps2-60mm，二者文件树一致。本地main跟踪最新origin/main，历史功能分支及source/main引用保留。旧工程302个文件逐个哈希一致地迁入当前工作区，原目录文件再次核对保持原哈希。旧目录未提交的80mm修正、机械臂和驱动功能全部带入。完整来源见[r7迁入记录](PREFLIGHT_2026-11-04.md)。

本次修改和固件等待审核与实板验证，提交、推送、PR及烧录均待后续执行。HAL/CMSIS依赖与CubeMX参考文件保持源文件哈希，现有工程继续使用RCT6的Makefile入口。

收尾逐文件核对源码/固件、r7备份、原目录与HAL/参考资料哈希，37份JSON解析及19份Markdown链接检查通过，暂存区为空。结果见[最终核对](software-stop-final-validation-20261104.json)，`git diff --check`通过。
