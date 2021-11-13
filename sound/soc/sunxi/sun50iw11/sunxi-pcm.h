/* sound\soc\sunxi\sun50iw11\sunxi-pcm.h
 * (C) Copyright 2019-2025
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * yumingfeng <yumingfeng@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#ifndef __SUNXI_PCM_H_
#define __SUNXI_PCM_H_

#define SUNXI_AUDIO_CMA_BLOCK_BYTES 1024
#define SUNXI_AUDIO_CMA_MAX_KBYTES 1024
#define SUNXI_AUDIO_CMA_MIN_KBYTES 64
#define SUNXI_AUDIO_CMA_MAX_BYTES_MAX \
		(SUNXI_AUDIO_CMA_BLOCK_BYTES * SUNXI_AUDIO_CMA_MAX_KBYTES)
#define SUNXI_AUDIO_CMA_MAX_BYTES_MIN \
		(SUNXI_AUDIO_CMA_BLOCK_BYTES * SUNXI_AUDIO_CMA_MIN_KBYTES)

struct sunxi_dma_params {
	char *name;
	dma_addr_t dma_addr;
	u8 src_maxburst;
	u8 dst_maxburst;
	u8 dma_drq_type_num;
	/* value must be (2^n)Kbyte */
	size_t cma_kbytes;
	size_t fifo_size;
};

int asoc_dma_platform_register(struct device *dev, unsigned int flags);
#endif /* __SUNXI_PCM_H_ */
