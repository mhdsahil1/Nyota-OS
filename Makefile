# =============================================================================
# Nyota OS — Makefile (Phase 1: x86_64 Kernel Foundation)
# Builds a bootable 64-bit OS image running under QEMU.
# =============================================================================

# ── Cross-Platform Detection ─────────────────────────────────────────────────
ifeq ($(OS),Windows_NT)
    HOST_OS   := windows
    EXE_EXT   := .exe
    ASM_FMT   := win64
    LD_FLAGS  := -m i386pep --image-base 0x0 --section-alignment 0x10 --file-alignment 0x10
    CLEAN_CMD := cmd /c if exist $(BUILD_DIR) rd /s /q $(BUILD_DIR)
    MKDIR_CMD := cmd /c if not exist $(BUILD_DIR) md $(BUILD_DIR)
else
    HOST_OS   := linux
    EXE_EXT   :=
    ASM_FMT   := elf64
    LD_FLAGS  := -m elf_x86_64
    CLEAN_CMD := rm -rf $(BUILD_DIR)
    MKDIR_CMD := mkdir -p $(BUILD_DIR)
endif

# ── Toolchain ─────────────────────────────────────────────────────────────────
CC      := gcc
LD      := ld
NASM    := nasm
OBJCOPY := objcopy
QEMU    := qemu-system-x86_64
GDB     := gdb

# ── Compiler Flags (Freestanding x86_64 Kernel) ──────────────────────────────
CFLAGS := \
    -m64                           \
    -mabi=sysv                     \
    -ffreestanding                 \
    -fno-pie                       \
    -fno-pic                       \
    -nostdlib                      \
    -nostartfiles                  \
    -fno-builtin                   \
    -fno-stack-protector           \
    -fno-asynchronous-unwind-tables\
    -fno-unwind-tables             \
    -mno-red-zone                  \
    -mgeneral-regs-only            \
    -Wall                          \
    -Wextra                        \
    -Iinclude                      \
    -std=gnu99                     \
    -O2

# ── Directories & Output Files ────────────────────────────────────────────────
BUILD_DIR   := build
BOOT_BIN    := $(BUILD_DIR)/boot.bin
STAGE2_BIN  := $(BUILD_DIR)/stage2.bin
KERNEL_ELF  := $(BUILD_DIR)/kernel.elf
KERNEL_BIN  := $(BUILD_DIR)/kernel.bin
IMAGE       := $(BUILD_DIR)/nyota.img
MKIMAGE     := $(BUILD_DIR)/mkimage$(EXE_EXT)

# ── Kernel Object Files ───────────────────────────────────────────────────────
KERNEL_ASM_OBJS := \
    $(BUILD_DIR)/kernel_entry.o     \
    $(BUILD_DIR)/gdt_flush.o        \
    $(BUILD_DIR)/interrupts.o       \
    $(BUILD_DIR)/user_jump.o

KERNEL_C_OBJS := \
    $(BUILD_DIR)/kernel.o           \
    $(BUILD_DIR)/memory.o           \
    $(BUILD_DIR)/cpu.o              \
    $(BUILD_DIR)/gdt.o              \
    $(BUILD_DIR)/tss.o              \
    $(BUILD_DIR)/vga.o              \
    $(BUILD_DIR)/serial.o           \
    $(BUILD_DIR)/idt.o              \
    $(BUILD_DIR)/dispatcher.o       \
    $(BUILD_DIR)/exceptions.o       \
    $(BUILD_DIR)/pic.o              \
    $(BUILD_DIR)/timer.o            \
    $(BUILD_DIR)/keyboard.o         \
    $(BUILD_DIR)/console.o          \
    $(BUILD_DIR)/pmm.o              \
    $(BUILD_DIR)/paging.o           \
    $(BUILD_DIR)/heap.o             \
    $(BUILD_DIR)/memtest.o          \
    $(BUILD_DIR)/syscall.o          \
    $(BUILD_DIR)/process.o          \
    $(BUILD_DIR)/usertest.o

ALL_KERNEL_OBJS := $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS)

# ── Phony Targets ─────────────────────────────────────────────────────────────
.PHONY: all run run-debug run-serial memtest debug clean rebuild help

# Default target: build bootable disk image
all: $(IMAGE)
	@echo.
	@echo ==========================================================
	@echo   Build successful: $(IMAGE)
	@echo   Launch in QEMU with: make run
	@echo ==========================================================
	@echo.

# ── Build Disk Image with host mkimage tool ───────────────────────────────────
$(IMAGE): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(MKIMAGE)
	@echo [IMAGE] $(IMAGE)
	@$(MKIMAGE) $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(IMAGE)

# Host tool to assemble disk image
$(MKIMAGE): tools/mkimage.c | $(BUILD_DIR)
	@echo [HOST]  tools/mkimage.c
	@$(CC) -O2 $< -o $@

# ── Bootloader Stage 1 (MBR) ──────────────────────────────────────────────────
$(BOOT_BIN): boot/boot.asm | $(BUILD_DIR)
	@echo [BUILD] boot/boot.asm
	@$(NASM) -f bin $< -o $@

# ── Bootloader Stage 2 (Long Mode Setup) ───────────────────────────────────────
$(STAGE2_BIN): boot/stage2.asm | $(BUILD_DIR)
	@echo [BUILD] boot/stage2.asm
	@$(NASM) -f bin $< -o $@

# ── Kernel: Link ELF / PE, then extract flat binary ───────────────────────────
$(KERNEL_BIN): $(KERNEL_ELF)
	@echo [STRIP] $(KERNEL_BIN)
	@$(OBJCOPY) -O binary $< $@

$(KERNEL_ELF): $(ALL_KERNEL_OBJS) linker.ld
	@echo [LINK]  $(KERNEL_ELF)
	@$(LD) $(LD_FLAGS) -T linker.ld -e kernel_entry -o $@ $(ALL_KERNEL_OBJS)

# ── Assembly Compilation ──────────────────────────────────────────────────────
$(BUILD_DIR)/kernel_entry.o: kernel/kernel_entry.asm | $(BUILD_DIR)
	@echo [BUILD] kernel/kernel_entry.asm
	@$(NASM) -f $(ASM_FMT) $< -o $@

$(BUILD_DIR)/gdt_flush.o: kernel/cpu/gdt_flush.asm | $(BUILD_DIR)
	@echo [BUILD] kernel/cpu/gdt_flush.asm
	@$(NASM) -f $(ASM_FMT) $< -o $@

$(BUILD_DIR)/interrupts.o: kernel/arch/x86_64/interrupts.asm | $(BUILD_DIR)
	@echo [BUILD] kernel/arch/x86_64/interrupts.asm
	@$(NASM) -f $(ASM_FMT) $< -o $@

$(BUILD_DIR)/user_jump.o: kernel/cpu/user_jump.asm | $(BUILD_DIR)
	@echo [BUILD] kernel/cpu/user_jump.asm
	@$(NASM) -f $(ASM_FMT) $< -o $@

# ── C Compilation ─────────────────────────────────────────────────────────────
$(BUILD_DIR)/kernel.o: kernel/kernel.c | $(BUILD_DIR)
	@echo [BUILD] kernel/kernel.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory/memory.c | $(BUILD_DIR)
	@echo [BUILD] kernel/memory/memory.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cpu.o: kernel/cpu/cpu.c | $(BUILD_DIR)
	@echo [BUILD] kernel/cpu/cpu.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: kernel/cpu/gdt.c | $(BUILD_DIR)
	@echo [BUILD] kernel/cpu/gdt.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tss.o: kernel/cpu/tss.c | $(BUILD_DIR)
	@echo [BUILD] kernel/cpu/tss.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vga.o: drivers/vga.c | $(BUILD_DIR)
	@echo [BUILD] drivers/vga.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: drivers/serial.c | $(BUILD_DIR)
	@echo [BUILD] drivers/serial.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: kernel/arch/x86_64/idt.c | $(BUILD_DIR)
	@echo [BUILD] kernel/arch/x86_64/idt.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/dispatcher.o: kernel/arch/x86_64/dispatcher.c | $(BUILD_DIR)
	@echo [BUILD] kernel/arch/x86_64/dispatcher.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exceptions.o: kernel/arch/x86_64/exceptions.c | $(BUILD_DIR)
	@echo [BUILD] kernel/arch/x86_64/exceptions.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pic.o: kernel/arch/x86_64/pic.c | $(BUILD_DIR)
	@echo [BUILD] kernel/arch/x86_64/pic.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timer.o: drivers/timer.c | $(BUILD_DIR)
	@echo [BUILD] drivers/timer.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: drivers/keyboard.c | $(BUILD_DIR)
	@echo [BUILD] drivers/keyboard.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/console.o: kernel/console.c | $(BUILD_DIR)
	@echo [BUILD] kernel/console.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pmm.o: kernel/memory/pmm.c | $(BUILD_DIR)
	@echo [BUILD] kernel/memory/pmm.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: kernel/arch/x86_64/paging.c | $(BUILD_DIR)
	@echo [BUILD] kernel/arch/x86_64/paging.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/heap.o: kernel/memory/heap.c | $(BUILD_DIR)
	@echo [BUILD] kernel/memory/heap.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memtest.o: kernel/memory/memtest.c | $(BUILD_DIR)
	@echo [BUILD] kernel/memory/memtest.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall.o: kernel/arch/x86_64/syscall.c | $(BUILD_DIR)
	@echo [BUILD] kernel/arch/x86_64/syscall.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process.o: kernel/process/process.c | $(BUILD_DIR)
	@echo [BUILD] kernel/process/process.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usertest.o: kernel/process/usertest.c | $(BUILD_DIR)
	@echo [BUILD] kernel/process/usertest.c
	@$(CC) $(CFLAGS) -c $< -o $@

# ── Build Directory ───────────────────────────────────────────────────────────
$(BUILD_DIR):
	@$(MKDIR_CMD)

# ── Run in QEMU ───────────────────────────────────────────────────────────────
run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -serial stdio

# Run in QEMU with serial output piped to terminal without popup window
run-serial: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -display none -serial stdio

# Run automated memory test runner
memtest: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -display none -serial stdio

# Run with GDB server attached (waits on port 1234)
run-debug: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -s -S -serial stdio

debug: CFLAGS += -g
debug: all

# ── Clean & Rebuild ───────────────────────────────────────────────────────────
clean:
	@$(CLEAN_CMD)
	@echo   Clean complete

rebuild: clean all

help:
	@echo Nyota OS -- Build System Targets:
	@echo   make              Build complete bootable image ($(IMAGE))
	@echo   make run          Launch in QEMU (with interactive display and serial)
	@echo   make run-serial   Launch in QEMU headless (serial output to terminal)
	@echo   make run-debug    Launch in QEMU with GDB stub paused on port 1234
	@echo   make memtest      Run headless QEMU for memory testing
	@echo   make clean        Remove all build artifacts and generated images
	@echo   make rebuild      Clean build directory and build fresh image
	@echo   make debug        Build with debug symbols (-g)
