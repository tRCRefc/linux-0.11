#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <linux/minix.h>
#include <linux/ramdisk.h>

#define IMAGE_BLOCKS 64
#define ROOT_BLOCK 5
#define BIN_BLOCK 6
#define INIT_BLOCK 7
#define LARGE_FIRST_BLOCK 8
#define LARGE_INDIRECT_BLOCK 15
#define LARGE_EIGHTH_BLOCK 16
#define HUGE_DOUBLE_BLOCK 17
#define HUGE_INDIRECT_BLOCK 18
#define HUGE_DATA_BLOCK 19

static unsigned char image[IMAGE_BLOCKS][MINIX_BLOCK_SIZE];

static void put_le16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void put_le32(unsigned char *p, unsigned long value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

static void put_inode(unsigned int ino, unsigned int mode,
                      unsigned int nlinks, unsigned long size,
                      const unsigned int zones[9])
{
    unsigned char *raw;
    unsigned int i;

    raw = image[4] + (ino - 1) * 32;
    put_le16(raw + 0, mode);
    put_le32(raw + 4, size);
    raw[13] = (unsigned char)nlinks;
    for (i = 0; i < 9; ++i)
        put_le16(raw + 14 + i * 2, zones[i]);
}

static void put_dir(unsigned int block, unsigned int slot,
                    unsigned int ino, const char *name)
{
    unsigned char *entry;
    size_t length;

    entry = image[block] + slot * 16;
    put_le16(entry, ino);
    length = strlen(name);
    if (length > MINIX_NAME_LEN)
        length = MINIX_NAME_LEN;
    memcpy(entry + 2, name, length);
}

static int image_read(void *context, unsigned long block, void *buffer)
{
    unsigned long blocks = *(unsigned long *)context;

    if (block >= blocks)
        return -1;
    memcpy(buffer, image[block], MINIX_BLOCK_SIZE);
    return 0;
}

static void make_image(void)
{
    static const char init_data[] = "minix init image\n";
    unsigned int root_zones[9] = { ROOT_BLOCK };
    unsigned int bin_zones[9] = { BIN_BLOCK };
    unsigned int init_zones[9] = { INIT_BLOCK };
    unsigned int large_zones[9] = {
        8, 9, 10, 11, 12, 13, 14, LARGE_INDIRECT_BLOCK, 0
    };
    unsigned int huge_zones[9] = {
        0, 0, 0, 0, 0, 0, 0, 0, HUGE_DOUBLE_BLOCK
    };
    int i;

    memset(image, 0, sizeof(image));
    put_le16(image[1] + 0, 32);
    put_le16(image[1] + 2, IMAGE_BLOCKS);
    put_le16(image[1] + 4, 1);
    put_le16(image[1] + 6, 1);
    put_le16(image[1] + 8, ROOT_BLOCK);
    put_le32(image[1] + 12, 0x100000);
    put_le16(image[1] + 16, MINIX_SUPER_MAGIC);
    image[2][0] = 0x3f;
    image[3][0] = 0xff;
    image[3][1] = 0xff;

    put_inode(1, 0040755, 3, 5 * 16, root_zones);
    put_inode(2, 0040755, 2, 3 * 16, bin_zones);
    put_inode(3, 0100755, 1, sizeof(init_data) - 1, init_zones);
    put_inode(4, 0100644, 1, 8 * MINIX_BLOCK_SIZE, large_zones);
    put_inode(5, 0100644, 1, 520 * MINIX_BLOCK_SIZE, huge_zones);

    put_dir(ROOT_BLOCK, 0, 1, ".");
    put_dir(ROOT_BLOCK, 1, 1, "..");
    put_dir(ROOT_BLOCK, 2, 2, "bin");
    put_dir(ROOT_BLOCK, 3, 4, "large");
    put_dir(ROOT_BLOCK, 4, 5, "huge");
    put_dir(BIN_BLOCK, 0, 2, ".");
    put_dir(BIN_BLOCK, 1, 1, "..");
    put_dir(BIN_BLOCK, 2, 3, "init");
    memcpy(image[INIT_BLOCK], init_data, sizeof(init_data) - 1);
    for (i = 0; i < 7; ++i)
        memset(image[LARGE_FIRST_BLOCK + i], 'A' + i,
               MINIX_BLOCK_SIZE);
    put_le16(image[LARGE_INDIRECT_BLOCK], LARGE_EIGHTH_BLOCK);
    memset(image[LARGE_EIGHTH_BLOCK], 'H', MINIX_BLOCK_SIZE);
    put_le16(image[HUGE_DOUBLE_BLOCK], HUGE_INDIRECT_BLOCK);
    put_le16(image[HUGE_INDIRECT_BLOCK], HUGE_DATA_BLOCK);
    memset(image[HUGE_DATA_BLOCK], 'Z', MINIX_BLOCK_SIZE);
}

static int expect(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "MINIX TEST FAIL: %s\n", message);
    return 1;
}

int main(int argc, char **argv)
{
    static const char init_data[] = "minix init image\n";
    static unsigned char ramdisk_data[sizeof(image)];
    unsigned char ramdisk_block[MINIX_BLOCK_SIZE];
    struct minix_inode inode;
    struct minix_fs fs;
    struct ramdisk rd;
    unsigned long blocks;
    unsigned char data[32];
    int failed;
    long bytes;

    make_image();
    if (argc == 3 && !strcmp(argv[1], "--write")) {
        FILE *file;
        int write_failed;

        file = fopen(argv[2], "wb");
        if (!file) {
            perror(argv[2]);
            return 1;
        }
        write_failed = fwrite(image, sizeof(image), 1, file) != 1;
        if (fclose(file))
            write_failed = 1;
        if (write_failed) {
            fprintf(stderr, "cannot write Minix image: %s\n", argv[2]);
            return 1;
        }
        printf("MINIX IMAGE: %s (%zu bytes)\n", argv[2], sizeof(image));
        return 0;
    }
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--write image]\n", argv[0]);
        return 1;
    }
    blocks = IMAGE_BLOCKS;
    failed = expect(minix_mount(&fs, image_read, &blocks) == 0,
                    "mount valid image");
    failed |= expect(minix_lookup(&fs, "/bin/init", &inode) == 0,
                     "lookup /bin/init");
    bytes = minix_read(&fs, &inode, 0, data, sizeof(data));
    failed |= expect(bytes == (long)sizeof(init_data) - 1,
                     "read init length");
    failed |= expect(!memcmp(data, init_data, sizeof(init_data) - 1),
                     "read init contents");

    failed |= expect(minix_lookup(&fs, "/large", &inode) == 0,
                     "lookup indirect file");
    bytes = minix_read(&fs, &inode, 7 * MINIX_BLOCK_SIZE - 4, data, 8);
    failed |= expect(bytes == 8 && !memcmp(data, "GGGGHHHH", 8),
                     "cross direct-indirect boundary");
    failed |= expect(minix_read(&fs, &inode, inode.size, data, 1) == 0,
                     "read at end of file");
    inode.zone[0] = 1;
    failed |= expect(minix_read(&fs, &inode, 0, data, 1) == -EIO,
                     "reject metadata as file data");
    failed |= expect(minix_lookup(&fs, "/huge", &inode) == 0,
                     "lookup double-indirect file");
    bytes = minix_read(&fs, &inode, (7 + 512) * MINIX_BLOCK_SIZE,
                       data, 4);
    failed |= expect(bytes == 4 && !memcmp(data, "ZZZZ", 4),
                     "read double-indirect block");
    failed |= expect(minix_lookup(&fs, "/missing", &inode) == -ENOENT,
                     "reject missing file");
    failed |= expect(minix_lookup(&fs, "/component-is-too-long", &inode) ==
                     -ENAMETOOLONG, "reject long component");

    put_le16(image[1] + 16, 0);
    failed |= expect(minix_mount(&fs, image_read, &blocks) == -EINVAL,
                     "reject bad superblock magic");
    blocks = 1;
    failed |= expect(minix_mount(&fs, image_read, &blocks) == -EIO,
                     "reject truncated image");

    make_image();
    blocks = IMAGE_BLOCKS;
    failed |= expect(rd_init(&rd, ramdisk_data, sizeof(ramdisk_data)) == 0,
                     "initialize ramdisk storage");
    failed |= expect(rd_load(&rd, image_read, &blocks) == 0 &&
                     rd.length == sizeof(image), "load ramdisk image");
    failed |= expect(rd_read(&rd, 0, ramdisk_block) == 0 &&
                     !memcmp(ramdisk_block, image[0], MINIX_BLOCK_SIZE),
                     "read loaded ramdisk image");
    put_le16(image[1] + 16, 0);
    failed |= expect(rd_load(&rd, image_read, &blocks) == -EINVAL &&
                     rd.length == 0, "reject ramdisk with bad magic");
    make_image();
    put_le16(image[1] + 2, IMAGE_BLOCKS + 1);
    failed |= expect(rd_load(&rd, image_read, &blocks) == -ENOSPC &&
                     rd.length == 0, "reject oversized ramdisk image");
    make_image();
    blocks = IMAGE_BLOCKS / 2;
    failed |= expect(rd_load(&rd, image_read, &blocks) == -EIO &&
                     rd.length == 0, "reject truncated ramdisk image");

    if (failed)
        return 1;
    puts("MINIX TEST PASS: superblock and inode");
    puts("MINIX TEST PASS: directory lookup");
    puts("MINIX TEST PASS: direct and indirect reads");
    puts("MINIX TEST PASS: malformed images");
    puts("MINIX TEST PASS: ramdisk loading errors");
    return 0;
}
