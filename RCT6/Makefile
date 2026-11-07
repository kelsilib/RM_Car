TARGET = rm_rct6
BUILD_DIR = build
CROSS_COMPILE = arm-none-eabi-
CC = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
SIZE = $(CROSS_COMPILE)size
HOST_CC ?= gcc
HOST_CFLAGS = -std=c11 -Wall -Wextra -Werror -Wshadow -Wundef -pedantic -O1 -g -Iinclude -Itarget
MCU = -mcpu=cortex-m3 -mthumb
HAL = ../Drivers/STM32F1xx_HAL_Driver
CMSIS = ../Drivers/CMSIS
CORE_SOURCES = $(wildcard src/*.c)
HAL_NAMES = hal hal_cortex hal_rcc hal_rcc_ex hal_gpio hal_uart hal_dma hal_pwr hal_tim hal_tim_ex
C_SOURCES = $(CORE_SOURCES) $(wildcard target/*.c) \
            $(addprefix $(HAL)/Src/stm32f1xx_,$(addsuffix .c,$(HAL_NAMES))) \
            $(CMSIS)/Device/ST/STM32F1xx/Source/Templates/system_stm32f1xx.c
ASM_SOURCE = target/startup_stm32f103xe.s
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o))) $(BUILD_DIR)/startup_stm32f103xe.o
vpath %.c $(sort $(dir $(C_SOURCES)))
INCLUDES = -Iinclude -Itarget -I$(HAL)/Inc -I$(CMSIS)/Include -I$(CMSIS)/Device/ST/STM32F1xx/Include
CFLAGS = $(MCU) -DSTM32F103xE -DUSE_HAL_DRIVER $(INCLUDES) -std=c11 -Og -g3 \
         -Wall -Wextra -Werror -Wshadow -Wundef -ffunction-sections -fdata-sections -fno-common
# 单独编译变体时可附加宏；使用另一个BUILD_DIR，避免和默认固件的对象文件混用。
EXTRA_CFLAGS ?=
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS = $(MCU) --specs=nano.specs --specs=nosys.specs -Ttarget/STM32F103RCTX_FLASH.ld \
          -Wl,--gc-sections,-Map=$(BUILD_DIR)/$(TARGET).map,--print-memory-usage

.PHONY: all test test-shovel test-arm-pair check-ps2
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR):
	$(if $(wildcard $(BUILD_DIR)/.),,mkdir "$(BUILD_DIR)")

# 上面先检查目录；已有目录时跳过创建，make -B 强制重编译也不会报“目录已存在”。

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(BUILD_DIR)/startup_stm32f103xe.o: $(ASM_SOURCE) Makefile | $(BUILD_DIR)
	$(CC) $(MCU) -g3 -x assembler-with-cpp -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) target/STM32F103RCTX_FLASH.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -Wl,--start-group -lc -lm -lgcc -Wl,--end-group -o $@
	$(SIZE) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary -S $< $@

$(BUILD_DIR)/test_core.exe: $(CORE_SOURCES) tests/test_core.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) $(CORE_SOURCES) tests/test_core.c -lm -o $@

# 把真正的主循环编进PC测试，只换掉访问硬件的函数；这两个文件不进入STM32固件。
$(BUILD_DIR)/test_firmware_main.o: target/main.c $(wildcard target/*.h) $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -Dmain=rm_firmware_main -c target/main.c -o $@

$(BUILD_DIR)/test_startup.exe: $(CORE_SOURCES) tests/test_startup.c $(BUILD_DIR)/test_firmware_main.o $(wildcard include/*.h) target/board_hal.h Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) $(CORE_SOURCES) tests/test_startup.c $(BUILD_DIR)/test_firmware_main.o -lm -o $@

$(BUILD_DIR)/test_service.exe: $(CORE_SOURCES) tests/test_service.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) $(CORE_SOURCES) tests/test_service.c -lm -o $@

$(BUILD_DIR)/test_ps2.exe: src/remote_ps2.c tests/test_ps2.c include/remote_ps2.h Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) src/remote_ps2.c tests/test_ps2.c -lm -o $@

$(BUILD_DIR)/test_arm.exe: $(CORE_SOURCES) tests/test_arm.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) $(CORE_SOURCES) tests/test_arm.c -lm -o $@

$(BUILD_DIR)/test_arm_main.exe: $(CORE_SOURCES) tests/test_arm_main.c $(BUILD_DIR)/test_firmware_main.o $(wildcard include/*.h) target/board_servo.h Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) $(CORE_SOURCES) tests/test_arm_main.c $(BUILD_DIR)/test_firmware_main.o -lm -o $@

$(BUILD_DIR)/test_bench.exe: $(CORE_SOURCES) tests/test_bench.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_BENCH_TEST=1 $(CORE_SOURCES) tests/test_bench.c -lm -o $@

$(BUILD_DIR)/test_bench_disabled.exe: $(CORE_SOURCES) tests/test_bench.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) $(CORE_SOURCES) tests/test_bench.c -lm -o $@

$(BUILD_DIR)/test_bench_main.o: target/main.c $(wildcard target/*.h) $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_BENCH_TEST=1 -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=0 -Dmain=rm_firmware_main -c target/main.c -o $@

$(BUILD_DIR)/test_bench_main.exe: $(CORE_SOURCES) tests/test_startup.c $(BUILD_DIR)/test_bench_main.o $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_BENCH_TEST=1 $(CORE_SOURCES) tests/test_startup.c $(BUILD_DIR)/test_bench_main.o -lm -o $@

$(BUILD_DIR)/test_field.exe: $(CORE_SOURCES) tests/test_field.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_BENCH_TEST=1 -DRM_FIELD_DRIVE=1 $(CORE_SOURCES) tests/test_field.c -lm -o $@

$(BUILD_DIR)/test_field_main.o: target/main.c $(wildcard target/*.h) $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_BENCH_TEST=1 -DRM_FIELD_DRIVE=1 -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=0 -Dmain=rm_firmware_main -c target/main.c -o $@

$(BUILD_DIR)/test_field_main.exe: $(CORE_SOURCES) tests/test_field_main.c $(BUILD_DIR)/test_field_main.o $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_BENCH_TEST=1 -DRM_FIELD_DRIVE=1 $(CORE_SOURCES) tests/test_field_main.c $(BUILD_DIR)/test_field_main.o -lm -o $@

$(BUILD_DIR)/test_match.exe: $(CORE_SOURCES) tests/test_match.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_BENCH_TEST=1 -DRM_FIELD_DRIVE=1 $(CORE_SOURCES) tests/test_match.c -lm -o $@

$(BUILD_DIR)/test_match_main.exe: $(CORE_SOURCES) tests/test_match_main.c $(BUILD_DIR)/test_field_main.o $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_BENCH_TEST=1 -DRM_FIELD_DRIVE=1 $(CORE_SOURCES) tests/test_match_main.c $(BUILD_DIR)/test_field_main.o -lm -o $@

$(BUILD_DIR)/test_servo_cal.exe: $(CORE_SOURCES) tests/test_servo_cal.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_SERVO_CAL_CHANNEL=2 $(CORE_SOURCES) tests/test_servo_cal.c -lm -o $@

$(BUILD_DIR)/test_servo_cal_main.o: target/servo_cal_main.c $(wildcard target/*.h) $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_SERVO_CAL_CHANNEL=2 -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=1 -Dmain=rm_firmware_main -c target/servo_cal_main.c -o $@

$(BUILD_DIR)/test_servo_cal_main.exe: $(CORE_SOURCES) tests/test_servo_cal_main.c $(BUILD_DIR)/test_servo_cal_main.o $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_SERVO_CAL_CHANNEL=2 $(CORE_SOURCES) tests/test_servo_cal_main.c $(BUILD_DIR)/test_servo_cal_main.o -lm -o $@

test: $(BUILD_DIR)/test_core.exe $(BUILD_DIR)/test_startup.exe $(BUILD_DIR)/test_service.exe $(BUILD_DIR)/test_ps2.exe $(BUILD_DIR)/test_arm.exe $(BUILD_DIR)/test_arm_main.exe $(BUILD_DIR)/test_bench.exe $(BUILD_DIR)/test_bench_disabled.exe $(BUILD_DIR)/test_bench_main.exe $(BUILD_DIR)/test_field.exe $(BUILD_DIR)/test_field_main.exe $(BUILD_DIR)/test_match.exe $(BUILD_DIR)/test_match_main.exe $(BUILD_DIR)/test_servo_cal.exe $(BUILD_DIR)/test_servo_cal_main.exe
	./$(BUILD_DIR)/test_core.exe
	./$(BUILD_DIR)/test_startup.exe
	./$(BUILD_DIR)/test_service.exe
	./$(BUILD_DIR)/test_ps2.exe
	./$(BUILD_DIR)/test_arm.exe
	./$(BUILD_DIR)/test_arm_main.exe
	./$(BUILD_DIR)/test_bench.exe
	./$(BUILD_DIR)/test_bench_disabled.exe
	./$(BUILD_DIR)/test_bench_main.exe
	./$(BUILD_DIR)/test_field.exe
	./$(BUILD_DIR)/test_field_main.exe
	./$(BUILD_DIR)/test_match.exe
	./$(BUILD_DIR)/test_match_main.exe
	./$(BUILD_DIR)/test_servo_cal.exe
	./$(BUILD_DIR)/test_servo_cal_main.exe

$(BUILD_DIR)/test_shovel.exe: $(CORE_SOURCES) tests/test_shovel.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_SHOVEL_CONTROL=1 -DRM_SERVO_CAL_CHANNEL=2 $(CORE_SOURCES) tests/test_shovel.c -lm -o $@

$(BUILD_DIR)/test_shovel_main.o: target/shovel_main.c $(wildcard target/*.h) $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_SHOVEL_CONTROL=1 -DRM_SERVO_CAL_CHANNEL=2 -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=1 -Dmain=rm_firmware_main -c target/shovel_main.c -o $@

$(BUILD_DIR)/test_shovel_main.exe: $(CORE_SOURCES) tests/test_shovel_main.c $(BUILD_DIR)/test_shovel_main.o $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -DRM_SHOVEL_CONTROL=1 -DRM_SERVO_CAL_CHANNEL=2 $(CORE_SOURCES) tests/test_shovel_main.c $(BUILD_DIR)/test_shovel_main.o -lm -o $@

test: test-shovel

test-shovel: $(BUILD_DIR)/test_shovel.exe $(BUILD_DIR)/test_shovel_main.exe
	./$(BUILD_DIR)/test_shovel.exe
	./$(BUILD_DIR)/test_shovel_main.exe

PAIR_FLAGS = -DRM_ARM_PAIR_CONTROL=1 -DRM_SHOVEL_CONTROL=1 -DRM_SERVO_CAL_CHANNEL=2 -DRM_PS2_WIRED=1 -DRM_ARM_WIRED=1

$(BUILD_DIR)/test_joint_pair.exe: $(CORE_SOURCES) tests/test_joint_pair.c $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) $(PAIR_FLAGS) $(CORE_SOURCES) tests/test_joint_pair.c -lm -o $@

$(BUILD_DIR)/test_arm_pair_main.o: target/shovel_main.c $(wildcard target/*.h) $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) $(PAIR_FLAGS) -Dmain=rm_firmware_main -c target/shovel_main.c -o $@

$(BUILD_DIR)/test_arm_pair_main.exe: $(CORE_SOURCES) tests/test_arm_pair_main.c $(BUILD_DIR)/test_arm_pair_main.o $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) $(PAIR_FLAGS) $(CORE_SOURCES) tests/test_arm_pair_main.c $(BUILD_DIR)/test_arm_pair_main.o -lm -o $@

$(BUILD_DIR)/test_board_servo_pair.exe: $(CORE_SOURCES) target/board_servo.c tests/test_board_servo_pair.c tests/fakes/stm32f1xx_hal.h $(wildcard include/*.h) Makefile | $(BUILD_DIR)
	"$(HOST_CC)" $(HOST_CFLAGS) -Itests/fakes $(PAIR_FLAGS) $(CORE_SOURCES) target/board_servo.c tests/test_board_servo_pair.c -lm -o $@

test: test-arm-pair

test-arm-pair: $(BUILD_DIR)/test_joint_pair.exe $(BUILD_DIR)/test_arm_pair_main.exe $(BUILD_DIR)/test_board_servo_pair.exe
	./$(BUILD_DIR)/test_joint_pair.exe
	./$(BUILD_DIR)/test_arm_pair_main.exe
	./$(BUILD_DIR)/test_board_servo_pair.exe

# 只检查PS2接线代码能否编译；不会改变正式固件中“默认关闭PS2”的设置。
check-ps2: | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DRM_PS2_WIRED=1 -c target/board_hal.c -o $(BUILD_DIR)/board_hal_ps2_check.o

-include $(wildcard $(BUILD_DIR)/*.d)
