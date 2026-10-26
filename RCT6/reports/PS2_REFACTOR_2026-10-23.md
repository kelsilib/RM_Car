# PS2按官方资料重构与验证记录

日期：2026-10-23。固件编号：`rm_rct6-chassis-20261023-r1`。

基于本地`bf62d3a2eab911ca48d46e224cdd324b5ff281d9`工作区进行修改，尚未暂存或提交。资料来自用户提供的[官方PS2页面](https://ycnp6l8ow4h6.feishu.cn/wiki/B0fUwn768ivrPCk4SQJcz1ILnnf)及本车RCT6资料。具体附件、哈希、行号、接线和按键表见[PS2_REFERENCE.md](../PS2_REFERENCE.md)。

## 本次修正

| 问题 / 改进 | 修改后行为 | 位置 |
|---|---|---|
| 旧代码在CLK拉高后读DAT，与官方STM32附件不同 | CLK拉低、等16us、读DAT，再拉高等16us；LSB先传，整包保持片选 | `src/remote_ps2.c / transfer_byte()` |
| 进入配置被统一补成9字节 | 按同页PS2X附件，进入5字节、设置9字节、退出9字节；间隔1ms后再轮询确认 | `rm_ps2_configure_analog()` |
| 把第1返回字节硬判为FF | 按附件的idle定义忽略该字节；仍严格限制9字节、模式73和标记5A | `rm_ps2_decode()` |
| 空输出指针会被直接解引用 | 解码、轮询和板级读包入口先检查；其他失败继续清空旧控制值 | `remote_ps2.c`、`board_hal.c` |
| 接线在初始化和回调两处重复写 | 使用同一组GPIO/引脚定义，保留PB12～PB15候选方案和默认关闭 | `target/board_hal.c/.h` |
| 旧模拟接收器不检查DAT采样相位 | 新增按官方资料编写的总线检查；主循环模型也检查片选、位数和采样阶段 | `tests/test_ps2.c`、`tests/test_startup.c` |
| 注释和文档仍写FF帧头、旧采样位置及旧设置耗时 | 改为白话解释，列出官方事实、软件选择、资料冲突及实测边界 | PS2相关注释、README、协议、硬件和测试索引 |

PS2数据布局和代码分层保持简单：逐位收发、整包收发、模式配置、解码及HAL适配各自负责一件事。没有新增压力模式、震动、争议按键控制或其他设备功能。

没有修改`Drivers/`、旧C8工程、启动/链接脚本、时钟配置、厂商`.ioc`、运动配置或安全状态机；保留DWT延时，不把示例的SysTick延时带入HAL。上电禁动、故障停车、恢复后回中及新按START解锁仍由原安全层执行。

## 先复现，再修复

新增测试在修改前的PS2源码上分别失败：

- `timing`：读取DAT时CLK已经为高，`phase_ok`失败。
- `configuration`：进入配置的字节数是9，测试依据附件要求5。
- `decode`：首字节为0、其余合法的模拟包被拒绝。

修改后全部通过。测试日志还保留一次测试模型修订：新增“坏PS2标记”场景会触发正常的禁动重试，旧测试末尾原本只排除了复位/拒绝模拟模式的情况，仍错误要求只配置一次；已将该坏包场景纳入排除条件。生产重试规则没有为迁就测试而修改。

完整记录见[测试日志](ps2-refactor-20261023-tests.log)：前段保留预期失败和上述测试开发过程，最终结果以`host-tests-final.log`与`board-tool-tests.log`段为准。

## 实际执行的检查

| 检查 | 结果 | 证据 / 范围 |
|---|---|---|
| PC核心逻辑 | 通过，129218条断言 | `build.ps1 -Test -Rebuild` |
| 标定/终端 | 通过，510条断言 | 同上；既有故障取消保护未回归 |
| 真实main接模拟设备 | 通过，29个场景 | 新增idle=0和坏PS2标记停车；保留晚开机、重启、故障锁存和重新解锁场景 |
| PS2专项 | 通过，3745项检查 | 4组：时序、配置、解码、错误参数；覆盖位序、片选、16us请求延时、输入边界和坏包清旧值 |
| Python日志工具 | 通过，17项 | `board.ps1 -Mode Test` |
| PowerShell回复检查 | 通过，33项 | 同上 |
| ARM GCC分析 | 通过，无诊断，警告视为错误 | 12个应用/目标C文件及PS2启用适配分支，实际执行`-fanalyzer -c` |
| 默认ARM固件全量构建 | 通过 | `RM_PS2_WIRED=0`，输出到正常`build/` |
| PS2启用固件全量构建 | 通过 | 独立`BUILD_DIR`、`EXTRA_CFLAGS=-DRM_PS2_WIRED=1` |
| 两种固件产物检查 | 通过 | ARM架构、入口、48KiB RAM顶初始栈、Reset/SysTick/USART1/USART2向量、HEX校验和及HEX/BIN一致 |
| 构建变体与版本 | 通过 | 两个BIN包含新版本行；PS2 GPIO/轮询符号仅存在于启用变体 |
| 本地差异与文档 | 通过 | `git diff --check`；55处本地Markdown链接有效；固件/测试输入哈希与执行检查时一致 |
| CubeIDE界面内导入/构建 | 未执行 | 使用CubeIDE附带的GNU Arm工具链做命令行构建，不等于IDE内已验证 |
| 烧录、电平、GPIO波形、实际运动与失联停车 | 未执行 | 没有主板；不得据此填写硬件验收通过 |

模拟主循环观察到的最大轮询间隔为62ms，小于现有100ms调度保护门槛；这是含串口/延时模型的结果，不是实测上限。PS2设置显式延时合计11192us，真实GPIO调用和中断可能增加耗时。

工具版本：ARM GCC 13.3.1（STM32 13.3.rel1）、PC GCC 16.2.0、GNU Make 4.4.1_st_20231030-1220、Python 3.12.14。构建使用已安装的build-makefile技能，参数含`--no-save-profile`，避开其缓存保存问题。static-analysis技能用于探测；实际分析直接运行`-fanalyzer -c`，没有把仅语法检查当作路径分析。未运行cppcheck、clang-tidy或MISRA认证。

构建调用见[构建日志](ps2-refactor-20261023-build.log)；分析的逐文件命令及结果见[分析JSON](ps2-refactor-20261023-analysis.json)。

## 产物与校验

| 构建 | Flash/BIN字节 | RAM含预留字节 | BIN SHA256 |
|---|---|---|---|
| 默认PS2关闭 | 20356 | 9016 | `42888eeca4acc9b31a748ad0ce2e5e645b669cc4ac881396a298816c518dea79` |
| PS2启用 | 21300 | 9016 | `d391306128eae1db746f1bd09d98c0a2a938b754861d49541f24ebc6e830f277` |

默认产物在`RCT6/build/rm_rct6.elf/.hex/.bin`；启用变体在聊天工作目录的`work/ps2-refactor-20261023-580bfe35/ps2/build/`。它只用于检查启用分支能完整链接，没有替换默认关闭PS2的产物。两种构建都保留未验收的运动/标定默认配置。

本轮导入`tools/verify_firmware.py`的`verify(project)`检查两种产物，启用变体旁放置仅用于检查的profile；另存[产物报告](ps2-refactor-20261023-artifacts.json)，保留旧`software-artifact-check.json`。JSON包含ELF/HEX/BIN哈希、固件/测试输入哈希、工具版本和`flashed=false`、`hardware_tested=false`。修改任一固件输入后应重新构建，不能继续引用这里的哈希为新版本证明。

后续在`RCT6/`、ARM GCC和Make已在PATH时，可复现两种构建：

```powershell
.\build.ps1 -Test -Rebuild
.\board.ps1 -Mode Test
.\build.ps1 -Rebuild
# 下面单独使用一个尚未混入其他变体的build目录；不是烧录命令。
make all -B -j4 BUILD_DIR=build/ps2-enabled EXTRA_CFLAGS=-DRM_PS2_WIRED=1
```

`board.ps1 -Mode Verify`仍会写它原有的`reports/software-artifact-check.json`；如需保留历史，应像本轮一样调用验证函数并另存报告。

## 仍待硬件验证

PB12～PB15的候选分配依据本车原理图；官方STM32示例的PA4～PA7属于另一块板。两份PS2附件对L3/R3左右名称有冲突，当前只用一致的START/SELECT，未新增争议按键功能。

官方资料尚不能证明用户手上转接板的DAT电平、供电余量、模式切换及无线断联行为。`valid`不是无线在线标志；关手柄后若接收器一直回旧包，格式检查无法发现。因此`RM_PS2_WIRED=0`、运动禁用、标定关闭和所有未验收标志继续保留。后续实测清单见[CONFIG_PENDING.md](../CONFIG_PENDING.md)。

## 工作区与回退

开始前已将259个项目文件和未提交协作文档复制到`work/ps2-refactor-20261023-580bfe35/before/`，哈希清单为同目录的`before-state.json`。上次的`.gitignore`、`AGENTS.md`、PR模板保留；根README在保留协作配置记录的基础上更新当前版本入口。

本轮只在本地修改、分析和构建。没有暂存、提交、推送、创建远程仓库/PR、修改GitHub设置或操作硬件。历史报告、参考资料和`SOURCE_MANIFEST.json`保留原样；旧源码ZIP仍是旧版本快照。

需要回退时先保存后续新增修改，再按备份逐文件审查恢复；不要用强制还原把上次尚未审核的文档一起丢弃。
