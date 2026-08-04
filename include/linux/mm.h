#ifndef _MM_H
#define _MM_H

#define PAGE_SIZE 4096

extern void mem_init(unsigned long start_mem, unsigned long end_mem);
extern unsigned long nr_free_pages(void);
extern unsigned long get_free_page(void);
extern unsigned long put_page(unsigned long page,unsigned long address);
extern void free_page(unsigned long addr);
extern void get_empty_page(unsigned long addr);
extern int resolve_addr(unsigned long va, unsigned long *pa);
extern int split_large_page(unsigned long va);

#endif
