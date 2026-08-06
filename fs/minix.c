#include <errno.h>
#include <linux/minix.h>

#define MINIX_INODE_SIZE 32UL
#define MINIX_INODES_PER_BLOCK (MINIX_BLOCK_SIZE / MINIX_INODE_SIZE)
#define MINIX_DIR_ENTRY_SIZE 16UL
#define MINIX_INDIRECT_ENTRIES (MINIX_BLOCK_SIZE / 2UL)
#define MINIX_S_IFMT 0170000U
#define MINIX_S_IFDIR 0040000U

static __UINT16_TYPE__ get_le16(const unsigned char *p)
{
    return (__UINT16_TYPE__)(p[0] | ((__UINT16_TYPE__)p[1] << 8));
}

static __UINT32_TYPE__ get_le32(const unsigned char *p)
{
    return (__UINT32_TYPE__)p[0] |
           ((__UINT32_TYPE__)p[1] << 8) |
           ((__UINT32_TYPE__)p[2] << 16) |
           ((__UINT32_TYPE__)p[3] << 24);
}

static int read_block(struct minix_fs *fs, unsigned long block,
                      unsigned char *buffer)
{
    if (block >= fs->nzones)
        return -EIO;
    if (fs->read_block(fs->context, block, buffer) < 0)
        return -EIO;
    return 0;
}

int minix_mount(struct minix_fs *fs, minix_read_block_t read,
                void *context)
{
    unsigned char block[MINIX_BLOCK_SIZE];
    unsigned long inode_blocks;

    if (!fs || !read)
        return -EINVAL;
    fs->read_block = read;
    fs->context = context;
    if (read(context, 1, block) < 0)
        return -EIO;

    fs->ninodes = get_le16(block + 0);
    fs->nzones = get_le16(block + 2);
    fs->imap_blocks = get_le16(block + 4);
    fs->zmap_blocks = get_le16(block + 6);
    fs->firstdatazone = get_le16(block + 8);
    if (get_le16(block + 10) != 0)
        return -EINVAL;
    fs->max_size = get_le32(block + 12);
    if (get_le16(block + 16) != MINIX_SUPER_MAGIC)
        return -EINVAL;
    if (!fs->ninodes || !fs->nzones || !fs->imap_blocks ||
        !fs->zmap_blocks || !fs->max_size ||
        fs->firstdatazone >= fs->nzones)
        return -EINVAL;
    inode_blocks = (fs->ninodes + MINIX_INODES_PER_BLOCK - 1) /
                   MINIX_INODES_PER_BLOCK;
    if (2UL + fs->imap_blocks + fs->zmap_blocks + inode_blocks >
        fs->firstdatazone)
        return -EINVAL;
    return 0;
}

int minix_iget(struct minix_fs *fs, unsigned int ino,
               struct minix_inode *inode)
{
    unsigned char block[MINIX_BLOCK_SIZE];
    const unsigned char *raw;
    unsigned long inode_block;
    unsigned long offset;
    int i;

    if (!fs || !inode || ino == 0 || ino > fs->ninodes)
        return -EINVAL;
    inode_block = 2UL + fs->imap_blocks + fs->zmap_blocks +
                  (ino - 1UL) / MINIX_INODES_PER_BLOCK;
    if (read_block(fs, inode_block, block) < 0)
        return -EIO;
    offset = ((ino - 1UL) % MINIX_INODES_PER_BLOCK) * MINIX_INODE_SIZE;
    raw = block + offset;
    inode->mode = get_le16(raw + 0);
    inode->uid = get_le16(raw + 2);
    inode->size = get_le32(raw + 4);
    inode->time = get_le32(raw + 8);
    inode->gid = raw[12];
    inode->nlinks = raw[13];
    for (i = 0; i < 9; ++i)
        inode->zone[i] = get_le16(raw + 14 + i * 2);
    return 0;
}

static int mapped_block(struct minix_fs *fs,
                        const struct minix_inode *inode,
                        unsigned long file_block,
                        unsigned long *disk_block)
{
    unsigned char block[MINIX_BLOCK_SIZE];
    unsigned long index;
    unsigned long zone;

    if (file_block < 7) {
        *disk_block = inode->zone[file_block];
        return *disk_block && *disk_block < fs->firstdatazone ? -EIO : 0;
    }
    index = file_block - 7;
    if (index < MINIX_INDIRECT_ENTRIES) {
        if (!inode->zone[7]) {
            *disk_block = 0;
            return 0;
        }
        if (inode->zone[7] < fs->firstdatazone)
            return -EIO;
        if (read_block(fs, inode->zone[7], block) < 0)
            return -EIO;
        *disk_block = get_le16(block + index * 2);
        return *disk_block && *disk_block < fs->firstdatazone ? -EIO : 0;
    }

    index -= MINIX_INDIRECT_ENTRIES;
    if (index >= MINIX_INDIRECT_ENTRIES * MINIX_INDIRECT_ENTRIES)
        return -EFBIG;
    if (!inode->zone[8]) {
        *disk_block = 0;
        return 0;
    }
    if (inode->zone[8] < fs->firstdatazone)
        return -EIO;
    if (read_block(fs, inode->zone[8], block) < 0)
        return -EIO;
    zone = get_le16(block + (index / MINIX_INDIRECT_ENTRIES) * 2);
    if (!zone) {
        *disk_block = 0;
        return 0;
    }
    if (zone < fs->firstdatazone)
        return -EIO;
    if (read_block(fs, zone, block) < 0)
        return -EIO;
    *disk_block = get_le16(block +
                           (index % MINIX_INDIRECT_ENTRIES) * 2);
    return *disk_block && *disk_block < fs->firstdatazone ? -EIO : 0;
}

long minix_read(struct minix_fs *fs, const struct minix_inode *inode,
                unsigned long offset, void *buffer, unsigned long count)
{
    unsigned char block[MINIX_BLOCK_SIZE];
    unsigned char *to;
    unsigned long available;
    unsigned long copied;

    if (!fs || !inode || (!buffer && count))
        return -EINVAL;
    if (offset >= inode->size)
        return 0;
    available = inode->size - offset;
    if (count > available)
        count = available;
    to = buffer;
    copied = 0;
    while (copied < count) {
        unsigned long disk_block;
        unsigned long in_block;
        unsigned long bytes;
        int error;

        error = mapped_block(fs, inode, offset / MINIX_BLOCK_SIZE,
                             &disk_block);
        if (error)
            return error;
        in_block = offset % MINIX_BLOCK_SIZE;
        bytes = MINIX_BLOCK_SIZE - in_block;
        if (bytes > count - copied)
            bytes = count - copied;
        if (disk_block) {
            if (read_block(fs, disk_block, block) < 0)
                return -EIO;
            while (bytes-- > 0)
                to[copied++] = block[in_block++];
        } else {
            while (bytes-- > 0)
                to[copied++] = 0;
        }
        offset = offset - offset % MINIX_BLOCK_SIZE + MINIX_BLOCK_SIZE;
    }
    return (long)copied;
}

static int same_name(const unsigned char *entry, const char *name,
                     unsigned long length)
{
    unsigned long i;

    if (length > MINIX_NAME_LEN)
        return 0;
    for (i = 0; i < length; ++i)
        if (entry[2 + i] != (unsigned char)name[i])
            return 0;
    if (length < MINIX_NAME_LEN && entry[2 + length] != 0)
        return 0;
    return 1;
}

static int find_inode(struct minix_fs *fs, const struct minix_inode *dir,
                      const char *name, unsigned long length,
                      unsigned int *ino)
{
    unsigned char entry[MINIX_DIR_ENTRY_SIZE];
    unsigned long offset;
    long bytes;

    if ((dir->mode & MINIX_S_IFMT) != MINIX_S_IFDIR)
        return -ENOTDIR;
    for (offset = 0; offset + MINIX_DIR_ENTRY_SIZE <= dir->size;
         offset += MINIX_DIR_ENTRY_SIZE) {
        bytes = minix_read(fs, dir, offset, entry, sizeof(entry));
        if (bytes != (long)sizeof(entry))
            return bytes < 0 ? (int)bytes : -EIO;
        *ino = get_le16(entry);
        if (*ino && same_name(entry, name, length))
            return 0;
    }
    return -ENOENT;
}

int minix_lookup(struct minix_fs *fs, const char *path,
                 struct minix_inode *inode)
{
    const char *component;
    unsigned long length;
    unsigned int ino;
    int error;

    if (!fs || !path || !inode)
        return -EINVAL;
    while (*path == '/')
        ++path;
    ino = MINIX_ROOT_INO;
    error = minix_iget(fs, ino, inode);
    if (error)
        return error;
    while (*path) {
        component = path;
        length = 0;
        while (path[length] && path[length] != '/')
            ++length;
        if (length > MINIX_NAME_LEN)
            return -ENAMETOOLONG;
        error = find_inode(fs, inode, component, length, &ino);
        if (error)
            return error;
        error = minix_iget(fs, ino, inode);
        if (error)
            return error;
        path += length;
        while (*path == '/')
            ++path;
    }
    return 0;
}
