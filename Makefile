# =============================================================================
# Nyota OS — Makefile
# Run all targets inside WSL: wsl make [target]
# =============================================================================

# ── Toolchain ─────────────────────────────────────────────────────────────────
CC      := gcc
LD      := ld
NASM    := nasm
OBJCOPY := objcopy

# ── Compiler flags (freestanding 32-bit kernel, no stdlib) ───────────────────
CFLAGS := \
    -m32                           \
    -ffreestanding                 \
    -fno-pie                       \
    -fno-pic                       \
    -nostdlib                      \
    -nostartfiles                  \
    -fno-builtin                   \
    -fno-stack-protector           \
    -fno-tree-loop-distribute-patterns \
    -Wall                          \
    -Wextra                        \
    -Ikernel                       \
    -std=gnu99                     \
    -O0

# ── Output files ──────────────────────────────────────────────────────────────
BUILD_DIR  := build
BOOT_BIN   := $(BUILD_DIR)/boot.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
IMAGE      := nyota.img

# ── Source → object mapping ───────────────────────────────────────────────────
KERNEL_ENTRY_OBJ := $(BUILD_DIR)/kernel_entry.o

KERNEL_OBJS := \
    $(BUILD_DIR)/kernel.o      \
    $(BUILD_DIR)/vga.o         \
    $(BUILD_DIR)/serial.o      \
    $(BUILD_DIR)/gdt.o         \
    $(BUILD_DIR)/gdt_flush.o   \
    $(BUILD_DIR)/idt.o         \
    $(BUILD_DIR)/idt_asm.o     \
    $(BUILD_DIR)/keyboard.o    \
    $(BUILD_DIR)/shell.o

# ── Default target ─────────────────────────────────────────────────────────────
.PHONY: all clean run run-debug help

all: $(IMAGE)
	@echo ""
	@echo "  ✓  Build complete → $(IMAGE)"
	@echo "     Run with: wsl make run"
	@echo ""

# ── Floppy disk image ─────────────────────────────────────────────────────────
# Layout:
#   Sector 0  (bytes     0-511): bootloader (boot.bin)
#   Sectors 1-50 (bytes 512-25599): kernel   (kernel.bin)
$(IMAGE): $(BOOT_BIN) $(KERNEL_BIN)
	dd if=/dev/zero    of=$(IMAGE) bs=512 count=2880 2>/dev/null
	dd if=$(BOOT_BIN)  of=$(IMAGE) conv=notrunc 2>/dev/null
	dd if=$(KERNEL_BIN) of=$(IMAGE) bs=512 seek=1 conv=notrunc 2>/dev/null

# ── Bootloader ────────────────────────────────────────────────────────────────
$(BOOT_BIN): bootloader/boot.asm | $(BUILD_DIR)
	$(NASM) -f bin $< -o $@

# ── Kernel: link ELF, then strip to flat binary ───────────────────────────────
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(KERNEL_ELF): $(KERNEL_ENTRY_OBJ) $(KERNEL_OBJS) linker.ld
	$(LD) -m elf_i386 -T linker.ld -o $@ $(KERNEL_ENTRY_OBJ) $(KERNEL_OBJS)

# ── Assembly objects ──────────────────────────────────────────────────────────
$(KERNEL_ENTRY_OBJ): kernel/kernel_entry.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/gdt_flush.o: kernel/gdt/gdt_flush.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/idt_asm.o: kernel/idt/idt_asm.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

# ── C objects ─────────────────────────────────────────────────────────────────
$(BUILD_DIR)/kernel.o: kernel/kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vga.o: kernel/vga/vga.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: kernel/serial/serial.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: kernel/gdt/gdt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: kernel/idt/idt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: kernel/keyboard/keyboard.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shell.o: kernel/shell/shell.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Build directory ───────────────────────────────────────────────────────────
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ── Run in QEMU ───────────────────────────────────────────────────────────────
run: $(IMAGE)
	qemu-system-i386 -fda $(IMAGE) -boot a -display curses

# Run with GDB stub attached (pause at start, attach with: gdb build/kernel.elf)
run-debug: $(IMAGE)
	qemu-system-i386 -fda $(IMAGE) -boot a -s -S -display curses

# ── Housekeeping ──────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR) $(IMAGE)
	@echo "  ✓  Clean"

help:
	@echo "Nyota OS — Build Targets"
	@echo "  make all       Build nyota.img"
	@echo "  make run       Launch in QEMU"
	@echo "  make run-debug Launch in QEMU + GDB stub (port 1234)"
	@echo "  make clean     Remove build artefacts"
