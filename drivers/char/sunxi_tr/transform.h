/*
 * Copyright (c) 2007-2017 Allwinnertech Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DE_TR_H__
#define __DE_TR_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/memory.h>
#include <asm/unistd.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "asm-generic/int-ll64.h"
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <asm/div64.h>
#include <linux/debugfs.h>
#include <linux/sunxi_tr.h>
#include <linux/cdev.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

int de_tr_set_base(uintptr_t reg_base);
int de_tr_irq_enable(void);
int de_tr_irq_query(void);
int de_tr_init(void);
int de_tr_exit(void);
int de_tr_set_cfg(tr_info *info);
int de_tr_reset(void);
int de_tr_exception(void);

unsigned int sunxi_tr_request(void);
int sunxi_tr_release(unsigned int id);
int sunxi_tr_commit(unsigned int id, tr_info *info);
int sunxi_tr_query(unsigned int id);
int sunxi_tr_set_timeout(unsigned int id, unsigned long timeout /* ms */);

#endif
