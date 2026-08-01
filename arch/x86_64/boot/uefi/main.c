#include "efi.h"

#define COM1_PORT 0x3f8

static efi_char16_t console_message[] = {
    'L', 'i', 'n', 'u', 'x', ' ', '0', '.', '1', '1', ' ',
    'x', '8', '6', '-', '6', '4', ':', ' ',
    'U', 'E', 'F', 'I', ' ', 'c', 'o', 'n', 's', 'o', 'l', 'e', ' ',
    'i', 's', ' ', 'a', 'v', 'a', 'i', 'l', 'a', 'b', 'l', 'e',
    '\r', '\n', 0
};

static inline void outb(efi_uint16_t port, efi_uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline efi_uint8_t inb(efi_uint16_t port)
{
    efi_uint8_t value;

    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void)
{
    outb((efi_uint16_t)(COM1_PORT + 1), 0x00);
    outb((efi_uint16_t)(COM1_PORT + 3), 0x80);
    outb((efi_uint16_t)(COM1_PORT + 0), 0x03);
    outb((efi_uint16_t)(COM1_PORT + 1), 0x00);
    outb((efi_uint16_t)(COM1_PORT + 3), 0x03);
    outb((efi_uint16_t)(COM1_PORT + 2), 0xc7);
    outb((efi_uint16_t)(COM1_PORT + 4), 0x0b);
}

static int serial_transmit_ready(void)
{
    efi_uint32_t attempts;

    for (attempts = 0; attempts < 1000000; ++attempts) {
        if ((inb((efi_uint16_t)(COM1_PORT + 5)) & 0x20) != 0)
            return 1;
    }

    return 0;
}

static void serial_putc(char value)
{
    if (serial_transmit_ready())
        outb((efi_uint16_t)COM1_PORT, (efi_uint8_t)value);
}

static void serial_write(const char *text)
{
    while (*text != '\0') {
        serial_putc(*text);
        ++text;
    }
}

efi_status_t EFIAPI efi_main(efi_handle_t image_handle,
                             struct efi_system_table *system_table)
{
    (void)image_handle;

    serial_init();
    serial_write("Linux 0.11 x86-64: efi_main reached\r\n");

    if (system_table != 0 && system_table->con_out != 0) {
        system_table->con_out->output_string(system_table->con_out,
                                             console_message);
    }

    return EFI_SUCCESS;
}
