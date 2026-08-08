	.equ __NR_exit, 1
	.equ __NR_fork, 2
	.equ __NR_waitpid, 7
	.equ USER_DATA, 0x0000008000001000
	.equ USER_ANON, 0x0000008000004000
	.equ USER_OOM, 0x0000008000400000

	.text
	.balign 16
	.globl process_user_start
	.globl process_user_anon
	.globl process_user_cow
	.globl process_user_fault
	.globl process_user_oom
	.globl process_user_exit
	.globl process_user_nested
	.globl process_user_orphan
	.globl process_user_end
process_user_start:
process_user_anon:
	movabsq $USER_ANON, %rax
	movabsq $0x123456789abcdef0, %rdx
	movq %rdx, 0(%rax)
	xorl %ebx, %ebx
	movl $__NR_exit, %eax
	int $0x80

	.balign 16
process_user_cow:
	movabsq $USER_DATA, %rax
	movabsq $0xfedcba9876543210, %rdx
	movq %rdx, 0(%rax)
	xorl %ebx, %ebx
	movl $__NR_exit, %eax
	int $0x80

	.balign 16
process_user_fault:
	xorl %eax, %eax
	movq 0(%rax), %rax
	ud2

	.balign 16
process_user_oom:
	movabsq $USER_OOM, %rax
	movq $1, 0(%rax)
	ud2

	.balign 16
process_user_exit:
	xorl %ebx, %ebx
	movl $__NR_exit, %eax
	int $0x80

	.balign 16
process_user_nested:
	movl $__NR_fork, %eax
	int $0x80
	testq %rax, %rax
	jz 1f
	movq %rax, %rbx
	movabsq $(USER_DATA + 8), %rcx
	xorl %edx, %edx
	movl $__NR_waitpid, %eax
	int $0x80
	cmpq %rbx, %rax
	jne 2f
	xorl %ebx, %ebx
	jmp 3f
1:	xorl %ebx, %ebx
	jmp 3f
2:	movl $1, %ebx
3:	movl $__NR_exit, %eax
	int $0x80

	.balign 16
process_user_orphan:
	movl $__NR_fork, %eax
	int $0x80
	testq %rax, %rax
	jz 4f
	xorl %ebx, %ebx
	jmp 5f
4:	movl $7, %ebx
5:	movl $__NR_exit, %eax
	int $0x80
process_user_end:
	ud2

	.section .note.GNU-stack,"",@progbits
