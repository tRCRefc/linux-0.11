	.equ KERNEL_CS, 0x08
	.equ KERNEL_DS, 0x10
	.equ IDT_GATE_COUNT, 256
	.equ IDT_GATE_SIZE, 16

	.text
	.globl x86_64_start
	.type x86_64_start, @function
x86_64_start:
	cli
	movq %rdi, %r12
	leaq boot_stack_top(%rip), %rsp
	andq $-16, %rsp
	xorq %rbp, %rbp
	call load_gdt
	call load_idt
	movq %r12, %rdi
	call x86_64_kernel_main

1:
	hlt
	jmp 1b
	.size x86_64_start, .-x86_64_start

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

	.section .note.GNU-stack,"",@progbits
