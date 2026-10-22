# Review问题修复与验证

日期：2026-10-22。固件编号：`rm_rct6-chassis-20261022-r1`。

上一轮review的P1、P2已修复。流程与之前一致：保存修改前文件、先让新增测试暴露问题、修复并补白话注释、运行回归、静态分析、重新构建两种ARM固件、核对产物和使用文档。

## 修了什么

| 问题 | 修改后的行为 | 主要代码 |
|---|---|---|
| P1：电脑串口故障后仍可能启动标定 | 回复失败后撤销许可；读手柄后再检查；发送前发现故障时，把已经算好的非零PWM文字替换成PWM0 | `target/main.c` |
| P2：自动状态被误认为指令回复 | 所有命令回复统一带`reply `；电脑工具要求恰好一条完整、对应的回复，排除自动日志、错误回复、半截回复和不对应的回复 | `src/console.c`、`board.ps1`、`tools/console_reply.ps1` |

标定停止仍保留已记录的电机通信故障和急停状态。普通PS2行驶继续使用自身的安全检查。默认正常行驶、单轮标定及PS2接线开关仍未开放。

回复缓存与调试发送上限一并设为192字节，发送等待上限约20ms；最长驱动原文加上前缀仍能显示。编译时检查回复缓冲是否超过调试发送上限。超长调试发送也会记录故障，避免静默丢掉回复。

电脑工具与新固件应配套使用。旧固件的回复没有`reply `时，Command模式会报缺少对应回复，不再根据自动日志判断成功。交互终端输入的命令写法不变，详细示例见[CONSOLE.md](../CONSOLE.md)。

## 怎样证明修复有效

修复前先把故障注入用例加入真实main的PC设备模型。旧代码在“标定回复发送失败”场景中仍启动电机输出，新断言失败；记录在工作区`work/review-20261022/before-fix-regression.log`。修复后，这个场景和后续新增场景全部通过。

新增的六个主循环场景检查：

1. `cal arm`回复失败后，不能继续使用许可。
2. `cal pulse`回复失败后，待执行脉冲不能启动。
3. 读PS2期间发生电脑接收故障，待执行脉冲取消。
4. 读PS2期间发生电脑接收故障，正在运行的脉冲停车。
5. 已经生成非零PWM、但尚未发送时发现故障，发送内容换成PWM0。
6. 电脑通道故障不会取消正常PS2行驶自身的安全逻辑。

测试对故障后的标定输出逐条计数，要求非零输出为0次。它不只是检查程序最后有没有停车。

电脑端新增33项检查，运行的是Command模式实际使用的`Get-BoardCommandResult()`。覆盖自动状态、开机版本、对应回复、错误回复、无回复、半截回复、多条回复、大小写、多余空格及数字前导零。C测试也验证了前导零和最长驱动回复，防止固件接受指令后电脑工具却误判失败。

## 已执行的验证

| 验证 | 结果 |
|---|---|
| 核心C逻辑 | 129218条断言通过，含大量固定噪声恢复检查 |
| 真实main与模拟设备 | 27个场景通过；模拟中最大轮询间隔48ms |
| 标定与电脑指令 | 六组、510条断言通过 |
| 日志工具 | 17项通过 |
| 电脑指令回复 | 33项通过，实际使用Windows PowerShell 5.1运行 |
| ARM GCC静态分析 | 12个C源文件及PS2启用适配分支执行`-fanalyzer -c`，无诊断，警告视为错误 |
| 默认ARM固件全量构建 | 通过；Flash20352字节，RAM含预留9016字节 |
| PS2启用固件独立全量构建 | 通过；Flash21264字节，RAM含预留9016字节 |
| 两种固件结构 | ARM架构、栈顶、Reset/SysTick/USART1/USART2向量、HEX校验和及HEX/BIN内容检查通过 |

默认BIN SHA256：`058bdbaad03d9eb6d691cef64dd9554e0b6ded23f350493c445a04ddd3cc146d`。

默认固件仍在`build/rm_rct6.elf`、`build/rm_rct6.hex`、`build/rm_rct6.bin`。PS2启用变体在工作区`work/review-fixes-20261022/ps2/build`，未替换默认禁用PS2的固件。

证据文件：`review-fix-tests.log`、`review-fix-tools-tests.log`、`review-fix-build.log`、`review-fix-ps2-build.log`、`review-fix-static.log`、`review-fix-delivery.log`、`review-fix-artifact-check.json`。`software-artifact-check.json`同步记录当前默认固件，PROJECT_PROFILE指向本页。

修改前19份相关文件保存在工作区`work/before-review-fixes-20261022`；源文件对比保存在`work/review-fixes-20261022/review-fixes.diff`。复查时已核对故障处理顺序、待发送文字替换、错误回复前缀、缓存容量和旧固件兼容提示，未发现新的确定问题。

## 在电脑上复跑

在RCT6目录打开PowerShell：

```powershell
.\build.ps1 -Test
.\board.ps1 -Mode Test
.\build.ps1 -Rebuild
.\board.ps1 -Mode Verify
```

以上操作不需要主板。尚未烧录、尚未做实际UART/GPIO收发或电机运动测试；物理急停输入、接线、尺寸、速度单位、无线失联和驱动独立停车保护仍按既有清单留待实测。
