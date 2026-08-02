#ifndef X86_64_ASM_BOOT_H
#define X86_64_ASM_BOOT_H

struct boot_info {
    __UINT64_TYPE__ memory_start;
    __UINT64_TYPE__ memory_end;
};

_Static_assert(sizeof(struct boot_info) == 16,
               "unexpected x86-64 boot info layout");
_Static_assert(__builtin_offsetof(struct boot_info, memory_end) == 8,
               "unexpected x86-64 memory end offset");

#endif
