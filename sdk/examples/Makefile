# =============================================================================
# Nyota OS SDK — Example Applications Makefile
# Demonstrates standalone compilation of native CLI and GUI applications.
# =============================================================================

CC      ?= gcc
LD      ?= ld
OBJCOPY ?= objcopy

SDK_DIR := ..
INC_DIR := $(SDK_DIR)/include
LIB_DIR := $(SDK_DIR)/lib

CFLAGS  := -m64 -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
           -mno-red-zone -Wall -Wextra -I$(INC_DIR) -O2

LDFLAGS := -nostdlib -m i386pep --image-base 0x0000008000000000 -T $(SDK_DIR)/user.ld

CRT0    := $(LIB_DIR)/crt0.o
LIBS    := $(LIB_DIR)/libgui.a $(LIB_DIR)/libnyota.a

TARGETS := hello_cli hello_gui

all: $(TARGETS)

hello_cli: hello_cli.c $(CRT0) $(LIB_DIR)/libnyota.a
	@echo [CC]  $<
	@$(CC) $(CFLAGS) -c $< -o hello_cli.o
	@echo [LD]  $@
	@$(LD) $(LDFLAGS) -o hello_cli.pe $(CRT0) hello_cli.o $(LIB_DIR)/libnyota.a
	@$(OBJCOPY) -O elf64-x86-64 hello_cli.pe $@
	@rm -f hello_cli.o hello_cli.pe

hello_gui: hello_gui.c $(CRT0) $(LIBS)
	@echo [CC]  $<
	@$(CC) $(CFLAGS) -c $< -o hello_gui.o
	@echo [LD]  $@
	@$(LD) $(LDFLAGS) -o hello_gui.pe $(CRT0) hello_gui.o $(LIBS)
	@$(OBJCOPY) -O elf64-x86-64 hello_gui.pe $@
	@rm -f hello_gui.o hello_gui.pe

clean:
	rm -f $(TARGETS) *.o *.pe

.PHONY: all clean
