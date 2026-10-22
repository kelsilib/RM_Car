# 第三方文件来源

- `Drivers/STM32F1xx_HAL_Driver`来自既有STM32工程的ST HAL，保留各文件内的版权及授权说明。
- `Drivers/CMSIS`来自既有工程的Arm CMSIS和ST设备支持。CMSIS头文件含Apache-2.0等授权标记，以相应文件声明为准。
- `RCT6/target/startup_stm32f103xe.s`来自用户已有RCT6资料中的ST CMSIS启动模板。
- `RCT6/target/STM32F103RCTX_FLASH.ld`依据既有ST模板调整为256KiB Flash和48KiB RAM，保留模板说明。
- `references`为用户已有板卡原理图和厂商UART示例，作为接线与配置依据，不代表本车已经通过实测。
- 亚博电机协议的固定来源版本和链接见[RCT6/PROTOCOL.md](RCT6/PROTOCOL.md)。

原有第三方版权声明保持原样。本仓库未为全部文件另行添加统一的开源许可证。
