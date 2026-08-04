#ifndef _MM_H
#define _MM_H

#define PAGE_SIZE 4096

/* Transitional supervisor-only identity mapping. */
#define KERNEL_IDENTITY_START 0x0000000000000000UL
#define KERNEL_IDENTITY_END   0x0000000100000000UL

/* Per-address-space user mappings; the exclusive limit is not an address. */
#define USER_ADDRESS_START 0x0000008000000000UL
#define USER_ADDRESS_LIMIT 0x0000800000000000UL

/* Shared supervisor-only window covering the first 4 GiB of physical space. */
#define PHYSICAL_MEMORY_WINDOW_START 0xffff800000000000UL
#define PHYSICAL_MEMORY_WINDOW_END   0xffff800100000000UL

#define phys_to_virt(phys) ((void *)(PHYSICAL_MEMORY_WINDOW_START + (phys)))

extern void mem_init(unsigned long start_mem, unsigned long end_mem);
extern unsigned long nr_free_pages(void);
extern unsigned long get_free_page(void);
extern unsigned long new_pg_dir(void);
extern void free_pg_dir(unsigned long pg_dir);
extern unsigned long switch_pg_dir(unsigned long pg_dir);
extern unsigned long put_user_page(unsigned long pg_dir, unsigned long page,
                                   unsigned long address);
extern unsigned long put_page(unsigned long page,unsigned long address);
extern void free_page(unsigned long addr);
extern void get_empty_page(unsigned long addr);
extern int resolve_addr(unsigned long va, unsigned long *pa);
extern int split_large_page(unsigned long va);

#endif
