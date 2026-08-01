#ifndef X86_64_ASM_SERIAL_H
#define X86_64_ASM_SERIAL_H

void serial_init(void);
void serial_write(const char *text);
void serial_write_uint64(__UINT64_TYPE__ value);
void serial_write_hex64(__UINT64_TYPE__ value);

#endif
