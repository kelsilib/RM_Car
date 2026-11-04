# 麦轮上电前软件准备与分支整理

本报告记录用户调整急停要求之前的r7准备结果。用户随后明确采用SELECT/电脑stop，当前r8见[软件急停验证](SOFTWARE_STOP_2026-11-04.md)。r7固件归档于work/preflight-20261104/before-software-stop/RCT6/对应build目录，当前build目录已更新为r8。

日期为 2026-11-04。本轮完成本地分支核对、工程迁入、工具修复、回归、构建及接线测试表。今晚操作从[联调入口](../TONIGHT_BRINGUP.md)开始，机器可读证据见[产物与源码清单](preflight-artifacts-20261104.json)。

## 分支与已有工作的去向

| 来源 | 实际状态 | 本次处理 |
|---|---|---|
| `origin/main` | `e6ad2e2dd7707051d71418000becf65ed23a5873`，已合并 PR #1 | 本地 main 跟踪此版本，以它建立联调分支 |
| `origin/feat/rct6-ps2-60mm` | `d1fcb37b4e0640aaf22289fb503d845ec26cc864`，已包含在 main，文件树与 main 一致 | 保留本地功能分支与远程引用，历史名称保留 |
| 旧本地 main | `bf62d3a2eab911ca48d46e224cdd324b5ff281d9` | 作为 `source/main` 引用保留 |
| 旧目录中的未提交工作 | 80mm几何修正、驱动r6、七路机械臂及相关资料/测试 | 逐文件复制进联调工作区，全部保留为未暂存改动 |
| 当前联调分支 | `test/mecanum-bringup-20261104`，HEAD 为上述 e6ad2e2 | 集中本次准备工作，等待审核 |

原来源为 `C:\Users\fakel\Documents\Codex\2026-10-21\g-g\outputs\RM_Car`，当前工程根为 `C:\Users\fakel\Documents\ChatGPT\adam校内赛`。迁入302个文件，复制后SHA256全部一致，收尾再次核对原来源文件保持原哈希。构建目录、缓存与旧 `.git` 独立保留在原处。相关聊天已只读核对，机械臂与驱动后续开发共享旧目录，其改动已包含在本次迁入内容中。

当前配置仍为80mm轮径，前后轮心距141.776179mm、左右轮心距194.101555mm，凹口朝前。历史分支名中的60mm对应早期记录。所有修改仍未暂存，当前HEAD仅表示基线，构建输入另以本次清单中的171个文件哈希标识。

## 本轮修改

| 文件 | 修改内容 |
|---|---|
| `board.ps1` | Check默认UART，显式SWD保留原入口，减少USB联调对SWD工具的依赖。使用端口枚举，兼容顶层sha256与default.sha256两种记录，哈希不符则失败，报告使用新文件名，JSON按UTF-8读取 |
| `tools/verify_firmware.py` | 支持独立构建目录、预期版本与指定新报告路径。默认从PROJECT_PROFILE取得固件编号，核对完整version行及结束符，已有报告拒绝覆盖 |
| `tests/test_preflight_tools.py` | 新增7项针对工具修复的回归，接入board.ps1正常Test入口。USB枚举使用假工具，夹具放临时目录 |
| `include/firmware_version.h` | 默认版与PS2版分别使用r7-safe/r7-ps2，便于今晚用version确认所烧构建 |
| `PROJECT_PROFILE.json` | 更新迁入后的路径、当前版本与报告入口 |
| README、AGENTS、联调相关文档 | 增加当前分支、供电核对、接线安排、固件哈希和测试顺序，历史报告保留 |

本轮应用层代码仅更新版本头。`Drivers/`、启动、链接脚本、HAL配置以及迁入的运动/机械臂实现均保持迁入时内容。所有生产验收项为false，`direction={0,0,0,0}`，标定关闭，机械臂配置为零。默认PS2为0，独立只读版通过编译参数启用PS2四根信号，舵机接口仍为0。

## 实际执行的检查

| 检查 | 结果 |
|---|---|
| PC核心逻辑 | 129334条断言通过 |
| 底盘真实主循环模拟 | 32个场景通过，包含独立停车缺失和轮径不匹配的禁动 |
| 标定/终端服务 | 555条断言通过 |
| PS2专项 | 6580项通过 |
| 机械臂逻辑/主循环 | 42651条断言及11个场景通过 |
| Python日志、PowerShell回复 | 17项及43项通过 |
| 新增联调工具回归 | 7项通过，覆盖新旧哈希格式、哈希不符/缺失、UART无需OpenOCD、版本前缀与默认版本、已有报告防覆盖 |
| 默认ARM、PS2只读ARM | 全量重编译成功，独立build与build-ps2-readonly目录 |
| 产物检查 | 两版ARM架构、48KiB RAM初始栈、Reset/USART1/USART2/SysTick向量、Flash/RAM边界、版本行、HEX校验和及HEX/BIN一致均通过 |
| GCC路径分析 | 13个C模块及PS2/舵机启用接口分支，共15次 `-fanalyzer -c` 无诊断，警告视错误，见[分析命令与结果](preflight-static-20261104.json) |
| 正常工具入口 | Windows PowerShell 5.1下的Test、Verify、默认UART Check均通过 |
| 电脑USB枚举 | CubeProgrammer 2.20.0仅列COM3、COM4，制造商Microsoft。未枚举到此前的COM5/CH340，今晚插板后重新Check，见[当前枚举](host-check-18a77094.json) |
| Git与文件检查 | `git diff --check`通过，暂存区为空。历史JSON换行提示按原始文件保留，新增文档/JSON另行检查 |
| CubeIDE界面构建、烧录、外设接线、电机运动及故障停车 | 未执行 |

构建命令使用已安装的build-makefile技能脚本，`--no-save-profile`避免改动技能缓存。Makefile已解析为GNU Arm/Cortex-M3，实际MCU宏为STM32F103xE，链接为256KiB Flash/48KiB RAM。静态分析技能脚本使用`-fsyntax-only`，本轮依其流程直接执行完整`-fanalyzer -c`。工具版本为ARM GCC 13.3.1、PC GCC 16.2.0、Make 4.4.1、Python 3.12.14。

初次构建在Windows沙箱内遇到DLL重定位623，Windows PowerShell启动也被沙箱阻止。随后在自动审核允许的本地执行环境完成上述检查。技能脚本第一次按GBK读取含中文路径的构建输出失败，改为本次Python `-X utf8`后成功。上述失败均未作为通过记录，系统执行策略和工具安装保持原设置。

可随源码交付的输出摘录见[软件测试原文](preflight-software-20261104.log)，主要命令如下，在工程根执行。

```powershell
.\RCT6\build.ps1 -Test -Rebuild
powershell.exe -NoProfile -File .\RCT6\board.ps1 -Mode Test
powershell.exe -NoProfile -File .\RCT6\board.ps1 -Mode Verify
powershell.exe -NoProfile -File .\RCT6\board.ps1 -Mode Check
```

本轮实际ARM构建由技能脚本设置工具PATH后运行，默认全量`make all -j 4 -B`。PS2只读版额外传入`BUILD_DIR=build-ps2-readonly`及`EXTRA_CFLAGS=-DRM_PS2_WIRED=1 -DRM_ARM_WIRED=0`。原始构建、PC、工具及分析日志保存在本地忽略目录`work/preflight-20261104/logs/`，哈希已写入清单。

## 已准备产物

| 构建 | 版本 | Flash | RAM含预留 | 结构报告 |
|---|---|---|---|---|
| 默认 | `rm_rct6-preflight-20261104-r7-safe` | 26056字节 | 9376字节 | [默认版](preflight-safe-check-20261104.json) |
| PS2只读 | `rm_rct6-preflight-20261104-r7-ps2` | 27016字节 | 9376字节 | [PS2版](preflight-ps2-check-20261104.json) |

可下载HEX分别位于`RCT6/build/rm_rct6.hex`和`RCT6/build-ps2-readonly/rm_rct6.hex`。版本与全部ELF/HEX/BIN哈希见[清单](preflight-artifacts-20261104.json)，用户操作表另列在[联调入口](../TONIGHT_BRINGUP.md)。当前默认config预期为`issues=0xBF speed_unit=1 cal_enabled=0`。最后确认在实板运行的仍为r1，当前两版尚待下载。

## 用户补充与待核实项

本轮用户提供两张购买截图，确认电池选用3S标称11.1V、3000mAh。截图中的XT60一分四用于分电，用户计划电池直接接驱动VIN，主控经原有稳压板固定5V输出接端子。用户最初转述驱动可用12V以内，解释常规3S满电12.6V后，用户进一步确认该输入没问题。本次按用户确认记录12.6V兼容性，实际标签、电压/极性与板卡版本仍需核对。图中的订单大标题写7.4V，选中规格与用户确认按3S处理。

用户先表示有保护，进一步确认指驱动板自带保护，随后再次明确它不会自动停车，手头只有现有主控和驱动。已有记录为断命令保持末值，无超时停车配置。转轮前需取得厂家超时锁存固件或新增独立保护硬件，并完成适配与实测。可直接发给厂家的说明及保护验收项已写入[联调入口第5节](../TONIGHT_BRINGUP.md#5-当前缺少自动停车怎样补齐)，`independent_stop_verified=false`保留。

PS2、电机与编码器今晚才接。本次指定M1左前、M2左后、M3右前、M4右后，与既有代码`channel={0,2,1,3}`一致。电机正反、编码器参数、PS2电气与无线失联均待测。当前准备可支持单板下载与分阶段无负载检查，转轮阶段待供电和保护条件落实后继续。

## 回退与审核

迁入前原始文件清单及哈希在`work/preflight-20261104/source-before.json`，本轮修改前主要文件在`work/preflight-20261104/before-preparation-edits/`。回退时先对比具体文件，仅回退本轮修改，保留旧目录中迁入的机械臂、驱动和几何后续工作。

当前本地main和历史功能分支均保留，远程分支配置与GitHub设置按原记录使用。没有暂存、提交、推送、创建PR或烧录。下一轮修改时先核对当前工作区，人工审核与硬件验收继续按AGENTS执行。
