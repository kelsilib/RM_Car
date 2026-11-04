# 代码阅读与测试索引

2026-11-04的r9增加△直达S1 K210升降台，S2～S7六路供机械臂使用。机械臂回归现为八组42667条断言，按键与本轮结果见[升降台说明](ARM_CONTROL.md#8-2026-11-04升降台独立键验证)，下方r8数字按该轮记录保留。

这份索引回答“哪里负责什么、想改哪里去哪里、测试时看什么”。注释使用白话；已实现连接恢复、文字指令、受限单轮标定和七路MG995手动控制，实物调试按各自待核实项进行。

2026-11-04的实际接线和测试顺序见[今晚麦轮联调](TONIGHT_BRINGUP.md)。`board.ps1 -Mode Test`已增加`tests/test_preflight_tools.py`的7项回归，检查UART模式无需OpenOCD、两种哈希格式、错误哈希/版本拒绝及历史报告防覆盖。

## 1. 建议从这里开始读

| 阅读顺序 | 文件 | 负责什么 |
|---|---|---|
| 1 | [target/main.c](target/main.c) | 上电顺序、20ms控制循环、500ms状态日志、全局调试对象g_app |
| 2 | [src/board_config.c](src/board_config.c) | 所有默认参数、单位、哪些实测项会阻止解锁 |
| 3 | [src/remote_ps2.c](src/remote_ps2.c) | PS2原始字节怎么变成摇杆、START和SELECT |
| 4 | [src/safety.c](src/safety.c) | 是否允许运动；回中、超时、故障后重新解锁 |
| 5 | [src/chassis_app.c](src/chassis_app.c) | 把遥控、安全、运动学和厂商命令串成一次控制过程 |
| 6 | [src/mecanum.c](src/mecanum.c) | 车体速度转成四轮速度，死区、限速和加速斜坡 |
| 7 | [src/motor_uart.c](src/motor_uart.c) | 厂商报文与普通配置行解析、命令构造、应用轮序与M1～M4映射 |
| 8 | [target/board_hal.c](target/board_hal.c) | 真正操作时钟、UART和PS2引脚；中断存电机字节 |
| 9 | [src/calibration.c](src/calibration.c) | 单轮标定的准备、一次短脉冲和取消条件 |
| 10 | [src/console.c](src/console.c) | 解释CH340电脑指令，输出手柄/计数/缺项；driver read/info查看配置，driver safety看已知行为和轮径匹配 |
| 11 | [tools/console_reply.ps1](tools/console_reply.ps1) | 电脑端检查reply回复，排除自动日志、错误回复和半截文字 |
| 12 | [src/arm_config.c](src/arm_config.c)、[src/arm_control.c](src/arm_control.c) | 七路参数、选路、逐路许可、限位限速、故障冻结；默认全部关闭 |
| 13 | [target/board_servo.c](target/board_servo.c) | TIM3/TIM4硬件PWM；先校验整组目标，再写比较预装载；不默认回中 |

`include/*.h`解释对外函数和结构体字段。`target/interrupts.c`是核心异常/SysTick入口；`target/syscalls.c`为C库提供占位接口，通常测试不用改。

**正常控制路径：** `main`读PS2 → 更新`g_app.remote` → `rm_app_step`先做安全检查 → 允许时处理速度/四轮映射 → 生成`frame` → USART2发送。禁止运动时直接生成PWM0，不继续计算非零运动目标。

**机械臂控制路径：** 持续L2进入模式，`rm_app_step`先取消底盘/单轮许可并生成PWM0，再更新选路和待写目标。`main`先发送电机命令、复查通信与调度，再调用`board_servo_apply()`；写入成功才`rm_arm_commit()`。`rm_arm_stop()`撤销尚未写入的新目标并保持最后已接受的脉宽，避免故障后补发动作。

**标定故障检查：** `main`中的`cancel_calibration_on_console_fault()`撤销电脑通道故障后的标定许可。读手柄后先检查；`send_motor()`发送前再检查，必要时把已经算好的文字换成PWM0。这样既能拦住未开始的脉冲，也能取消正在运行的脉冲。

## 2. 想调整功能，改哪里

| 想改的内容 | 位置 | 当前含义 |
|---|---|---|
| 摇杆对应关系/正负 | `remote_ps2.c`的`rm_ps2_decode()` | 左Y前后、左X横移、右X旋转；向前/左/逆时针为正 |
| 解锁/停车按键 | 同函数的`out->arm/out->stop` | START为低字节bit3；SELECT为bit0；按下的原始位为0 |
| PS2引脚 | `board_hal.c`的`PS2_GPIO_PORT`和`PS2_*_PIN` | PB12 ATT、PB13 CLK、PB14 DAT、PB15 CMD；初始化与回调共用一份定义 |
| 允许初始化PS2接线 | `target/board_hal.h`的`RM_PS2_WIRED` | 默认0；核实电气接线后才改1并重编译，不等于解锁电机 |
| 轮半径/半轴距/半轮距 | `board_config.c`的`radius_m/half_length_m/half_width_m` | 当前为0.040/0.070888089544/0.097050777636m；80mm轮径来自用户，轮心距来自CAD，实物装好后复核 |
| 轮序 | 同文件`channel[]` | 应用顺序FL/FR/RL/RR，对应通道号0/2/1/3，即M1/M3/M2/M4 |
| 单轮正反方向 | 同文件`direction[]` | 每轮标定后填+1/-1；当前0=尚未确认 |
| 平移/旋转速度上限 | 同文件`max_v_m_s/max_w_rad_s` | m/s和rad/s，不能填rpm或度/秒 |
| 加速缓急 | 同文件`accel_m_s2/accel_rad_s2` | 各自的加速度；故障禁动不等待斜坡结束 |
| 单轮速度上限 | 同文件`wheel_limit_mm_s` | 当前按轮缘mm/s的转换层设计，单位确认前禁止解锁 |
| 摇杆中点漂移 | 同文件`deadzone` | 0.08=归一化单侧量程的8%以内当回中 |
| 两条链路超时 | 同文件`remote_timeout_ms/motor_timeout_ms` | 默认各200ms，只改变检测阈值，不代表实物已经通过失联测试 |
| 回中等待 | 同文件`neutral_hold_ms` | 默认300ms；时间到了还要新按START，保持按住无效 |
| 控制/日志周期 | `main.c`的20/500ms判断 | 更改周期须重新评估100ms调度迟滞判定及通信新鲜度 |
| 连接重试间隔 | `main.c`的`RETRY_MS`和`retry_links()` | 默认至少500ms；只在禁动且没有已记住的通信故障时重试 |
| 单轮标定开关/限制 | `board_config.c`的`calibration_*` | 默认关闭；原始PWM参数最多100、时长最多200ms，不代表已测功率 |
| 七路机械臂参数 | `src/arm_config.c` | 周期、每路限位/初始脉宽/速率/方向与验收；全零表示未填写 |
| 允许配置舵机GPIO | `target/board_servo.h`的`RM_ARM_WIRED` | 默认0；改1仍需配置、PS2验收和逐路许可才能输出 |
| 机械臂按键 | `src/arm_control.c` | 持续L2、方向左右选路、新START许可、R2加右Y调整，见[按键表](ARM_CONTROL.md) |

尺寸的换算和来源见[几何说明](GEOMETRY_REFERENCE.md)：轮径、前后轮心距、左右轮心距各除2000，得到以m为单位的半径、半轴距、半轮距。车头是凹口端，车体前进=CAD -Z、向左=CAD -X；应用轮序FL/FR/RL/RR对应原CAD记录D/C/B/A。

`verified`标志只在相应验收通过后填写。本次填入尺寸后，运动和标定仍禁用，`geometry_verified=false`。单独验证PS2读包不需要把它们改true，也不需要改成ARMED。

## 3. 测试前先区分两种运行方式

在本文件所在目录打开PowerShell：

```powershell
.\build.ps1 -Test
.\build.ps1
```

第一行运行核心、32个底盘主循环、10组服务及PS2、机械臂测试。测试中的完整配置与软件停车验收由PC夹具提供，第二行构建真实ARM固件。

test_core.c保留80mm运动学与MM_S换算，检查软件停车缺失、轮径缺失/非法/不匹配及恢复后重新解锁，共129334条断言。当前r8证据见[本次报告](reports/SOFTWARE_STOP_2026-11-04.md)，r6及其他报告保留历史。

运行`.\board.ps1 -Mode Test`另查17项日志规则和43项回复规则，本次通过，含新增driver safety对应/错配回复。r8服务测试十组745条断言通过；原标定、配置行边界和电脑故障规则保留。PS2专项6580项、机械臂42651条断言和11个主循环场景也通过，仍不能代替实物验收。

`test_arm.c`共42651条断言，检查配置门槛、显式许可、单路启用、限位限速、小增量累积、选路、反向、回绕、坏输入、退出模式后重新解锁。`test_arm_main.c`的11场景检查默认关闭、正常单路输出、电机/电脑/舵机写入故障、旧遥控与SELECT优先；模拟设备成功不代表实物波形或承重已验收。

担心旧文件没重编译时，加 `-Rebuild`，例如 `.\build.ps1 -Test -Rebuild`。这会重新编译所需文件，已有build目录也能运行。

不要把PC生成的`test_core.exe`当固件；不要使用上级旧F103C8工程的构建入口。下载和实际供电按[BRINGUP.md](BRINGUP.md)执行。

## 4. PS2读包时怎么看

先看[PS2资料依据和对应关系](PS2_REFERENCE.md)，其中列明了每根线、每个字节、START/SELECT及摇杆的含义。

先保持电机动力断开。默认`RM_PS2_WIRED=0`时，`g_app.remote.valid=false`是预期，并不说明接收器坏了。

核实接收器供电、电平和针序后，可启用PS2接线开关并重编译下载，**继续保持运动配置未解锁**。串口发送`ps2`就能查看摇杆千分比和按键；也可以在支持该ELF的调试会话中，把下面表达式加入Watch/Expressions。

| 观察变量 | 操作与预期 |
|---|---|
| `g_app.remote.valid` | 9字节包的`[1]=73`且`[2]=5A`时为true；`[0]`是idle，不强制FF；包不合法时false |
| `g_app.remote.forward` | 左摇杆回中接近0；向上趋近+1，向下趋近-1 |
| `g_app.remote.left` | 左摇杆向左趋近+1，向右趋近-1 |
| `g_app.remote.yaw` | 右摇杆向左趋近+1，向右趋近-1 |
| `g_app.remote.right_y` | 右摇杆向上趋近+1，向下趋近-1；机械臂需许可且按R2才能调整 |
| `g_app.remote.buttons` | 按下为1的统一位图，L2=0x0100、R2=0x0200、方向左/右=0x0080/0x0020 |
| `g_app.remote.arm` | 按START为true，松开false；只是按键状态，不表示已经解锁 |
| `g_app.remote.stop` | 按SELECT为true，松开false |
| `g_app.remote.at_ms` | 是本次读包时间；即使包无效也可能更新，不能单看它证明链路在线 |

看原始字节可在`rm_ps2_decode()`入口观察`frame[0..8]`：`[0]`为idle，`[1..2]`为73/5A，`[3..4]`为按键，`[5]`右X、`[6]`右Y、`[7]`左X、`[8]`左Y。右Y用于机械臂，底盘不使用它。摇杆按压键的左右名称在两份官方附件中有冲突，当前没有给它们分配动作。

不要在PS2逐位时序中长时间单步，暂停会破坏通信时序；读完一整帧后观察更合适。断点也会暂停20ms控制命令，故进行这类调试前保持电机动力断开。

关手柄、超距、接收器断电必须分别记录。接收器如果持续返回上一帧合法值，`valid`可能仍为true；这不能通过无线失联验收。

## 5. 为什么不能解锁：先看这两个变量

`g_app.safety.state`：

| 数值 | 含义 |
|---|---|
| 0 / RM_DISARMED | 禁动 |
| 1 / RM_ARMED | 允许生成运动命令 |
| 2 / RM_FAULT | 故障，先排原因，再按完整重新解锁流程 |
| 3 / RM_ESTOP | 软件急停状态；当前目标没有接物理急停状态GPIO |
| 4 / RM_CALIBRATING | 受限单轮标定；与普通行驶分开，cal字段进一步区分准备/脉冲 |

`g_app.safety.inhibit`建议按十六进制显示，它是**当前**原因位图，可叠加：

| 位值 | 原因 | 排查位置 |
|---|---|---|
| 0x01 | 配置/验收未就绪 | `board_config.c`的`rm_config_motion_ready()`及CONFIG_PENDING.md |
| 0x02 | 遥控包无效、轴越界或超时 | `remote.valid`、轴值、`at_ms`，以及PS2接线开关 |
| 0x04 | 没见合法MAll累计帧或反馈超时 | 接收线、波特率、`motor_seen/motor_at_ms` |
| 0x08 | 传输故障 | `transport_fault`、`parser.malformed`、UART错误/缓冲/发送 |
| 0x10 | 软件急停请求 | 应用传入的estop；当前main传false，不代表已有物理急停检测 |
| 0x20 | 控制检查间隔超过100ms | 主循环是否被阻塞、是否打了断点 |
| 0x40 | 按L2进入机械臂模式，底盘禁动 | `g_app.arm.mode`与PS2的L2状态 |

例如0x07=0x01+0x02+0x04，没有接手柄和驱动且配置未填时是预期。原因恢复为0后，FAULT/ESTOP也不会自己回到ARMED，还需：健康链路中曾释放START、摇杆回中保持300ms、再新按START。太早按下后一直保持也不会解锁。

`transport_fault`当前会一直保留；修复线路后复位主控重新初始化，不能靠一份好包自动清除。

新增的自动重试只解决“设备还没准备好”或“设备重启忘了上传/模拟模式设置”。重试前已先发PWM0；连接回来后，仍需回中、松开START、重新按START。若断线时收到半包/乱码而记住了通信故障，则继续按上面的规则排查并复位，不会自动清除。

## 6. 电机通信怎么观察

| 观察量 | 说明 |
|---|---|
| `g_app.motor_seen` | 是否曾收到一份合法MAll帧，不代表此刻仍在线；要结合时间差 |
| `g_app.motor_at_ms` / 日志`age_ms` | 最后一份合法MAll收到的时间 / 距今时间 |
| `g_app.last_packet.kind` | 1累计计数、2增量、3速度、4电压，先看类型再看字段 |
| `g_app.last_packet.counts[0..3]` | kind为1/2时对应原始M1/M2/M3/M4，未做应用轮序和方向映射 |
| `g_app.last_packet.speeds[0..3]` | kind为3时是MSPD原始浮点反馈，单位mm/s；不能据此自动确认SPD控制单位 |
| `g_app.driver_info.fields` | 普通文本配置收到哪些项；0x3F为六项收齐，不表示参数正确 |
| `g_app.driver_info` | 版本、类型、死区、磁环线数、减速比、轮径及最后字段时间；driver info可直接查看 |
| `g_app.parser.malformed` / 日志`bad` | 坏帧数，正常联调不应持续增长 |
| `g_app.parser.unknown` / 日志`unknown` | 未解释的合法长度帧数，例如某些配置回复；不是运动ACK |
| `g_app.ramp` | 已解锁时参与四轮计算的车体速度；禁动时为0 |
| `rm_app_step()`局部`wheel/channel/frame` | 分别是应用轮序速度、厂商通道顺序速度、最终ASCII命令 |

默认运动和标定均关闭。r8使用SELECT/电脑stop软件停车验收，主控与TX正常时发PWM0，详细操作见[CONSOLE.md](CONSOLE.md)。带动力时保持通信线接好，断线/复位排查先隔离动力。

driver read后等回复，再查driver info及driver safety中的diameter_match和software_stop_verified。轮径匹配后仍需其他配置及回中、新START，普通配置文本与MSPD不刷新motor_at_ms，MAll保持唯一健康反馈。

## 7. 机械臂怎么观察

串口发送`arm`、`servo 1`～`servo 7`只读查询，不会启用输出；`arm`和单轮标定的`cal arm`含义不同。

| 观察量 | 说明 |
|---|---|
| `g_app.arm.config` | 全局及七路参数；生产默认全零，不能用测试夹具替代 |
| `g_app.arm.mode` / `armed` / `selected` | 是否处于L2模式、本路是否许可、所选下标0～6；终端显示1～7 |
| `g_app.arm.pulse_us` / `milli_us` | 待写目标及小于1us的累计余量，尚不代表定时器已接收 |
| `g_app.arm.applied_us` / `applied_mask` | 最后已接受的硬件写入；不是角度反馈或承重验证 |
| `g_app.servo_hardware_ready` | PWM初始化是否成功，默认false为预期 |
| `g_app.arm.inhibit` / `io_fault` | 当前阻止原因与已锁存的IO故障，位表见[控制说明](ARM_CONTROL.md) |

完整引脚、按键、配置及故障保持边界见[ARM_CONTROL.md](ARM_CONTROL.md)。

## 8. 不需要从头改的文件

`../Drivers/`下的HAL/CMSIS和ST启动汇编通常不参与日常调参。先读本索引的应用文件即可。`reports/`是已经执行过的构建/测试证据，不是可调参数文件。

当前r8保持80mm及机械臂既有功能，尚未烧录。旧r1单板记录见[历史报告](reports/USB_ISP_2026-10-25.md)，当前软件停车和架空测试见[今晚入口](TONIGHT_BRINGUP.md)。
