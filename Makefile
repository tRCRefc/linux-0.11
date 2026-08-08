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
MINIX_SOURCE := fs/minix.c
EXEC_SOURCE := fs/exec.c
RAMDISK_SOURCE := kernel/blk_drv/ramdisk.c
HD_SOURCE := kernel/blk_drv/hd.c
INCLUDE_DIR := include

CC := gcc
LD := ld
OBJDUMP := objdump
NM := nm
READELF := readelf
STRINGS := strings
FSCK_MINIX := fsck.minix
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
MINIX_OBJ := $(BUILD_DIR)/fs/minix.o
EXEC_OBJ := $(BUILD_DIR)/fs/exec.o
RAMDISK_OBJ := $(BUILD_DIR)/kernel/ramdisk.o
HD_OBJ := $(BUILD_DIR)/kernel/hd.o
EFI_OBJS := $(UEFI_OBJ) $(HEAD_OBJ) $(KERNEL_OBJ) $(MEMORY_OBJ) $(PAGE_OBJ) \
	$(SCHED_OBJ) $(SWITCH_OBJ) $(SYSTEM_CALL_OBJ) $(FORK_OBJ) $(EXIT_OBJ) \
	$(PANIC_OBJ) $(SERIAL_OBJ) $(MINIX_OBJ) $(RAMDISK_OBJ) $(HD_OBJ) \
	$(EXEC_OBJ)
EFI_IMAGE := $(BUILD_DIR)/BOOTX64.EFI
ESP_IMAGE := $(BUILD_DIR)/esp.img
OVMF_CODE := /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS_TEMPLATE := /usr/share/OVMF/OVMF_VARS_4M.fd
OVMF_VARS := $(BUILD_DIR)/OVMF_VARS_4M.fd

TEST_BUILD_DIR := $(BUILD_DIR)/test
TEST_KERNEL_OBJ := $(TEST_BUILD_DIR)/process.o
TEST_USER_OBJ := $(TEST_BUILD_DIR)/process_user.o
TEST_EFI_OBJS := $(UEFI_OBJ) $(HEAD_OBJ) $(TEST_KERNEL_OBJ) $(TEST_USER_OBJ) \
	$(MEMORY_OBJ) $(PAGE_OBJ) $(SCHED_OBJ) $(SWITCH_OBJ) $(SYSTEM_CALL_OBJ) \
	$(FORK_OBJ) $(EXIT_OBJ) $(PANIC_OBJ) $(SERIAL_OBJ) $(MINIX_OBJ) \
	$(RAMDISK_OBJ) $(HD_OBJ) $(EXEC_OBJ)
TEST_EFI_IMAGE := $(TEST_BUILD_DIR)/BOOTX64.EFI
TEST_ESP_IMAGE := $(TEST_BUILD_DIR)/esp.img
TEST_OVMF_VARS := $(TEST_BUILD_DIR)/OVMF_VARS_4M.fd
TEST_MINIX := $(TEST_BUILD_DIR)/minix-test
TEST_ROOT_IMAGE := $(TEST_BUILD_DIR)/root.img
TEST_INIT_OBJ := $(TEST_BUILD_DIR)/init.o
TEST_INIT_ELF := $(TEST_BUILD_DIR)/init
TEST_INIT_EXIT_OBJ := $(TEST_BUILD_DIR)/init-exit.o
TEST_INIT_EXIT_ELF := $(TEST_BUILD_DIR)/init-exit
TEST_INIT_ROOT_IMAGE := $(TEST_BUILD_DIR)/init-root.img
TEST_INIT_OVMF_VARS := $(TEST_BUILD_DIR)/INIT_OVMF_VARS_4M.fd
TEST_FS_KERNEL_OBJ := $(TEST_BUILD_DIR)/fs.o
TEST_FS_EFI_OBJS := $(UEFI_OBJ) $(HEAD_OBJ) $(TEST_FS_KERNEL_OBJ) \
	$(MEMORY_OBJ) $(PAGE_OBJ) $(SCHED_OBJ) $(SWITCH_OBJ) $(SYSTEM_CALL_OBJ) \
	$(FORK_OBJ) $(EXIT_OBJ) $(PANIC_OBJ) $(SERIAL_OBJ) $(MINIX_OBJ) \
	$(RAMDISK_OBJ) $(HD_OBJ) $(EXEC_OBJ)
TEST_FS_EFI_IMAGE := $(TEST_BUILD_DIR)/FSX64.EFI
TEST_FS_ESP_IMAGE := $(TEST_BUILD_DIR)/fs-esp.img
TEST_FS_OVMF_VARS := $(TEST_BUILD_DIR)/FS_OVMF_VARS_4M.fd
TEST_EXEC_KERNEL_OBJ := $(TEST_BUILD_DIR)/exec.o
TEST_EXEC_USER_OBJ := $(TEST_BUILD_DIR)/exec_user.o
TEST_EXEC_EFI_OBJS := $(UEFI_OBJ) $(HEAD_OBJ) $(TEST_EXEC_KERNEL_OBJ) \
	$(TEST_EXEC_USER_OBJ) $(MEMORY_OBJ) $(PAGE_OBJ) $(SCHED_OBJ) \
	$(SWITCH_OBJ) $(SYSTEM_CALL_OBJ) $(FORK_OBJ) $(EXIT_OBJ) $(PANIC_OBJ) \
	$(SERIAL_OBJ) $(MINIX_OBJ) $(RAMDISK_OBJ) $(HD_OBJ) $(EXEC_OBJ)
TEST_EXEC_EFI_IMAGE := $(TEST_BUILD_DIR)/EXECX64.EFI
TEST_EXEC_ESP_IMAGE := $(TEST_BUILD_DIR)/exec-esp.img
TEST_EXEC_OVMF_VARS := $(TEST_BUILD_DIR)/EXEC_OVMF_VARS_4M.fd

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

.PHONY: all image check run test-exec test-fs test-init test-minix test-process clean

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

$(BUILD_DIR)/fs:
	mkdir -p $@

$(TEST_BUILD_DIR):
	mkdir -p $@

$(UEFI_OBJ): $(UEFI_DIR)/main.c $(UEFI_DIR)/efi.h \
		$(INCLUDE_DIR)/asm/boot.h $(INCLUDE_DIR)/asm/serial.h | $(BUILD_DIR)/uefi
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(HEAD_OBJ): $(BOOT_DIR)/head.S | $(BUILD_DIR)/boot
	$(CC) $(X86_64_ASFLAGS) -c $< -o $@

$(KERNEL_OBJ): $(KERNEL_MAIN) $(INCLUDE_DIR)/asm/boot.h \
		$(INCLUDE_DIR)/asm/serial.h $(INCLUDE_DIR)/linux/hd.h \
		$(INCLUDE_DIR)/linux/minix.h $(INCLUDE_DIR)/linux/mm.h \
		$(INCLUDE_DIR)/linux/exec.h $(INCLUDE_DIR)/linux/ramdisk.h \
		$(INCLUDE_DIR)/linux/sched.h | $(BUILD_DIR)/kernel
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
		$(INCLUDE_DIR)/linux/exec.h $(INCLUDE_DIR)/linux/mm.h \
		$(INCLUDE_DIR)/linux/sched.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(EXIT_OBJ): $(EXIT_SOURCE) $(INCLUDE_DIR)/asm/ptrace.h \
		$(INCLUDE_DIR)/linux/kernel.h $(INCLUDE_DIR)/linux/mm.h \
		$(INCLUDE_DIR)/linux/sched.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(PANIC_OBJ): $(PANIC_SOURCE) $(INCLUDE_DIR)/asm/serial.h \
		$(INCLUDE_DIR)/linux/kernel.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(SERIAL_OBJ): $(SERIAL_SOURCE) $(INCLUDE_DIR)/asm/serial.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(MINIX_OBJ): $(MINIX_SOURCE) $(INCLUDE_DIR)/linux/minix.h | $(BUILD_DIR)/fs
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(EXEC_OBJ): $(EXEC_SOURCE) $(INCLUDE_DIR)/asm/ptrace.h \
		$(INCLUDE_DIR)/linux/elf.h $(INCLUDE_DIR)/linux/exec.h \
		$(INCLUDE_DIR)/linux/minix.h $(INCLUDE_DIR)/linux/mm.h \
		$(INCLUDE_DIR)/linux/sched.h | $(BUILD_DIR)/fs
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(RAMDISK_OBJ): $(RAMDISK_SOURCE) $(INCLUDE_DIR)/linux/minix.h \
		$(INCLUDE_DIR)/linux/ramdisk.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(HD_OBJ): $(HD_SOURCE) $(INCLUDE_DIR)/linux/hd.h \
		$(INCLUDE_DIR)/linux/hdreg.h | $(BUILD_DIR)/kernel
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(TEST_KERNEL_OBJ): tests/x86_64/process.c $(INCLUDE_DIR)/asm/boot.h \
		$(INCLUDE_DIR)/asm/ptrace.h $(INCLUDE_DIR)/asm/serial.h \
		$(INCLUDE_DIR)/linux/mm.h $(INCLUDE_DIR)/linux/sched.h | $(TEST_BUILD_DIR)
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(TEST_USER_OBJ): tests/x86_64/process_user.S | $(TEST_BUILD_DIR)
	$(CC) $(X86_64_ASFLAGS) -c $< -o $@

$(TEST_FS_KERNEL_OBJ): tests/x86_64/fs.c $(INCLUDE_DIR)/asm/boot.h \
		$(INCLUDE_DIR)/asm/serial.h $(INCLUDE_DIR)/linux/hd.h \
		$(INCLUDE_DIR)/linux/minix.h \
		$(INCLUDE_DIR)/linux/mm.h $(INCLUDE_DIR)/linux/ramdisk.h | $(TEST_BUILD_DIR)
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(TEST_EXEC_KERNEL_OBJ): tests/x86_64/exec.c $(INCLUDE_DIR)/asm/boot.h \
		$(INCLUDE_DIR)/asm/ptrace.h $(INCLUDE_DIR)/asm/serial.h \
		$(INCLUDE_DIR)/linux/exec.h $(INCLUDE_DIR)/linux/hd.h \
		$(INCLUDE_DIR)/linux/minix.h $(INCLUDE_DIR)/linux/mm.h \
		$(INCLUDE_DIR)/linux/ramdisk.h $(INCLUDE_DIR)/linux/sched.h | $(TEST_BUILD_DIR)
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(TEST_EXEC_USER_OBJ): tests/x86_64/exec_user.S | $(TEST_BUILD_DIR)
	$(CC) $(X86_64_ASFLAGS) -c $< -o $@

$(TEST_INIT_OBJ): tests/x86_64/init.S | $(TEST_BUILD_DIR)
	$(CC) $(X86_64_ASFLAGS) -c $< -o $@

$(TEST_INIT_EXIT_OBJ): tests/x86_64/init.S | $(TEST_BUILD_DIR)
	$(CC) $(X86_64_ASFLAGS) -DINIT_TEST_EXIT -c $< -o $@

$(TEST_INIT_ELF): $(TEST_INIT_OBJ) tests/x86_64/init.ld
	$(LD) -m elf_x86_64 -nostdlib -static -T tests/x86_64/init.ld \
		-o $@ $(TEST_INIT_OBJ)
	@$(READELF) -h $@ | grep -q 'Class:[[:space:]]*ELF64'
	@$(READELF) -h $@ | grep -q 'Machine:[[:space:]]*Advanced Micro Devices X86-64'
	@$(READELF) -l $@ | grep -q 'LOAD'

$(TEST_INIT_EXIT_ELF): $(TEST_INIT_EXIT_OBJ) tests/x86_64/init.ld
	$(LD) -m elf_x86_64 -nostdlib -static -T tests/x86_64/init.ld \
		-o $@ $(TEST_INIT_EXIT_OBJ)
	@$(READELF) -h $@ | grep -q 'Class:[[:space:]]*ELF64'
	@$(READELF) -h $@ | grep -q 'Machine:[[:space:]]*Advanced Micro Devices X86-64'
	@$(READELF) -l $@ | grep -q 'LOAD'

$(TEST_MINIX): $(MINIX_SOURCE) $(RAMDISK_SOURCE) tests/minix.c \
		$(INCLUDE_DIR)/linux/minix.h $(INCLUDE_DIR)/linux/ramdisk.h | $(TEST_BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -idirafter $(INCLUDE_DIR) \
		$(MINIX_SOURCE) $(RAMDISK_SOURCE) tests/minix.c -o $@

$(TEST_ROOT_IMAGE): $(TEST_MINIX) $(TEST_INIT_ELF) | $(TEST_BUILD_DIR)
	$(TEST_MINIX) --write $@ $(TEST_INIT_ELF)
	$(FSCK_MINIX) -f $@

$(TEST_INIT_ROOT_IMAGE): $(TEST_MINIX) $(TEST_INIT_EXIT_ELF) | $(TEST_BUILD_DIR)
	$(TEST_MINIX) --write $@ $(TEST_INIT_EXIT_ELF)
	$(FSCK_MINIX) -f $@

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

$(TEST_EFI_IMAGE): $(TEST_EFI_OBJS) | $(TEST_BUILD_DIR)
	$(LD) -mi386pep --subsystem 10 --entry efi_main --image-base 0 \
		--file-alignment 0x200 --section-alignment 0x1000 --stack 0x10000 \
		-o $@ $(TEST_EFI_OBJS)

$(TEST_ESP_IMAGE): $(TEST_EFI_IMAGE) | $(TEST_BUILD_DIR)
	rm -f $@
	truncate -s 64M $@
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $(TEST_EFI_IMAGE) ::/EFI/BOOT/BOOTX64.EFI

$(TEST_FS_EFI_IMAGE): $(TEST_FS_EFI_OBJS) | $(TEST_BUILD_DIR)
	$(LD) -mi386pep --subsystem 10 --entry efi_main --image-base 0 \
		--file-alignment 0x200 --section-alignment 0x1000 --stack 0x10000 \
		-o $@ $(TEST_FS_EFI_OBJS)

$(TEST_FS_ESP_IMAGE): $(TEST_FS_EFI_IMAGE) | $(TEST_BUILD_DIR)
	rm -f $@
	truncate -s 64M $@
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $(TEST_FS_EFI_IMAGE) ::/EFI/BOOT/BOOTX64.EFI

$(TEST_EXEC_EFI_IMAGE): $(TEST_EXEC_EFI_OBJS) | $(TEST_BUILD_DIR)
	$(LD) -mi386pep --subsystem 10 --entry efi_main --image-base 0 \
		--file-alignment 0x200 --section-alignment 0x1000 --stack 0x10000 \
		-o $@ $(TEST_EXEC_EFI_OBJS)

$(TEST_EXEC_ESP_IMAGE): $(TEST_EXEC_EFI_IMAGE) | $(TEST_BUILD_DIR)
	rm -f $@
	truncate -s 64M $@
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $(TEST_EXEC_EFI_IMAGE) ::/EFI/BOOT/BOOTX64.EFI

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

run: image $(OVMF_VARS) $(TEST_ROOT_IMAGE)
	$(QEMU) \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive if=pflash,format=raw,file=$(OVMF_VARS) \
		-drive if=ide,index=0,format=raw,file=$(ESP_IMAGE) \
		-drive if=ide,index=1,format=raw,file=$(TEST_ROOT_IMAGE) \
		-serial stdio -display none -monitor none -no-reboot -no-shutdown

test-process: $(EFI_IMAGE) $(TEST_ESP_IMAGE)
	@! $(STRINGS) $(EFI_IMAGE) | grep -q 'PROCESS TEST'
	tests/x86_64/run-process.sh $(QEMU) $(OVMF_CODE) \
		$(OVMF_VARS_TEMPLATE) $(TEST_OVMF_VARS) $(TEST_ESP_IMAGE)

test-exec: $(EFI_IMAGE) $(TEST_INIT_ROOT_IMAGE) $(TEST_EXEC_ESP_IMAGE)
	@! $(STRINGS) $(EFI_IMAGE) | grep -q 'EXEC TEST'
	tests/x86_64/run-exec.sh $(QEMU) $(OVMF_CODE) \
		$(OVMF_VARS_TEMPLATE) $(TEST_EXEC_OVMF_VARS) \
		$(TEST_EXEC_ESP_IMAGE) $(TEST_INIT_ROOT_IMAGE)

test-init: $(ESP_IMAGE) $(TEST_INIT_ROOT_IMAGE)
	tests/x86_64/run-init.sh $(QEMU) $(OVMF_CODE) \
		$(OVMF_VARS_TEMPLATE) $(TEST_INIT_OVMF_VARS) \
		$(ESP_IMAGE) $(TEST_INIT_ROOT_IMAGE)

test-minix: $(TEST_MINIX)
	$(TEST_MINIX)

test-fs: $(EFI_IMAGE) $(TEST_ROOT_IMAGE) $(TEST_FS_ESP_IMAGE)
	@! $(STRINGS) $(EFI_IMAGE) | grep -Eq 'FS TEST|minix init image'
	tests/x86_64/run-fs.sh $(QEMU) $(OVMF_CODE) \
		$(OVMF_VARS_TEMPLATE) $(TEST_FS_OVMF_VARS) \
		$(TEST_FS_ESP_IMAGE) $(TEST_ROOT_IMAGE)

clean:
	test "$(BUILD_DIR)" = "build/x86_64"
	rm -rf -- "$(BUILD_DIR)"
