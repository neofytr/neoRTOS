# Cross-compilation toolchain definition for bare metal ARM
CC = arm-none-eabi-gcc
LD = arm-none-eabi-gcc
OBJDUMP = arm-none-eabi-objdump
OBJCOPY = arm-none-eabi-objcopy
GDB = arm-none-eabi-gdb

# Directory structure
INC_DIR = includes
SRC_DIR = source
CORESYS_DIR = ../coresys
STM_INC_DIR = ../coresys/includes
CORE_INC_DIR = ../coresys/includes/core
STARTUP_DIR = $(CORESYS_DIR)/startup
SYSCALL_DIR = $(CORESYS_DIR)/syscalls
SYSTEM_CORE_DIR = $(CORESYS_DIR)/system_core
LINKER_DIR = $(CORESYS_DIR)/linker_script
GDB_CMDS_FILE = gdbcmds.txt
OUTPUT_DIR = binaries

TARGET = output

# Create output directory if it doesn't exist
$(shell mkdir -p $(OUTPUT_DIR))

# Common compiler flags for both debug and release builds
# -nostdlib: Don't use any standard system startup files or libraries
# -nostartfiles: Don't use any standard system startup files
# These are crucial for bare metal development where we provide our own startup code
COMMON_FLAGS = -mcpu=cortex-m4 \
               -mthumb \
               -mfloat-abi=hard \
               -mfpu=fpv4-sp-d16 \
               -std=c11 \
               -Wall \
               -Wextra \
               -nostdlib \
               -nostartfiles \
               -I$(INC_DIR) \
               -I$(CORE_INC_DIR) \
               -I$(STM_INC_DIR)

# Release build flags - Maximum optimization for size and performance
# -Os: Optimize for size while maintaining performance
RELEASE_FLAGS = $(COMMON_FLAGS) \
                -Os \
                -DNDEBUG

# Debug build flags
# -O0: No optimization for better debugging experience
# -g3: Maximum debug information
# -ggdb3: Generate debug info for GDB
# -fno-omit-frame-pointer: Maintain stack frame for better debugging
DEBUG_FLAGS = $(COMMON_FLAGS) \
              -O0 \
              -g3 \
              -ggdb3 \
              -fno-omit-frame-pointer \
              -DDEBUG

# Linker flags
# -nostdlib: Don't use standard system libraries
# -Wl,--gc-sections: Remove unused sections during linking
# Map file generation for analyzing memory layout
LDFLAGS = -T$(LINKER_DIR)/linker_script.ld \
          -Wl,-Map=$(OUTPUT_DIR)/$(TARGET).map \
          -nostdlib \
          -Wl,--gc-sections

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
STARTUP_SRC = $(STARTUP_DIR)/startup_nostdlib.c
SYSTEM_CORE_SRC = $(SYSTEM_CORE_DIR)/system_core.c

# Object files
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OUTPUT_DIR)/%.o)
STARTUP_OBJ = $(OUTPUT_DIR)/startup.o
SYSTEM_CORE_OBJ = $(OUTPUT_DIR)/system_core.o

# Default target (release build)
all: CFLAGS = $(RELEASE_FLAGS)
all: $(OUTPUT_DIR)/$(TARGET).bin $(OUTPUT_DIR)/$(TARGET).asm

# Debug target
debug: CFLAGS = $(DEBUG_FLAGS)
debug: $(OUTPUT_DIR)/$(TARGET).bin $(OUTPUT_DIR)/$(TARGET).asm

# Binary generation rules
$(OUTPUT_DIR)/$(TARGET).bin: $(OUTPUT_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Assembly listing generation for analysis and debugging
$(OUTPUT_DIR)/$(TARGET).asm: $(OUTPUT_DIR)/$(TARGET).elf
	$(OBJDUMP) -D $< > $@

# Linking object files
$(OUTPUT_DIR)/$(TARGET).elf: $(OBJS) $(STARTUP_OBJ) $(SYSTEM_CORE_OBJ)
	$(LD) $(OBJS) $(STARTUP_OBJ) $(SYSTEM_CORE_OBJ) $(LDFLAGS) -o $@

# Compilation rules
$(OUTPUT_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT_DIR)/startup.o: $(STARTUP_DIR)/startup_nostdlib.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT_DIR)/system_core.o: $(SYSTEM_CORE_DIR)/system_core.c
	$(CC) $(CFLAGS) -c $< -o $@

# Utility targets
clean:
	rm -rf $(OUTPUT_DIR)

reset:
	st-flash reset 

erase:
	st-flash erase

rcnt_erase:
	st-flash --connect-under-reset erase

# GDB debugging target - uses the debug build
gdb: debug
	$(GDB) -q -x $(GDB_CMDS_FILE) ./binaries/output.elf

# Flash target
# Only .bin files should be flashed as they are raw binaries without debug info
# or ELF headers, making them suitable for direct execution on the microcontroller
flash: $(OUTPUT_DIR)/$(TARGET).bin
	st-flash write $< 0x08000000
	st-flash reset

.PHONY: all debug clean flash gdb reset erase rcnt_erase