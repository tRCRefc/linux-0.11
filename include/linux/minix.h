#ifndef _LINUX_MINIX_H
#define _LINUX_MINIX_H

#define MINIX_BLOCK_SIZE 1024UL
#define MINIX_NAME_LEN 14UL
#define MINIX_ROOT_INO 1U
#define MINIX_SUPER_MAGIC 0x137fU

typedef int (*minix_read_block_t)(void *context, unsigned long block,
                                  void *buffer);

struct minix_inode {
    __UINT16_TYPE__ mode;
    __UINT16_TYPE__ uid;
    __UINT32_TYPE__ size;
    __UINT32_TYPE__ time;
    __UINT8_TYPE__ gid;
    __UINT8_TYPE__ nlinks;
    __UINT16_TYPE__ zone[9];
};

struct minix_fs {
    minix_read_block_t read_block;
    void *context;
    __UINT16_TYPE__ ninodes;
    __UINT16_TYPE__ nzones;
    __UINT16_TYPE__ imap_blocks;
    __UINT16_TYPE__ zmap_blocks;
    __UINT16_TYPE__ firstdatazone;
    __UINT32_TYPE__ max_size;
};

extern int minix_mount(struct minix_fs *fs, minix_read_block_t read_block,
                       void *context);
extern int minix_iget(struct minix_fs *fs, unsigned int ino,
                      struct minix_inode *inode);
extern int minix_lookup(struct minix_fs *fs, const char *path,
                        struct minix_inode *inode);
extern long minix_read(struct minix_fs *fs, const struct minix_inode *inode,
                       unsigned long offset, void *buffer,
                       unsigned long count);

#endif
