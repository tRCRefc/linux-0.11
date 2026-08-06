/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#define PAGING_MEMORY (15UL * 1024 * 1024)
#define PAGING_PAGES (PAGING_MEMORY / PAGE_SIZE)
#define USED 100

#define PAGE_PRESENT 0x001UL
#define PAGE_SIZE_FLAG 0x080UL
#define PAGE_TABLE_ADDR_MASK 0x000ffffffffff000UL
#define LARGE_PAGE_ADDR_MASK 0x000fffffffe00000UL
#define LARGE_PAGE_OFFSET 0x001fffffUL
#define PAGE_WRITE 0x002UL
#define PAGE_USER 0x004UL
#define PAGE_TABLE_ENTRIES 512UL

#define PAGE_FAULT_WRITE 0x002UL
#define PAGE_FAULT_USER 0x004UL

static unsigned long low_mem;
static unsigned long high_mem;
static unsigned char mem_map[PAGING_PAGES];

static void bad_page(unsigned long error, const char *message)
{
    if (error & PAGE_FAULT_USER)
        do_exit(SIGSEGV);
    panic(message);
}

static void oom(unsigned long error)
{
    bad_page(error, "out of memory");
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
    unsigned long i;
    unsigned long pages;

    low_mem = start_mem;
    high_mem = end_mem;

    for (i = 0; i < PAGING_PAGES; ++i)
        mem_map[i] = USED;

    pages = (high_mem - low_mem) / PAGE_SIZE;
    for (i = 0; i < pages; ++i)
        mem_map[i] = 0;
}

unsigned long nr_free_pages(void)
{
    unsigned long i;
    unsigned long free = 0;

    for (i = 0; i < PAGING_PAGES; ++i) {
        if (mem_map[i] == 0)
            ++free;
    }

    return free;
}

unsigned long get_free_page(void)
{
    unsigned long index = PAGING_PAGES;

    while (index > 0) {
        unsigned long *word;
        unsigned long page;
        unsigned long words;

        --index;
        if (mem_map[index] != 0)
            continue;

        mem_map[index] = 1;
        page = low_mem + index * PAGE_SIZE;
        word = phys_to_virt(page);
        words = PAGE_SIZE / sizeof(*word);
        while (words-- > 0)
            *word++ = 0;

        return page;
    }

    return 0;
}

void free_page(unsigned long address)
{
    unsigned long index;

    if (address < low_mem)
        return;
    if (address >= high_mem ||
        (address & (PAGE_SIZE - 1)) != 0)
        panic("trying to free nonexistent page");

    index = (address - low_mem) / PAGE_SIZE;
    if (mem_map[index] == 0)
        panic("trying to free free page");

    --mem_map[index];
}

unsigned long new_pg_dir(void)
{
    unsigned long cr3;
    unsigned long page;
    unsigned long *pg_dir;
    unsigned long *new_dir;

    page = get_free_page();
    if (page == 0)
        return 0;

    __asm__ volatile ("movq %%cr3, %0" : "=r" (cr3));
    pg_dir = phys_to_virt(cr3 & PAGE_TABLE_ADDR_MASK);
    new_dir = phys_to_virt(page);
    new_dir[0] = pg_dir[0];
    new_dir[256] = pg_dir[256];

    return page;
}

static void free_pt(unsigned long page)
{
    unsigned long *pt;
    unsigned long i;

    pt = phys_to_virt(page);
    for (i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        if (pt[i] & PAGE_PRESENT)
            free_page(pt[i] & PAGE_TABLE_ADDR_MASK);
    }
    free_page(page);
}

static void free_pd(unsigned long page)
{
    unsigned long *pd;
    unsigned long i;

    pd = phys_to_virt(page);
    for (i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        if (pd[i] & PAGE_PRESENT)
            free_pt(pd[i] & PAGE_TABLE_ADDR_MASK);
    }
    free_page(page);
}

static void free_pdpt(unsigned long page)
{
    unsigned long *pdpt;
    unsigned long i;

    pdpt = phys_to_virt(page);
    for (i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        if (pdpt[i] & PAGE_PRESENT)
            free_pd(pdpt[i] & PAGE_TABLE_ADDR_MASK);
    }
    free_page(page);
}

void free_pg_dir(unsigned long page)
{
    unsigned long *pg_dir;
    unsigned long i;

    pg_dir = phys_to_virt(page);
    for (i = USER_ADDRESS_START >> 39;
         i < USER_ADDRESS_LIMIT >> 39; ++i) {
        if (pg_dir[i] & PAGE_PRESENT)
            free_pdpt(pg_dir[i] & PAGE_TABLE_ADDR_MASK);
    }
    free_page(page);
}

static void invalidate(void)
{
    unsigned long cr3;

    __asm__ volatile ("movq %%cr3, %0" : "=r" (cr3));
    __asm__ volatile ("movq %0, %%cr3" :: "r" (cr3) : "memory");
}

static unsigned long copy_pt(unsigned long from)
{
    unsigned long *from_pt;
    unsigned long *to_pt;
    unsigned long to;
    unsigned long i;

    to = get_free_page();
    if (to == 0)
        return 0;

    from_pt = phys_to_virt(from);
    to_pt = phys_to_virt(to);
    for (i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        unsigned long entry;
        unsigned long page;

        entry = from_pt[i];
        if (!(entry & PAGE_PRESENT))
            continue;
        entry &= ~PAGE_WRITE;
        from_pt[i] = entry;
        to_pt[i] = entry;
        page = entry & PAGE_TABLE_ADDR_MASK;
        if (page >= low_mem)
            ++mem_map[(page - low_mem) / PAGE_SIZE];
    }
    return to;
}

static unsigned long copy_pd(unsigned long from)
{
    unsigned long *from_pd;
    unsigned long *to_pd;
    unsigned long to;
    unsigned long i;

    to = get_free_page();
    if (to == 0)
        return 0;

    from_pd = phys_to_virt(from);
    to_pd = phys_to_virt(to);
    for (i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        unsigned long entry;
        unsigned long pt;

        entry = from_pd[i];
        if (!(entry & PAGE_PRESENT))
            continue;
        pt = copy_pt(entry & PAGE_TABLE_ADDR_MASK);
        if (pt == 0) {
            free_pd(to);
            return 0;
        }
        to_pd[i] = pt | (entry & ~PAGE_TABLE_ADDR_MASK);
    }
    return to;
}

static unsigned long copy_pdpt(unsigned long from)
{
    unsigned long *from_pdpt;
    unsigned long *to_pdpt;
    unsigned long to;
    unsigned long i;

    to = get_free_page();
    if (to == 0)
        return 0;

    from_pdpt = phys_to_virt(from);
    to_pdpt = phys_to_virt(to);
    for (i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        unsigned long entry;
        unsigned long pd;

        entry = from_pdpt[i];
        if (!(entry & PAGE_PRESENT))
            continue;
        pd = copy_pd(entry & PAGE_TABLE_ADDR_MASK);
        if (pd == 0) {
            free_pdpt(to);
            return 0;
        }
        to_pdpt[i] = pd | (entry & ~PAGE_TABLE_ADDR_MASK);
    }
    return to;
}

unsigned long copy_pg_dir(unsigned long from)
{
    unsigned long *from_dir;
    unsigned long *to_dir;
    unsigned long to;
    unsigned long i;

    to = get_free_page();
    if (to == 0)
        return 0;

    from_dir = phys_to_virt(from);
    to_dir = phys_to_virt(to);
    to_dir[0] = from_dir[0];
    to_dir[256] = from_dir[256];
    for (i = USER_ADDRESS_START >> 39;
         i < USER_ADDRESS_LIMIT >> 39; ++i) {
        unsigned long entry;
        unsigned long pdpt;

        entry = from_dir[i];
        if (!(entry & PAGE_PRESENT))
            continue;
        pdpt = copy_pdpt(entry & PAGE_TABLE_ADDR_MASK);
        if (pdpt == 0) {
            free_pg_dir(to);
            invalidate();
            return 0;
        }
        to_dir[i] = pdpt | (entry & ~PAGE_TABLE_ADDR_MASK);
    }
    invalidate();
    return to;
}

unsigned long switch_pg_dir(unsigned long page)
{
    unsigned long old;

    __asm__ volatile ("movq %%cr3, %0" : "=r" (old));
    __asm__ volatile ("movq %0, %%cr3" :: "r" (page) : "memory");
    return old;
}

void do_wp_page(unsigned long error, unsigned long addr)
{
    unsigned long cr3;
    unsigned long *pml4;
    unsigned long *pdpt;
    unsigned long *pd;
    unsigned long *pt;
    unsigned long old_page;
    unsigned long new_page;
    unsigned long *from;
    unsigned long *to;
    unsigned long words;

    if (!(error & PAGE_FAULT_WRITE))
        bad_page(error, "unexpected page protection fault");
    if (addr < USER_ADDRESS_START || addr >= USER_ADDRESS_LIMIT)
        bad_page(error, "page fault outside user memory");
    __asm__ volatile ("movq %%cr3, %0" : "=r" (cr3));
    pml4 = phys_to_virt(cr3 & PAGE_TABLE_ADDR_MASK);
    pdpt = phys_to_virt(pml4[(addr >> 39) & 0x1ffUL] &
                        PAGE_TABLE_ADDR_MASK);
    pd = phys_to_virt(pdpt[(addr >> 30) & 0x1ffUL] &
                      PAGE_TABLE_ADDR_MASK);
    pt = phys_to_virt(pd[(addr >> 21) & 0x1ffUL] &
                      PAGE_TABLE_ADDR_MASK);
    pt += (addr >> 12) & 0x1ffUL;
    old_page = *pt & PAGE_TABLE_ADDR_MASK;

    if (old_page >= low_mem &&
        mem_map[(old_page - low_mem) / PAGE_SIZE] == 1) {
        *pt |= PAGE_WRITE;
        invalidate();
        return;
    }

    new_page = get_free_page();
    if (new_page == 0)
        oom(error);
    from = phys_to_virt(old_page);
    to = phys_to_virt(new_page);
    words = PAGE_SIZE / sizeof(*from);
    while (words-- > 0)
        *to++ = *from++;

    if (old_page >= low_mem)
        --mem_map[(old_page - low_mem) / PAGE_SIZE];
    *pt = new_page | ((*pt & ~PAGE_TABLE_ADDR_MASK) | PAGE_WRITE);
    invalidate();
}

void do_no_page(unsigned long error, unsigned long addr)
{
    unsigned long cr3;
    unsigned long page;

    if (addr < USER_ADDRESS_START || addr >= USER_ADDRESS_LIMIT)
        bad_page(error, "page fault outside user memory");
    addr &= ~(PAGE_SIZE - 1UL);

    page = get_free_page();
    if (page == 0)
        oom(error);

    __asm__ volatile ("movq %%cr3, %0" : "=r" (cr3));
    if (put_user_page(cr3 & PAGE_TABLE_ADDR_MASK, page, addr) == 0) {
        free_page(page);
        oom(error);
    }
}

unsigned long put_user_page(unsigned long pg_dir, unsigned long page,
                            unsigned long addr)
{
    unsigned long pdpt_page = 0;
    unsigned long pd_page = 0;
    unsigned long pt_page = 0;
    unsigned long *pml4;
    unsigned long *pdpt;
    unsigned long *pd;
    unsigned long *pt;
    unsigned long entry;

    pml4 = phys_to_virt(pg_dir);
    entry = pml4[(addr >> 39) & 0x1ffUL];
    if (entry & PAGE_PRESENT) {
        pdpt = phys_to_virt(entry & PAGE_TABLE_ADDR_MASK);
    } else {
        pdpt_page = get_free_page();
        if (pdpt_page == 0)
            return 0;
        pdpt = phys_to_virt(pdpt_page);
    }

    entry = pdpt[(addr >> 30) & 0x1ffUL];
    if (entry & PAGE_PRESENT) {
        pd = phys_to_virt(entry & PAGE_TABLE_ADDR_MASK);
    } else {
        pd_page = get_free_page();
        if (pd_page == 0)
            goto no_memory;
        pd = phys_to_virt(pd_page);
    }

    entry = pd[(addr >> 21) & 0x1ffUL];
    if (entry & PAGE_PRESENT) {
        pt = phys_to_virt(entry & PAGE_TABLE_ADDR_MASK);
    } else {
        pt_page = get_free_page();
        if (pt_page == 0)
            goto no_memory;
        pt = phys_to_virt(pt_page);
    }

    pt[(addr >> 12) & 0x1ffUL] =
        (page & PAGE_TABLE_ADDR_MASK) |
        PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    if (pt_page != 0)
        pd[(addr >> 21) & 0x1ffUL] =
            pt_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    if (pd_page != 0)
        pdpt[(addr >> 30) & 0x1ffUL] =
            pd_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    if (pdpt_page != 0)
        pml4[(addr >> 39) & 0x1ffUL] =
            pdpt_page | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

    return page;

no_memory:
    if (pt_page != 0)
        free_page(pt_page);
    if (pd_page != 0)
        free_page(pd_page);
    if (pdpt_page != 0)
        free_page(pdpt_page);
    return 0;
}

int resolve_addr(unsigned long va, unsigned long *pa)
{
    unsigned long cr3;
    unsigned long *pml4;
    unsigned long *pdpt;
    unsigned long *pd;
    unsigned long *pt;
    unsigned long pml4e;
    unsigned long pdpte;
    unsigned long pde;
    unsigned long pte;

    __asm__ volatile ("movq %%cr3, %0" : "=r" (cr3));

    pml4 = phys_to_virt(cr3 & PAGE_TABLE_ADDR_MASK);
    pml4e = pml4[(va >> 39) & 0x1ffUL];
    if (!(pml4e & PAGE_PRESENT)) return 0;

    pdpt = phys_to_virt(pml4e & PAGE_TABLE_ADDR_MASK);
    pdpte = pdpt[(va >> 30) & 0x1ffUL];
    if (!(pdpte & PAGE_PRESENT)) return 0;
    if (pdpte & PAGE_SIZE_FLAG) return 0;

    pd = phys_to_virt(pdpte & PAGE_TABLE_ADDR_MASK);
    pde = pd[(va >> 21) & 0x1ffUL];
    if (!(pde & PAGE_PRESENT)) return 0;
    if (pde & PAGE_SIZE_FLAG) {
        *pa = ((pde & LARGE_PAGE_ADDR_MASK) | (va & LARGE_PAGE_OFFSET));
        return 1;
    } else {
        pt = phys_to_virt(pde & PAGE_TABLE_ADDR_MASK);
        pte = pt[(va >> 12) & 0x1ffUL];
        if (!(pte & PAGE_PRESENT)) return 0;

        *pa = ((pte & PAGE_TABLE_ADDR_MASK) | (va & (PAGE_SIZE - 1UL)));
        return 1;
    }
}

int split_large_page(unsigned long va)
{
    unsigned long cr3;
    unsigned long *pml4;
    unsigned long *pdpt;
    unsigned long *pd;

    unsigned long pml4e;
    unsigned long pdpte;
    unsigned long pde;
    unsigned long flush_addr;

    __asm__ volatile ("movq %%cr3, %0" : "=r" (cr3));

    pml4 = phys_to_virt(cr3 & PAGE_TABLE_ADDR_MASK);
    pml4e = pml4[(va >> 39) & 0x1ffUL];
    if (!(pml4e & PAGE_PRESENT)) return 0;

    pdpt = phys_to_virt(pml4e & PAGE_TABLE_ADDR_MASK);
    pdpte = pdpt[(va >> 30) & 0x1ffUL];
    if (!(pdpte & PAGE_PRESENT)) return 0;
    if (pdpte & PAGE_SIZE_FLAG) return 0;

    pd = phys_to_virt(pdpte & PAGE_TABLE_ADDR_MASK);
    pde = pd[(va >> 21) & 0x1ffUL];
    if (!(pde & PAGE_PRESENT)) return 0;
    if (!(pde & PAGE_SIZE_FLAG)) return 1;

    unsigned long base_addr;
    unsigned long pt_page;
    unsigned long *pt;

    base_addr = pde & LARGE_PAGE_ADDR_MASK;
    pt_page = get_free_page();
    if (pt_page == 0) return 0;
    pt = phys_to_virt(pt_page);

    unsigned long i;

    for (i = 0; i < PAGE_TABLE_ENTRIES; ++i){
        pt[i] = (base_addr + i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
    }

    pd[(va >> 21) & 0x1ffUL] =
        pt_page | PAGE_PRESENT | PAGE_WRITE;

    flush_addr = va & ~LARGE_PAGE_OFFSET;
    __asm__ volatile ("invlpg (%0)" :: "r" (flush_addr) : "memory");

    return 1;
}

unsigned long put_page(unsigned long page, unsigned long addr)
{
    unsigned long cr3;
    unsigned long *pml4;
    unsigned long *pdpt;
    unsigned long *pd;
    unsigned long *pt;

    unsigned long pml4e;
    unsigned long pdpte;
    unsigned long pde;
    unsigned long flush_addr;

    __asm__ volatile ("movq %%cr3, %0" : "=r" (cr3));

    pml4 = phys_to_virt(cr3 & PAGE_TABLE_ADDR_MASK);
    pml4e = pml4[(addr >> 39) & 0x1ffUL];
    if (!(pml4e & PAGE_PRESENT)) return 0;

    pdpt = phys_to_virt(pml4e & PAGE_TABLE_ADDR_MASK);
    pdpte = pdpt[(addr >> 30) & 0x1ffUL];
    if (!(pdpte & PAGE_PRESENT)) return 0;
    if (pdpte & PAGE_SIZE_FLAG) return 0;

    pd = phys_to_virt(pdpte & PAGE_TABLE_ADDR_MASK);
    pde = pd[(addr >> 21) & 0x1ffUL];
    if (!(pde & PAGE_PRESENT)) return 0;

    if (pde & PAGE_SIZE_FLAG) {
        if (!split_large_page(addr)) return 0;

        pde = pd[(addr >> 21) & 0x1ffUL];
        if (!(pde & PAGE_PRESENT)) return 0;
        if (pde & PAGE_SIZE_FLAG) return 0;
    }

    pt = phys_to_virt(pde & PAGE_TABLE_ADDR_MASK);
    pt[(addr >> 12) & 0x1ffUL] =
        (page & PAGE_TABLE_ADDR_MASK) | PAGE_PRESENT | PAGE_WRITE;

    flush_addr = addr & ~(PAGE_SIZE - 1UL);
    __asm__ volatile ("invlpg (%0)" :: "r" (flush_addr) : "memory");

    return page;
}

void get_empty_page(unsigned long addr)
{
    unsigned long page;
    page = get_free_page();

    if (page == 0) {
        panic("out of memory");
    }

    if (put_page(page, addr) == 0) {
        free_page(page);
        panic("out of memory");
    }
}
