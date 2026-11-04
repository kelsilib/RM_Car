# 四路驱动协议与烧录前核对

后续更新：用户随后提供了本地驱动资料包，补充明确MSPD反馈单位和普通配置行格式，代码已修正为r4。最新结果见[驱动资料修正报告](DRIVER_ALIGNMENT_2026-10-26.md)和[资料依据](../DRIVER_REFERENCE.md)；下文保留原r3只读检查及旧哈希，不作为r4构建记录。

核对日期：2026-10-26。对象：本地`rm_rct6-chassis-80mm-r3`。本次只读核对资料、代码和已有产物，没有连接设备、烧录或实测电机，也没有重新构建。

**结论：适合在断开电机/舵机动力、只给RCT6单板USB供电的条件下烧录，验证启动和文字查询。已查的UART命令条目与官方公开协议一致；整车运动和驱动保护仍未验收，不能声明所有实物条件均符合。**

## 官方依据

- [官方仓库](https://github.com/YahboomTechnology/4-Channel-Motor-Drive-Module/tree/de79d8327642e83f8f2ea34f9398e4ee7bce084f)：本次实时查询main仍为`de79d8327642e83f8f2ea34f9398e4ee7bce084f`。
- [官方Control command PDF](https://github.com/YahboomTechnology/4-Channel-Motor-Drive-Module/blob/de79d8327642e83f8f2ea34f9398e4ee7bce084f/1.Introduction/1.2%20Control%20command.pdf)：9页，70601字节。逐页提取，并渲染查看第1、3、4、5、6页的UART、轮径、速度/PWM和反馈表。Git blob为`5133586f8a7d9b9cab0942924f2496379aecfe75`，与当前官方目录返回值一致；本地PDF的SHA256为`c098c11177cdd2d7d8dd1055a5354e4875fbf7337abd92bbe609a3e25cc6ad29`。
- [官方STM32 USART教程](https://www.yahboom.net/study/Quad-MD-Module/14007/)：明确适用于C8T6/RCT6，给出PA2/PA3、RX2/TX2和四轮通道；串口与IIC二选一。
- [官方电机与接线说明](https://www.yahboom.net/study/Quad-MD-Module/13991/)：AB同名连接和交叉连接会影响mtype选择，不能只凭“520”字样填参数。

## 对照结果

| 项目 | 当前代码及核对结果 |
|---|---|
| 串口与接线 | `target/board_hal.c`为115200、8N1、无流控；PA2 TX→RX2、PA3 RX←TX2，接线表另列共地。与已查教程一致，实物未核线 |
| 通道顺序 | 应用FL/FR/RL/RR映射M1/M3/M2/M4；对应官方M1左前、M2左后、M3右前、M4右后。每轮实际正负方向仍未确认 |
| 指令格式及范围 | `src/motor_uart.c`按`$...#`构造四通道ASCII指令；速度限制±1000、PWM限制±3600。符合已查协议条目 |
| 停车语义与回复 | 上电及禁动选择PWM0；没有把SPD0当作解除闭环，也没有等待运动命令ACK。与协议说明一致；软件PWM0不等于独立动力切断 |
| 查询与反馈 | 使用read_flash、upload，解析MAll/MTEP/MSPD/Battery；不把发出查询当成参数已正确。完整实物收发与驱动固件版本仍待确认 |
| 80mm轮径 | 主控半径为0.040m；驱动板Flash中的wdiameter是另一份持久配置，本固件不自动写入。需要在实际驱动上读回并核对为80mm，不能沿用官方示例67mm |
| 电机参数 | mtype、mline、mphase、PID及反馈方向尚未读回确认，motor_parameters_verified仍为false |
| 速度单位与失联保护 | 已查UART速度表没有明确时间单位；也未找到足以保证用户实际固件断命令自行停车的条款。保留UNKNOWN及未验收标志，需要实际固件依据和硬件验证 |

## 当前产物与后续边界

复核了[80mm产物记录](geometry-80mm-artifacts.json)中32个源码/测试输入哈希和3个ELF/HEX/BIN哈希，全部一致。HEX的SHA256为`5687e2a4e11304deccd7b3fb9de72149622f6a22719f52e5e0f4b7e88023c0db`。这是复核上轮测试/构建产物，没有把它写成此次重新运行测试。

运动、标定、PS2接线开关继续关闭；所有生产验收标志仍为false。单板下载使用本地`build/rm_rct6.hex`，流程见[USB ISP说明](../USB_ISP.md)；下载后version预期为80mm-r3，仍应保持禁动。主控单板验证完成后，再按[待核实配置](../CONFIG_PENDING.md)核对驱动参数、线序/电平、轮向和失联保护。

本次的协议逐页文本、渲染图和核对JSON保存在聊天工作目录`work/driver-readiness-20261026/`。没有改动固件输入、历史CAD记录或串口日志；未暂存、提交、推送或烧录。
