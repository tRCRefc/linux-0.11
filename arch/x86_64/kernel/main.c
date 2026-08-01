#include <asm/boot.h>
#include <asm/serial.h>

void __attribute__((noreturn))
x86_64_kernel_main(const struct boot_info *boot_info)
{
    serial_write("x86-64 kernel stack installed\r\n");
    serial_write("x86-64 kernel main reached\r\n");
    serial_write("x86-64 page tables installed\r\n");

    serial_write("x86-64 descriptor tables installed\r\n");

    if (boot_info == 0 || boot_info->memory_map == 0 ||
        boot_info->memory_descriptor_size == 0) {
        serial_write("invalid x86-64 boot info\r\n");
    } else {
        serial_write("boot memory map entries: ");
        serial_write_uint64(boot_info->memory_map_size /
                            boot_info->memory_descriptor_size);
        serial_write("\r\n");
    }

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
