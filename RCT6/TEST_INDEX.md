# 代码阅读与测试索引

这份索引回答“哪里负责什么、想改哪里去哪里、测试时看什么”。注释使用白话；已实现连接恢复、文字指令和受限单轮标定，主板调试留到设备就绪后。

## 1. 建议从这里开始读

| 阅读顺序 | 文件 | 负责什么 |
|---|---|---|
| 1 | [target/main.c](target/main.c) | 上电顺序、20ms控制循环、500ms状态日志、全局调试对象g_app |
| 2 | [src/board_config.c](src/board_config.c) | 所有默认参数、单位、哪些实测项会阻止解锁 |
| 3 | [src/remote_ps2.c](src/remote_ps2.c) | PS2原始字节怎么变成摇杆、START和SELECT |
| 4 | [src/safety.c](src/safety.c) | 是否允许运动；回中、超时、故障后重新解锁 |
| 5 | [src/chassis_app.c](src/chassis_app.c) | 把遥控、安全、运动学和厂商命令串成一次控制过程 |
| 6 | [src/mecanum.c](src/mecanum.c) | 车体速度转成四轮速度，死区、限速和加速斜坡 |
| 7 | [src/motor_uart.c](src/motor_uart.c) | 厂商报文解析、命令构造、应用轮序与M1～M4映射 |
| 8 | [target/board_hal.c](target/board_hal.c) | 真正操作时钟、UART和PS2引脚；中断存电机字节 |
| 9 | [src/calibration.c](src/calibration.c) | 单轮标定的准备、一次短脉冲和取消条件 |
| 10 | [src/console.c](src/console.c) | 解释CH340电脑指令，输出手柄/计数/配置等信息 |
| 11 | [tools/console_reply.ps1](tools/console_reply.ps1) | 电脑端检查reply回复，排除自动日志、错误回复和半截文字 |

`include/*.h`解释对外函数和结构体字段。`target/interrupts.c`是核心异常/SysTick入口；`target/syscalls.c`为C库提供占位接口，通常测试不用改。

**正常控制路径：** `main`读PS2 → 更新`g_app.remote` → `rm_app_step`先做安全检查 → 允许时处理速度/四轮映射 → 生成`frame` → USART2发送。禁止运动时直接生成PWM0，不继续计算非零运动目标。

**标定故障检查：** `main`中的`cancel_calibration_on_console_fault()`撤销电脑通道故障后的标定许可。读手柄后先检查；`send_motor()`发送前再检查，必要时把已经算好的文字换成PWM0。这样既能拦住未开始的脉冲，也能取消正在运行的脉冲。

## 2. 想调整功能，改哪里

| 想改的内容 | 位置 | 当前含义 |
|---|---|---|
| 摇杆对应关系/正负 | `remote_ps2.c`的`rm_ps2_decode()` | 左Y前后、左X横移、右X旋转；向前/左/逆时针为正 |
| 解锁/停车按键 | 同函数的`out->arm/out->stop` | START为低字节bit3；SELECT为bit0；按下的原始位为0 |
| PS2引脚 | `board_hal.c`的`PS2_GPIO_PORT`和`PS2_*_PIN` | PB12 ATT、PB13 CLK、PB14 DAT、PB15 CMD；初始化与回调共用一份定义 |
| 允许初始化PS2接线 | `target/board_hal.h`的`RM_PS2_WIRED` | 默认0；核实电气接线后才改1并重编译，不等于解锁电机 |
| 轮半径/半轴距/半轮距 | `board_config.c`的`radius_m/half_length_m/half_width_m` | 当前为0.030/0.070888089544/0.097050777636m；60mm轮径来自用户，轮心距来自CAD，实物装好后复核 |
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

尺寸的换算和来源见[几何说明](GEOMETRY_REFERENCE.md)：轮径、前后轮心距、左右轮心距各除2000，得到以m为单位的半径、半轴距、半轮距。车头是凹口端，车体前进=CAD -Z、向左=CAD -X；应用轮序FL/FR/RL/RR对应原CAD记录D/C/B/A。

`verified`标志只在相应验收通过后填写。本次填入尺寸后，运动和标定仍禁用，`geometry_verified=false`。单独验证PS2读包不需要把它们改true，也不需要改成ARMED。

## 3. 测试前先区分两种运行方式

在本文件所在目录打开PowerShell：

```powershell
.\build.ps1 -Test
.\build.ps1
```

第一行在PC上运行原六组逻辑测试、29个模拟主循环场景、六组标定/指令测试和四组PS2协议专项测试。真实主循环接到模拟设备，不连接实物；测试局部副本中的模拟尺寸和确认项不会写回固件配置。第二行编译STM32固件，产物位于`build/rm_rct6.elf/.hex/.bin`，本脚本不自动下载。

本次在`test_core.c`的`test_kinematics()`中补了当前60mm默认参数检查：0.1m/s前进为四轮约3.333333rad/s，左移和旋转的正负顺序正确；未知速度单位仍拒绝输出，尺寸已填仍不能解锁。核心共129233条断言通过，详见[60mm配置验证](reports/GEOMETRY_60MM_CONFIG.md)。

运行`.\board.ps1 -Mode Test`另外检查17项日志规则和33项电脑指令回复规则。既有用例检查标定回复失败、读手柄时出错、发送前出错、自动日志误当回复、半截回复及最长驱动原文。这两组电脑工具检查本次未重跑；PS2低时钟采样、配置命令长度、idle字节、坏包清旧值等检查的原始依据见[PS2重构报告](reports/PS2_REFACTOR_2026-10-23.md)。

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
| `g_app.remote.arm` | 按START为true，松开false；只是按键状态，不表示已经解锁 |
| `g_app.remote.stop` | 按SELECT为true，松开false |
| `g_app.remote.at_ms` | 是本次读包时间；即使包无效也可能更新，不能单看它证明链路在线 |

看原始字节可在`rm_ps2_decode()`入口观察`frame[0..8]`：`[0]`为idle，`[1..2]`为73/5A，`[3..4]`为按键，`[5]`右X、`[6]`右Y、`[7]`左X、`[8]`左Y。右Y目前不用。摇杆按压键的左右名称在两份官方附件中有冲突，当前没有给它们分配动作。

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
| `g_app.parser.malformed` / 日志`bad` | 坏帧数，正常联调不应持续增长 |
| `g_app.parser.unknown` / 日志`unknown` | 未解释的合法长度帧数，例如某些配置回复；不是运动ACK |
| `g_app.ramp` | 已解锁时参与四轮计算的车体速度；禁动时为0 |
| `rm_app_step()`局部`wheel/channel/frame` | 分别是应用轮序速度、厂商通道顺序速度、最终ASCII命令 |

默认运动和标定均关闭，只输出PWM0及只读查询/上传开关。单轮标定入口已经实现，具体保护条件和指令见[CONSOLE.md](CONSOLE.md)。收到驱动反馈只能证明驱动TX→主控RX这个方向，不代表主控TX未断，也不能替代驱动独立失联停车验证。

## 7. 不需要从头改的文件

`../Drivers/`下的HAL/CMSIS和ST启动汇编通常不参与日常调参。先读本索引的应用文件即可。`reports/`是已经执行过的构建/测试证据，不是可调参数文件。

新60mm版本未烧录；旧r1的单板启动和version/config通信记录见[USB实测报告](reports/USB_ISP_2026-10-25.md)。PS2与运动尚未验收，详细步骤继续看[BRINGUP.md](BRINGUP.md)。
