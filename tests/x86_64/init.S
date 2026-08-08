	.equ __NR_exit, 1

	.text
	.globl _start
	.type _start, @function
_start:
	cmpq $2, 0(%rsp)
	jne 1f
	movq 8(%rsp), %rax
	cmpb $'i', 0(%rax)
	jne 1f
	cmpb $'n', 1(%rax)
	jne 1f
	cmpb $'i', 2(%rax)
	jne 1f
	cmpb $'t', 3(%rax)
	jne 1f
	cmpb $0, 4(%rax)
	jne 1f
	movq 16(%rsp), %rax
	cmpb $'s', 0(%rax)
	jne 1f
	cmpb $'t', 1(%rax)
	jne 1f
	cmpb $'a', 2(%rax)
	jne 1f
	cmpb $'g', 3(%rax)
	jne 1f
	cmpb $'e', 4(%rax)
	jne 1f
	cmpb $'6', 5(%rax)
	jne 1f
	cmpb $0, 6(%rax)
	jne 1f
	cmpq $0, 24(%rsp)
	jne 1f
	movq 32(%rsp), %rax
	cmpb $'P', 0(%rax)
	jne 1f
	cmpb $'O', 1(%rax)
	jne 1f
	cmpb $'R', 2(%rax)
	jne 1f
	cmpb $'T', 3(%rax)
	jne 1f
	cmpb $'=', 4(%rax)
	jne 1f
	cmpb $'e', 5(%rax)
	jne 1f
	cmpb $'x', 6(%rax)
	jne 1f
	cmpb $'e', 7(%rax)
	jne 1f
	cmpb $'c', 8(%rax)
	jne 1f
	cmpb $0, 9(%rax)
	jne 1f
	cmpq $0, 40(%rsp)
	jne 1f
	cmpq $0, bss_word(%rip)
	jne 1f
	cmpq $0x12345678, data_word(%rip)
	jne 1f
	xorl %ebx, %ebx
	jmp 2f
1:	movl $1, %ebx
2:	movl $__NR_exit, %eax
	int $0x80
	ud2
	.size _start, .-_start

	.data
	.balign 8
data_word:
	.quad 0x12345678

	.bss
	.balign 8
bss_word:
	.skip 8

	.section .note.GNU-stack,"",@progbits
