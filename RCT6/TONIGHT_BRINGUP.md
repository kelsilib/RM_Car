# 2026-11-04 麦轮联调入口

同日追加r9，用△直接选择S1 K210升降台，S2～S7共六路供机械臂使用，操作及本轮验证见[机构说明](ARM_CONTROL.md)。下方可下载文件和哈希已更新为r9，r8软件停车检查按原报告保留。

当前工作目录为 `C:\Users\fakel\Documents\ChatGPT\adam校内赛`，联调分支为 `test/mecanum-bringup-20261104`。从本文件开始操作，软件检查记录见[本次报告](reports/SOFTWARE_STOP_2026-11-04.md)，详细接线依据见[整车排线表](WIRING_PLAN.md)。

## 1. 今晚先核对的实物条件

用户确认四轮可以全部架空，PS2、电机和编码器尚未接好，由本次给出接线安排。电池按用户确认的商品选项记为 3S、标称 11.1V、3000mAh，准备经 XT60 一分四分电，主控经原有稳压板固定 5V 输出取电。商品图及购买选项提供规格线索，实际电压仍需测量。

| 必查项 | 当前记录 | 进入下一阶段的条件 |
|---|---|---|
| 驱动 VIN 电压 | 用户进一步确认 3S 满电 12.6V 输入可用，计划直连电池 | 兼容性来源为本次用户确认。实际接线前量取电池电压、极性，并核对实物驱动版本和接口 |
| 主控供电 | XT60 分电后经稳压板固定 5V-OUT，接主控 5V/GND 端子 | 万用表核对接口、极性和实际输出。XT60 分电板各路仍为电池电压，主控 5V 端子须接稳压后的 5V |
| 软件急停 | 用户明确选择PS2 SELECT或电脑stop，人工看护、四轮架空 | 先核对按键和命令入口、PWM0输出，再在首次短脉冲确认实际停轮。r8使用software_stop_verified记录检查，默认false |
| PS2 电气与失联 | 未接、未测 | 核对转接板供电、DAT 电平及四根信号，分别记录关手柄、超距、接收器断电的返回行为 |
| 电机、编码器与轮向 | 未接、未测 | 按下表贴线标，逐轮核对实际位置、方向和计数，再填写参数 |

两个固件均保持底盘禁动、标定和机械臂关闭，先做单板、PS2与驱动无电机负载检查，再按真实结果填写对应验收。用户已明确本轮使用软件急停，步骤按第5节执行。

## 2. 接线安排

凹口朝前。轮子从车体自身的前后左右命名。按用户已有麦轮照片和[几何记录](GEOMETRY_REFERENCE.md)保持 X 布局，两种轮子对角配对，装车后再拍照复核。

| 驱动口 | 今晚指定位置 | 代码中的应用位置 | 正反方向 |
|---|---|---|---|
| M1 | 左前 | FL，`channel[0]=0` | 待逐轮实测 |
| M2 | 左后 | RL，`channel[2]=1` | 待逐轮实测 |
| M3 | 右前 | FR，`channel[1]=2` | 待逐轮实测 |
| M4 | 右后 | RR，`channel[3]=3` | 待逐轮实测 |

代码的数组顺序为 FL、FR、RL、RR，因此 `channel={0,2,1,3}`。终端 `counts` 和 `output` 按 M1、M2、M3、M4 显示，记录时依照表格换算。`direction={0,0,0,0}` 保留待测，每一项最终填写 +1 或 -1。

电机与编码器整束线接四路驱动板。六针线序按[原交接](../RM_CODEX_HANDOFF.md)与实际接口逐功能核对，先标电机正负、编码器电源地、A/B，再插线。接口形状相同仍需核线。

| 主控端 | 外设端 | 图纸主控针号 |
|---|---|---|
| PA2 / USART2_TX | 驱动 RX2 | JP1-12 |
| PA3 / USART2_RX | 驱动 TX2 | JP1-11 |
| GND | 驱动 UART GND | 如 JP1-30/32 |
| PB12 | PS2 ATT/CS | JP1-24 |
| PB13 | PS2 CLK | JP1-23 |
| PB14 | PS2 DAT | JP1-26 |
| PB15 | PS2 CMD | JP1-25 |

驱动 UART 的 5V 脚留空。PS2 的 VDD、GND 和 DAT 高电平按[官方资料核对表](PS2_REFERENCE.md)及实物转接板确定。GPIO 要求 3.3V 兼容逻辑，电源针与信号针分清。表内针号为排针连接器针号，按板上 GPIO 丝印及 1 脚方向核对。

动力支路和主控稳压支路从 XT60 分电处分开，负极共参考地，大电流回路直接回分电点。下载阶段用单板 USB 供电。主控 USB VBUS 与外部 5V 共网，切换到普通 USB 下载前先断外部 5V，并隔离其他模块信号以防反向供电。后续边外部供电边看电脑日志，需要先确认供电隔离方案，详见[排线表第 7 节](WIRING_PLAN.md#7-usb下载时怎样切换)。

## 3. 已准备的固件

| 用途 | 固件 | `version` 中的编号 |
|---|---|---|
| 主控单板、默认禁动 | [build/rm_rct6.hex](build/rm_rct6.hex) | `rm_rct6-bench-20261104-r9-safe` |
| 核实电气后读取 PS2 | [build-ps2-readonly/rm_rct6.hex](build-ps2-readonly/rm_rct6.hex) | `rm_rct6-bench-20261104-r9-ps2` |

默认版 `RM_PS2_WIRED=0`，PS2 版通过编译参数设为 1。两版 `RM_ARM_WIRED=0`，所有底盘验收标志为 false，标定关闭。PS2 版会操作 PB12～PB15，使用前先完成接线电气核对。

| 文件 | SHA256 |
|---|---|
| 默认 HEX | `535300a76a945b3fc65ba48fa928e2fa3a4e042cce46e8d20182b8b6df7c00fe` |
| 默认 BIN | `23394b67f3e0c942f9a366b1806bc6e5177b2afa5ce465e5d40393553fa42d9a` |
| PS2 HEX | `faf91ead354f91489a24827af71b588fd77a857b05bdd52f0be2d03a642f9267` |
| PS2 BIN | `2431bb0b3aeaa8513236b7ee45e9a3c8213ef4148c9e2e6d3f137c446bc721ad` |

HEX 自带地址，BIN 基地址为 `0x08000000`。完整产物及源码哈希见[本次清单](reports/lift-shortcut-artifacts-20261104.json)。重新构建后重新核对哈希，下载时核对完整路径，避免选中旧目录里的同名 HEX。

## 4. 按顺序完成检查

### A. 断动力，只接主控

1. 断开外部 5V、外设信号和全部电机/舵机动力，用 USB-A 转 C 数据线接主控的 CH340“ISP 下载”口。
2. 在 PowerShell 进入 `C:\Users\fakel\Documents\ChatGPT\adam校内赛\RCT6`，运行下列两项。

```powershell
.\board.ps1 -Mode Verify
.\board.ps1 -Mode Check
```

`Check` 默认 UART，枚举端口并核对默认 BIN 哈希。报告记录实际 COM 号，COM5 仅为旧记录。SWD 环境检查可显式加 `-Interface SWD`。

3. 按[USB ISP 步骤](USB_ISP.md)用 FlyMCU 选择默认 HEX，115200，DTR 低复位/RTS 高进 Bootloader，勾写后校验及编程后执行，选项字节写入保持关闭。
4. 退出 FlyMCU，换成实际 COM 号运行以下命令。每次 Command 会保存独立日志并检查对应 `reply` 行。

```powershell
.\board.ps1 -Mode Command -Port COM5 -Command version
.\board.ps1 -Mode Command -Port COM5 -Command config
.\board.ps1 -Mode Command -Port COM5 -Command output
```

默认版预期 `version=rm_rct6-bench-20261104-r9-safe`、`config issues=0xBF speed_unit=1 cal_enabled=0 cal_max_pwm=100 cal_max_ms=200`，`output mode=pwm M1=0 M2=0 M3=0 M4=0`。单板无 PS2、无驱动时 `state=2 inhibit=0x07 motor_seen=0` 为预期。`bad=0`、`console_fault=0` 应保持。

需要抓重启日志时运行下列命令，在提示开始监听后按下并松开 RST。

```powershell
.\board.ps1 -Mode Monitor -Port COM5 -Seconds 15 -RequireBoot
```

### B. PS2 只读

电气接线核对完成后，仍断电机负载，下载 PS2 版，再使用 `ps2` 查询。前后由左 Y 控制，横移由左 X 控制，旋转由右 X 控制，START 解锁请求、SELECT 停车。L2 是机械臂模式键，底盘验收时保持松开。

| 操作 | 需要记录的结果 |
|---|---|
| 松开摇杆与按键 | `valid=1`，fwd/left/yaw 接近 0，start/select 为 0 |
| 左摇杆向前、向左 | 分别 fwd 为正、left 为正，千分比最大接近 1000 |
| 右摇杆向左 | yaw 为正 |
| START/SELECT 按下再松开 | 对应字段和 buttons 位图随操作变化 |
| 关手柄、超距、接收器断电，分别试 | 记录原始回复、超时与恢复行为。持续返回最后合法轴值时，保留无线失联验收未通过 |

当前版config issues=0xBF保持。先记录SELECT/START与轴值，软件停车入口按第5节检查，实测后再更新相应配置。

### C. 驱动串口与编码器

先实测 VIN 电压并完成主控/USB 供电隔离，断电核线，断开电机负载，再接驱动 UART 的 TX、RX、GND。两路 USART 都使用 115200/8N1，USART1 通电脑，USART2 通驱动。运行下面的查询。

```powershell
.\board.ps1 -Mode Command -Port COM5 -Command 'driver read'
# 等待驱动返回配置。后续info可以重复查看，重复read会清空旧记录。
.\board.ps1 -Mode Command -Port COM5 -Command 'driver info'
.\board.ps1 -Mode Command -Port COM5 -Command 'driver safety'
.\board.ps1 -Mode Command -Port COM5 -Command counts
```

记录驱动版本、mtype、deadzone、mline、mphase、diameter_mm。fields=0x3F表示六项收齐，本车期望80.000mm。driver safety显示spd_unit=mm_s、command_loss=hold_last、timeout_config=unsupported、software_stop_verified=0，收到匹配轮径后diameter_match=1。读回值若为67mm或其他值，先保存回复，再按厂家资料处理，当前程序只读。

`motor_seen=1` 且 `age_ms` 持续小于 200ms，表示合法 MAll 反馈新鲜。手转计数测试须在电机动力隔离、编码器供电方案核实后逐路进行。记录轮子正向、实际转数、该 M 通道前后计数及其他通道变化，计数/圈由实测计算。

### D. 具备保护和验收条件后进行转轮

按第5节完成软件急停检查，同时完成PS2接线、无线行为和新鲜反馈，再开放单轮标定。软件停车验收与标定开关分开填写，轮序/方向保持待测。每次改配置后重新构建，记录新版本号与哈希。

四轮架空且切断开关可达，逐口检查 M1～M4，每次重新回中、申请一次 `cal arm`，5 秒内发一次 `cal pulse M PWM MS`。M 为驱动口 1～4，PWM 幅度和脉冲时间结合电机及驱动死区核定，当前生产限制为幅度最多 100、时长最多 200ms。命令在 [CONSOLE.md](CONSOLE.md) 有完整说明，电脑 `Terminal` 适合连续手动输入，重要观察另存记录。

| 驱动口 | 实际轮位 | 正 PWM 对车体前进的方向 | 编码器计数变化 | 需填的 direction | 脉冲结束及停车表现 |
|---|---|---|---|---|---|
| M1 | 待测 | 待测 | 待测 | 待测 | 待测 |
| M2 | 待测 | 待测 | 待测 | 待测 | 待测 |
| M3 | 待测 | 待测 | 待测 | 待测 | 待测 |
| M4 | 待测 | 待测 | 待测 | 待测 | 待测 |

普通行驶还要求几何、映射、电机参数全部验收及 `diameter_match=1`。全部满足后，先架空核对四轮，再做受控故障停车，最后低速落地检查。每轮恢复后保持回中、松键至少 300ms，再新按 START。

| 目标动作 | FL 左前 | FR 右前 | RL 左后 | RR 右后 |
|---|---|---|---|---|
| 前进 | + | + | + | + |
| 后退 | − | − | − | − |
| 左移 | − | + | + | − |
| 右移 | + | − | − | + |
| 逆时针原地旋转 | − | + | − | + |
| 顺时针原地旋转 | + | − | + | − |

表中为滚子布局匹配时各轮对车体前进的机械正负，软件随后套用实测 `direction` 并映射到 M1～M4。四轮同比例限幅，控制有加速斜坡，逐次查询的幅度随时间变化。

## 5. SELECT / 电脑stop的软件急停检查

用户本轮明确使用这两条路径，今晚由人在旁看护、四轮架空。r8用software_stop_verified记录软件停车检查，默认false，替代r6/r7的独立停车门禁。默认issues的0x20现在对应软件停车待测。

1. 先断开电机负载，在PS2只读版中松开摇杆，分别按下/松开SELECT和START，用ps2确认对应字段。两键同时按时SELECT优先。
2. 保持USART2连接，运行下列命令，保存stop回复与随后output/status的日志。

```powershell
.\board.ps1 -Mode Command -Port COM5 -Command stop
.\board.ps1 -Mode Command -Port COM5 -Command output
.\board.ps1 -Mode Command -Port COM5 -Command status
```

3. stop应回复OK stopped; fresh_START_required，output为四路PWM0。SELECT也应使主控撤销许可并输出PWM0，松开后保持禁动，重新许可要回中、松START再新按。
4. 记录以上入口与零输出检查后，填写software_stop_verified。PS2接线、无线行为、新鲜MAll等其他条件按各自实测填写，单轮标定另设calibration_enabled。几何、轮序、电机方向继续等待逐轮实测。
5. 首次架空短脉冲由人在旁看护，分别试SELECT和电脑stop，记录实际停轮效果、许可是否撤销。当前限制幅度100、时间200ms，具体脉冲参数按电机/驱动资料核定。任一入口异常先隔离动力并保留日志，恢复后重新检查。

主控正常、命令TX畅通时，两个入口发PWM0。主控停机或TX断线会使该命令无法送达，而驱动保持末值。带动力测试期间保持串口线连接，断线与复位排查先隔离动力，人员可立即断开电机动力供给。本轮软件测试已覆盖行驶及标定各自的SELECT/电脑stop、同时按START/SELECT和停车后按住START，实物结果按今晚观察登记。

## 6. 每轮留存结果

记录日期、操作者、分支/HEAD 和未提交状态、固件编号与 BIN SHA256、主板/驱动版本、电源实测电压、接线照片、轮子架空状态及每项实际观察。PC 测试、固件校验、单板启动、转轮、失联停车分别填写结果。保存 `reports/capture-*.log` 等原始回复。

本轮已有软件准备证据，实际烧录、接线、转轮、无线行为与软件停车实测均待执行。K210、灯带和机械臂的其他待办仍按各自记录处理。
