#ifndef _LINUX_HD_H
#define _LINUX_HD_H

struct hard_disk {
    unsigned int drive;
};

extern int hd_init(struct hard_disk *disk, unsigned int drive)
    __attribute__((visibility("hidden")));
extern int hd_read(void *context, unsigned long block, void *buffer)
    __attribute__((visibility("hidden")));

#endif
