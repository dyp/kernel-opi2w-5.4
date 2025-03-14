/*
 * linux-5.4/drivers/media/platform/sunxi-vin/utility/vin_os.h
 *
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

#ifndef __VIN__OS__H__
#define __VIN__OS__H__

#include <linux/device.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include "../platform/platform_cfg.h"

#ifdef SUNXI_MEM
#include <linux/ion.h>      /*for all "ion api"*/
#include <linux/dma-mapping.h>  /*just include "PAGE_SIZE" macro*/
#else
#include <linux/dma-mapping.h>
#endif

#define ION_HEAP_SYSTEM_MASK		(1 << 0)
#define ION_HEAP_TYPE_DMA_MASK		(1 << 1)

#define IS_FLAG(x, y) (((x)&(y)) == y)

#define VIN_LOG_MD				(1 << 0)	/*0x1 */
#define VIN_LOG_FLASH				(1 << 1)	/*0x2 */
#define VIN_LOG_CCI				(1 << 2)	/*0x4 */
#define VIN_LOG_CSI				(1 << 3)	/*0x8 */
#define VIN_LOG_MIPI				(1 << 4)	/*0x10*/
#define VIN_LOG_ISP				(1 << 5)	/*0x20*/
#define VIN_LOG_STAT				(1 << 6)	/*0x40*/
#define VIN_LOG_SCALER				(1 << 7)	/*0x80*/
#define VIN_LOG_POWER				(1 << 8)	/*0x100*/
#define VIN_LOG_CONFIG				(1 << 9)	/*0x200*/
#define VIN_LOG_VIDEO				(1 << 10)	/*0x400*/
#define VIN_LOG_FMT				(1 << 11)	/*0x800*/
#define VIN_LOG_TDM				(1 << 12)	/*0x1000*/

extern unsigned int vin_log_mask;
#if defined CONFIG_VIN_LOG
#define vin_log(flag, arg...) do { \
	if (flag & vin_log_mask) { \
		switch (flag) { \
		case VIN_LOG_MD: \
			printk(KERN_DEBUG "[VIN_LOG_MD]" arg); \
			break; \
		case VIN_LOG_FLASH: \
			printk(KERN_DEBUG "[VIN_LOG_FLASH]" arg); \
			break; \
		case VIN_LOG_CCI: \
			printk(KERN_DEBUG "[VIN_LOG_CCI]" arg); \
			break; \
		case VIN_LOG_CSI: \
			printk(KERN_DEBUG "[VIN_LOG_CSI]" arg); \
			break; \
		case VIN_LOG_MIPI: \
			printk(KERN_DEBUG "[VIN_LOG_MIPI]" arg); \
			break; \
		case VIN_LOG_ISP: \
			printk(KERN_DEBUG "[VIN_LOG_ISP]" arg); \
			break; \
		case VIN_LOG_STAT: \
			printk(KERN_DEBUG "[VIN_LOG_STAT]" arg); \
			break; \
		case VIN_LOG_SCALER: \
			printk(KERN_DEBUG "[VIN_LOG_SCALER]" arg); \
			break; \
		case VIN_LOG_POWER: \
			printk(KERN_DEBUG "[VIN_LOG_POWER]" arg); \
			break; \
		case VIN_LOG_CONFIG: \
			printk(KERN_DEBUG "[VIN_LOG_CONFIG]" arg); \
			break; \
		case VIN_LOG_VIDEO: \
			printk(KERN_DEBUG "[VIN_LOG_VIDEO]" arg); \
			break; \
		case VIN_LOG_FMT: \
			printk(KERN_DEBUG "[VIN_LOG_FMT]" arg); \
			break; \
		case VIN_LOG_TDM: \
			printk(KERN_DEBUG "[VIN_LOG_TDM]" arg); \
			break; \
		default: \
			printk(KERN_DEBUG "[VIN_LOG]" arg); \
			break; \
		} \
	} \
} while (0)
#else
#define vin_log(flag, arg...) do { } while (0)
#endif
#define vin_err(x, arg...) pr_err("[VIN_ERR]"x, ##arg)
#define vin_warn(x, arg...) pr_warn("[VIN_WARN]"x, ##arg)
#define vin_print(x, arg...) pr_info("[VIN]"x, ##arg)

struct vin_mm {
	size_t size;
	void *phy_addr;
	void *vir_addr;
	void *dma_addr;
	struct dma_buf *buf;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt;
	struct ion_heap *heap;
};

extern int os_gpio_write(u32 gpio, __u32 out_value, int force_value_flag);
extern int os_mem_alloc(struct device *dev, struct vin_mm *mem_man);
extern void os_mem_free(struct device *dev, struct vin_mm *mem_man);

#endif	/*__VIN__OS__H__*/
