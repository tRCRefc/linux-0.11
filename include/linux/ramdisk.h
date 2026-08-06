#ifndef _LINUX_RAMDISK_H
#define _LINUX_RAMDISK_H

#include <linux/minix.h>

struct ramdisk {
    unsigned char *start;
    unsigned long length;
    unsigned long capacity;
};

extern int rd_init(struct ramdisk *rd, void *start,
                   unsigned long length) __attribute__((visibility("hidden")));
extern int rd_load(struct ramdisk *rd, minix_read_block_t read_block,
                   void *context) __attribute__((visibility("hidden")));
extern int rd_read(void *context, unsigned long block, void *buffer)
    __attribute__((visibility("hidden")));

#endif
