#ifndef X86_64_ASM_BOOT_H
#define X86_64_ASM_BOOT_H

struct boot_info {
    void *memory_map;
    __UINT64_TYPE__ memory_map_size;
    __UINT64_TYPE__ memory_descriptor_size;
    __UINT32_TYPE__ memory_descriptor_version;
};

_Static_assert(sizeof(struct boot_info) == 32,
               "unexpected x86-64 boot info layout");

#endif
