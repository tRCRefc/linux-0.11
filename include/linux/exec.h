#ifndef _LINUX_EXEC_H
#define _LINUX_EXEC_H

struct minix_fs;
struct pt_regs;

extern void exec_init(struct minix_fs *fs);
extern long sys_execve(struct pt_regs *regs);

#endif
