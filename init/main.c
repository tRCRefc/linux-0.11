#include <asm/boot.h>
#include <asm/serial.h>
#include <linux/hd.h>
#include <linux/kernel.h>
#include <linux/minix.h>
#include <linux/mm.h>
#include <linux/ramdisk.h>

#define RAMDISK_SIZE (64UL * MINIX_BLOCK_SIZE)

static struct hard_disk root_disk;
static struct ramdisk root_ramdisk;
static struct minix_fs root_fs;

void __attribute__((noreturn))
x86_64_kernel_main(const struct boot_info *boot_info)
{
    struct minix_inode init_inode;
    unsigned long free;

    serial_write("x86-64 kernel stack installed\r\n");
    serial_write("x86-64 kernel main reached\r\n");
    serial_write("x86-64 page tables installed\r\n");

    serial_write("x86-64 descriptor tables installed\r\n");

    serial_write("main memory start: ");
    serial_write_hex64(boot_info->mem_start);
    serial_write("\r\nmain memory end: ");
    serial_write_hex64(boot_info->mem_end);
    serial_write("\r\nmain memory pages: ");
    serial_write_uint64((boot_info->mem_end -
                         boot_info->mem_start) >> 12);
    serial_write("\r\n");

    if (boot_info->mem_end - boot_info->mem_start <= RAMDISK_SIZE)
        panic("not enough memory for ramdisk");
    if (rd_init(&root_ramdisk, phys_to_virt(boot_info->mem_start),
                RAMDISK_SIZE))
        panic("cannot reserve ramdisk");
    mem_init(boot_info->mem_start + RAMDISK_SIZE, boot_info->mem_end);
    free = nr_free_pages();
    serial_write("free memory pages: ");
    serial_write_uint64(free);
    serial_write("\r\n");

    if (hd_init(&root_disk, 1) ||
        rd_load(&root_ramdisk, hd_read, &root_disk))
        panic("cannot load Minix root disk");
    serial_write("ramdisk image bytes: ");
    serial_write_uint64(root_ramdisk.length);
    serial_write("\r\n");
    if (minix_mount(&root_fs, rd_read, &root_ramdisk))
        panic("cannot mount Minix root");
    if (minix_lookup(&root_fs, "/bin/init", &init_inode))
        panic("cannot find /bin/init");
    serial_write("Minix root mounted; /bin/init bytes: ");
    serial_write_uint64(init_inode.size);
    serial_write("\r\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
