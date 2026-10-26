# RM_Car：STM32F103RCT6 麦克纳姆底盘

STM32F103RCT6 + 亚博四路智能串口驱动 + PS2 手柄 + 四轮麦克纳姆。当前本地固件：`rm_rct6-chassis-60mm-r2`，已构建、尚未烧录。最后确认在主板上运行的仍是`rm_rct6-chassis-20261023-r1`。

**开发、编译和测试从 [RCT6/README.md](RCT6/README.md) 开始。** 当前采用用户确认的60mm轮径、凹口车头和CAD轮心距初值，见[尺寸与方向说明](RCT6/GEOMETRY_REFERENCE.md)。新r2已完成电脑测试和真实ARM构建；2026-10-25旧r1通过CH340/FlyMCU烧录后，已捕获主板启动信息和59行连续禁动状态日志，并取得version/config双向查询回复。PS2、电机及整车验收仍待完成，见[单板实测记录](RCT6/reports/USB_ISP_2026-10-25.md)。默认行驶与标定均禁用，待核实项见[CONFIG_PENDING.md](RCT6/CONFIG_PENDING.md)。

## 阅读入口

| 文档 | 用途 |
|---|---|
| [代码阅读与测试索引](RCT6/TEST_INDEX.md) | 每个模块干什么、改参数去哪里、测试看什么 |
| [软件范围](RCT6/SOFTWARE_SCOPE.md) | 当前已完成的软件和实物待办 |
| [电脑串口指令](RCT6/CONSOLE.md) | 查询、停车、受限单轮标定与reply回复 |
| [硬件说明](RCT6/HARDWARE.md) | 引脚、供电和接线资料 |
| [协议说明](RCT6/PROTOCOL.md) | 电机UART、PS2与故障处理 |
| [PS2资料依据与对应关系](RCT6/PS2_REFERENCE.md) | 官方附件、接线/按键、时序、资料冲突和待实测项 |
| [上板步骤](RCT6/BRINGUP.md) | 有主板后的下载和联调 |
| [A 转 C 下载说明](RCT6/USB_ISP.md) | CH340 端口识别、FlyMCU 串口 ISP、写后校验与日志 |
| [尺寸与方向说明](RCT6/GEOMETRY_REFERENCE.md) | 60mm轮径、CAD轮心距、凹口车头及四轮名称 |
| [当前配置验证记录](RCT6/reports/GEOMETRY_60MM_CONFIG.md) | 60mm配置、PC回归、默认ARM构建与固件哈希；未烧录 |
| [PS2重构记录](RCT6/reports/PS2_REFACTOR_2026-10-23.md) | PS2重构、测试及默认/启用两种构建的历史证据 |

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

此前交付的仓库和源码ZIP包含当时的项目README、说明文档、源码、必要驱动、已有参考资料和验证报告；后续本地改动不会自动进入旧ZIP或GitHub。编译产物、缓存和临时文件由`.gitignore`排除，需在本机重新生成。K210回传后三色灯带亮灯、机械臂控制已另列待办，资料缺项见[软件范围](RCT6/SOFTWARE_SCOPE.md)，当前固件尚未实现这两项。

2026-10-23设备状态更新：主板已到手，尚未接电脑。主下载路径改为USB-A转Type-C，经板载CH340执行串口ISP；详见[A转C下载说明](RCT6/USB_ISP.md)。本次只整理文档，未连接、烧录或进行实物测试。下方历史Git检查记录按当时状态保留。

## 当前验证

当前60mm配置：核心C逻辑129233条断言、标定/终端510条断言、29个模拟主循环场景和PS2专项3745项检查通过；默认ARM构建及ELF/HEX/BIN检查通过。新r2没有烧录，实际轮心距、轮序/转向、速度单位及失联停车仍待验证，见[本次报告](RCT6/reports/GEOMETRY_60MM_CONFIG.md)。

2026-10-23历史：核心C逻辑129218条断言、标定/终端510条断言、29个模拟主循环场景、PS2专项3745项检查、17项日志工具检查和33项电脑回复检查通过；默认及PS2启用两种ARM构建和产物检查通过，ARM GCC静态分析无诊断。日志工具、电脑回复、PS2启用构建和静态分析本次未重跑，不将旧结果作为新r2的执行记录。

第三方来源及现有授权标记见[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 项目协作与 Git 工作流（2026-10-23 补充）

仓库协作规则见 [AGENTS.md](AGENTS.md)，PR 使用[审核模板](.github/PULL_REQUEST_TEMPLATE.md)。本节补充现有说明；旧 README、源码、HAL/CMSIS 和构建文件均保留。

本节下方的Git检查表和“本轮未重跑”等表述，记录的是PS2重构之前的那次本地协作配置检查。后续结果见上方“当前验证”、[60mm配置报告](RCT6/reports/GEOMETRY_60MM_CONFIG.md)及[PS2重构报告](RCT6/reports/PS2_REFACTOR_2026-10-23.md)，原检查记录保留。

### 目录与 CubeIDE/CubeMX 的关系

```text
RM_Car/
├── AGENTS.md                       开发者与 AI 的协作规则
├── .github/PULL_REQUEST_TEMPLATE.md 每次 PR 的审核、测试与硬件状态
├── RCT6/
│   ├── src/、include/              当前应用逻辑，与 HAL 分开
│   ├── target/                     当前手工维护的 HAL 适配、启动与链接
│   ├── tests/、tools/、openocd/     PC 测试和后续硬件联调工具
│   ├── reports/                    已有软件验证证据
│   └── Makefile、build.ps1         当前 RCT6 构建入口
├── Drivers/                        共用 ST HAL/CMSIS，保留版权声明
├── references/                     原理图、厂商示例 .ioc 和迁移记录
├── App/、BSP/、Core/、ld/、startup/ 旧 F103C8/TB6612 参考工程
└── README_MIGRATION.md             原 README 与迁移过程
```

当前是使用 HAL 的 Makefile 工程，尚无本车 CubeIDE `.project`、`.cproject` 或 CubeMX `.ioc`。不能直接按“导入现有 CubeIDE 工程”认定配置已就绪。需要在 CubeIDE 内构建时，可从 `File > New > Other…` 查找 `Makefile Project with Existing Code`，选择 `RCT6/`，使用现有 Makefile并配置本机 ARM GCC/Make 的路径；若项目属性中有自动生成 Makefile 选项，应关闭它。不同版本的菜单和可用向导可能不同；该导入路径本轮未在界面执行，仍需验证，它也不会生成本车 `.ioc`。

导入时保留 `../Drivers/` 相对路径，只使用当前 `RCT6/target/` 的 main、启动文件、中断和链接脚本，避免把根目录旧工程一起编译。导入成功、编辑器识别头文件、命令行构建通过、CubeIDE 内构建通过要分别记录。

如果需要 CubeMX 受管工程，先按 [BRINGUP.md](RCT6/BRINGUP.md) 和硬件待核实清单确认配置，在独立新目录生成，审查后再迁入。现有厂商 `.ioc` 的项目名和工具链属于示例；不在当前目录上直接生成或覆盖。生成文件中只在成对的 `USER CODE BEGIN/END` 区块添加业务调用，核心逻辑仍放独立模块；保留用户代码选项、生成前备份和生成后差异审查缺一不可。

`.gitignore` 保留 `.ioc`、`.project`、`.cproject`、可移植的 `.settings/`、HAL 配置、启动与链接脚本的跟踪能力，只排除构建目录、本机缓存和私密文件。调试配置若要共享，先确认无本机绝对路径、设备序列号或敏感信息。

### 本次 Git 与远程检查

| 项目 | 2026-10-23 检查结果 |
|---|---|
| Git 仓库根目录 | 本 README 所在的 `outputs/RM_Car`，不是聊天工作目录 |
| 分支及上游 | 本地 `main` 跟踪 `origin/main` |
| 本地与远程 main | 均为 `bf62d3a2eab911ca48d46e224cdd324b5ff281d9`（本轮修改前） |
| origin 获取/推送地址 | `https://github.com/kelsilib/RM_Car.git` |
| GitHub 默认分支 | `main` |
| GitHub 当前可见性 | 公开（实时读取结果，早期上传记录中的私有状态不再代表当前） |
| 修改前工作区 | 干净；本轮修改后有 4 个待审核文件，均未暂存 |
| 换行设置 | 本仓库 `core.autocrlf=false`，本轮未改 Git 设置 |
| 分支保护与 CI | 本轮未核实保护规则，也未配置远程保护或 CI；文档约定不等于平台已强制执行 |

该表是一次检查记录。之后操作前重新查看分支、工作区和远程状态，不用旧表代替实时检查。本轮只新增 `AGENTS.md`、PR 模板，并追加 README 和忽略规则；未提交、推送、创建 PR 或改动 GitHub 设置。

### 日常修改怎么走

1. 检查 `git status --short --branch`、`git remote -v` 和已有差异，先保留未提交工作。
2. 获得相应 Git 操作授权后，在工作区可安全切换时读取远程进度，从最新 `origin/main` 建工作分支：`feat/<主题>`、`fix/<主题>`、`docs/<主题>` 或 `chore/<主题>`。不直接在 `main` 提交开发改动，不额外要求 `develop`。
3. 小范围修改，运行相关检查。文档修改查链接和忽略规则；代码修改跑对应 PC 测试；固件输入修改增加 ARM 构建和 ELF/HEX/BIN 检查。硬件未测必须明写。
4. 用户允许提交后，只暂存明确文件，检查 `git diff --check`、`git diff --cached --check` 与 `git diff --cached`，再提交。标题用 `type(scope): 说明`；AI 参与时在正文末尾加 `AI-Assisted: Codex`（其他工具填实际名称）。
5. 用户允许推送和创建 PR 后，向 `main` 提交 PR，并填写模板。至少一位有权限的人工审核人确认后再合并；个人维护者可在 PR 记录自行复核，不冒充另一位审核人。AI 审查不替代人工批准，squash 合并也须保留 AI 标记。

例如，AI 协助补充文档时的提交正文如下。这里只展示约定，本轮没有执行：

```text
docs(workflow): 补充 STM32 协作规则

记录工程入口、生成代码保护和验证要求。

AI-Assisted: Codex
```

### 测试结果如何记录

| 层次 | 可以说明什么 | 当前证据 |
|---|---|---|
| PC 逻辑和工具测试 | 算法、解析、安全状态及模拟故障行为 | 2026-10-22 报告记录通过，本轮未重跑 |
| ARM 命令行构建与产物检查 | 源码能生成 RCT6 ELF/HEX/BIN，文件结构符合检查 | 2026-10-22 报告记录通过，本轮未重跑 |
| CubeIDE 内导入与构建 | 本机 IDE 配置可实际使用 | 本轮未执行，不能用命令行构建结果代替 |
| 烧录、实物 UART/GPIO、电机运动 | 对应主板上的实际表现 | 没有主板，未执行 |
| 无线断联、断线、复位与独立停车 | 实物故障状态是否可靠停车 | 未执行，不能用发送 PWM0 代替验收 |

软件历史证据见[最新修复验证记录](RCT6/reports/REVIEW_FIXES_2026-10-22.md)。每个 PR 填写本次的“通过 / 失败 / 未执行 / 不适用”，并给出日期、命令、工具版本和证据；不能默认勾选通过。硬件实测另需记录固件提交号、构建变体、BIN SHA256、板卡版本、接线/供电条件和实际观察。涉及开启运动、标定或硬件已验证标志的修改，要有对应实测记录；缺少主板时继续保持默认禁动。

`SOURCE_MANIFEST.json` 和此前的源码 ZIP 对应 2026-10-22 的打包快照，未包含本轮新增的协作文档。它们保留作历史交付证据，不作为当前工作区的实时清单；下次重新发包时再生成新的清单和哈希。
