#include <asm/serial.h>

#define COM1_PORT 0x3f8

static inline void outb(__UINT16_TYPE__ port, __UINT8_TYPE__ value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline __UINT8_TYPE__ inb(__UINT16_TYPE__ port)
{
    __UINT8_TYPE__ value;

    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void)
{
    outb((__UINT16_TYPE__)(COM1_PORT + 1), 0x00);
    outb((__UINT16_TYPE__)(COM1_PORT + 3), 0x80);
    outb((__UINT16_TYPE__)(COM1_PORT + 0), 0x03);
    outb((__UINT16_TYPE__)(COM1_PORT + 1), 0x00);
    outb((__UINT16_TYPE__)(COM1_PORT + 3), 0x03);
    outb((__UINT16_TYPE__)(COM1_PORT + 2), 0xc7);
    outb((__UINT16_TYPE__)(COM1_PORT + 4), 0x0b);
}

static int serial_transmit_ready(void)
{
    __UINT32_TYPE__ attempts;

    for (attempts = 0; attempts < 1000000; ++attempts) {
        if ((inb((__UINT16_TYPE__)(COM1_PORT + 5)) & 0x20) != 0)
            return 1;
    }

    return 0;
}

static void serial_putc(char value)
{
    if (serial_transmit_ready())
        outb((__UINT16_TYPE__)COM1_PORT, (__UINT8_TYPE__)value);
}

void serial_write(const char *text)
{
    while (*text != '\0') {
        serial_putc(*text);
        ++text;
    }
}

void serial_write_uint64(__UINT64_TYPE__ value)
{
    char digits[20];
    __UINTPTR_TYPE__ count = 0;

    if (value == 0) {
        serial_putc('0');
        return;
    }

    while (value != 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }

    while (count != 0)
        serial_putc(digits[--count]);
}

void serial_write_hex64(__UINT64_TYPE__ value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    serial_write("0x");
    for (shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xf]);
}
