	.equ KERNEL_CS, 0x08
	.equ KERNEL_DS, 0x10
	.equ IDT_GATE_COUNT, 256
	.equ IDT_GATE_SIZE, 16
	.equ PAGE_SIZE, 4096
	.equ PAGE_TABLE_PAGES, 6
	.equ PAGE_DIRECTORY_COUNT, 4
	.equ LARGE_PAGE_COUNT, 2048
	.equ PHYS_MAP_SLOT, 256
	.equ PAGE_PRESENT_WRITE, 0x003
	.equ LARGE_PAGE_FLAGS, 0x083
	.equ CR0_WRITE_PROTECT, 0x00010000
	.equ CR4_PAGE_GLOBAL, 0x00000080

	.text
	.globl x86_64_start
	.type x86_64_start, @function
x86_64_start:
	cli
	cld
	movq %rdi, %r12
	leaq boot_stack_top(%rip), %rsp
	andq $-16, %rsp
	xorq %rbp, %rbp
	call load_page_tables
	call load_gdt
	call load_idt
	movq %r12, %rdi
	call x86_64_kernel_main

1:
	hlt
	jmp 1b
	.size x86_64_start, .-x86_64_start

	.type load_page_tables, @function
load_page_tables:
	leaq x86_64_start(%rip), %rax
	shrq $32, %rax
	jnz page_table_failure
	leaq boot_stack_top - 1(%rip), %rax
	shrq $32, %rax
	jnz page_table_failure
	leaq early_page_tables_end - 1(%rip), %rax
	shrq $32, %rax
	jnz page_table_failure

	leaq early_pml4(%rip), %rdi
	xorl %eax, %eax
	movl $(PAGE_TABLE_PAGES * PAGE_SIZE / 8), %ecx
	rep stosq

	leaq early_page_directories(%rip), %rdi
	movq $LARGE_PAGE_FLAGS, %rax
	movl $LARGE_PAGE_COUNT, %ecx
1:
	movq %rax, 0(%rdi)
	addq $0x200000, %rax
	addq $8, %rdi
	decl %ecx
	jne 1b

	leaq early_page_directories(%rip), %rax
	leaq early_pdpt(%rip), %rdi
	movl $PAGE_DIRECTORY_COUNT, %ecx
2:
	movq %rax, %rdx
	orq $PAGE_PRESENT_WRITE, %rdx
	movq %rdx, 0(%rdi)
	addq $PAGE_SIZE, %rax
	addq $8, %rdi
	decl %ecx
	jne 2b

	leaq early_pdpt(%rip), %rax
	orq $PAGE_PRESENT_WRITE, %rax
	movq %rax, early_pml4(%rip)
	movq %rax, early_pml4 + PHYS_MAP_SLOT * 8(%rip)

	leaq early_pml4(%rip), %rax
	movq %rax, %cr3
	movq %cr4, %rax
	andq $-129, %rax
	movq %rax, %cr4
	movq %cr0, %rax
	orq $CR0_WRITE_PROTECT, %rax
	movq %rax, %cr0
	ret
	.size load_page_tables, .-load_page_tables

page_table_failure:
	cli
1:
	hlt
	jmp 1b

	.type load_gdt, @function
load_gdt:
	subq $16, %rsp
	movw $(gdt_end - gdt - 1), 0(%rsp)
	leaq gdt(%rip), %rax
	movq %rax, 2(%rsp)
	lgdt 0(%rsp)
	addq $16, %rsp

	pushq $KERNEL_CS
	leaq 1f(%rip), %rax
	pushq %rax
	lretq
1:
	movl $KERNEL_DS, %eax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw %ax, %fs
	movw %ax, %gs
	ret
	.size load_gdt, .-load_gdt

	.type load_idt, @function
load_idt:
	leaq early_idt(%rip), %rdi
	leaq early_exception(%rip), %rax
	movl $IDT_GATE_COUNT, %ecx
1:
	movw %ax, 0(%rdi)
	movw $KERNEL_CS, 2(%rdi)
	movb $0, 4(%rdi)
	movb $0x8e, 5(%rdi)
	movq %rax, %rdx
	shrq $16, %rdx
	movw %dx, 6(%rdi)
	movq %rax, %rdx
	shrq $32, %rdx
	movl %edx, 8(%rdi)
	movl $0, 12(%rdi)
	addq $IDT_GATE_SIZE, %rdi
	decl %ecx
	jne 1b

	leaq early_idt + 14 * IDT_GATE_SIZE(%rip), %rdi
	leaq page_fault(%rip), %rax
	movw %ax, 0(%rdi)
	movq %rax, %rdx
	shrq $16, %rdx
	movw %dx, 6(%rdi)
	shrq $16, %rdx
	movl %edx, 8(%rdi)

	subq $16, %rsp
	movw $(IDT_GATE_COUNT * IDT_GATE_SIZE - 1), 0(%rsp)
	leaq early_idt(%rip), %rax
	movq %rax, 2(%rsp)
	lidt 0(%rsp)
	addq $16, %rsp
	ret
	.size load_idt, .-load_idt

	.type early_exception, @function
early_exception:
	cli
1:
	hlt
	jmp 1b
	.size early_exception, .-early_exception

	.section .rodata
	.balign 8
gdt:
	.quad 0x0000000000000000
	.quad 0x00af9a000000ffff
	.quad 0x00cf92000000ffff
gdt_end:

	.bss
	.balign 16
early_idt:
	.skip IDT_GATE_COUNT * IDT_GATE_SIZE

	.balign 4096
boot_stack:
	.skip 16384
boot_stack_top:

	.balign PAGE_SIZE
early_pml4:
	.skip PAGE_SIZE
early_pdpt:
	.skip PAGE_SIZE
early_page_directories:
	.skip PAGE_DIRECTORY_COUNT * PAGE_SIZE
early_page_tables_end:

	.section .note.GNU-stack,"",@progbits
