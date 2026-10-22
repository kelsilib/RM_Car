# RM_Car：STM32F103RCT6 麦克纳姆底盘

STM32F103RCT6 + 亚博四路智能串口驱动 + PS2 手柄 + 四轮麦克纳姆。当前固件：`rm_rct6-chassis-20261022-r1`。

**开发、编译和测试从 [RCT6/README.md](RCT6/README.md) 开始。** 当前软件已完成电脑测试和真实ARM构建；尚未烧录或做实物UART/GPIO、电机测试。默认行驶与标定均禁用，待核实项见[CONFIG_PENDING.md](RCT6/CONFIG_PENDING.md)。

## 阅读入口

| 文档 | 用途 |
|---|---|
| [代码阅读与测试索引](RCT6/TEST_INDEX.md) | 每个模块干什么、改参数去哪里、测试看什么 |
| [软件范围](RCT6/SOFTWARE_SCOPE.md) | 当前已完成的软件和实物待办 |
| [电脑串口指令](RCT6/CONSOLE.md) | 查询、停车、受限单轮标定与reply回复 |
| [硬件说明](RCT6/HARDWARE.md) | 引脚、供电和接线资料 |
| [协议说明](RCT6/PROTOCOL.md) | 电机UART、PS2与故障处理 |
| [上板步骤](RCT6/BRINGUP.md) | 有主板后的下载和联调 |
| [最新修复验证记录](RCT6/reports/REVIEW_FIXES_2026-10-22.md) | review修复、测试、构建与固件哈希 |

## 在电脑上运行

在PowerShell进入仓库目录后：

```powershell
cd RCT6
.\build.ps1 -Test
.\board.ps1 -Mode Test
.\build.ps1
.\board.ps1 -Mode Verify
```

前两项运行C逻辑/模拟设备、Python日志和PowerShell回复检查；第三项编译ARM固件；第四项检查固件地址、向量及HEX/BIN内容。它们均不连接主板。首次获取源码时，应先编译，再运行Verify。

脚本记录了开发电脑的工具安装位置。换电脑时，安装GNU Arm工具链、Make、PC版GCC、Python和PowerShell，再调整`RCT6/build.ps1`中的`taskPlugins`、`taskHostGcc`，以及`RCT6/PROJECT_PROFILE.json`和`RCT6/tools/board-tools.json`中的工具路径。在工具已加入PATH时，也可于RCT6目录运行`make test HOST_CC=gcc`和`make all`。

## 源码与资料

- `RCT6/src`、`include`、`target`：当前底盘的应用、安全、遥控、运动学、串口、标定和MCU适配。
- `RCT6/tests`、`tools`、`openocd`：电脑测试和后续联调工具。
- `RCT6/reports`：测试与构建证据；以最新修复验证记录为准。
- `Drivers`：构建必需的ST HAL/CMSIS依赖，保留原有版权声明。
- `references`：已有原理图、厂商UART示例ioc和迁移依据。
- `App`、`BSP`、`Core`、`ld`、`startup`与根目录构建文件：保留的旧F103C8/TB6612工程作参考；当前RCT6使用子目录入口。
- [README_MIGRATION.md](README_MIGRATION.md)：上传前根README的完整内容和迁移记录。
- [RM_CODEX_HANDOFF.md](RM_CODEX_HANDOFF.md)：原始交接文档；较早范围以当前软件范围为准。

所有项目README、说明文档、源码、必要驱动、已有参考资料和验证报告均包含在仓库及源码ZIP中。编译产物、缓存和临时文件由`.gitignore`排除，需在本机重新生成。K210、视觉和机械臂不属于当前开发范围。

## 当前验证

核心C逻辑129218条断言、标定/终端510条断言、27个模拟主循环场景、17项日志工具检查和33项电脑回复检查通过。默认及PS2启用两种ARM构建通过，ARM GCC静态分析无诊断。实物接线、尺寸、速度单位和失联停车仍需主板就绪后验证。

第三方来源及现有授权标记见[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
