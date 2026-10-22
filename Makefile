##############################################################################
# RM_Car 底盘电控固件
#
# 用法：
#   ./build.ps1            增量编译
#   ./build.ps1 -Clean     全量重编
#   make -j8               前提是 PATH 里有 arm-none-eabi-gcc / make
##############################################################################

TARGET   = rm_car
BUILD_DIR = build

DEBUG = 1
OPT   = -Og

C_SOURCES := $(wildcard Core/Src/*.c) \
             $(wildcard BSP/Src/*.c) \
             $(wildcard App/Src/*.c) \
             $(wildcard Drivers/STM32F1xx_HAL_Driver/Src/*.c) \
             Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/system_stm32f1xx.c

ASM_SOURCES := startup/startup_stm32f103xb.s

PREFIX = arm-none-eabi-
CC     = $(PREFIX)gcc
AS     = $(PREFIX)gcc -x assembler-with-cpp
CP     = $(PREFIX)objcopy
SZ     = $(PREFIX)size

CPU   = -mcpu=cortex-m3
MCU   = $(CPU) -mthumb

C_DEFS := -DUSE_HAL_DRIVER -DSTM32F103xB

C_INCLUDES := -ICore/Inc \
              -IBSP/Inc \
              -IApp/Inc \
              -IDrivers/STM32F1xx_HAL_Driver/Inc \
              -IDrivers/CMSIS/Device/ST/STM32F1xx/Include \
              -IDrivers/CMSIS/Include

CFLAGS  := $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -g3 -std=gnu11 \
           -Wall -Wextra -Wshadow -Wundef \
           -fdata-sections -ffunction-sections -fno-common \
           -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@"

ASFLAGS := $(MCU) $(OPT) -g3 -Wall -fdata-sections -ffunction-sections

LDSCRIPT := ld/STM32F103C8TX_FLASH.ld

LIBS    := -lc -lm -lnosys
LDFLAGS := $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections \
           -Wl,--print-memory-usage

OBJECTS  := $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(CP) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(CP) -O binary -S $< $@

$(BUILD_DIR):
	mkdir $(BUILD_DIR)

clean:
	@powershell -NoProfile -Command "if (Test-Path '$(BUILD_DIR)') { Remove-Item -Recurse -Force '$(BUILD_DIR)' }"

.PHONY: all clean

-include $(wildcard $(BUILD_DIR)/*.d)
