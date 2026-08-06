#ifndef _LINUX_RAMDISK_H
#define _LINUX_RAMDISK_H

struct ramdisk {
    const unsigned char *start;
    unsigned long length;
};

extern int rd_init(struct ramdisk *rd, const void *start,
                   unsigned long length) __attribute__((visibility("hidden")));
extern int rd_read(void *context, unsigned long block, void *buffer)
    __attribute__((visibility("hidden")));

#endif
