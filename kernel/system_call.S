	.equ RAX, 112
	.equ NR_SYSTEM_CALLS, 21

	.text
	.globl system_call
	.hidden system_call
	.type system_call, @function
system_call:
	cld
	pushq %rax
	pushq %rbx
	pushq %rcx
	pushq %rdx
	pushq %rsi
	pushq %rdi
	pushq %rbp
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

	movq RAX(%rsp), %rax
	cmpq $NR_SYSTEM_CALLS - 1, %rax
	ja 1f
	leaq sys_call_table(%rip), %rdx
	leaq 0(%rdx,%rax,4), %rdx
	movslq 0(%rdx), %rax
	testq %rax, %rax
	je 1f
	addq %rdx, %rax
	call *%rax
	jmp 2f
1:	movq $-1, %rax
2:	movq %rax, RAX(%rsp)

	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rbp
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	iretq
	.size system_call, .-system_call

	.section .rodata
	.balign 4
sys_call_table:
	.rept NR_SYSTEM_CALLS - 1
	.long 0
	.endr
	.long sys_getpid - .

	.section .note.GNU-stack,"",@progbits
