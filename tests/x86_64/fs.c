#include <asm/boot.h>
#include <asm/serial.h>
#include <linux/hd.h>
#include <linux/minix.h>
#include <linux/mm.h>
#include <linux/ramdisk.h>

#define RAMDISK_SIZE (64UL * MINIX_BLOCK_SIZE)
#define TEST_EXIT_SUCCESS 0x10U
#define TEST_EXIT_FAILURE 0x11U

static void stop(unsigned int code) __attribute__((noreturn));

static void stop(unsigned int code)
{
    __asm__ volatile ("outl %0, $0xf4" :: "a" (code));
    for (;;)
        __asm__ volatile ("hlt");
}

static void fail(const char *message)
{
    serial_write("FS TEST FAIL: ");
    serial_write(message);
    serial_write("\r\n");
    stop(TEST_EXIT_FAILURE);
}

static int same(const char *left, const char *right, unsigned long length)
{
    while (length-- > 0)
        if (*left++ != *right++)
            return 0;
    return 1;
}

void __attribute__((noreturn))
x86_64_kernel_main(const struct boot_info *boot_info)
{
    static const char expected[] = "minix init image\n";
    struct hard_disk disk;
    struct minix_inode inode;
    struct minix_fs fs;
    struct ramdisk rd;
    char data[sizeof(expected)];
    long bytes;

    serial_write("FS TEST START\r\n");
    if (boot_info->mem_end - boot_info->mem_start <= RAMDISK_SIZE)
        fail("ramdisk memory");
    if (rd_init(&rd, phys_to_virt(boot_info->mem_start), RAMDISK_SIZE))
        fail("ramdisk init");
    mem_init(boot_info->mem_start + RAMDISK_SIZE, boot_info->mem_end);
    if (hd_init(&disk, 1) || rd_load(&rd, hd_read, &disk))
        fail("load root disk");
    serial_write("FS PASS: ATA root input\r\n");
    if (rd_read(&rd, RAMDISK_SIZE / MINIX_BLOCK_SIZE, data) == 0)
        fail("ramdisk accepted an out-of-range block");
    serial_write("FS PASS: ramdisk bounds\r\n");

    if (minix_mount(&fs, rd_read, &rd))
        fail("mount Minix root");
    serial_write("FS PASS: mount Minix root\r\n");
    if (minix_lookup(&fs, "/bin/init", &inode))
        fail("lookup /bin/init");
    bytes = minix_read(&fs, &inode, 0, data, sizeof(data));
    if (bytes != (long)sizeof(expected) - 1 ||
        !same(data, expected, sizeof(expected) - 1))
        fail("read /bin/init");
    serial_write("FS PASS: read /bin/init\r\n");
    serial_write("FS TEST COMPLETE\r\n");
    stop(TEST_EXIT_SUCCESS);
}
