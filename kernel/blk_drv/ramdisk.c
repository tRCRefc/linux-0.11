/*
 *  linux/kernel/blk_drv/ramdisk.c
 *
 *  Written by Theodore Ts'o, 12/2/91
 */

#ifdef __x86_64__

#include <errno.h>
#include <linux/minix.h>
#include <linux/ramdisk.h>

static __UINT16_TYPE__ get_le16(const unsigned char *p)
{
    return (__UINT16_TYPE__)(p[0] | ((__UINT16_TYPE__)p[1] << 8));
}

int rd_init(struct ramdisk *rd, void *start, unsigned long length)
{
    if (!rd || !start || length < MINIX_BLOCK_SIZE ||
        length % MINIX_BLOCK_SIZE)
        return -EINVAL;
    rd->start = start;
    rd->length = 0;
    rd->capacity = length;
    while (length-- > 0)
        rd->start[length] = 0;
    return 0;
}

int rd_load(struct ramdisk *rd, minix_read_block_t read_block, void *context)
{
    unsigned char super[MINIX_BLOCK_SIZE];
    unsigned long blocks;
    unsigned long block;
    unsigned int log_zone_size;

    if (!rd || !read_block || !rd->start || !rd->capacity)
        return -EINVAL;
    rd->length = 0;
    if (read_block(context, 1, super) < 0)
        return -EIO;
    if (get_le16(super + 16) != MINIX_SUPER_MAGIC)
        return -EINVAL;
    log_zone_size = get_le16(super + 10);
    if (log_zone_size >= 8 * sizeof(blocks))
        return -EINVAL;
    blocks = (unsigned long)get_le16(super + 2) << log_zone_size;
    if (!blocks || blocks > rd->capacity / MINIX_BLOCK_SIZE)
        return -ENOSPC;
    for (block = 0; block < blocks; ++block) {
        if (read_block(context, block,
                       rd->start + block * MINIX_BLOCK_SIZE) < 0)
            return -EIO;
    }
    rd->length = blocks * MINIX_BLOCK_SIZE;
    return 0;
}

int rd_read(void *context, unsigned long block, void *buffer)
{
    struct ramdisk *rd;
    const unsigned char *from;
    unsigned char *to;
    unsigned long count;

    rd = context;
    if (!rd || !buffer || block >= rd->length / MINIX_BLOCK_SIZE)
        return -EIO;
    from = rd->start + block * MINIX_BLOCK_SIZE;
    to = buffer;
    count = MINIX_BLOCK_SIZE;
    while (count-- > 0)
        *to++ = *from++;
    return 0;
}

#else

#include <string.h>

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/memory.h>

#define MAJOR_NR 1
#include "blk.h"

char	*rd_start;
int	rd_length = 0;

void do_rd_request(void)
{
	int	len;
	char	*addr;

	INIT_REQUEST;
	addr = rd_start + (CURRENT->sector << 9);
	len = CURRENT->nr_sectors << 9;
	if ((MINOR(CURRENT->dev) != 1) || (addr+len > rd_start+rd_length)) {
		end_request(0);
		goto repeat;
	}
	if (CURRENT-> cmd == WRITE) {
		(void ) memcpy(addr,
			      CURRENT->buffer,
			      len);
	} else if (CURRENT->cmd == READ) {
		(void) memcpy(CURRENT->buffer, 
			      addr,
			      len);
	} else
		panic("unknown ramdisk-command");
	end_request(1);
	goto repeat;
}

/*
 * Returns amount of memory which needs to be reserved.
 */
long rd_init(long mem_start, int length)
{
	int	i;
	char	*cp;

	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	rd_start = (char *) mem_start;
	rd_length = length;
	cp = rd_start;
	for (i=0; i < length; i++)
		*cp++ = '\0';
	return(length);
}

/*
 * If the root device is the ram disk, try to load it.
 * In order to do this, the root device is originally set to the
 * floppy, and we later change it to be ram disk.
 */
void rd_load(void)
{
	struct buffer_head *bh;
	struct super_block	s;
	int		block = 256;	/* Start at block 256 */
	int		i = 1;
	int		nblocks;
	char		*cp;		/* Move pointer */
	
	if (!rd_length)
		return;
	printk("Ram disk: %d bytes, starting at 0x%x\n", rd_length,
		(int) rd_start);
	if (MAJOR(ROOT_DEV) != 2)
		return;
	bh = breada(ROOT_DEV,block+1,block,block+2,-1);
	if (!bh) {
		printk("Disk error while looking for ramdisk!\n");
		return;
	}
	*((struct d_super_block *) &s) = *((struct d_super_block *) bh->b_data);
	brelse(bh);
	if (s.s_magic != SUPER_MAGIC)
		/* No ram disk image present, assume normal floppy boot */
		return;
	nblocks = s.s_nzones << s.s_log_zone_size;
	if (nblocks > (rd_length >> BLOCK_SIZE_BITS)) {
		printk("Ram disk image too big!  (%d blocks, %d avail)\n", 
			nblocks, rd_length >> BLOCK_SIZE_BITS);
		return;
	}
	printk("Loading %d bytes into ram disk... 0000k", 
		nblocks << BLOCK_SIZE_BITS);
	cp = rd_start;
	while (nblocks) {
		if (nblocks > 2) 
			bh = breada(ROOT_DEV, block, block+1, block+2, -1);
		else
			bh = bread(ROOT_DEV, block);
		if (!bh) {
			printk("I/O error on block %d, aborting load\n", 
				block);
			return;
		}
		(void) memcpy(cp, bh->b_data, BLOCK_SIZE);
		brelse(bh);
		printk("\010\010\010\010\010%4dk",i);
		cp += BLOCK_SIZE;
		block++;
		nblocks--;
		i++;
	}
	printk("\010\010\010\010\010done \n");
	ROOT_DEV=0x0101;
}

#endif
