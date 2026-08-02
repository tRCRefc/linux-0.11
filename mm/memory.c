/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/mm.h>

#define PAGING_MEMORY (15UL * 1024 * 1024)
#define PAGING_PAGES (PAGING_MEMORY / PAGE_SIZE)
#define USED 100

static unsigned long low_memory;
static unsigned long high_memory;
static unsigned char mem_map[PAGING_PAGES];

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
    unsigned long i;
    unsigned long pages;

    low_memory = start_mem;
    high_memory = end_mem;

    for (i = 0; i < PAGING_PAGES; ++i)
        mem_map[i] = USED;

    pages = (high_memory - low_memory) / PAGE_SIZE;
    for (i = 0; i < pages; ++i)
        mem_map[i] = 0;
}

unsigned long nr_free_pages(void)
{
    unsigned long i;
    unsigned long free_pages = 0;

    for (i = 0; i < PAGING_PAGES; ++i) {
        if (mem_map[i] == 0)
            ++free_pages;
    }

    return free_pages;
}
