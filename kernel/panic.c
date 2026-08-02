/*
 *  linux/kernel/panic.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <asm/serial.h>
#include <linux/kernel.h>

void __attribute__((noreturn)) panic(const char *message)
{
    __asm__ volatile ("cli" : : : "memory");
    serial_write("Kernel panic: ");
    serial_write(message);
    serial_write("\r\n");

    for (;;)
        __asm__ volatile ("hlt");
}
