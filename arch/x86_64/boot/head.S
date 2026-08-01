	.text
	.globl x86_64_start
	.type x86_64_start, @function
x86_64_start:
	cli
	leaq boot_stack_top(%rip), %rsp
	andq $-16, %rsp
	xorq %rbp, %rbp
	call x86_64_kernel_main

1:
	hlt
	jmp 1b
	.size x86_64_start, .-x86_64_start

	.bss
	.balign 4096
boot_stack:
	.skip 16384
boot_stack_top:

	.section .note.GNU-stack,"",@progbits
