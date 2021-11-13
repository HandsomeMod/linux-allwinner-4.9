// SPDX-License-Identifier: GPL-2.0
#include "nand_panic.h"
#include "nand_lib.h"
#include <linux/kernel.h>
#include <linux/sunxi-panicpart.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/string.h>

#define PANIC_INFO(...) printk(KERN_INFO "[NI] " __VA_ARGS__)
#define PANIC_DBG(...) printk(KERN_DEBUG "[ND] " __VA_ARGS__)
#define PANIC_ERR(...) printk(KERN_ERR "[NE] " __VA_ARGS__)

static dma_addr_t panic_dma;
struct dma_buf {
	void *buf;
	size_t *size;
};
static struct dma_buf panic_buf;
static struct dma_buf panic_map;

static ssize_t nand_panic_read(struct panic_part *part, loff_t sec_off,
		size_t sec_cnt, char *buf)
{
	int ret;
	struct _nftl_blk *nftl_blk = part->private;

	ret = panic_read(nftl_blk, sec_off, sec_cnt, buf);
	if (ret)
		return ret;
	return sec_cnt;
}

static ssize_t nand_panic_write(struct panic_part *part, loff_t sec_off,
		size_t sec_cnt, const char *buf)
{
	int ret;
	struct _nftl_blk *nftl_blk = part->private;

	ret = panic_write(nftl_blk, sec_off, sec_cnt, buf);
	if (ret)
		return ret;
	return sec_cnt;
}

static struct panic_part nand_panic = {
	.type = SUNXI_FLASH_NAND,
	.panic_read = nand_panic_read,
	.panic_write = nand_panic_write,
};

static int rawnand_panic_alloc_dma_buf(size_t size)
{
	char *buf;

	buf = dma_alloc_coherent(NULL, size, &panic_dma, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	panic_buf.buf = buf;
	panic_buf.size = size;
	return 0;
}

dma_addr_t nand_panic_dma_map(__u32 rw, void *buf, __u32 len)
{
	if (!is_panic_enable())
		return -EBUSY;

	/* ONLY rawnand allow to use DMA */
	if (get_storage_type() != NAND_STORAGE_TYPE_RAWNAND)
		return -EINVAL;

	if (rw == 1) {
		size_t size = min_t(size_t, len, panic_buf.size);
		memcpy(panic_buf.buf, buf, size);
	} else {
		panic_map.buf = buf;
		panic_map.size = (size_t)len;
	}

	return panic_dma;
}

void nand_panic_dma_unmap(__u32 rw, dma_addr_t dma_handle, __u32 len)
{
	if (!is_panic_enable())
		return;

	/* ONLY rawnand allow to use DMA */
	if (get_storage_type() != NAND_STORAGE_TYPE_RAWNAND)
		return -EINVAL;

	if (rw == 0) {
		size_t size = min_t(size_t, panic_map.size, panic_buf.size);
		memcpy(panic_map.buf, panic_buf.buf, size);
	}

	return;
}

int nand_support_panic(void)
{
	int ret;

	ret = sunxi_parse_blkdev(NULL, 0);
	if (ret)
		return ret;

	/* allocate memory for rawnand dma transfer */
	if (get_storage_type() == NAND_STORAGE_TYPE_RAWNAND) {
		ret = rawnand_panic_alloc_dma_buf(32 * 1024);
		if (ret)
			return ret;
	}

	panic_mark_enable();
	return 0;
}

int nand_panic_init(struct _nftl_blk *nftl_blk)
{
	int ret;

	if (!is_panic_enable())
		return -EBUSY;

	ret = sunxi_panicpart_init(&nand_panic);
	if (ret)
		return ret;

	ret = panic_init_part(nand_panic.bdev, nand_panic.start_sect,
			nand_panic.sects);
	if (ret)
		return ret;

	nand_panic.private = (void *)nftl_blk;
	PANIC_INFO("panic nand version: %s\n", PANIC_NAND_VERSION);
	return 0;
}
