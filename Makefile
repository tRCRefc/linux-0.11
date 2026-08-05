.DEFAULT_GOAL := all

BUILD_DIR := build/x86_64
BOOT_DIR := boot
UEFI_DIR := boot/uefi
KERNEL_MAIN := init/main.c
MEMORY_SOURCE := mm/memory.c
SCHED_SOURCE := kernel/sched.c
SWITCH_SOURCE := kernel/switch.S
SYSTEM_CALL_SOURCE := kernel/system_call.S
FORK_SOURCE := kernel/fork.c
EXIT_SOURCE := kernel/exit.c
PANIC_SOURCE := kernel/panic.c
SERIAL_SOURCE := kernel/chr_drv/serial.c
INCLUDE_DIR := include

CC := gcc
LD := ld
OBJDUMP := objdump
NM := nm
READELF := readelf
QEMU := qemu-system-x86_64

UEFI_OBJ := $(BUILD_DIR)/uefi/main.o
HEAD_OBJ := $(BUILD_DIR)/boot/head.o
KERNEL_OBJ := $(BUILD_DIR)/kernel/main.o
MEMORY_OBJ := $(BUILD_DIR)/mm/memory.o
PAGE_OBJ := $(BUILD_DIR)/mm/page.o
SCHED_OBJ := $(BUILD_DIR)/kernel/sched.o
SWITCH_OBJ := $(BUILD_DIR)/kernel/switch.o
SYSTEM_CALL_OBJ := $(BUILD_DIR)/kernel/system_call.o
FORK_OBJ := $(BUILD_DIR)/kernel/fork.o
EXIT_OBJ := $(BUILD_DIR)/kernel/exit.o
PANIC_OBJ := $(BUILD_DIR)/kernel/panic.o
SERIAL_OBJ := $(BUILD_DIR)/kernel/serial.o
EFI_OBJS := $(UEFI_OBJ) $(HEAD_OBJ) $(KERNEL_OBJ) $(MEMORY_OBJ) $(PAGE_OBJ) \
	$(SCHED_OBJ) $(SWITCH_OBJ) $(SYSTEM_CALL_OBJ) $(FORK_OBJ) $(EXIT_OBJ) \
	$(PANIC_OBJ) $(SERIAL_OBJ)
EFI_IMAGE := $(BUILD_DIR)/BOOTX64.EFI
ESP_IMAGE := $(BUILD_DIR)/esp.img
OVMF_CODE := /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS_TEMPLATE := /usr/share/OVMF/OVMF_VARS_4M.fd
OVMF_VARS := $(BUILD_DIR)/OVMF_VARS_4M.fd

X86_64_CFLAGS := \
	-m64 \
	-std=gnu11 \
	-ffreestanding \
	-fno-stack-protector \
	-fpic \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-fno-ident \
	-fno-builtin \
	-fno-common \
	-fcf-protection=none \
	-mno-red-zone \
	-mgeneral-regs-only \
	-maccumulate-outgoing-args \
	-Wall -Wextra -Werror \
	-I$(UEFI_DIR) \
	-I$(INCLUDE_DIR)

X86_64_ASFLAGS := \
	-m64 \
	-ffreestanding \
	-fpic \
	-mno-red-zone \
	-I$(INCLUDE_DIR)

.PHONY: all image check run clean

all: $(EFI_IMAGE)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/uefi:
	mkdir -p $@

$(BUILD_DIR)/boot:
	mkdir -p $@

$(BUILD_DIR)/kernel:
	mkdir -p $@

$(BUILD_DIR)/mm:
	mkdir -p $@

$(UEFI_OBJ): $(UEFI_DIR)/main.c $(UEFI_DIR)/efi.h \
		$(INCLUDE_DIR)/asm/boot.h $(INCLUDE_DIR)/asm/serial.h | $(BUILD_DIR)/uefi
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(HEAD_OBJ): $(BOOT_DIR)/head.S | $(BUILD_DIR)/boot
	$(CC) $(X86_64_ASFLAGS) -c $< -o $@

$(KERNEL_OBJ): $(KERNEL_MAIN) $(INCLUDE_DIR)/asm/boot.h \
		$(INCLUDE_DIR)/asm/serial.h $(INCLUDE_DIR)/linux/mm.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(MEMORY_OBJ): $(MEMORY_SOURCE) $(INCLUDE_DIR)/linux/mm.h | $(BUILD_DIR)/mm
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(PAGE_OBJ): mm/page.S | $(BUILD_DIR)/mm
	$(CC) $(X86_64_ASFLAGS) -c $< -o $@

$(SCHED_OBJ): $(SCHED_SOURCE) $(INCLUDE_DIR)/asm/system.h \
		$(INCLUDE_DIR)/linux/sched.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(SWITCH_OBJ): $(SWITCH_SOURCE) | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_ASFLAGS) -c $< -o $@

$(SYSTEM_CALL_OBJ): $(SYSTEM_CALL_SOURCE) | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_ASFLAGS) -c $< -o $@

$(FORK_OBJ): $(FORK_SOURCE) $(INCLUDE_DIR)/asm/ptrace.h \
		$(INCLUDE_DIR)/linux/mm.h $(INCLUDE_DIR)/linux/sched.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(EXIT_OBJ): $(EXIT_SOURCE) $(INCLUDE_DIR)/asm/ptrace.h \
		$(INCLUDE_DIR)/linux/sched.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(PANIC_OBJ): $(PANIC_SOURCE) $(INCLUDE_DIR)/asm/serial.h \
		$(INCLUDE_DIR)/linux/kernel.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(SERIAL_OBJ): $(SERIAL_SOURCE) $(INCLUDE_DIR)/asm/serial.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(EFI_IMAGE): $(EFI_OBJS) | $(BUILD_DIR)
	$(LD) -mi386pep --subsystem 10 --entry efi_main --image-base 0 \
		--file-alignment 0x200 --section-alignment 0x1000 --stack 0x10000 \
		-o $@ $(EFI_OBJS)

$(ESP_IMAGE): $(EFI_IMAGE) | $(BUILD_DIR)
	rm -f $@
	truncate -s 64M $@
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $(EFI_IMAGE) ::/EFI/BOOT/BOOTX64.EFI
	mdir -i $@ ::/EFI/BOOT

$(OVMF_VARS): $(OVMF_VARS_TEMPLATE) | $(BUILD_DIR)
	cp $< $@

image: $(ESP_IMAGE)

check: $(EFI_IMAGE)
	file $(EFI_IMAGE)
	$(OBJDUMP) -f $(EFI_IMAGE)
	$(OBJDUMP) -x $(EFI_IMAGE)
	$(NM) -u $(EFI_IMAGE)
	$(READELF) -r $(EFI_OBJS)
	@file $(EFI_IMAGE) | grep -q 'PE32+ executable (EFI application) x86-64'
	@$(OBJDUMP) -f $(EFI_IMAGE) | grep -q 'file format pei-x86-64'
	@$(OBJDUMP) -f $(EFI_IMAGE) | grep -q 'architecture: i386:x86-64'
	@$(OBJDUMP) -x $(EFI_IMAGE) | grep -q 'Subsystem[[:space:]]*0000000a'
	@$(OBJDUMP) -x $(EFI_IMAGE) | grep -Eq 'AddressOfEntryPoint[[:space:]]+[0-9a-fA-F]*[1-9a-fA-F][0-9a-fA-F]*'
	@test -z "$$($(NM) -u $(EFI_IMAGE))"
	@! $(READELF) -r $(EFI_OBJS) | \
		grep -Eq 'R_X86_64_(32|32S)([[:space:]]|$$)'

run: image $(OVMF_VARS)
	$(QEMU) \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS) \
		-drive format=raw,file=$(ESP_IMAGE) \
		-serial stdio -display none -monitor none -no-reboot -no-shutdown

clean:
	test "$(BUILD_DIR)" = "build/x86_64"
	rm -rf -- "$(BUILD_DIR)"
