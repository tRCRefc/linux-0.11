#ifndef _LINUX_EXEC_H
#define _LINUX_EXEC_H

struct minix_fs;
struct pt_regs;

extern void exec_init(struct minix_fs *fs);
extern int exec_load(const char *path, const char *const argv[],
                     const char *const envp[], unsigned long *pg_dir,
                     unsigned long *entry, unsigned long *stack_pointer);
extern long sys_execve(struct pt_regs *regs);

#endif
