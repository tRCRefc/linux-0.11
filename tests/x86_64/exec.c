#include <asm/boot.h>
#include <asm/ptrace.h>
#include <asm/serial.h>
#include <errno.h>
#include <linux/exec.h>
#include <linux/hd.h>
#include <linux/minix.h>
#include <linux/mm.h>
#include <linux/ramdisk.h>
#include <linux/sched.h>

#define RAMDISK_SIZE (64UL * MINIX_BLOCK_SIZE)
#define USER_CODE USER_ADDRESS_START
#define USER_DATA (USER_CODE + PAGE_SIZE)
#define USER_STACK (USER_DATA + PAGE_SIZE)
#define USER_STACK_TOP (USER_STACK + PAGE_SIZE)
#define USER_ARGV (USER_DATA + 128)
#define USER_ENVP (USER_DATA + 160)
#define USER_EXPECTED (USER_DATA + 192)
#define USER_CS 0x23UL
#define USER_DS 0x1bUL
#define USER_RFLAGS 0x202UL
#define TEST_EXIT_SUCCESS 0x10U
#define TEST_EXIT_FAILURE 0x11U
#define MAX_HELD_PAGES 4096

extern char exec_user_start[] __attribute__((visibility("hidden")));
extern char exec_user_end[] __attribute__((visibility("hidden")));
extern long sys_fork(const struct pt_regs *regs);
extern long sys_waitpid(const struct pt_regs *regs);

static unsigned long initial_free;
static unsigned long held_pages[MAX_HELD_PAGES];

static void stop(unsigned int code) __attribute__((noreturn));

static void stop(unsigned int code)
{
    __asm__ volatile ("outl %0, $0xf4" :: "a" (code));
    for (;;)
        __asm__ volatile ("hlt");
}

static void fail(const char *message)
{
    serial_write("EXEC TEST FAIL: ");
    serial_write(message);
    serial_write("\r\n");
    stop(TEST_EXIT_FAILURE);
}

static void pass(const char *message)
{
    serial_write("EXEC PASS: ");
    serial_write(message);
    serial_write("\r\n");
}

static void map_user_page(unsigned long address)
{
    unsigned long page = get_free_page();

    if (!page)
        fail("cannot allocate user page");
    if (!put_user_page(current->pg_dir, page, address)) {
        free_page(page);
        fail("cannot map user page");
    }
}

static void prepare_user_space(void)
{
    char *from = exec_user_start;
    char *to;
    unsigned long size = (unsigned long)(exec_user_end - exec_user_start);

    if (size > PAGE_SIZE)
        fail("user stub exceeds one page");
    map_user_page(USER_CODE);
    map_user_page(USER_DATA);
    map_user_page(USER_STACK);
    to = (char *)USER_CODE;
    while (size-- > 0)
        *to++ = *from++;
}

static void put_string(unsigned long address, const char *string)
{
    do {
        *(char *)address++ = *string;
    } while (*string++);
}

static void configure(const char *path, long expected)
{
    unsigned long *argv = (unsigned long *)USER_ARGV;
    unsigned long *envp = (unsigned long *)USER_ENVP;
    unsigned long *word = (unsigned long *)USER_DATA;

    while ((unsigned long)word < USER_STACK)
        *word++ = 0;
    put_string(USER_DATA, path);
    put_string(USER_DATA + 64, "init");
    put_string(USER_DATA + 80, "stage6");
    put_string(USER_DATA + 96, "PORT=exec");
    argv[0] = USER_DATA + 64;
    argv[1] = USER_DATA + 80;
    envp[0] = USER_DATA + 96;
    *(long *)USER_EXPECTED = expected;
}

static long fork_user(void)
{
    struct pt_regs regs = { 0 };

    regs.rip = USER_CODE;
    regs.cs = USER_CS;
    regs.rflags = USER_RFLAGS;
    regs.rsp = USER_STACK_TOP - 16;
    regs.ss = USER_DS;
    return sys_fork(&regs);
}

static struct task_struct *find_task(long pid)
{
    int i;

    for (i = 1; i < NR_TASKS; ++i)
        if (task[i] && task[i]->pid == pid)
            return task[i];
    return 0;
}

static void reap(long pid)
{
    struct pt_regs regs = { 0 };
    struct task_struct *child = find_task(pid);

    if (!child || child->state != TASK_ZOMBIE || child->exit_code != 0)
        fail("exec child did not exit successfully");
    regs.rbx = (unsigned long)pid;
    if (sys_waitpid(&regs) != pid)
        fail("cannot reap exec child");
}

static void run_exec(const char *path, long expected)
{
    unsigned long free;
    long pid;

    configure(path, expected);
    free = nr_free_pages();
    pid = fork_user();
    if (pid < 1)
        fail("cannot fork exec child");
    schedule();
    reap(pid);
    if (nr_free_pages() != free)
        fail("exec lifecycle leaked pages");
}

static int hold_pages(unsigned long remaining)
{
    int count = 0;

    while (nr_free_pages() > remaining) {
        if (count == MAX_HELD_PAGES)
            fail("held page array is too small");
        held_pages[count] = get_free_page();
        if (!held_pages[count])
            fail("cannot exhaust free pages");
        ++count;
    }
    return count;
}

static void release_pages(int count)
{
    while (count-- > 0)
        free_page(held_pages[count]);
}

static void test_oom(void)
{
    unsigned long free;
    long pid;
    int held;

    configure("/bin/init", -ENOMEM);
    free = nr_free_pages();
    pid = fork_user();
    if (pid < 1)
        fail("cannot fork OOM child");
    held = hold_pages(1);
    schedule();
    release_pages(held);
    reap(pid);
    if (nr_free_pages() != free)
        fail("failed exec did not roll back pages");
    pass("OOM rollback");
}

void __attribute__((noreturn))
x86_64_kernel_main(const struct boot_info *boot_info)
{
    struct hard_disk disk;
    struct ramdisk rd;
    struct minix_fs fs;

    serial_write("EXEC TEST START\r\n");
    if (boot_info->mem_end - boot_info->mem_start <= RAMDISK_SIZE)
        fail("ramdisk memory");
    if (rd_init(&rd, phys_to_virt(boot_info->mem_start), RAMDISK_SIZE))
        fail("ramdisk init");
    mem_init(boot_info->mem_start + RAMDISK_SIZE, boot_info->mem_end);
    initial_free = nr_free_pages();
    if (hd_init(&disk, 1) || rd_load(&rd, hd_read, &disk))
        fail("load root disk");
    if (minix_mount(&fs, rd_read, &rd))
        fail("mount Minix root");
    exec_init(&fs);
    prepare_user_space();

    run_exec("/bin/init", 0);
    pass("replace user image");
    run_exec("/bin/bad", -ENOEXEC);
    pass("malformed image rollback");
    run_exec("/bin/range", -ENOEXEC);
    pass("invalid segment rollback");
    run_exec("/bin/missing", -ENOENT);
    pass("missing image rollback");
    test_oom();

    free_user_pages(current->pg_dir);
    if (nr_free_pages() != initial_free)
        fail("controller user pages were not released");
    serial_write("EXEC TEST COMPLETE\r\n");
    stop(TEST_EXIT_SUCCESS);
}
