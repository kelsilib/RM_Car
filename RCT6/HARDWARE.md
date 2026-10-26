# RCT6 第一阶段硬件接口

资料依据：`../references/双TypeCF103RCT6原理图.pdf`、`../references/vendor_UART_Test.ioc` 与配套 HAL 示例。原理图已渲染并逐项核对；以下是图纸依据，不代表实物接线已验证。

## 主控资源

| 功能 | 引脚 / 设置 | 依据与状态 |
|---|---|---|
| MCU | STM32F103RCT6，256KB Flash / 48KB RAM | 图纸和官方示例一致 |
| 系统时钟 | HSE 8MHz ×9 =72MHz；APB1/2=36MHz | 图纸晶振与配套示例；待上板验证 |
| 电机 UART | USART2，PA2 TX / PA3 RX，115200 8N1，无流控 | 图纸引出，官方示例支持该 UART |
| 调试串口 | USART1，PA9 TX / PA10 RX，115200 8N1 | 已连接板载CH340；日志输出和文字指令接收，见CONSOLE.md |
| 可选SWD调试 | PA13 SWDIO / PA14 SWCLK；JP5 另有3V3/GND | 保留接口，用于以后断点/单步调试 |
| USB J1 | CH340 USB串口，带自动BOOT/RESET电路 | 本次首选A转C下载和串口日志入口，见[USB_ISP.md](USB_ISP.md)；物理C口位置需看实物 |
| USB J2 | MCU USB，PA11/PA12 | 本固件没有USB设备协议；不能当成USART调试口 |
| PS2 候选 | PB12 ATT、PB13 CLK、PB14 DAT、PB15 CMD | 分别引出到JP1的22/21/24/23脚，图纸未见板载占用；**尚未接线确认，固件默认不初始化** |
| 软件急停输入 | 未分配GPIO | `estop`是应用接口，当前目标不声称有物理急停状态检测 |

板载 SPI Flash 占 PC0～PC3，按键占 PA0/PC8/PC9，LED 占 PA8。新版不沿用旧工程的 PC13 LED、TB6612 STBY 或主控编码器接线。

## 主控与驱动板

| 主控端 | 驱动板端 | 注意 |
|---|---|---|
| PA2 / USART2_TX | RX2 | 按实物丝印识别，不按插头左右顺序猜 |
| PA3 / USART2_RX | TX2 | 交叉连接 |
| GND | GND | 共参考地 |
| 不连接 | UART口5V | 初次不把两路5V输出并联 |

驱动 UART 信号电平需按实物核对为3.3V兼容。驱动板供电走动力支路；四个电机及AB编码器都接智能驱动板，**不接主控编码器GPIO**。电机PH2.0针序与驱动板不同，逐功能做转接，详见上级交接文件第4节。

应用轮序 `FL,FR,RL,RR`，候选通道为 `M1,M3,M2,M4`。生产方向标志全为0，待架空逐轮核验后填±1；不默认四轮电气正向等于整车前进。

## 下载和供电边界

本次按用户要求使用A转C数据线，经CH340执行串口ISP。首次只给裸主控USB供电，外部5V、电机/舵机动力及其他模块断开；实际下载步骤见[USB_ISP.md](USB_ISP.md)。不要把板载PA8指示灯当作用户要求的三条红绿蓝灯带，灯带接口仍待型号及驱动资料确认。

日后若使用ST-Link调试：接SWDIO、SWCLK、GND及适当的目标参考电压，可选NRST。先辨认具体ST-Link的“3.3V”是供电输出还是电平参考，不将两个供电输出并联。

图纸J1/J2的VBUS以及排针5V连到同一5V网，没有看到USB与外部5V之间的隔离器件。USB与外部5V不要未经处理同时供电；具体供电方法按板卡实物和下载器落实。

2026-10-23已核对用户提供的[官方PS2资料](https://ycnp6l8ow4h6.feishu.cn/wiki/B0fUwn768ivrPCk4SQJcz1ILnnf)：接收器VDD标注3～5V，转接板六针标为GND/VCC/DAT/CMD/CS/CLK。完整九针、六针和RCT6对应关系见[PS2_REFERENCE.md](PS2_REFERENCE.md)。官方STM32示例的PA4～PA7属于OpenCTR板，不替换本板候选GPIO。

实物转接板版本、稳压电路、DAT逻辑电平、供电余量和实际接线尚未确认。启用 `RM_PS2_WIRED` 前仍需核对；接收器供电范围不等于DAT允许电平，也不能仅因某个MCU引脚的耐压特性就认定整套连接安全。
