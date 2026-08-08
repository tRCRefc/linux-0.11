#ifndef _LINUX_ELF_H
#define _LINUX_ELF_H

#define EI_NIDENT 16

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_X86_64 62

#define PT_LOAD 1

#define PF_X 1
#define PF_W 2
#define PF_R 4

struct elf64_ehdr {
    __UINT8_TYPE__ ident[EI_NIDENT];
    __UINT16_TYPE__ type;
    __UINT16_TYPE__ machine;
    __UINT32_TYPE__ version;
    __UINT64_TYPE__ entry;
    __UINT64_TYPE__ phoff;
    __UINT64_TYPE__ shoff;
    __UINT32_TYPE__ flags;
    __UINT16_TYPE__ ehsize;
    __UINT16_TYPE__ phentsize;
    __UINT16_TYPE__ phnum;
    __UINT16_TYPE__ shentsize;
    __UINT16_TYPE__ shnum;
    __UINT16_TYPE__ shstrndx;
};

struct elf64_phdr {
    __UINT32_TYPE__ type;
    __UINT32_TYPE__ flags;
    __UINT64_TYPE__ offset;
    __UINT64_TYPE__ vaddr;
    __UINT64_TYPE__ paddr;
    __UINT64_TYPE__ filesz;
    __UINT64_TYPE__ memsz;
    __UINT64_TYPE__ align;
};

#endif
