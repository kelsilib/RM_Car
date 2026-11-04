# 驱动资料包对照修正与验证

日期：2026-10-26。固件：`rm_rct6-driver-align-r4`。Git基线：`d1fcb37b4e0640aaf22289fb503d845ec26cc864`，分支`feat/rct6-ps2-60mm`；修改未暂存、未提交、未推送。该分支名称保留历史60mm字样，当前实际轮径为80mm。

本轮使用用户指定的`C:/Users/fakel/Downloads/驱动板`。资料、冲突处理和终端用法见[DRIVER_REFERENCE.md](../DRIVER_REFERENCE.md)，9个主要来源哈希见[driver-align-sources.json](driver-align-sources.json)。原资料包保持不变。

## 修改结果

| 问题 | 修改 | 保留的边界 |
|---|---|---|
| read_flash返回普通文字行，原先只留未知`$...#`帧，无法查看这些字段 | `motor_uart.c`新增只读配置行解析；`chassis_app.c`接入；终端新增`driver info` | 只显示版本、类型、死区、磁环线数、减速比和轮径；不解析PID、不写参数、不自动填写verified |
| MSPD反馈误用SPD发送范围±1000，可能锁存无必要的坏包故障 | 按示例浮点反馈接收，去掉指令范围限制 | 仍拒绝NaN/Inf、指数和过大数字；发送SPD仍±1000，PWM仍±3600 |
| 新查询可能显示上次的完整配置 | 合法`driver read`先清旧记录；再等回复并用`driver info`查看 | 被拒绝的查询不清记录；无事务序号，不能保证延迟回复属于本次查询 |
| 电脑工具不认识新回复 | 增加`driver info`对应回复规则和回归 | 自动日志、无换行及不对应的回复不能冒充成功 |

新增配置解析与运动反馈隔离：只记录收到的字段，只有MAll刷新电机健康时间；即使收到六字段`fields=0x3F`，也不能解锁。配置字段有版本长度、字符、数值、行长、截断、50ms超时及uint32计时回绕检查。测试夹具来自资料格式，不是实物抓包。

串口表有两处笔误：G14把mline误称减速比，E42在PWM示例中写成spd。结合STM32源码、IIC表及官方PDF处理，未修改厂商原件。反馈单位mm/s已确认，但控制SPD时间单位仍缺明确依据，`speed_unit=UNKNOWN`保持。

上一轮80mm修正完整保留：半径0.040m、半轴距0.070888089544m、半轮距0.097050777636m、凹口端为车头。所有生产verified=false、标定关闭、RM_PS2_WIRED=0。HAL/CMSIS、target板级文件、CubeMX参考文件和旧C8工程未修改。

## 实际执行的验证

| 检查 | 结果 | 证据/范围 |
|---|---|---|
| `build.ps1 -Test -Rebuild` | 通过 | 核心129235条断言、29个主循环模拟场景、8组服务共545条断言、PS2专项3745项 |
| `board.ps1 -Mode Test` | 通过 | Python日志17项、PowerShell回复35项 |
| 默认ARM全量构建 | 通过 | 使用build-makefile流程，生成ELF/HEX/BIN；无硬件访问 |
| ARM GCC `-fanalyzer -c` | 通过，无诊断 | 仅本轮修改的motor_uart、chassis_app、console三个C模块，警告视错误；不是全仓库MISRA认证 |
| ELF/HEX/BIN检查 | 通过 | 初始栈、Reset/UART1/UART2/SysTick向量、Flash/RAM范围、HEX与BIN一致、BIN包含r4版本 |
| PS2启用变体构建 | 本轮未执行 | 未改PS2适配，默认禁用构建通过；历史启用变体证据不算本轮重跑 |
| CubeIDE界面导入/构建 | 未执行 | 命令行使用CubeIDE所附工具链，不代表IDE界面构建通过 |
| 烧录、配置实物回读、接线/运动/停车验收 | 未执行 | 本轮未访问任何设备 |

工具版本：GNU Tools for STM32 ARM GCC 13.3.1、GNU Make 4.4.1_st_20231030-1220、PC GCC 16.2.0、Python 3.12.14。已安装静态分析脚本的探测步骤用了`-fsyntax-only`，不把它当作分析结果；上表记录的是另外实际执行的`-fanalyzer -c`。

## 产物

产物在`RCT6/build/`，仅适用于RCT6主控。Flash使用21648字节；RAM含预留9144字节，初始栈`0x2000c000`，Reset向量`0x08003ec9`。这些是文件结构检查，不是芯片运行结果。

| 文件 | SHA256 |
|---|---|
| `rm_rct6.bin` | `be7ae74441114d659fde2b66486146aeb4cca9bc99d0cb14c6570c46e3471379` |
| `rm_rct6.elf` | `ff562493ab3e25205b2863130f94f265f623d0201096d001c6fb49f502cb5395` |
| `rm_rct6.hex` | `df7349be7e1a7e2d5959a97dae50f9c7a71720c5d3f02ed68ec1111cfd4f2df9` |

源码、测试、工具输入与产物哈希，以及明确的`flashed=false`、`hardware_tested=false`见[产物记录](driver-align-artifacts.json)。旧80mm-r3记录和真实r1串口日志均保留，不能用旧哈希识别r4。

文档同步后的最终检查通过：35个源码/测试/工具输入哈希、3个产物哈希一致；9个厂商来源文件未改；281个修改前备份均可校验；13个文档中的123个本地链接有效，17个JSON可解析。与备份相比仅改动本轮允许的22个已有文件，80mm配置、HAL/CMSIS、板级文件、旧工程和其余历史记录保持不变。Git差异无空白错误，暂存区为空，HEAD未变化。证据为聊天工作目录`work/driver-align-20261026/final-check.json`。

## 备份与后续验证

修改前备份位于聊天工作目录`work/driver-align-20261026/before/`，包含281个项目文件和r3产物，`before.json`记录原Git状态与哈希，保留此前未提交的80mm修正。需要回退时只比较并恢复本轮明确修改路径，不使用整仓库强制还原。

本轮原始`tests.log`、`tools-tests.log`、`build.log`、`static-analysis.json`、`artifact-check.json`及资料提取结果保存在同一work目录，不属于真实设备日志。原厂商HEX只静态读取了格式字符串，未执行或烧录；它是驱动板固件，绝不能当作本主控下载文件。

新r4尚未烧录；最后确认在主板上运行的仍为`rm_rct6-chassis-20261023-r1`，仅取得单板启动、禁动日志和version/config查询证据。后续先按[USB ISP说明](../USB_ISP.md)核对主控固件并完成单板验证，再按[无负载UART步骤](../BRINGUP.md)执行`driver read`、等待、`driver info`，记录实际固件版本及参数。真实参数、控制单位、轮向和失联保护验收见[待核实配置](../CONFIG_PENDING.md)，不会因软件测试通过而自动放行。
