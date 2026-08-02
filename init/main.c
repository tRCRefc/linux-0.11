#include <asm/boot.h>
#include <asm/serial.h>
void __attribute__((noreturn))
x86_64_kernel_main(const struct boot_info *boot_info)
{
    serial_write("x86-64 kernel stack installed\r\n");
    serial_write("x86-64 kernel main reached\r\n");
    serial_write("x86-64 page tables installed\r\n");

    serial_write("x86-64 descriptor tables installed\r\n");

    serial_write("main memory start: ");
    serial_write_hex64(boot_info->memory_start);
    serial_write("\r\nmain memory end: ");
    serial_write_hex64(boot_info->memory_end);
    serial_write("\r\nmain memory pages: ");
    serial_write_uint64((boot_info->memory_end -
                         boot_info->memory_start) >> 12);
    serial_write("\r\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
