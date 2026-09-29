# =============================================================================
# Nyota OS — Makefile (Phase 6: Filesystem, ELF Loader & Real Userland)
# Builds a bootable 64-bit OS image and NyotaFS filesystem disk running under QEMU.
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
AR      := ar
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
    -mno-stack-arg-probe           \
    -mgeneral-regs-only            \
    -Wall                          \
    -Wextra                        \
    -Iinclude                      \
    -std=gnu99                     \
    -O2

# ── Compiler Flags (Freestanding x86_64 Userland) ────────────────────────────
USER_CFLAGS := \
    -m64                           \
    -mabi=sysv                     \
    -ffreestanding                 \
    -fno-pie                       \
    -fno-pic                       \
    -nostdlib                      \
    -nostartfiles                  \
    -fno-builtin                   \
    -fno-stack-protector           \
    -mno-red-zone                  \
    -mno-stack-arg-probe           \
    -mno-sse                       \
    -mno-sse2                      \
    -Wall                          \
    -Wextra                        \
    -Iuser/libnyota                \
    -O2

USER_LD_FLAGS := -m i386pep --image-base 0x8000000000 --section-alignment 0x1000 --file-alignment 0x1000

# ── Directories & Output Files ────────────────────────────────────────────────
BUILD_DIR   := build
BOOT_BIN    := $(BUILD_DIR)/boot.bin
STAGE2_BIN  := $(BUILD_DIR)/stage2.bin
KERNEL_ELF  := $(BUILD_DIR)/kernel.elf
KERNEL_BIN  := $(BUILD_DIR)/kernel.bin
IMAGE       := $(BUILD_DIR)/nyota.img
DATA_IMAGE  := $(BUILD_DIR)/nyota-data.img
MKIMAGE     := $(BUILD_DIR)/mkimage$(EXE_EXT)
MKNYOTAFS   := $(BUILD_DIR)/mknyotafs$(EXE_EXT)

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
    $(BUILD_DIR)/scheduler.o        \
    $(BUILD_DIR)/user_programs.o    \
    $(BUILD_DIR)/schedtest.o        \
    $(BUILD_DIR)/usertest.o         \
    $(BUILD_DIR)/block.o            \
    $(BUILD_DIR)/ata.o              \
    $(BUILD_DIR)/nyotafs.o          \
    $(BUILD_DIR)/vfs.o              \
    $(BUILD_DIR)/elf.o              \
    $(BUILD_DIR)/pci.o              \
    $(BUILD_DIR)/e1000.o            \
    $(BUILD_DIR)/netdev.o           \
    $(BUILD_DIR)/net.o              \
    $(BUILD_DIR)/packet.o           \
    $(BUILD_DIR)/ethernet.o         \
    $(BUILD_DIR)/arp.o              \
    $(BUILD_DIR)/ipv4.o             \
    $(BUILD_DIR)/route.o            \
    $(BUILD_DIR)/icmp.o             \
    $(BUILD_DIR)/udp.o              \
    $(BUILD_DIR)/tcp.o              \
    $(BUILD_DIR)/socket.o           \
    $(BUILD_DIR)/signal.o           \
    $(BUILD_DIR)/capability.o       \
    $(BUILD_DIR)/random.o           \
    $(BUILD_DIR)/security.o         \
    $(BUILD_DIR)/pipe.o             \
    $(BUILD_DIR)/shm.o              \
    $(BUILD_DIR)/rtc.o              \
    $(BUILD_DIR)/clock.o            \
    $(BUILD_DIR)/tty.o              \
    $(BUILD_DIR)/logging.o

ALL_KERNEL_OBJS := $(KERNEL_ASM_OBJS) $(KERNEL_C_OBJS)

# ── Userspace Library & Binaries ─────────────────────────────────────────────
LIBNYOTA := $(BUILD_DIR)/libnyota.a

USER_BINARIES := \
    fs/root/init            \
    fs/root/sbin/init       \
    fs/root/sbin/nyotad     \
    fs/root/sbin/loggerd    \
    fs/root/sbin/ttyd       \
    fs/root/sbin/netd       \
    fs/root/sbin/logind     \
    fs/root/bin/sh          \
    fs/root/bin/service     \
    fs/root/bin/logger      \
    fs/root/bin/uname       \
    fs/root/bin/date        \
    fs/root/bin/uptime      \
    fs/root/bin/hostname    \
    fs/root/bin/sysinfo     \
    fs/root/bin/free        \
    fs/root/bin/df          \
    fs/root/bin/reboot      \
    fs/root/bin/shutdown    \
    fs/root/bin/hello       \
    fs/root/bin/echo        \
    fs/root/bin/ls          \
    fs/root/bin/cat         \
    fs/root/bin/ps          \
    fs/root/bin/test        \
    fs/root/bin/ifconfig    \
    fs/root/bin/ping        \
    fs/root/bin/netstat     \
    fs/root/bin/nslookup    \
    fs/root/bin/netcat      \
    fs/root/bin/echo-server \
    fs/root/bin/secinfo     \
    fs/root/bin/kill        \
    fs/root/bin/ipctest     \
    fs/root/bin/memtest     \
    fs/root/bin/crash       \
    fs/root/bin/stressproc  \
    fs/root/bin/lifecycletest

# ── QEMU Drive & Network Flags (Primary: Boot, Secondary: NyotaFS Data, NIC: E1000) ──
QEMU_DRIVE_FLAGS := -drive format=raw,file=$(IMAGE),index=0,media=disk -drive format=raw,file=$(DATA_IMAGE),index=1,media=disk
QEMU_NET_FLAGS   := -netdev user,id=net0,hostfwd=tcp::8080-:8080 -device e1000,netdev=net0

# ── Phony Targets ─────────────────────────────────────────────────────────────
.PHONY: all run run-debug run-serial memtest scheduler-test stress-test debug clean rebuild help user fs disk

# Default target: build bootable kernel image and NyotaFS filesystem disk
all: $(IMAGE) $(DATA_IMAGE)
	@echo.
	@echo ==========================================================
	@echo   Build successful: $(IMAGE) and $(DATA_IMAGE)
	@echo   Launch in QEMU with: make run
	@echo ==========================================================
	@echo.

user: $(USER_BINARIES)
fs: $(DATA_IMAGE)
disk: $(DATA_IMAGE)

# ── Build Disk Image with host mkimage tool ───────────────────────────────────
$(IMAGE): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(MKIMAGE)
	@echo [IMAGE] $(IMAGE)
	@$(MKIMAGE) $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(IMAGE)

# Host tools
$(MKIMAGE): tools/mkimage.c | $(BUILD_DIR)
	@echo [HOST]  tools/mkimage.c
	@$(CC) -O2 $< -o $@

$(MKNYOTAFS): tools/mknyotafs.c | $(BUILD_DIR)
	@echo [HOST]  tools/mknyotafs.c
	@$(CC) -O2 $< -o $@

# ── Build NyotaFS Persistent Disk Image ───────────────────────────────────────
$(DATA_IMAGE): $(MKNYOTAFS) $(USER_BINARIES) fs/root/etc/nyota.conf fs/root/home/welcome.txt | $(BUILD_DIR)
	@echo [FS]    $(DATA_IMAGE)
	@$(MKNYOTAFS) fs/root $(DATA_IMAGE) 16

# ── Userspace Standard C Library (libnyota) ───────────────────────────────────
$(BUILD_DIR)/crt0.o: user/libnyota/crt0.asm | $(BUILD_DIR)
	@echo [BUILD] user/libnyota/crt0.asm
	@$(NASM) -f $(ASM_FMT) $< -o $@

$(BUILD_DIR)/lib_syscall.o: user/libnyota/syscall.c | $(BUILD_DIR)
	@echo [BUILD] user/libnyota/syscall.c
	@$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/lib_string.o: user/libnyota/string.c | $(BUILD_DIR)
	@echo [BUILD] user/libnyota/string.c
	@$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/lib_io.o: user/libnyota/io.c | $(BUILD_DIR)
	@echo [BUILD] user/libnyota/io.c
	@$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/lib_env.o: user/libnyota/env.c | $(BUILD_DIR)
	@echo [BUILD] user/libnyota/env.c
	@$(CC) $(USER_CFLAGS) -c $< -o $@

$(LIBNYOTA): $(BUILD_DIR)/lib_syscall.o $(BUILD_DIR)/lib_string.o $(BUILD_DIR)/lib_io.o $(BUILD_DIR)/lib_env.o
	@echo [LIB]   $@
	@$(AR) rcs $@ $^

# ── Userspace Programs ────────────────────────────────────────────────────────
fs/root/init: user/init/main.c $(BUILD_DIR)/crt0.o $(LIBNYOTA) user.ld | $(BUILD_DIR)
	@echo [USER]  /init
	@$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/user_init.o
	@$(LD) $(USER_LD_FLAGS) -T user.ld -o $(BUILD_DIR)/init.pe $(BUILD_DIR)/crt0.o $(BUILD_DIR)/user_init.o $(LIBNYOTA)
	@$(OBJCOPY) -O elf64-x86-64 $(BUILD_DIR)/init.pe $@

fs/root/sbin/%: user/%/main.c $(BUILD_DIR)/crt0.o $(LIBNYOTA) user.ld | $(BUILD_DIR)
	@echo [USER]  /sbin/$*
	@$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/user_$*.o
	@$(LD) $(USER_LD_FLAGS) -T user.ld -o $(BUILD_DIR)/$*.pe $(BUILD_DIR)/crt0.o $(BUILD_DIR)/user_$*.o $(LIBNYOTA)
	@$(OBJCOPY) -O elf64-x86-64 $(BUILD_DIR)/$*.pe $@

fs/root/bin/%: user/%/main.c $(BUILD_DIR)/crt0.o $(LIBNYOTA) user.ld | $(BUILD_DIR)
	@echo [USER]  /bin/$*
	@$(CC) $(USER_CFLAGS) -c $< -o $(BUILD_DIR)/user_$*.o
	@$(LD) $(USER_LD_FLAGS) -T user.ld -o $(BUILD_DIR)/$*.pe $(BUILD_DIR)/crt0.o $(BUILD_DIR)/user_$*.o $(LIBNYOTA)
	@$(OBJCOPY) -O elf64-x86-64 $(BUILD_DIR)/$*.pe $@

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
	@$(OBJCOPY) -R .reloc -O binary $< $@

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

$(BUILD_DIR)/scheduler.o: kernel/process/scheduler.c | $(BUILD_DIR)
	@echo [BUILD] kernel/process/scheduler.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_programs.o: kernel/process/user_programs.c | $(BUILD_DIR)
	@echo [BUILD] kernel/process/user_programs.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/schedtest.o: kernel/process/schedtest.c | $(BUILD_DIR)
	@echo [BUILD] kernel/process/schedtest.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usertest.o: kernel/process/usertest.c | $(BUILD_DIR)
	@echo [BUILD] kernel/process/usertest.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/block.o: kernel/storage/block.c | $(BUILD_DIR)
	@echo [BUILD] kernel/storage/block.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ata.o: kernel/storage/ata.c | $(BUILD_DIR)
	@echo [BUILD] kernel/storage/ata.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/nyotafs.o: kernel/fs/nyotafs.c | $(BUILD_DIR)
	@echo [BUILD] kernel/fs/nyotafs.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: kernel/fs/vfs.c | $(BUILD_DIR)
	@echo [BUILD] kernel/fs/vfs.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/elf.o: kernel/elf/elf.c | $(BUILD_DIR)
	@echo [BUILD] kernel/elf/elf.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pci.o: kernel/drivers/pci/pci.c | $(BUILD_DIR)
	@echo [BUILD] kernel/drivers/pci/pci.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/e1000.o: kernel/drivers/net/e1000.c | $(BUILD_DIR)
	@echo [BUILD] kernel/drivers/net/e1000.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/netdev.o: kernel/net/netdev.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/netdev.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/net.o: kernel/net/net.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/net.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/packet.o: kernel/net/packet.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/packet.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ethernet.o: kernel/net/ethernet.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/ethernet.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/arp.o: kernel/net/arp.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/arp.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ipv4.o: kernel/net/ipv4.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/ipv4.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/route.o: kernel/net/route.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/route.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/icmp.o: kernel/net/icmp.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/icmp.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/udp.o: kernel/net/udp.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/udp.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tcp.o: kernel/net/tcp.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/tcp.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/socket.o: kernel/net/socket.c | $(BUILD_DIR)
	@echo [BUILD] kernel/net/socket.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/signal.o: kernel/process/signal.c | $(BUILD_DIR)
	@echo [BUILD] kernel/process/signal.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/capability.o: kernel/security/capability.c | $(BUILD_DIR)
	@echo [BUILD] kernel/security/capability.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/random.o: kernel/security/random.c | $(BUILD_DIR)
	@echo [BUILD] kernel/security/random.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/security.o: kernel/security/security.c | $(BUILD_DIR)
	@echo [BUILD] kernel/security/security.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pipe.o: kernel/ipc/pipe.c | $(BUILD_DIR)
	@echo [BUILD] kernel/ipc/pipe.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shm.o: kernel/ipc/shm.c | $(BUILD_DIR)
	@echo [BUILD] kernel/ipc/shm.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rtc.o: kernel/time/rtc.c | $(BUILD_DIR)
	@echo [BUILD] kernel/time/rtc.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/clock.o: kernel/time/clock.c | $(BUILD_DIR)
	@echo [BUILD] kernel/time/clock.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tty.o: kernel/drivers/tty.c | $(BUILD_DIR)
	@echo [BUILD] kernel/drivers/tty.c
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/logging.o: kernel/logging.c | $(BUILD_DIR)
	@echo [BUILD] kernel/logging.c
	@$(CC) $(CFLAGS) -c $< -o $@

# ── Build Directory ───────────────────────────────────────────────────────────
$(BUILD_DIR):
	@$(MKDIR_CMD)

# ── Run in QEMU ───────────────────────────────────────────────────────────────
run: $(IMAGE) $(DATA_IMAGE)
	$(QEMU) $(QEMU_DRIVE_FLAGS) $(QEMU_NET_FLAGS) -serial stdio

# Run in QEMU with serial output piped to terminal without popup window
run-serial: $(IMAGE) $(DATA_IMAGE)
	$(QEMU) $(QEMU_DRIVE_FLAGS) $(QEMU_NET_FLAGS) -display none -serial stdio

# Run scheduler / multitasking tests
scheduler-test: $(IMAGE) $(DATA_IMAGE)
	$(QEMU) $(QEMU_DRIVE_FLAGS) $(QEMU_NET_FLAGS) -display none -serial stdio

stress-test: $(IMAGE) $(DATA_IMAGE)
	$(QEMU) $(QEMU_DRIVE_FLAGS) $(QEMU_NET_FLAGS) -display none -serial stdio

# Run automated memory test runner
memtest: $(IMAGE) $(DATA_IMAGE)
	$(QEMU) $(QEMU_DRIVE_FLAGS) $(QEMU_NET_FLAGS) -display none -serial stdio

# Run with GDB server attached (waits on port 1234)
run-debug: $(IMAGE) $(DATA_IMAGE)
	$(QEMU) $(QEMU_DRIVE_FLAGS) $(QEMU_NET_FLAGS) -s -S -serial stdio

debug: CFLAGS += -g
debug: all

# ── Clean & Rebuild ───────────────────────────────────────────────────────────
clean:
	@$(CLEAN_CMD)
	@echo   Clean complete

rebuild: clean all

help:
	@echo Nyota OS -- Build System Targets:
	@echo   make              Build complete bootable image ($(IMAGE)) and disk ($(DATA_IMAGE))
	@echo   make user         Build userspace libraries and ELF binaries
	@echo   make fs           Build NyotaFS disk image ($(DATA_IMAGE))
	@echo   make run          Launch in QEMU (with interactive display and serial)
	@echo   make run-serial   Launch in QEMU headless (serial output to terminal)
	@echo   make run-debug    Launch in QEMU with GDB stub paused on port 1234
	@echo   make memtest      Run headless QEMU for memory testing
	@echo   make clean        Remove all build artifacts and generated images
	@echo   make rebuild      Clean build directory and build fresh images
	@echo   make debug        Build with debug symbols (-g)
