	.text
	.globl page_fault
	.type page_fault, @function
page_fault:
	cld
	pushq %rax
	pushq %rcx
	pushq %rdx
	pushq %rsi
	pushq %rdi
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %rbx
	pushq %rbp
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

	movq 120(%rsp), %rdi
	movq %cr2, %rsi
	movq %rsp, %rbx
	andq $-16, %rsp
	call do_page_fault
	movq %rbx, %rsp

	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbp
	popq %rbx
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rax
	addq $8, %rsp
	iretq
	.size page_fault, .-page_fault

	.section .note.GNU-stack,"",@progbits
