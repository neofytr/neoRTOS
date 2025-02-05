# Cross-compilation toolchain definition
CC = arm-none-eabi-gcc
LD = arm-none-eabi-gcc
GDB = arm-none-eabi-gdb
OBJDUMP = arm-none-eabi-objdump
OBJCOPY = arm-none-eabi-objcopy

# Directory structure
INC_DIR = includes
SRC_DIR = source
CORESYS_DIR = ../coresys
STM_INC_DIR = ../coresys/includes
CORE_INC_DIR = ../coresys/includes/core
STARTUP_DIR = $(CORESYS_DIR)/startup
SYSCALL_DIR = $(CORESYS_DIR)/syscalls
LINKER_DIR = $(CORESYS_DIR)/linker_script
OUTPUT_DIR = binaries

GDB_CMDS_FILE = gdbcmds.txt
TARGET = output

# Create output directory if it doesn't exist
$(shell mkdir -p $(OUTPUT_DIR))

# Common compiler flags for both debug and release builds
COMMON_FLAGS = -mcpu=cortex-m4 \
               -mthumb \
               -mfloat-abi=hard \
               -mfpu=fpv4-sp-d16 \
               -std=c11 \
               -Wall \
               -Wextra \
               -I$(INC_DIR) \
               -I$(CORE_INC_DIR) \
               -I$(STM_INC_DIR)

# Release build flags - Maximum optimization for size and performance
# -O3: Maximum optimization
# -flto: Link-time optimization
# -ffunction-sections -fdata-sections: Place each function/data in its own section for dead code elimination
# -ffast-math: Aggressive floating-point optimizations
# -ftree-vectorize: Enable vectorization optimizations
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
# --specs=nano.specs: Use nano libc for reduced code size
# -Wl,--gc-sections: Remove unused sections during linking
LDFLAGS = -T$(LINKER_DIR)/linker_script.ld \
          -Wl,-Map=$(OUTPUT_DIR)/$(TARGET).map \
          -mcpu=cortex-m4 \
          -mthumb \
          -mfloat-abi=hard \
          -mfpu=fpv4-sp-d16 \
          --specs=nano.specs \
          -Wl,--gc-sections

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
STARTUP_SRC = $(STARTUP_DIR)/startup.c
SYSCALL_SRC = $(SYSCALL_DIR)/syscall.c

# Object files for both debug and release builds
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OUTPUT_DIR)/%.o)
STARTUP_OBJ = $(OUTPUT_DIR)/startup.o
SYSCALL_OBJ = $(OUTPUT_DIR)/syscall.o

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
$(OUTPUT_DIR)/$(TARGET).elf: $(OBJS) $(STARTUP_OBJ) $(SYSCALL_OBJ)
	$(LD) $(OBJS) $(STARTUP_OBJ) $(SYSCALL_OBJ) $(LDFLAGS) -o $@

# Compilation rules
$(OUTPUT_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT_DIR)/startup.o: $(STARTUP_DIR)/startup.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT_DIR)/syscall.o: $(SYSCALL_DIR)/syscall.c
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

# GDB debugging target
gdb: debug
	$(GDB) -q -x $(GDB_CMDS_FILE) ./binaries/output.elf

# Flash target - Only .bin files should be flashed as they are raw binaries
# .elf files contain additional debug info and headers not needed for execution
flash: $(OUTPUT_DIR)/$(TARGET).bin
	st-flash write $< 0x08000000
	st-flash reset

.PHONY: all debug clean flash gdb nostd reset erase rcnt_erase