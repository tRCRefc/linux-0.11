#include <asm/boot.h>
#include <asm/ptrace.h>
#include <asm/serial.h>
#include <errno.h>
#include <linux/mm.h>
#include <linux/sched.h>

#define USER_CODE USER_ADDRESS_START
#define USER_DATA (USER_CODE + PAGE_SIZE)
#define USER_STACK (USER_DATA + PAGE_SIZE)
#define USER_STACK_TOP (USER_STACK + PAGE_SIZE)
#define USER_SECOND_PT (USER_CODE + 0x200000UL)
#define USER_SLOT (USER_CODE >> 39)
#define USER_CS 0x23UL
#define USER_DS 0x1bUL
#define USER_RFLAGS 0x202UL
#define TEST_EXIT_SUCCESS 0x10U
#define TEST_EXIT_FAILURE 0x11U
#define REPEAT_TASKS 8
#define MAX_HELD_PAGES 4096

extern char process_user_start[] __attribute__((visibility("hidden")));
extern char process_user_anon[] __attribute__((visibility("hidden")));
extern char process_user_cow[] __attribute__((visibility("hidden")));
extern char process_user_fault[] __attribute__((visibility("hidden")));
extern char process_user_oom[] __attribute__((visibility("hidden")));
extern char process_user_exit[] __attribute__((visibility("hidden")));
extern char process_user_nested[] __attribute__((visibility("hidden")));
extern char process_user_end[] __attribute__((visibility("hidden")));

extern long sys_fork(const struct pt_regs *regs);
extern long sys_waitpid(const struct pt_regs *regs);
extern long last_pid __attribute__((visibility("hidden")));

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
    serial_write("PROCESS TEST FAIL: ");
    serial_write(message);
    serial_write("\r\n");
    stop(TEST_EXIT_FAILURE);
}

static void pass(const char *message)
{
    serial_write("PROCESS PASS: ");
    serial_write(message);
    serial_write("\r\n");
}

static void copy_user_code(void)
{
    char *from;
    char *to;
    unsigned long size;

    size = (unsigned long)(process_user_end - process_user_start);
    if (size > PAGE_SIZE)
        fail("user program exceeds one page");
    from = process_user_start;
    to = (char *)USER_CODE;
    while (size-- > 0)
        *to++ = *from++;
}

static void map_user_page(unsigned long address)
{
    unsigned long page;

    page = get_free_page();
    if (page == 0)
        fail("cannot allocate user page");
    if (put_user_page(current->pg_dir, page, address) == 0) {
        free_page(page);
        fail("cannot map user page");
    }
}

static void prepare_user_space(void)
{
    map_user_page(USER_CODE);
    map_user_page(USER_DATA);
    map_user_page(USER_STACK);
    map_user_page(USER_SECOND_PT);
    copy_user_code();
}

static int hold_pages(unsigned long remaining)
{
    int count;

    count = 0;
    while (nr_free_pages() > remaining) {
        if (count == MAX_HELD_PAGES)
            fail("held page array is too small");
        held_pages[count] = get_free_page();
        if (held_pages[count] == 0)
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

static struct task_struct *find_task(long pid)
{
    int i;

    for (i = 1; i < NR_TASKS; ++i)
        if (task[i] && task[i]->pid == pid)
            return task[i];
    return 0;
}

static int child_tasks(void)
{
    int count;
    int i;

    count = 0;
    for (i = 1; i < NR_TASKS; ++i)
        if (task[i])
            ++count;
    return count;
}

static long fork_user(char *entry)
{
    struct pt_regs regs = { 0 };

    regs.rip = USER_CODE + (unsigned long)(entry - process_user_start);
    regs.cs = USER_CS;
    regs.rflags = USER_RFLAGS;
    regs.rsp = USER_STACK_TOP - 16;
    regs.ss = USER_DS;
    return sys_fork(&regs);
}

static void check_zombie(long pid, long code)
{
    struct task_struct *p;
    unsigned long *pml4;

    p = find_task(pid);
    if (!p || p->state != TASK_ZOMBIE)
        fail("child did not become zombie");
    if (p->exit_code != code)
        fail("unexpected child status");
    pml4 = phys_to_virt(p->pg_dir);
    if (pml4[USER_SLOT] != 0)
        fail("zombie retained user mappings");
}

static void wait_child(long pid, unsigned int code)
{
    struct pt_regs regs = { 0 };
    volatile unsigned int *status;
    long result;

    status = (volatile unsigned int *)(USER_DATA + 8);
    *status = ~0U;
    regs.rbx = (unsigned long)pid;
    regs.rcx = (unsigned long)status;
    result = sys_waitpid(&regs);
    if (result != pid)
        fail("waitpid returned wrong pid");
    if (*status != code)
        fail("waitpid returned wrong status");
}

static void run_child(char *entry, unsigned int code)
{
    unsigned long free;
    long pid;

    free = nr_free_pages();
    pid = fork_user(entry);
    if (pid < 1)
        fail("fork failed");
    schedule();
    if (current != task[0])
        fail("controller task was not restored");
    check_zombie(pid, code);
    if (nr_free_pages() != free - 2)
        fail("zombie retained more than root and task pages");
    wait_child(pid, code);
    if (nr_free_pages() != free)
        fail("waitpid did not restore free pages");
}

static void test_anonymous_fault(void)
{
    run_child(process_user_anon, 0);
    pass("anonymous fault");
}

static void test_cow(void)
{
    volatile unsigned long *data;

    data = (volatile unsigned long *)USER_DATA;
    *data = 0x1122334455667788UL;
    run_child(process_user_cow, 0);
    if (*data != 0x1122334455667788UL)
        fail("child changed parent COW data");
    pass("copy on write");
}

static void test_supervisor_fault(void)
{
    run_child(process_user_fault, SIGSEGV);
    pass("supervisor access");
}

static void test_user_oom(void)
{
    unsigned long free;
    long pid;
    int held;

    free = nr_free_pages();
    pid = fork_user(process_user_oom);
    if (pid < 1)
        fail("OOM child fork failed");
    held = hold_pages(1);
    schedule();
    check_zombie(pid, SIGSEGV);
    wait_child(pid, SIGSEGV);
    release_pages(held);
    if (nr_free_pages() != free)
        fail("user OOM did not roll back pages");
    pass("user OOM rollback");
}

static void test_fork_rollback(void)
{
    volatile unsigned long *data;
    unsigned long free;
    long pid;
    int held;

    free = nr_free_pages();
    held = hold_pages(5);
    pid = fork_user(process_user_exit);
    if (pid != -EAGAIN)
        fail("deep fork did not fail with EAGAIN");
    if (child_tasks() != 0 || nr_free_pages() != 5)
        fail("failed fork retained task or table pages");
    release_pages(held);
    if (nr_free_pages() != free)
        fail("failed fork changed free page count");

    data = (volatile unsigned long *)USER_DATA;
    free = nr_free_pages();
    *data = 0xa5a5a5a5a5a5a5a5UL;
    if (*data != 0xa5a5a5a5a5a5a5a5UL || nr_free_pages() != free)
        fail("failed fork retained a leaf reference");
    pass("fork failure rollback");
}

static void test_nested_wait(void)
{
    unsigned long free;
    long pid;

    free = nr_free_pages();
    pid = fork_user(process_user_nested);
    if (pid < 1)
        fail("nested parent fork failed");
    schedule();
    if (child_tasks() != 1)
        fail("nested child was not reaped");
    check_zombie(pid, 0);
    if (nr_free_pages() != free - 2)
        fail("nested wait leaked pages");
    wait_child(pid, 0);
    if (nr_free_pages() != free)
        fail("nested parent was not reaped");
    pass("nested wait");
}

static void test_repeated_lifecycle(void)
{
    unsigned long free;
    long pids[REPEAT_TASKS];
    int i;

    free = nr_free_pages();
    for (i = 0; i < REPEAT_TASKS; ++i) {
        pids[i] = fork_user(process_user_exit);
        if (pids[i] < 1)
            fail("repeated fork failed");
    }
    for (i = 0; i < REPEAT_TASKS; ++i)
        schedule();
    if (child_tasks() != REPEAT_TASKS)
        fail("repeated children did not all exit");
    for (i = 0; i < REPEAT_TASKS; ++i)
        check_zombie(pids[i], 0);
    if (nr_free_pages() != free - 2 * REPEAT_TASKS)
        fail("zombies did not retain exactly two pages");
    for (i = 0; i < REPEAT_TASKS; ++i)
        wait_child(pids[i], 0);
    if (nr_free_pages() != free)
        fail("repeated wait did not restore free pages");
    pass("repeated lifecycle");
}

void __attribute__((noreturn))
x86_64_kernel_main(const struct boot_info *boot_info)
{
    serial_write("PROCESS TEST START\r\n");
    mem_init(boot_info->mem_start, boot_info->mem_end);
    last_pid = 1;
    initial_free = nr_free_pages();
    prepare_user_space();

    test_anonymous_fault();
    test_cow();
    test_supervisor_fault();
    test_user_oom();
    test_fork_rollback();
    test_nested_wait();
    test_repeated_lifecycle();

    free_user_pages(current->pg_dir);
    if (nr_free_pages() != initial_free)
        fail("controller user pages were not released");
    serial_write("PROCESS TEST COMPLETE\r\n");
    stop(TEST_EXIT_SUCCESS);
}
