# 60mm麦轮配置与软件验证

记录日期：2026-10-26。固件编号：`rm_rct6-chassis-60mm-r2`。Git基准为`bf62d3a2eab911ca48d46e224cdd324b5ff281d9`，本次及此前修改均保留为未暂存工作区内容。**本次没有连接设备或烧录，新固件没有硬件测试结果。**

## 输入依据与修改

- 用户明确实物轮径为60mm、底盘凹口端为车头；用户照片中两种麦轮对角配对，当前摆放符合X布局。照片尚未展示安装到底盘后的状态。
- 原CAD的前后轮心距为141.776179mm，左右轮心距为194.101555mm；当前以此为几何初值，换装60mm轮子后还需测量实物装配轮心距。原CAD中的约80mm轮模型和37.4mm模型轮宽保留为历史数据，不能用作当前实物规格。
- 底盘缓存网格的中央部分在CAD -Z端缩进40mm，结合凹口朝前，确定车体前进=CAD -Z、向左=CAD -X、向上=CAD +Y。左前/右前/左后/右后对应原CAD记录D/C/B/A；这不证明实际驱动接线顺序。
- [board_config.c](../src/board_config.c)已填`radius_m=0.030`、`half_length_m=0.070888089544`、`half_width_m=0.097050777636`。注释说明来源、换算和仍需复核的部分；[几何说明](../GEOMETRY_REFERENCE.md)保留完整原始数据及来源。
- [test_core.c](../tests/test_core.c)在现有运动学测试中增加15条检查，覆盖60mm默认参数、前进/横移/旋转换算和禁动条件；[firmware_version.h](../include/firmware_version.h)更新固件编号以区别旧板上版本。

所有生产`*_verified`仍为false，方向仍为0，速度单位仍为UNKNOWN；标定关闭、`RM_PS2_WIRED=0`、默认运动禁用。候选通道FL/FR/RL/RR → M1/M3/M2/M4未变，没有写入智能驱动参数。

## 实际检查

下列测试和构建在本次本地参数修改后完成，未用旧报告代替新结果。

| 检查 | 结果 | 范围 |
|---|---|---|
| `.\build.ps1 -Test -Rebuild` | 通过 | 核心129233条断言、29个主循环模拟场景、标定/终端510条断言、PS2专项3745项检查 |
| 默认ARM全量构建 | 通过 | 已安装build-makefile脚本执行`make all -j 4 -B`；Cortex-M3、STM32F103xE、RCT6链接脚本 |
| ELF/HEX/BIN检查 | 通过 | Flash 20352字节，含预留RAM 9016字节；初始栈0x2000c000，复位向量0x08003a99；UART1/UART2/SysTick向量匹配，HEX与BIN内容一致，新版本字符串存在 |
| 日志工具17项、电脑回复33项检查 | 本次未执行 | 保留[PS2重构记录](PS2_REFACTOR_2026-10-23.md)的历史结果 |
| PS2启用变体构建、ARM静态分析 | 本次未执行 | 本次未修改PS2适配；只重建默认禁用版本 |
| CubeIDE界面内构建、烧录、实物运动/保护验收 | 未执行 | 不用PC模拟或ARM构建代替硬件结论 |

新增运动学检查包括：0.1m/s前进时四轮约3.333333rad/s；0.1m/s左移为`[-,+,+,-]`；0.5rad/s逆时针旋转为`[-,+,-,+] × 2.798981rad/s`。仅在测试局部副本设置MM_S后，`vx=0.1m/s、vy=0.02m/s、wz=0.2rad/s`换算为FL/FR/RL/RR的`[46,154,86,114]mm/s`；实际默认UNKNOWN单位仍拒绝输出，填入尺寸后仍不能解锁运动或标定。

## 固件与复核记录

当前产物为`build/rm_rct6.elf`、`build/rm_rct6.hex`、`build/rm_rct6.bin`。产物哈希、本次记录的32个固件/测试输入哈希、照片哈希与校验结果见[本次产物记录](geometry-60mm-artifacts.json)。原始[CAD数据快照](cad-geometry-20261025.json)保留原样。

| 产物 | SHA256 |
|---|---|
| BIN | `2635fd9707a86bc7ca1c8df2471f4312f17a0db843376ae7b0e9f58db2305d45` |
| HEX | `4cc264dc638c0ec90c3a404dda02f2abf794e684f79fcd61063add2c3ba0ce8c` |
| ELF | `161b06c5a75ba40aa0b92ac7046468b36b5805c914ed4c1987b499de1a6fe7f7` |

工具沿用[PROJECT_PROFILE.json](../PROJECT_PROFILE.json)中的CubeIDE GNU Arm GCC/Make和`build.ps1`中的w64devkit主机GCC；本次读取的版本为ARM GCC 13.3.1（STM32 13.3.rel1.20240926-1715）、GNU Make 4.4.1_st_20231030-1220、主机GCC 16.2.0、Python 3.12.14。构建由已安装的`build-makefile/scripts/makefile_builder.py`执行，参数为`--target all --extra-args=-B --no-save-profile -j 4`。产物检查调用`tools/verify_firmware.py`的`verify()`，没有覆盖旧的`software-artifact-check.json`历史记录。

本地收尾检查：32个已记录输入哈希和3个产物哈希一致，91处本地文档链接可解析，Project Profile引用的报告均存在；`git diff --check`通过，暂存区为空。

本轮修改前的文件和固件备份，以及测试/构建原日志，保存在聊天工作目录`work/geometry-60mm-d14bca95/`，其中`before.json`记录修改前哈希，`tests.log`、`build.log`、`artifact-check.json`对应本次结果。备份属于本机工作材料，不作为仓库内可移植链接。

最后确认在主板上运行的仍是`rm_rct6-chassis-20261023-r1`，其单板启动和version/config通信证据见[USB实测记录](USB_ISP_2026-10-25.md)。本次没有暂存、提交、推送、创建PR或再次烧录；实际轮心距、轮序/转向、速度单位、PS2与独立停车保护按[待核实配置](../CONFIG_PENDING.md)逐项完成。
