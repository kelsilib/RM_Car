# 第一阶段：编译、下载与无动力联调

当前已完成真实主机测试与RCT6固件构建，**尚未烧录，所有实物结果待验证**。

## 1. 重现软件验证

PowerShell进入本文件目录，运行：

```powershell
.\build.ps1 -Test
.\build.ps1
```

需要把所有文件重新编译时用 `.\build.ps1 -Rebuild`；PC测试可用 `.\build.ps1 -Test -Rebuild`。测试包含原六组逻辑、27个模拟主循环场景、六组标定/指令检查，都不连接实物。`.\board.ps1 -Mode Test`还会检查日志和电脑回复的判断规则。主板就绪前只做这些软件验证即可。

`-Test`使用本机w64devkit GCC；固件使用 `D:\stm32\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins` 中的GCC与Make，脚本中可修改路径。

固件在 `build/rm_rct6.elf`、`.hex`、`.bin`。HEX自带地址；BIN写入地址为 `0x08000000`。不要使用上级旧F103C8工程的构建入口或产物。

## 2. 主控单板下载

1. 电机和舵机动力断开；驱动电机负载先不连接。
2. 主控BOOT0保持运行Flash所需低电平。ST-Link接PA13/SWDIO、PA14/SWCLK、GND及合适的目标参考电压，按HARDWARE.md落实单一路径供电。
3. 在STM32CubeProgrammer中选择ST-LINK、SWD连接，先读取并核对目标型号/Flash大小。确认是F103RCT6后，再选择本目录HEX下载，勾选写后校验，然后复位运行。
4. 本次没有自动执行连接、擦除或下载；不要把构建通过当成已下载成功。

第一版提供真实Make构建工程，可用CubeIDE查看源码和ELF。这里尚未生成本车的CubeMX `.ioc` 或受管CubeIDE `.cproject`。如用CubeIDE建立工程，请按下方配置清单生成，再纳入本目录应用模块，避免重复main与中断定义。

## 3. 主控启动验证

使用连接CH340的那个C口，串口终端115200/8N1/无流控；这是图纸J1，位置以实物为准。避免串口软件自动切换DTR/RTS引发板上自动下载电路复位/BOOT变化。

复位后预期可见：

```text
RM RCT6 stage 1: default motion INHIBITED
state=2 inhibit=0x07 motor_seen=0 age_ms=... bad=0 unknown=0
```

`state`：0禁动、1已解锁、2故障、3软件急停；`inhibit`：0x01配置未核实、0x02遥控无效/超时、0x04电机反馈无效/超时、0x08传输故障、0x10急停、0x20调度迟滞，可按位叠加。无遥控、无驱动时0x07是预期。

若无日志，先检查CH340口、供电、BOOT0和复位；再用SWD检查PC和 `g_app`，不接电机尝试“看是否转动”。

## 4. 无电机负载的UART联调

断电后接PA2→驱动RX2、PA3←驱动TX2、GND↔GND；UART口5V先不接。确认驱动板信号电平、供电极性和版本；电机仍不接。

上电时固件发送PWM0、查询配置、开累计反馈，并持续发PWM0。缺少反馈时，在禁动状态至少间隔500ms重发上传开关；不会自动设置电机参数或让电机运行。

预期驱动返回合法累计帧后 `motor_seen=1`，`age_ms`保持较小；`inhibit`通常剩0x03。`unknown`可因配置回复增加；`bad`应保持0。未知回复不当作已正确配置的证明。可在SWD观察 `g_app.last_packet`，手动转接入的单个编码器（确认电源和针序后）观察原始通道值。

断开驱动TX→主控RX，200ms后电机反馈超时标志应出现。主控仍能运行时会尝试PWM0。**这个试验不能证明主控TX断开后驱动会停机。** 当前无动力联调只能检查报文，不填写运动验收通过。

再测两种情况：主控先开、驱动后开；只重启驱动。没有坏帧/UART错误时，驱动就绪后应收到重发的上传开关，`motor_seen=1`且`age_ms`重新变小。恢复通信后仍不会自动解锁。如果`bad`增加或`inhibit`带0x08，属于已经记住的通信故障，排查后复位主控；不能期待重试把它清掉。

## 5. 后续运动阶段的前置条件

先完成CONFIG_PENDING.md中的轮几何、通道/方向、速度单位、电机参数、PS2电气与无线断联、驱动独立失联停车记录。

单轮受限PWM标定代码已提供，默认关闭。使用前单独启用`calibration_enabled`，并完成PS2接线、无线失联和驱动独立停车保护的确认；通过`cal arm`和一次`cal pulse`操作，详见CONSOLE.md。它不依赖未知的尺寸/方向/速度单位，但也不能通过把全部verified随便改true来使用。

PS2适配器默认不驱动GPIO。核线后可单独启用 `target/board_hal.h` 的 `RM_PS2_WIRED` 进行遥控读包；仍保持运动配置未解锁，先检查关手柄和断接收器表现。只有已验证的失联标识才能填写相关标志。

PS2还应测试晚供电和重新供电。接收器若回到数字模式，程序会在禁动时隔至少500ms重试模拟模式；成功读到正确包后`remote.valid`才会变true。读包恢复不代表无线失联停车已验收。

已实现的电脑入口：`board.ps1 -Mode Terminal -Port COM7`交互发指令，`-Mode Monitor -Port COM7`抓日志，`-Mode Analyze -Log ...`解释默认禁动阶段日志。端口改成实物对应的CH340；工具和串口实际收发仍留到有板后验证。

真实运动验收：架空单轮→四轮→断两条UART线和复位主控→新按键解锁→低速落地。全过程记录实际结果，硬件急停可达。

## CubeMX配置清单（用于后续生成，当前未伪造工程）

- STM32F103RCT6；SYS Debug=Serial Wire。
- HSE=Crystal/Ceramic Resonator 8MHz；PLL×9，SYSCLK/HCLK72MHz，APB1/2均36MHz。
- USART2异步PA2/PA3，115200/8N1/无流控；启用USART2 IRQ。
- USART1异步PA9/PA10，115200/8N1，调试输出；不要并入电机串口。
- 不启用主控编码器、TB6612 PWM或舵机/视觉外设。
- PS2 PB12/13/14/15在电气确认前保持未启用。
- Cube生成UART中断与本工程自定义收包ISR二选一，不重复定义；迁入HAL收包回调时保留长度、时间戳及错误策略。
