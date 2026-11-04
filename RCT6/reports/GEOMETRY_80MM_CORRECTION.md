# 80mm麦轮轮径修正与验证

记录日期：2026-10-26。用户更正为“80mm麦轮，其余正确”。当前本地版本为`rm_rct6-chassis-80mm-r3`；Git基准为`d1fcb37b4e0640aaf22289fb503d845ec26cc864`。本次未暂存、提交、推送或烧录。

## 修正范围

- [board_config.c](../src/board_config.c)将轮半径从0.030m改为0.040m，即80mm轮径除以2000。已检查除注释外只有这一项配置值变化。
- 前后轮心距141.776179mm、左右轮心距194.101555mm及其半值继续沿用。凹口朝前、车体前进=CAD -Z、向左=CAD -X，以及照片中的X布局和FL/FR/RL/RR名称均不变。
- [test_core.c](../tests/test_core.c)调整现有默认几何测试的角速度期望；[firmware_version.h](../include/firmware_version.h)、README、配置、终端及下载说明同步更新。
- 所有生产验收标志仍为false，`direction`仍为0、速度单位仍为UNKNOWN、`RM_PS2_WIRED=0`，运动与标定继续关闭。未改HAL、CubeMX生成代码、驱动参数或协议。

此前[60mm报告](GEOMETRY_60MM_CONFIG.md)只保留当时错误轮径配置的测试历史；[原60mm产物JSON](geometry-60mm-artifacts.json)和[原CAD数据](cad-geometry-20261025.json)保持原样。当前以用户更正的80mm名义轮径为准，不以CAD最大网格包络80.142745mm代替有效滚动直径，模型轮宽37.4mm仍不是实物轮宽测量值。

## 本次实际检查

| 检查 | 结果 |
|---|---|
| PC回归：`build.ps1 -Test -Rebuild` | 通过：核心129233条断言、29个主循环模拟场景、标定/终端510条断言、PS2专项3745项检查 |
| 默认ARM全量构建 | 通过：build-makefile脚本执行`make all -j 4 -B`，Cortex-M3、STM32F103xE、RCT6链接脚本 |
| ELF/HEX/BIN校验 | 通过：Flash 20352字节、含预留RAM 9016字节，初始栈0x2000c000、复位向量0x08003a99，UART1/UART2/SysTick向量匹配，HEX与BIN一致 |
| 新旧版本区分 | 通过：新BIN包含80mm-r3版本号，不含60mm-r2版本号 |
| 日志工具/电脑回复测试、PS2启用构建、静态分析 | 本次未执行；相关代码未改动，保留历史结果，不记为本次通过 |
| CubeIDE界面内构建、烧录及硬件验收 | 未执行；不访问设备 |

0.1m/s前进时四轮角速度应为2.5rad/s；0.1m/s左移为`[-,+,+,-] × 2.5rad/s`；0.5rad/s逆时针旋转为`[-,+,-,+] × 2.09923584rad/s`。轮缘线速度以角速度乘半径换回，所以上一版混合指令的`[46,154,86,114]mm/s`期望不变。该单位只在PC局部副本中启用；实际UNKNOWN单位仍拒绝输出，尺寸已填仍不能解锁。

## 产物与复核

当前下载文件为`build/rm_rct6.hex`，对应版本80mm-r3，尚未烧录。全部哈希、工具版本、输入文件哈希及检查结果见[本次产物记录](geometry-80mm-artifacts.json)。

| 产物 | SHA256 |
|---|---|
| BIN | `572f1c4c0fbba64cbd0056021092258c65f9345acf89e837a2ddd3dacfb5ef3c` |
| HEX | `5687e2a4e11304deccd7b3fb9de72149622f6a22719f52e5e0f4b7e88023c0db` |
| ELF | `6da51482a5b614b5789be8c8b847322201c20b7d3d1881e6c8558541dffdef60` |

使用ARM GCC 13.3.1、主机GCC 16.2.0、GNU Make 4.4.1_st_20231030-1220、Python 3.12.14。修改前的16个文件/固件备份、278个已跟踪文件的哈希、测试及构建日志保存在聊天工作目录`work/geometry-80mm-20261026-r3/`。产物校验直接调用`tools/verify_firmware.py`的`verify()`，未覆盖旧报告。

最后确认在主板运行的仍是`rm_rct6-chassis-20261023-r1`，限于[单板启动及version/config通信](USB_ISP_2026-10-25.md)。实际安装、轮序/转向、速度单位和保护验收继续按[待核实配置](../CONFIG_PENDING.md)完成。
