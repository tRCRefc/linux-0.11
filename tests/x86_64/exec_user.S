	.equ __NR_exit, 1
	.equ __NR_execve, 11
	.equ USER_DATA, 0x0000008000001000
	.equ USER_ARGV, USER_DATA + 128
	.equ USER_ENVP, USER_DATA + 160
	.equ USER_EXPECTED, USER_DATA + 192

	.text
	.globl exec_user_start
	.globl exec_user_end
	.type exec_user_start, @function
exec_user_start:
	movabsq $USER_DATA, %rbx
	movabsq $USER_ARGV, %rcx
	movabsq $USER_ENVP, %rdx
	movl $__NR_execve, %eax
	int $0x80
	movabsq $USER_EXPECTED, %r10
	cmpq 0(%r10), %rax
	jne 1f
	xorl %ebx, %ebx
	jmp 2f
1:	movl $1, %ebx
2:	movl $__NR_exit, %eax
	int $0x80
exec_user_end:
	ud2
	.size exec_user_start, .-exec_user_start

	.section .note.GNU-stack,"",@progbits
