/*
 * linux-5.4/drivers/media/platform/sunxi-vin/vin-isp/sunxi_isp.c
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

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/compat.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>
#include "../platform/platform_cfg.h"
#include "sunxi_isp.h"

#include "../vin-csi/sunxi_csi.h"
#include "../vin-vipp/sunxi_scaler.h"
#include "../vin-video/vin_core.h"
#include "../utility/vin_io.h"
#include "isp_default_tbl.h"

#define ISP_MODULE_NAME "vin_isp"

struct isp_dev *glb_isp[VIN_MAX_ISP];

#ifdef CONFIG_D3D_COMPRESS_EN
#define D3D_RAW_LBC_MODE		16 /* <=10 = lossless, 12 = 1.2x, 30 = 3x(< 3x)*/
#define D3D_K_LBC_MODE			20 /* <=10 = lossless, 12 = 1.2x, 20 = 2x(< 2x)*/
#else
#define D3D_RAW_LBC_MODE		10 /* <=10 = lossless, 12 = 1.2x, 30 = 3x(< 3x)*/
#define D3D_K_LBC_MODE			10 /* <=10 = lossless, 12 = 1.2x, 20 = 2x(< 2x)*/
#endif

#ifdef CONFIG_WDR_COMPRESS_EN
#define WDR_RAW_LBC_MODE		26 /* <=10 = lossless, 12 = 1.2x, 30 = 3x(< 3x)*/
#else
#define WDR_RAW_LBC_MODE		10 /* <=10 = lossless, 12 = 1.2x, 30 = 3x(< 3x)*/
#endif

#define LARGE_IMAGE_OFF			32

#define MIN_IN_WIDTH			192
#define MIN_IN_HEIGHT			128
#define MAX_IN_WIDTH			4224
#define MAX_IN_HEIGHT			4224

static struct isp_pix_fmt sunxi_isp_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.mbus_code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.infmt = ISP_BGGR,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.mbus_code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.infmt = ISP_GBRG,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.mbus_code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.infmt = ISP_GRBG,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.mbus_code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.infmt = ISP_RGGB,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.infmt = ISP_BGGR,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.mbus_code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.infmt = ISP_GBRG,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.infmt = ISP_GRBG,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.mbus_code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.infmt = ISP_RGGB,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.infmt = ISP_BGGR,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.mbus_code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.infmt = ISP_GBRG,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.mbus_code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.infmt = ISP_GRBG,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.infmt = ISP_RGGB,
	},
};

static void __isp_s_sensor_stby_handle(struct work_struct *work)
{
	int sensor_stby_stat, i;
	struct isp_dev *isp =
			container_of(work, struct isp_dev, s_sensor_stby_task);
	struct vin_md *vind = dev_get_drvdata(isp->subdev.v4l2_dev->dev);
	struct vin_core *vinc = NULL;

	for (i = 0; i < VIN_MAX_DEV; i++) {
		if (vind->vinc[i] == NULL)
			continue;
		if (!vin_streaming(&vind->vinc[i]->vid_cap))
			continue;

		vinc = vind->vinc[i];
		if (vinc->isp_sel == isp->id)
			break;
	}

	sensor_stby_stat = STBY_ON;
	v4l2_subdev_call(vinc->vid_cap.pipe.sd[VIN_IND_SENSOR], core, ioctl,
				SET_SENSOR_STANDBY, &sensor_stby_stat);

	sensor_stby_stat = STBY_OFF;
	v4l2_subdev_call(vinc->vid_cap.pipe.sd[VIN_IND_SENSOR], core, ioctl,
				SET_SENSOR_STANDBY, &sensor_stby_stat);


	vin_print("%s done, %s is standby and then on!\n", __func__, vinc->vid_cap.pipe.sd[VIN_IND_SENSOR]->name);
}

#if defined CONFIG_D3D
static int isp_3d_pingpong_alloc(struct isp_dev *isp)
{
	int ret = 0;
#if defined CONFIG_ARCH_SUN8IW19P1
	int cmp_ratio, bitdepth, wth;

	if (D3D_K_LBC_MODE > 10) {
		if (D3D_K_LBC_MODE > 20)
			cmp_ratio = 1000/2;
		else
			cmp_ratio = 1000*10/D3D_K_LBC_MODE;
	} else
		cmp_ratio = 1000;
	bitdepth = 5;
	wth = roundup(isp->mf.width, 16);
	if (D3D_K_LBC_MODE <= 10)
		isp->d3d_k_lbc.line_tar_bits = roundup(wth*bitdepth + wth/16*2, 512);
	else
		isp->d3d_k_lbc.line_tar_bits = roundup(cmp_ratio*wth*bitdepth/1000, 512);
	isp->d3d_k_lbc.mb_min_bit = clamp((cmp_ratio*bitdepth*16)/1000, 0, 127);

	if (D3D_RAW_LBC_MODE > 10) {
		if (D3D_RAW_LBC_MODE > 30)
			cmp_ratio = 1000/3;
		else
			cmp_ratio = 1000*10/D3D_RAW_LBC_MODE;
	} else
		cmp_ratio = 1000;
	bitdepth = 12;
	wth = roundup(isp->mf.width, 32);
	if (D3D_RAW_LBC_MODE <= 10)
		isp->d3d_raw_lbc.line_tar_bits = roundup(wth*bitdepth + wth/32*2, 512);
	else
		isp->d3d_raw_lbc.line_tar_bits = roundup(cmp_ratio*wth*bitdepth/1000, 512);
	isp->d3d_raw_lbc.mb_min_bit = clamp(((cmp_ratio*bitdepth + 500)/1000-1)*32+6, 0, 511);

	isp->d3d_pingpong[0].size = isp->d3d_k_lbc.line_tar_bits * isp->mf.height / 8;
	isp->d3d_pingpong[1].size = isp->d3d_raw_lbc.line_tar_bits * isp->mf.height / 8;
#if defined CONFIG_D3D_LTF_EN
	isp->d3d_pingpong[2].size = isp->d3d_raw_lbc.line_tar_bits * isp->mf.height / 8;
	ret = os_mem_alloc(&isp->pdev->dev, &isp->d3d_pingpong[2]);
	if (ret < 0) {
		vin_err("isp 3d pingpong buf2 requset failed!\n");
		return -ENOMEM;
	}
#endif
#elif defined CONFIG_ARCH_SUN8IW16P1
#if defined CONFIG_D3D_LTF_EN
	isp->d3d_pingpong[0].size = roundup(isp->mf.width, 64) * isp->mf.height * 29 / 8;
	isp->d3d_pingpong[1].size = roundup(isp->mf.width, 64) * isp->mf.height * 29 / 8;
#else
	isp->d3d_pingpong[0].size = roundup(isp->mf.width, 64) * isp->mf.height * 17 / 8;
	isp->d3d_pingpong[1].size = roundup(isp->mf.width, 64) * isp->mf.height * 17 / 8;
#endif
#else
	isp->d3d_pingpong[0].size = roundup(isp->mf.width, 64) * isp->mf.height * 29 / 8;
	isp->d3d_pingpong[1].size = roundup(isp->mf.width, 64) * isp->mf.height * 29 / 8;
#endif
	ret = os_mem_alloc(&isp->pdev->dev, &isp->d3d_pingpong[0]);
	if (ret < 0) {
		vin_err("isp 3d pingpong buf0 requset failed!\n");
		return -ENOMEM;
	}

	ret = os_mem_alloc(&isp->pdev->dev, &isp->d3d_pingpong[1]);
	if (ret < 0) {
		vin_err("isp 3d pingpong buf1 requset failed!\n");
		return -ENOMEM;
	}
	return ret;
}
static void isp_3d_pingpong_free(struct isp_dev *isp)
{
	os_mem_free(&isp->pdev->dev, &isp->d3d_pingpong[0]);
	os_mem_free(&isp->pdev->dev, &isp->d3d_pingpong[1]);
#if defined CONFIG_ARCH_SUN8IW19P1
#if defined CONFIG_D3D_LTF_EN
	os_mem_free(&isp->pdev->dev, &isp->d3d_pingpong[2]);
#endif
#endif
}

static int isp_3d_pingpong_update(struct isp_dev *isp)
{
	dma_addr_t addr;

#if defined CONFIG_ARCH_SUN8IW19P1
	addr = (dma_addr_t)isp->d3d_pingpong[0].dma_addr;
	bsp_isp_set_d3d_ref_k_addr(isp->id, addr);
	addr = (dma_addr_t)isp->d3d_pingpong[1].dma_addr;
	bsp_isp_set_d3d_ref_raw_addr(isp->id, addr);
#if defined CONFIG_D3D_LTF_EN
	addr = (dma_addr_t)isp->d3d_pingpong[2].dma_addr;
	bsp_isp_set_d3d_ltf_raw_addr(isp->id, addr);
#endif

	if (D3D_K_LBC_MODE <= 10)
		bsp_isp_set_d3d_k_lbc_ctrl(isp->id, &isp->d3d_k_lbc, 0);
	else
		bsp_isp_set_d3d_k_lbc_ctrl(isp->id, &isp->d3d_k_lbc, 1);

	if (D3D_RAW_LBC_MODE <= 10)
		bsp_isp_set_d3d_raw_lbc_ctrl(isp->id, &isp->d3d_raw_lbc, 0);
	else
		bsp_isp_set_d3d_raw_lbc_ctrl(isp->id, &isp->d3d_raw_lbc, 1);

	bsp_isp_set_d3d_stride(isp->id, isp->d3d_k_lbc.line_tar_bits/32, isp->d3d_raw_lbc.line_tar_bits/32);
	bsp_isp_d3d_fifo_en(isp->id, 1);
#else
	struct vin_mm tmp;

	tmp = isp->d3d_pingpong[0];
	isp->d3d_pingpong[0] = isp->d3d_pingpong[1];
	isp->d3d_pingpong[1] = tmp;

	addr = (dma_addr_t)isp->d3d_pingpong[0].dma_addr;
	bsp_isp_set_d3d_addr0(isp->id, addr);
	addr = (dma_addr_t)isp->d3d_pingpong[1].dma_addr;
	bsp_isp_set_d3d_addr1(isp->id, addr);
#if defined CONFIG_ARCH_SUN8IW16P1 && !defined CONFIG_D3D_LTF_EN
	/* close d3d long time frame */
	writel(readl(isp->isp_load.vir_addr + 0x2d4) & ~(1 << 24), isp->isp_load.vir_addr + 0x2d4);
#endif
#endif
	return 0;
}
#else
static int isp_3d_pingpong_alloc(struct isp_dev *isp)
{
	return 0;
}
static void isp_3d_pingpong_free(struct isp_dev *isp)
{

}
static int isp_3d_pingpong_update(struct isp_dev *isp)
{
	return 0;
}
#endif

#if defined CONFIG_WDR
static int isp_wdr_pingpong_alloc(struct isp_dev *isp)
{
	int ret = 0, i;
#if !defined CONFIG_ARCH_SUN8IW16P1 && !defined CONFIG_ARCH_SUN8IW19P1
	short *wdr_tbl = isp->isp_lut_tbl.vir_addr + ISP_WDR_GAMMA_FE_MEM_OFS;
#else
	short *wdr_tbl = isp->isp_load.vir_addr + ISP_LOAD_REG_SIZE + ISP_FE_TBL_SIZE + ISP_S0_LC_TBL_SIZE;
#endif

#if defined CONFIG_ARCH_SUN8IW19P1
	int cmp_ratio, bitdepth, wth;

	if (WDR_RAW_LBC_MODE > 10) {
		if (WDR_RAW_LBC_MODE > 30)
			cmp_ratio = 1000/3;
		else
			cmp_ratio = 1000*10/WDR_RAW_LBC_MODE;
	} else
		cmp_ratio = 1000;
	bitdepth = 12;
	wth = roundup(isp->mf.width, 32);
	if (WDR_RAW_LBC_MODE <= 10)
		isp->wdr_raw_lbc.line_tar_bits = roundup(wth*bitdepth + wth/32*2, 512);
	else
		isp->wdr_raw_lbc.line_tar_bits = roundup(cmp_ratio*wth*bitdepth/1000, 512);
	isp->wdr_raw_lbc.mb_min_bit = clamp(((cmp_ratio*bitdepth + 500)/1000-1)*32+16, 0, 511);

	isp->wdr_pingpong[0].size = isp->wdr_raw_lbc.line_tar_bits * isp->mf.height / 8;
#else
	isp->wdr_pingpong[0].size = isp->mf.width * isp->mf.height * 2;
	isp->wdr_pingpong[1].size = isp->mf.width * isp->mf.height * 2;

	ret = os_mem_alloc(&isp->pdev->dev, &isp->wdr_pingpong[1]);
	if (ret < 0) {
		vin_err("isp wdr pingpong buf1 requset failed!\n");
		return -ENOMEM;
	}
#endif
	ret = os_mem_alloc(&isp->pdev->dev, &isp->wdr_pingpong[0]);
	if (ret < 0) {
		vin_err("isp wdr pingpong buf0 requset failed!\n");
		return -ENOMEM;
	}

	for (i = 0; i < 4096; i++) {
		wdr_tbl[i] = i;
		wdr_tbl[i + 4096] = i*16;
	}

	return ret;
}
static void isp_wdr_pingpong_free(struct isp_dev *isp)
{
#if defined CONFIG_ARCH_SUN8IW19P1
	os_mem_free(&isp->pdev->dev, &isp->wdr_pingpong[0]);
#else
	os_mem_free(&isp->pdev->dev, &isp->wdr_pingpong[0]);
	os_mem_free(&isp->pdev->dev, &isp->wdr_pingpong[1]);
#endif
}

static int isp_wdr_pingpong_set(struct isp_dev *isp)
{
	dma_addr_t addr;
#if defined  CONFIG_ARCH_SUN8IW19P1
	addr = (dma_addr_t)isp->wdr_pingpong[0].dma_addr;
	bsp_isp_set_wdr_addr0(isp->id, addr);
	if (WDR_RAW_LBC_MODE <= 10)
		bsp_isp_set_wdr_raw_lbc_ctrl(isp->id, &isp->wdr_raw_lbc, 0);
	else
		bsp_isp_set_wdr_raw_lbc_ctrl(isp->id, &isp->wdr_raw_lbc, 1);
	bsp_isp_set_wdr_stride(isp->id, isp->wdr_raw_lbc.line_tar_bits / 32);
	bsp_isp_wdr_fifo_en(isp->id, 1);
#else
	addr = (dma_addr_t)isp->wdr_pingpong[0].dma_addr;
	bsp_isp_set_wdr_addr0(isp->id, addr);
	addr = (dma_addr_t)isp->wdr_pingpong[1].dma_addr;
	bsp_isp_set_wdr_addr1(isp->id, addr);
#endif
	return 0;
}
#else
static int isp_wdr_pingpong_alloc(struct isp_dev *isp)
{
	isp->wdr_mode = ISP_NORMAL_MODE;

	return 0;
}
static void isp_wdr_pingpong_free(struct isp_dev *isp) {}
static int isp_wdr_pingpong_set(struct isp_dev *isp)
{
	return 0;
}
#endif

#ifdef SUPPORT_ISP_TDM
static int __sunxi_isp_tdm_off(struct isp_dev *isp)
{
	struct vin_md *vind = dev_get_drvdata(isp->subdev.v4l2_dev->dev);
	struct vin_core *vinc = NULL;
	int i, j;

	for (i = 0; i < VIN_MAX_DEV; i++) {
		if (vind->vinc[i] == NULL)
			continue;
		if (!vin_streaming(&vind->vinc[i]->vid_cap))
			continue;
		vinc = vind->vinc[i];
		for (j = 0; j < VIN_MAX_ISP; j++) {
			if (vinc->isp_sel == j)
				return -1;
		}
	}
	return 0;
}
#endif

static int sunxi_isp_subdev_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf = &isp->mf;
	struct mbus_framefmt_res *res = (void *)mf->reserved;
	struct v4l2_event event;
#if defined CONFIG_ARCH_SUN8IW16P1 || defined CONFIG_ARCH_SUN8IW19P1 || defined CONFIG_ARCH_SUN50IW10P1
	struct isp_wdr_mode_cfg wdr_cfg;
#endif
	unsigned int load_val;
	__maybe_unused int i;

	if (!isp->use_isp)
		return 0;

	switch (res->res_pix_fmt) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		vin_log(VIN_LOG_FMT, "%s output fmt is raw, return directly\n", __func__);
		if (isp->isp_dbg.debug_en) {
			bsp_isp_debug_output_cfg(isp->id, 1, isp->isp_dbg.debug_sel);
			break;
		} else {
			return 0;
		}
	default:
		break;
	}

	if (enable) {
		isp->h3a_stat.frame_number = 0;
		isp->ptn_isp_cnt = 0;
		isp->isp_ob.set_cnt = 0;
		isp->sensor_lp_mode = res->res_lp_mode;
		/*when normal to wdr, old register would lead timeout, so we clean it up*/
		if (isp->wdr_mode != res->res_wdr_mode) {
			isp->wdr_mode = res->res_wdr_mode;
			memcpy(isp->isp_load.vir_addr, &isp_default_reg[0], ISP_LOAD_REG_SIZE);
		}
		if (isp->load_flag)
			memcpy(isp->isp_load.vir_addr, &isp->load_shadow[0], ISP_LOAD_DRAM_SIZE);

		if (isp->large_image == 0) {
			if (isp->runtime_flag == 0) {
				if (isp_3d_pingpong_alloc(isp))
					return -ENOMEM;
				isp_3d_pingpong_update(isp);
			} else
				isp->runtime_flag = 0;
		}
		if (isp->wdr_mode != ISP_NORMAL_MODE) {
			if (isp_wdr_pingpong_alloc(isp)) {
				isp_3d_pingpong_free(isp);
				return -ENOMEM;
			}
			isp_wdr_pingpong_set(isp);
		}
#ifndef SUPPORT_ISP_TDM
		bsp_isp_enable(isp->id, 1);
#else
		for (i = 0; i < VIN_MAX_ISP; i++)
			bsp_isp_enable(i, 1);
#endif
		bsp_isp_clr_irq_status(isp->id, ISP_IRQ_EN_ALL);
#if !defined CONFIG_ARCH_SUN8IW16P1 && !defined CONFIG_ARCH_SUN8IW19P1 && !defined CONFIG_ARCH_SUN50IW10P1
		bsp_isp_irq_enable(isp->id, FINISH_INT_EN | PARA_LOAD_INT_EN | SRC0_FIFO_INT_EN
				     | FRAME_ERROR_INT_EN | FRAME_LOST_INT_EN);

		load_val = bsp_isp_load_update_flag(isp->id);
		if (isp->wdr_mode == ISP_DOL_WDR_MODE) {
			load_val = load_val | WDR_UPDATE;
			bsp_isp_module_enable(isp->id, WDR_EN);
			bsp_isp_set_wdr_mode(isp->id, ISP_DOL_WDR_MODE);
			bsp_isp_ch_enable(isp->id, ISP_CH1, 1);
		} else if (isp->wdr_mode == ISP_COMANDING_MODE) {
			load_val = load_val | WDR_UPDATE;
			bsp_isp_module_enable(isp->id, WDR_EN);
			bsp_isp_set_wdr_mode(isp->id, ISP_COMANDING_MODE);
		} else {
			load_val = load_val & ~WDR_UPDATE;
			bsp_isp_module_disable(isp->id, WDR_EN);
			bsp_isp_set_wdr_mode(isp->id, ISP_NORMAL_MODE);
		}
#else
		bsp_isp_irq_enable(isp->id, FINISH_INT_EN | S0_PARA_LOAD_INT_EN | S0_FIFO_INT_EN
				     | S0_FRAME_ERROR_INT_EN | S0_FRAME_LOST_INT_EN);
#if defined CONFIG_ARCH_SUN8IW19P1
		bsp_isp_irq_enable(isp->id, S0_BTYPE_ERROR_INT_EN | ADDR_ERROR_INT_EN | LBC_ERROR_INT_EN);
#endif

		load_val = bsp_isp_load_update_flag(isp->id);
		if (isp->wdr_mode == ISP_DOL_WDR_MODE) {
			load_val = load_val | WDR_UPDATE;
			wdr_cfg.wdr_exp_seq = 0;
			wdr_cfg.wdr_ch_seq = 0;
			wdr_cfg.wdr_mode = 0;
			bsp_isp_module_enable(isp->id, WDR_EN);
			bsp_isp_wdr_mode_cfg(isp->id, &wdr_cfg);
			bsp_isp_ch_enable(isp->id, ISP_CH1, 1);
		} else if (isp->wdr_mode == ISP_COMANDING_MODE) {
			load_val = load_val | WDR_UPDATE;
			wdr_cfg.wdr_exp_seq = 0;
			wdr_cfg.wdr_ch_seq = 0;
			wdr_cfg.wdr_mode = ISP_COMANDING_MODE;
			bsp_isp_module_enable(isp->id, WDR_EN);
			bsp_isp_wdr_mode_cfg(isp->id, &wdr_cfg);
		} else {
			load_val = load_val & ~WDR_UPDATE;
			bsp_isp_module_disable(isp->id, WDR_EN);
		}
#endif

#if !defined CONFIG_D3D
		bsp_isp_module_disable(isp->id, D3D_EN);
		load_val = load_val & ~D3D_UPDATE;
#endif
		if (isp->large_image == 2)
			bsp_isp_module_disable(isp->id, PLTM_EN | D3D_EN | AE_EN | AWB_EN | AF_EN | HIST_EN);
		bsp_isp_update_table(isp->id, load_val);

		bsp_isp_module_enable(isp->id, SRC0_EN);
		bsp_isp_set_input_fmt(isp->id, isp->isp_fmt->infmt);
		bsp_isp_set_size(isp->id, &isp->isp_ob);
		bsp_isp_set_para_ready_mode(isp->id, 1);
		bsp_isp_set_para_ready(isp->id, PARA_READY);
		bsp_isp_set_last_blank_cycle(isp->id, 5);
		bsp_isp_set_speed_mode(isp->id, 3);
#if !defined CONFIG_D3D_LTF_EN && !defined CONFIG_WDR
		bsp_isp_set_fifo_mode(isp->id, 0);
		bsp_isp_fifo_raw_write(isp->id, 0x200);
#endif
		bsp_isp_ch_enable(isp->id, ISP_CH0, 1);
		bsp_isp_capture_start(isp->id);
	} else {
		if (!isp->nosend_ispoff && !isp->runtime_flag) {
			memset(&event, 0, sizeof(event));
			event.type = V4L2_EVENT_VIN_ISP_OFF;
			event.id = 0;
			v4l2_event_queue(isp->subdev.devnode, &event);
		}
		bsp_isp_capture_stop(isp->id);
		bsp_isp_module_disable(isp->id, SRC0_EN);
		bsp_isp_ch_enable(isp->id, ISP_CH0, 0);
		bsp_isp_irq_disable(isp->id, ISP_IRQ_EN_ALL);
		bsp_isp_clr_irq_status(isp->id, ISP_IRQ_EN_ALL);
#ifndef SUPPORT_ISP_TDM
		bsp_isp_enable(isp->id, 0);
#else
		if (__sunxi_isp_tdm_off(isp) == 0) {
			for (i = 0; i < VIN_MAX_ISP; i++)
				bsp_isp_enable(i, 0);

		} else
			vin_warn("ISP is used in TDM mode, ISP%d cannot be closing when other isp is used!\n", isp->id);
#endif
		if (isp->large_image == 0) {
			if (isp->runtime_flag == 0)
				isp_3d_pingpong_free(isp);
			else
				isp->runtime_flag = 0;
		}
		if (isp->wdr_mode != ISP_NORMAL_MODE) {
			bsp_isp_ch_enable(isp->id, ISP_CH1, 0);
			isp_wdr_pingpong_free(isp);
		}
		isp->f1_after_librun = 0;
	}

	vin_log(VIN_LOG_FMT, "isp%d %s, %d*%d hoff: %d voff: %d code: %x field: %d\n",
		isp->id, enable ? "stream on" : "stream off",
		isp->isp_ob.ob_valid.width, isp->isp_ob.ob_valid.height,
		isp->isp_ob.ob_start.hor, isp->isp_ob.ob_start.ver,
		mf->code, mf->field);

	return 0;
}

static struct isp_pix_fmt *__isp_try_format(struct isp_dev *isp,
					struct v4l2_mbus_framefmt *mf)
{
	struct isp_pix_fmt *isp_fmt = NULL;
	struct isp_size_settings *ob = &isp->isp_ob;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sunxi_isp_formats); ++i)
		if (mf->code == sunxi_isp_formats[i].mbus_code)
			isp_fmt = &sunxi_isp_formats[i];

	if (isp_fmt == NULL)
		isp_fmt = &sunxi_isp_formats[0];

	ob->ob_black.width = mf->width;
	ob->ob_black.height = mf->height;

	if (!isp->large_image) {
		if (isp->id == 1) {
			mf->width = clamp_t(u32, mf->width, MIN_IN_WIDTH, 3264);
			mf->height = clamp_t(u32, mf->height, MIN_IN_HEIGHT, 3264);
		} else {
			mf->width = clamp_t(u32, mf->width, MIN_IN_WIDTH, 4224);
			mf->height = clamp_t(u32, mf->height, MIN_IN_HEIGHT, 4224);
		}
	}

	ob->ob_valid.width = mf->width;
	ob->ob_valid.height = mf->height;
	ob->ob_start.hor = (ob->ob_black.width - ob->ob_valid.width) / 2;
	ob->ob_start.ver = (ob->ob_black.height - ob->ob_valid.height) / 2;

	if (isp->large_image == 2) {
		isp->left_right = 0;
		isp->isp_ob.ob_valid.width = mf->width / 2 + LARGE_IMAGE_OFF;
	}

	switch (mf->colorspace) {
	case V4L2_COLORSPACE_REC709:
		mf->colorspace = V4L2_COLORSPACE_REC709;
		break;
	case V4L2_COLORSPACE_BT2020:
		mf->colorspace = V4L2_COLORSPACE_BT2020;
		break;
	default:
		mf->colorspace = V4L2_COLORSPACE_JPEG;
		break;
	}
	return isp_fmt;
}

int sunxi_isp_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	isp->capture_mode = cp->capturemode;
	isp->large_image = cp->reserved[2];

	return 0;
}

int sunxi_isp_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->capturemode = isp->capture_mode;

	return 0;
}

static int sunxi_isp_subdev_get_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_format *fmt)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	mutex_lock(&isp->subdev_lock);
	fmt->format = isp->mf;
	mutex_unlock(&isp->subdev_lock);
	return 0;
}

static int sunxi_isp_subdev_set_fmt(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_format *fmt)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf = &isp->mf;
	struct isp_pix_fmt *isp_fmt;

	vin_log(VIN_LOG_FMT, "%s %d*%d %x %d\n", __func__,
		fmt->format.width, fmt->format.height,
		fmt->format.code, fmt->format.field);

	if (fmt->pad == ISP_PAD_SOURCE) {
		if (mf) {
			mutex_lock(&isp->subdev_lock);
			fmt->format = *mf;
			mutex_unlock(&isp->subdev_lock);
		}
		return 0;
	}
	isp_fmt = __isp_try_format(isp, &fmt->format);
	if (mf) {
		mutex_lock(&isp->subdev_lock);
		*mf = fmt->format;
		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			isp->isp_fmt = isp_fmt;
		mutex_unlock(&isp->subdev_lock);
	}

	return 0;

}

int sunxi_isp_subdev_init(struct v4l2_subdev *sd, u32 val)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct vin_md *vind = dev_get_drvdata(isp->subdev.v4l2_dev->dev);

	if (!isp->use_isp)
		return 0;

	if (val && isp->use_cnt++ > 0)
		return 0;
	else if (!val && (isp->use_cnt == 0 || --isp->use_cnt > 0))
		return 0;

	vin_log(VIN_LOG_ISP, "isp%d %s use_cnt = %d.\n", isp->id,
		val ? "init" : "uninit", isp->use_cnt);

	if (val) {
		bsp_isp_ver_read_en(isp->id, 1);
		bsp_isp_get_isp_ver(isp->id, &vind->isp_ver_major, &vind->isp_ver_minor);
		bsp_isp_ver_read_en(isp->id, 0);
		if (!isp->have_init) {
			memcpy(isp->isp_load.vir_addr, &isp_default_reg[0], ISP_LOAD_REG_SIZE);
			memset(&isp->load_shadow[0], 0, ISP_LOAD_DRAM_SIZE);
			isp->load_flag = 0;
			isp->have_init = 1;
		} else {
#if defined CONFIG_D3D
			if ((isp->load_shadow[0x2d4 + 0x3]) & (1<<1)) {
				/* clear D3D rec_en */
				isp->load_shadow[0x2d4 + 0x3] = (isp->load_shadow[0x2d4 + 0x3]) & (~(1<<1));
				memcpy(isp->isp_load.vir_addr, &isp->load_shadow[0], ISP_LOAD_DRAM_SIZE);
			}
#endif
		}
		isp->isp_frame_number = 0;
		isp->h3a_stat.buf[0].empty = 1;
		isp->h3a_stat.buf[0].dma_addr = isp->isp_stat.dma_addr;
		isp->h3a_stat.buf[0].virt_addr = isp->isp_stat.vir_addr;
		bsp_isp_set_statistics_addr(isp->id, (dma_addr_t)isp->isp_stat.dma_addr);
		bsp_isp_set_saved_addr(isp->id, (unsigned long)isp->isp_save.dma_addr);
#if !defined CONFIG_ARCH_SUN8IW16P1 && !defined CONFIG_ARCH_SUN8IW19P1 && !defined CONFIG_ARCH_SUN50IW10P1
		bsp_isp_set_load_addr(isp->id, (unsigned long)isp->isp_load.dma_addr);
		bsp_isp_set_table_addr(isp->id, LENS_GAMMA_TABLE, (unsigned long)(isp->isp_lut_tbl.dma_addr));
		bsp_isp_set_table_addr(isp->id, DRC_TABLE, (unsigned long)(isp->isp_drc_tbl.dma_addr));
#else
		bsp_isp_set_load_addr0(isp->id, (dma_addr_t)isp->isp_load.dma_addr);
		bsp_isp_set_load_addr1(isp->id, (dma_addr_t)isp->isp_load.dma_addr);
#endif
		bsp_isp_set_para_ready(isp->id, PARA_NOT_READY);
	}

	return 0;
}

static int __isp_set_load_reg(struct v4l2_subdev *sd, struct isp_table_reg_map *reg)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	if (!isp->use_isp)
		return 0;

	if (reg->size > ISP_LOAD_DRAM_SIZE) {
		vin_err("user ask for 0x%x data, it more than isp load_data 0x%x\n", reg->size, ISP_LOAD_DRAM_SIZE);
		return -EINVAL;
	}

	isp->load_flag = 1;
	return copy_from_user(&isp->load_shadow[0], reg->addr, reg->size);
}

#if !defined CONFIG_ARCH_SUN8IW16P1 && !defined CONFIG_ARCH_SUN8IW19P1 && !defined CONFIG_ARCH_SUN50IW10P1
static int __isp_set_table1_map(struct v4l2_subdev *sd, struct isp_table_reg_map *tbl)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	int ret;

	if (!isp->use_isp)
		return 0;

	if (tbl->size > ISP_TABLE_MAPPING1_SIZE) {
		vin_err("user ask for 0x%x data, it more than isp table1_data 0x%x\n", tbl->size, ISP_TABLE_MAPPING1_SIZE);
		return -EINVAL;
	}

	ret = copy_from_user(&isp->load_shadow[0] + ISP_LOAD_REG_SIZE, tbl->addr, tbl->size);
	if (ret < 0) {
		vin_err("copy table mapping1 from usr error!\n");
		return ret;
	}

	return 0;
}

static int __isp_set_table2_map(struct v4l2_subdev *sd, struct isp_table_reg_map *tbl)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	int ret;

	if (!isp->use_isp)
		return 0;

	if (tbl->size > ISP_TABLE_MAPPING2_SIZE) {
		vin_err("user ask for 0x%x data, it more than isp table1_data 0x%x\n", tbl->size, ISP_TABLE_MAPPING2_SIZE);
		return -EINVAL;
	}

	ret = copy_from_user(&isp->load_shadow[0] + ISP_LOAD_REG_SIZE + ISP_TABLE_MAPPING1_SIZE, tbl->addr, tbl->size);
	if (ret < 0) {
		vin_err("copy table mapping2 from usr error!\n");
		return ret;
	}

	return 0;
}
#endif

static long sunxi_isp_subdev_ioctl(struct v4l2_subdev *sd, unsigned int cmd,
				   void *arg)
{
	int ret = 0;

	switch (cmd) {
	case VIDIOC_VIN_ISP_LOAD_REG:
		ret = __isp_set_load_reg(sd, (struct isp_table_reg_map *)arg);
		break;
#if !defined CONFIG_ARCH_SUN8IW16P1 && !defined CONFIG_ARCH_SUN8IW19P1 && !defined CONFIG_ARCH_SUN50IW10P1
	case VIDIOC_VIN_ISP_TABLE1_MAP:
		ret = __isp_set_table1_map(sd, (struct isp_table_reg_map *)arg);
		break;
	case VIDIOC_VIN_ISP_TABLE2_MAP:
		ret = __isp_set_table2_map(sd, (struct isp_table_reg_map *)arg);
		break;
#endif
	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

#ifdef CONFIG_COMPAT

struct isp_table_reg_map32 {
	compat_caddr_t addr;
	unsigned int size;
};

#define VIDIOC_VIN_ISP_LOAD_REG32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 70, struct isp_table_reg_map32)

#define VIDIOC_VIN_ISP_TABLE1_MAP32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 71, struct isp_table_reg_map32)

#define VIDIOC_VIN_ISP_TABLE2_MAP32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 72, struct isp_table_reg_map32)

static int get_isp_table_reg_map32(struct isp_table_reg_map *kp,
			      struct isp_table_reg_map32 __user *up)
{
	u32 tmp;

	if (!access_ok(up, sizeof(struct isp_table_reg_map32)) ||
	    get_user(kp->size, &up->size) || get_user(tmp, &up->addr))
		return -EFAULT;
	kp->addr = compat_ptr(tmp);
	return 0;
}

static int put_isp_table_reg_map32(struct isp_table_reg_map *kp,
			      struct isp_table_reg_map32 __user *up)
{
	u32 tmp = (u32) ((unsigned long)kp->addr);

	if (!access_ok(up, sizeof(struct isp_table_reg_map32)) ||
	    put_user(kp->size, &up->size) || put_user(tmp, &up->addr))
		return -EFAULT;
	return 0;
}

static long isp_compat_ioctl32(struct v4l2_subdev *sd,
		unsigned int cmd, unsigned long arg)
{
	union {
		struct isp_table_reg_map isd;
	} karg;
	void __user *up = compat_ptr(arg);
	int compatible_arg = 1;
	long err = 0;

	vin_log(VIN_LOG_ISP, "%s cmd is %d\n", __func__, cmd);

	switch (cmd) {
	case VIDIOC_VIN_ISP_LOAD_REG32:
		cmd = VIDIOC_VIN_ISP_LOAD_REG;
		break;
	case VIDIOC_VIN_ISP_TABLE1_MAP32:
		cmd = VIDIOC_VIN_ISP_TABLE1_MAP;
		break;
	case VIDIOC_VIN_ISP_TABLE2_MAP32:
		cmd = VIDIOC_VIN_ISP_TABLE2_MAP;
		break;
	}

	switch (cmd) {
	case VIDIOC_VIN_ISP_LOAD_REG:
	case VIDIOC_VIN_ISP_TABLE1_MAP:
	case VIDIOC_VIN_ISP_TABLE2_MAP:
		err = get_isp_table_reg_map32(&karg.isd, up);
		compatible_arg = 0;
		break;
	}

	if (err)
		return err;

	if (compatible_arg)
		err = sunxi_isp_subdev_ioctl(sd, cmd, up);
	else {
		mm_segment_t old_fs = get_fs();

		set_fs(KERNEL_DS);
		err = sunxi_isp_subdev_ioctl(sd, cmd, &karg);
		set_fs(old_fs);
	}

	switch (cmd) {
	case VIDIOC_VIN_ISP_LOAD_REG:
	case VIDIOC_VIN_ISP_TABLE1_MAP:
	case VIDIOC_VIN_ISP_TABLE2_MAP:
		err = put_isp_table_reg_map32(&karg.isd, up);
		break;
	}

	return err;
}
#endif

/*
 * must reset all the pipeline through isp.
 */
void sunxi_isp_reset(struct isp_dev *isp)
{
#ifndef SUPPORT_ISP_TDM
	struct vin_md *vind = dev_get_drvdata(isp->subdev.v4l2_dev->dev);
	struct vin_core *vinc = NULL;
	struct prs_cap_mode mode = {.mode = VCAP};
	bool flags = 1;
	int i = 0;

	if (!isp->use_isp)
		return;

	if (!isp->subdev.entity.stream_count) {
		vin_err("isp%d is not used, cannot be resetted!!!\n", isp->id);
		return;
	}

	vin_print("%s:isp%d reset!!!,ISP frame number is %d\n", __func__, isp->id, isp->isp_frame_number);

	bsp_isp_set_para_ready(isp->id, PARA_NOT_READY);
#if defined CONFIG_D3D
	if ((isp->load_shadow[0x2d4 + 0x3]) & (1<<1)) {
		/* clear D3D rec_en 0x2d4 bit25*/
		isp->load_shadow[0x2d4 + 0x3] = (isp->load_shadow[0x2d4 + 0x3]) & (~(1<<1));
		memcpy(isp->isp_load.vir_addr, &isp->load_shadow[0], ISP_LOAD_DRAM_SIZE);
	}
#endif

	/*****************stop*******************/
	for (i = 0; i < VIN_MAX_DEV; i++) {
		if (vind->vinc[i] == NULL)
			continue;
		if (!vin_streaming(&vind->vinc[i]->vid_cap))
			continue;

		if (vind->vinc[i]->isp_sel == isp->id) {
			vinc = vind->vinc[i];
			vinc->vid_cap.frame_delay_cnt = 1;

			if (flags) {
				csic_prs_capture_stop(vinc->csi_sel);

#if defined CONFIG_ARCH_SUN8IW16P1
				if (vinc->mipi_sel == 0)
					cmb_rx_disable(vinc->mipi_sel);
#endif
				csic_prs_disable(vinc->csi_sel);
				csic_isp_bridge_disable(0);

				bsp_isp_clr_irq_status(isp->id, ISP_IRQ_EN_ALL);
				bsp_isp_enable(isp->id, 0);
				bsp_isp_capture_stop(isp->id);
				flags = 0;
			}
			vipp_disable(vinc->vipp_sel);
			vipp_top_clk_en(vinc->vipp_sel, 0);
			csic_dma_int_clear_status(vinc->vipp_sel, DMA_INT_ALL);
			csic_dma_top_disable(vinc->vipp_sel);
		}
	}

	/*****************start*******************/
	flags = 1;
	for (i = 0; i < VIN_MAX_DEV; i++) {
		if (vind->vinc[i] == NULL)
			continue;
		if (!vin_streaming(&vind->vinc[i]->vid_cap))
			continue;

		if (vind->vinc[i]->isp_sel == isp->id) {
			vinc = vind->vinc[i];

			csic_dma_top_enable(vinc->vipp_sel);
			vipp_top_clk_en(vinc->vipp_sel, 1);
			vipp_enable(vinc->vipp_sel);
			vinc->vin_status.frame_cnt = 0;
			vinc->vin_status.lost_cnt = 0;

			if (flags) {
				bsp_isp_enable(isp->id, 1);
				bsp_isp_set_para_ready(isp->id, PARA_READY);
				bsp_isp_capture_start(isp->id);
				isp->isp_frame_number = 0;

				csic_isp_bridge_enable(0);

				csic_prs_enable(vinc->csi_sel);

#if defined CONFIG_ARCH_SUN8IW16P1
				if (vinc->mipi_sel == 0)
					cmb_rx_enable(vinc->mipi_sel);
#endif

				csic_prs_capture_start(vinc->csi_sel, 1, &mode);
				flags = 0;
			}
		}
	}

	if (isp->sensor_lp_mode)
		schedule_work(&isp->s_sensor_stby_task);
#endif
}

void sunxi_isp_frame_sync_isr(struct v4l2_subdev *sd)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	struct v4l2_event event;
	static short isp_log_param;
	bool send_event = 0;

	memset(&event, 0, sizeof(event));
	event.type = V4L2_EVENT_FRAME_SYNC;
	event.id = 0;
	event.u.data[0] = 1;/*load type (0: load seperate; 1: load together)*/
	switch (isp->mf.colorspace) {
	case V4L2_COLORSPACE_REC709:
		event.u.data[1] = 1;
		break;
	case V4L2_COLORSPACE_BT2020:
		event.u.data[1] = 2;
		break;
	default:
		event.u.data[1] = 0;
		break;
	}
	if (isp_log_param != (vin_log_mask >> 16)) {
		isp_log_param = vin_log_mask >> 16;
		send_event = 1;
	} else {
		send_event = 0;
	}
	event.u.data[2] = isp_log_param;
	event.u.data[3] = isp_log_param >> 8;
	if ((isp->h3a_stat.frame_number < 2) || send_event)
		v4l2_event_queue(isp->subdev.devnode, &event);

	isp_stat_isr(&isp->h3a_stat);
}

int sunxi_isp_subscribe_event(struct v4l2_subdev *sd,
				  struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	vin_log(VIN_LOG_ISP, "%s id = %d\n", __func__, sub->id);
	if (sub->type == V4L2_EVENT_CTRL)
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	else
		return v4l2_event_subscribe(fh, sub, 1, NULL);
}

static const struct v4l2_subdev_core_ops sunxi_isp_subdev_core_ops = {
	.init = sunxi_isp_subdev_init,
	.ioctl = sunxi_isp_subdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = isp_compat_ioctl32,
#endif
	.subscribe_event = sunxi_isp_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops sunxi_isp_subdev_video_ops = {
	.s_stream = sunxi_isp_subdev_s_stream,
};

static const struct v4l2_subdev_pad_ops sunxi_isp_subdev_pad_ops = {
	.get_fmt = sunxi_isp_subdev_get_fmt,
	.set_fmt = sunxi_isp_subdev_set_fmt,
};

static struct v4l2_subdev_ops sunxi_isp_subdev_ops = {
	.core = &sunxi_isp_subdev_core_ops,
	.video = &sunxi_isp_subdev_video_ops,
	.pad = &sunxi_isp_subdev_pad_ops,
};

static int __sunxi_isp_ctrl(struct isp_dev *isp, struct v4l2_ctrl *ctrl)
{
	int ret = 0;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_AUTO_WHITE_BALANCE:
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_GAIN:
	case V4L2_CID_POWER_LINE_FREQUENCY:
	case V4L2_CID_HUE_AUTO:
	case V4L2_CID_WHITE_BALANCE_TEMPERATURE:
	case V4L2_CID_SHARPNESS:
	case V4L2_CID_CHROMA_AGC:
	case V4L2_CID_COLORFX:
	case V4L2_CID_AUTOBRIGHTNESS:
	case V4L2_CID_BAND_STOP_FILTER:
	case V4L2_CID_ILLUMINATORS_1:
	case V4L2_CID_ILLUMINATORS_2:
	case V4L2_CID_EXPOSURE_AUTO:
	case V4L2_CID_EXPOSURE_ABSOLUTE:
	case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
	case V4L2_CID_FOCUS_ABSOLUTE:
	case V4L2_CID_FOCUS_RELATIVE:
	case V4L2_CID_FOCUS_AUTO:
	case V4L2_CID_AUTO_EXPOSURE_BIAS:
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
	case V4L2_CID_WIDE_DYNAMIC_RANGE:
	case V4L2_CID_IMAGE_STABILIZATION:
	case V4L2_CID_ISO_SENSITIVITY:
	case V4L2_CID_ISO_SENSITIVITY_AUTO:
	case V4L2_CID_EXPOSURE_METERING:
	case V4L2_CID_SCENE_MODE:
	case V4L2_CID_3A_LOCK:
	case V4L2_CID_AUTO_FOCUS_START:
	case V4L2_CID_AUTO_FOCUS_STOP:
	case V4L2_CID_AUTO_FOCUS_RANGE:
	case V4L2_CID_FLASH_LED_MODE:
	case V4L2_CID_AUTO_FOCUS_INIT:
	case V4L2_CID_AUTO_FOCUS_RELEASE:
	case V4L2_CID_FLASH_LED_MODE_V1:
	case V4L2_CID_TAKE_PICTURE:
		break;
	default:
		break;
	}
	return ret;
}

#define ctrl_to_sunxi_isp(ctrl) \
	container_of(ctrl->handler, struct isp_dev, ctrls.handler)

static int sunxi_isp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isp_dev *isp = ctrl_to_sunxi_isp(ctrl);
	unsigned long flags;
	int ret;

	vin_log(VIN_LOG_ISP, "%s, val = %d, cur.val = %d\n",
		v4l2_ctrl_get_name(ctrl->id), ctrl->val, ctrl->cur.val);
	spin_lock_irqsave(&isp->slock, flags);
	ret = __sunxi_isp_ctrl(isp, ctrl);
	spin_unlock_irqrestore(&isp->slock, flags);

	return ret;
}

static int sunxi_isp_try_ctrl(struct v4l2_ctrl *ctrl)
{
	/*
	 * to cheat control framework, because of  when ctrl->cur.val == ctrl->val
	 * s_ctrl would not be called
	 */
	if ((ctrl->minimum == 0) && (ctrl->maximum == 1)) {
		if (ctrl->val)
			ctrl->cur.val = 0;
		else
			ctrl->cur.val = 1;
	} else {
		if (ctrl->val == ctrl->maximum)
			ctrl->cur.val = ctrl->val - 1;
		else
			ctrl->cur.val = ctrl->val + 1;
	}

	/*
	 * to cheat control framework, because of  when ctrl->flags is
	 * V4L2_CTRL_FLAG_VOLATILE, s_ctrl would not be called
	 */
	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
	case V4L2_CID_EXPOSURE_ABSOLUTE:
	case V4L2_CID_GAIN:
		if (ctrl->val != ctrl->cur.val)
			ctrl->flags &= ~V4L2_CTRL_FLAG_VOLATILE;
		break;
	default:
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops sunxi_isp_ctrl_ops = {
	.s_ctrl = sunxi_isp_s_ctrl,
	.try_ctrl = sunxi_isp_try_ctrl,
};

static const struct v4l2_ctrl_config ae_win_ctrls[] = {
	{
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AE_WIN_X1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AE_WIN_Y1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AE_WIN_X2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AE_WIN_Y2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}
};

static const struct v4l2_ctrl_config af_win_ctrls[] = {
	{
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AF_WIN_X1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AF_WIN_Y1,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AF_WIN_X2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AF_WIN_Y2,
		.name = "R GAIN",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 32,
		.max = 3264,
		.step = 16,
		.def = 256,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}
};

static const struct v4l2_ctrl_config custom_ctrls[] = {
	{
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_FOCUS_LENGTH,
		.name = "Focus Length",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 1000,
		.step = 1,
		.def = 280,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AUTO_FOCUS_INIT,
		.name = "AutoFocus Initial",
		.type = V4L2_CTRL_TYPE_BUTTON,
		.min = 0,
		.max = 0,
		.step = 0,
		.def = 0,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_AUTO_FOCUS_RELEASE,
		.name = "AutoFocus Release",
		.type = V4L2_CTRL_TYPE_BUTTON,
		.min = 0,
		.max = 0,
		.step = 0,
		.def = 0,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_TAKE_PICTURE,
		.name = "Take Picture",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 16,
		.step = 1,
		.def = 0,
	}, {
		.ops = &sunxi_isp_ctrl_ops,
		.id = V4L2_CID_FLASH_LED_MODE_V1,
		.name = "VIN Flash ctrl",
		.type = V4L2_CTRL_TYPE_MENU,
		.min = 0,
		.max = 2,
		.def = 0,
		.menu_skip_mask = 0x0,
		.qmenu = flash_led_mode_v1,
		.flags = 0,
		.step = 0,
	},
};
static const s64 iso_qmenu[] = {
	100, 200, 400, 800, 1600, 3200, 6400,
};
static const s64 exp_bias_qmenu[] = {
	-4, -3, -2, -1, 0, 1, 2, 3, 4,
};

int __isp_init_subdev(struct isp_dev *isp)
{
	struct v4l2_ctrl_handler *handler = &isp->ctrls.handler;
	struct v4l2_subdev *sd = &isp->subdev;
	struct sunxi_isp_ctrls *ctrls = &isp->ctrls;
	struct v4l2_ctrl *ctrl;
	int i, ret;

	mutex_init(&isp->subdev_lock);
	v4l2_subdev_init(sd, &sunxi_isp_subdev_ops);
	sd->grp_id = VIN_GRP_ID_ISP;
	sd->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "sunxi_isp.%u", isp->id);
	v4l2_set_subdevdata(sd, isp);

	v4l2_ctrl_handler_init(handler, 38 + ARRAY_SIZE(ae_win_ctrls)
		+ ARRAY_SIZE(af_win_ctrls) + ARRAY_SIZE(custom_ctrls));

	for (i = 0; i < ARRAY_SIZE(ae_win_ctrls); i++)
		ctrls->ae_win[i] = v4l2_ctrl_new_custom(handler,
						&ae_win_ctrls[i], NULL);
	v4l2_ctrl_cluster(ARRAY_SIZE(ae_win_ctrls), &ctrls->ae_win[0]);

	for (i = 0; i < ARRAY_SIZE(af_win_ctrls); i++)
		ctrls->af_win[i] = v4l2_ctrl_new_custom(handler,
						&af_win_ctrls[i], NULL);
	v4l2_ctrl_cluster(ARRAY_SIZE(af_win_ctrls), &ctrls->af_win[0]);

	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_BRIGHTNESS, -128, 128, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_CONTRAST, -128, 128, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_SATURATION, -256, 512, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_HUE, -180, 180, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_AUTO_WHITE_BALANCE, 0, 1, 1, 1);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_EXPOSURE, 1, 65536 * 16, 1, 1);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_GAIN, 16, 6000 * 16, 1, 16);

	v4l2_ctrl_new_std_menu(handler, &sunxi_isp_ctrl_ops,
			       V4L2_CID_POWER_LINE_FREQUENCY,
			       V4L2_CID_POWER_LINE_FREQUENCY_AUTO, 0,
			       V4L2_CID_POWER_LINE_FREQUENCY_AUTO);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_HUE_AUTO, 0, 1, 1, 1);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops,
			  V4L2_CID_WHITE_BALANCE_TEMPERATURE, 2800, 10000, 1, 6500);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_SHARPNESS, 0, 1000, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_CHROMA_AGC, 0, 1, 1, 1);
	v4l2_ctrl_new_std_menu(handler, &sunxi_isp_ctrl_ops, V4L2_CID_COLORFX,
			       V4L2_COLORFX_SET_CBCR, 0, V4L2_COLORFX_NONE);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_AUTOBRIGHTNESS, 0, 1, 1, 1);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_BAND_STOP_FILTER, 0, 1, 1, 1);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_ILLUMINATORS_1, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_ILLUMINATORS_2, 0, 1, 1, 0);
	v4l2_ctrl_new_std_menu(handler, &sunxi_isp_ctrl_ops, V4L2_CID_EXPOSURE_AUTO,
			       V4L2_EXPOSURE_APERTURE_PRIORITY, 0,
			       V4L2_EXPOSURE_AUTO);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_EXPOSURE_ABSOLUTE, 1, 30 * 1000000, 1, 1);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_EXPOSURE_AUTO_PRIORITY, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_FOCUS_ABSOLUTE, 0, 127, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_FOCUS_RELATIVE, -127, 127, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_FOCUS_AUTO, 0, 1, 1, 1);
	v4l2_ctrl_new_int_menu(handler, &sunxi_isp_ctrl_ops, V4L2_CID_AUTO_EXPOSURE_BIAS,
			       ARRAY_SIZE(exp_bias_qmenu) - 1,
			       ARRAY_SIZE(exp_bias_qmenu) / 2, exp_bias_qmenu);
	v4l2_ctrl_new_std_menu(handler, &sunxi_isp_ctrl_ops,
			       V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,
			       V4L2_WHITE_BALANCE_SHADE, 0,
			       V4L2_WHITE_BALANCE_AUTO);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_WIDE_DYNAMIC_RANGE, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_IMAGE_STABILIZATION, 0, 1, 1, 0);
	v4l2_ctrl_new_int_menu(handler, &sunxi_isp_ctrl_ops, V4L2_CID_ISO_SENSITIVITY,
			       ARRAY_SIZE(iso_qmenu) - 1,
			       ARRAY_SIZE(iso_qmenu) / 2 - 1, iso_qmenu);
	v4l2_ctrl_new_std_menu(handler, &sunxi_isp_ctrl_ops,
			       V4L2_CID_ISO_SENSITIVITY_AUTO,
			       V4L2_ISO_SENSITIVITY_AUTO, 0,
			       V4L2_ISO_SENSITIVITY_AUTO);
	v4l2_ctrl_new_std_menu(handler, &sunxi_isp_ctrl_ops,
			       V4L2_CID_EXPOSURE_METERING,
			       V4L2_EXPOSURE_METERING_MATRIX, 0,
			       V4L2_EXPOSURE_METERING_AVERAGE);
	v4l2_ctrl_new_std_menu(handler, &sunxi_isp_ctrl_ops, V4L2_CID_SCENE_MODE,
			       V4L2_SCENE_MODE_TEXT, 0, V4L2_SCENE_MODE_NONE);
	ctrl = v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_3A_LOCK, 0, 7, 0, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_AUTO_FOCUS_START, 0, 0, 0, 0);
	v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_AUTO_FOCUS_STOP, 0, 0, 0, 0);
	ctrl = v4l2_ctrl_new_std(handler, &sunxi_isp_ctrl_ops, V4L2_CID_AUTO_FOCUS_STATUS, 0, 7, 0, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_new_std_menu(handler, &sunxi_isp_ctrl_ops, V4L2_CID_AUTO_FOCUS_RANGE,
			       V4L2_AUTO_FOCUS_RANGE_INFINITY, 0,
			       V4L2_AUTO_FOCUS_RANGE_AUTO);
	v4l2_ctrl_new_std_menu(handler, &sunxi_isp_ctrl_ops, V4L2_CID_FLASH_LED_MODE,
			       V4L2_FLASH_LED_MODE_RED_EYE, 0,
			       V4L2_FLASH_LED_MODE_NONE);

	for (i = 0; i < ARRAY_SIZE(custom_ctrls); i++)
		v4l2_ctrl_new_custom(handler, &custom_ctrls[i], NULL);

	if (handler->error)
		return handler->error;

	/*sd->entity->ops = &isp_media_ops;*/
	isp->isp_pads[ISP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	isp->isp_pads[ISP_PAD_SOURCE_ST].flags = MEDIA_PAD_FL_SOURCE;
	isp->isp_pads[ISP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;

	ret = media_entity_pads_init(&sd->entity, ISP_PAD_NUM, isp->isp_pads);
	if (ret < 0)
		return ret;

	sd->ctrl_handler = handler;
	/*sd->internal_ops = &sunxi_isp_sd_internal_ops;*/
	return 0;
}

static int isp_resource_alloc(struct isp_dev *isp)
{
	int ret = 0;
#if !defined CONFIG_ARCH_SUN8IW16P1 && !defined CONFIG_ARCH_SUN8IW19P1 && !defined CONFIG_ARCH_SUN50IW10P1
	isp->isp_stat.size = ISP_SAVE_DRAM_SIZE + ISP_LOAD_DRAM_SIZE;
	ret = os_mem_alloc(&isp->pdev->dev, &isp->isp_stat);
	if (ret < 0) {
		vin_err("isp statistic buf requset failed!\n");
		return -ENOMEM;
	}
	isp->isp_save.dma_addr = isp->isp_stat.dma_addr + ISP_STAT_TOTAL_SIZE;
	isp->isp_save.vir_addr = isp->isp_stat.vir_addr + ISP_STAT_TOTAL_SIZE;
	isp->isp_load.dma_addr = isp->isp_save.dma_addr + ISP_SAVED_REG_SIZE;
	isp->isp_load.vir_addr = isp->isp_save.vir_addr + ISP_SAVED_REG_SIZE;
	isp->isp_lut_tbl.dma_addr = isp->isp_load.dma_addr + ISP_LOAD_REG_SIZE;
	isp->isp_lut_tbl.vir_addr = isp->isp_load.vir_addr + ISP_LOAD_REG_SIZE;
	isp->isp_drc_tbl.dma_addr = isp->isp_lut_tbl.dma_addr + ISP_TABLE_MAPPING1_SIZE;
	isp->isp_drc_tbl.vir_addr = isp->isp_lut_tbl.vir_addr + ISP_TABLE_MAPPING1_SIZE;
#else
	isp->isp_stat.size = ISP_SAVE_DRAM_SIZE + ISP_LOAD_DRAM_SIZE;
	ret = os_mem_alloc(&isp->pdev->dev, &isp->isp_stat);
	if (ret < 0) {
		vin_err("isp statistic buf requset failed!\n");
		return -ENOMEM;
	}
	isp->isp_load.dma_addr = isp->isp_stat.dma_addr + ISP_STAT_TOTAL_SIZE;
	isp->isp_load.vir_addr = isp->isp_stat.vir_addr + ISP_STAT_TOTAL_SIZE;
	isp->isp_save.dma_addr = isp->isp_stat.dma_addr;
	isp->isp_save.vir_addr = isp->isp_stat.vir_addr;
#endif
	return ret;
}
static void isp_resource_free(struct isp_dev *isp)
{
	os_mem_free(&isp->pdev->dev, &isp->isp_stat);
}

static irqreturn_t isp_isr(int irq, void *priv)
{
	struct isp_dev *isp = (struct isp_dev *)priv;
	unsigned int load_val;
	unsigned long flags;

	if (!isp->use_isp)
		return 0;

	if (isp->subdev.entity.stream_count == 0) {
		bsp_isp_clr_irq_status(isp->id, ISP_IRQ_EN_ALL);
		return IRQ_HANDLED;
	}

	vin_log(VIN_LOG_ISP, "isp%d interrupt, status is 0x%x!!!\n", isp->id,
		bsp_isp_get_irq_status(isp->id, ISP_IRQ_STATUS_ALL));

	spin_lock_irqsave(&isp->slock, flags);
#if !defined CONFIG_ARCH_SUN8IW16P1 && !defined CONFIG_ARCH_SUN8IW19P1 && !defined CONFIG_ARCH_SUN50IW10P1
	if (bsp_isp_get_irq_status(isp->id, SRC0_FIFO_OF_PD)) {
		vin_err("isp%d source0 fifo overflow\n", isp->id);
		bsp_isp_clr_irq_status(isp->id, SRC0_FIFO_OF_PD);
		if (bsp_isp_get_irq_status(isp->id, CIN_FIFO_OF_PD)) {
			vin_err("isp%d Cin0 fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, CIN_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, DPC_FIFO_OF_PD)) {
			vin_err("isp%d DPC fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, DPC_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, D2D_FIFO_OF_PD)) {
			vin_err("isp%d D2D fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, D2D_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, BIS_FIFO_OF_PD)) {
			vin_err("isp%d BIS fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, BIS_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, CNR_FIFO_OF_PD)) {
			vin_err("isp%d CNR fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, CNR_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, PLTM_FIFO_OF_PD)) {
			vin_err("isp%d PLTM fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, PLTM_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, D3D_WRITE_FIFO_OF_PD)) {
			vin_err("isp%d D3D cmp write to DDR fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, D3D_WRITE_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, D3D_READ_FIFO_OF_PD)) {
			vin_err("isp%d D3D umcmp read from DDR fifo empty\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, D3D_READ_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, D3D_WT2CMP_FIFO_OF_PD)) {
			vin_err("isp%d D3D write to cmp fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, D3D_WT2CMP_FIFO_OF_PD);
		}

		if (bsp_isp_get_irq_status(isp->id, WDR_WRITE_FIFO_OF_PD)) {
			vin_err("isp%d WDR cmp write to DDR fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, WDR_WRITE_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, WDR_READ_FIFO_OF_PD)) {
			vin_err("isp%d WDR umcmp read from DDR fifo empty\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, WDR_READ_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, WDR_WT2CMP_FIFO_OF_PD)) {
			vin_err("isp%d WDR write to cmp fifo overflow\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, WDR_WT2CMP_FIFO_OF_PD);
		}
		if (bsp_isp_get_irq_status(isp->id, D3D_HB_PD)) {
			vin_err("isp%d Hblanking is not enough for D3D\n", isp->id);
			bsp_isp_clr_irq_status(isp->id, D3D_HB_PD);
		}
		/*isp reset*/
		sunxi_isp_reset(isp);
	}
#else
	if (bsp_isp_get_irq_status(isp->id, S0_FIFO_OF_PD)) {
		vin_err("isp%d source0 fifo overflow\n", isp->id);
		bsp_isp_clr_irq_status(isp->id, S0_FIFO_OF_PD);
		if (bsp_isp_get_internal_status0(isp->id, S0_CIN_FIFO_OF_PD)) {
			vin_err("isp%d Cin0 fifo overflow\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, S0_CIN_FIFO_OF_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, BIS_FIFO_OF_PD)) {
			vin_err("isp%d BIS fifo overflow\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, BIS_FIFO_OF_PD);
		}
#if !defined CONFIG_ARCH_SUN50IW10P1
		if (bsp_isp_get_internal_status0(isp->id, DPC_FIFO_OF_PD)) {
			vin_err("isp%d DPC fifo overflow\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, DPC_FIFO_OF_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, CNR_FIFO_OF_PD)) {
			vin_err("isp%d CNR fifo overflow\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, CNR_FIFO_OF_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, PLTM_FIFO_OF_PD)) {
			vin_err("isp%d PLTM fifo overflow\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, PLTM_FIFO_OF_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, D3D_WRITE_FIFO_OF_PD)) {
			vin_err("isp%d D3D cmp write to DDR fifo overflow\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, D3D_WRITE_FIFO_OF_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, D3D_READ_FIFO_OF_PD)) {
			vin_err("isp%d D3D umcmp read from DDR fifo empty\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, D3D_READ_FIFO_OF_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, WDR_WRITE_FIFO_OF_PD)) {
			vin_err("isp%d WDR cmp write to DDR fifo overflow\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, WDR_WRITE_FIFO_OF_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, WDR_READ_FIFO_OF_PD)) {
			vin_err("isp%d WDR umcmp read from DDR fifo empty\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, WDR_READ_FIFO_OF_PD);
		}
#endif
#if defined CONFIG_ARCH_SUN8IW19P1
		if (bsp_isp_get_internal_status0(isp->id, LCA_RGB_FIFO_R_EMP_PD)) {
			vin_err("isp%d RGB fifo of LCA read empty\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, LCA_RGB_FIFO_R_EMP_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, LCA_RGB_FIFO_W_FULL_PD)) {
			vin_err("isp%d RGB fifo of LCA write full\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, LCA_RGB_FIFO_W_FULL_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, LCA_BY_FIFO_R_EMP_PD)) {
			vin_err("isp%d bayer fifo of LCA read empty\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, LCA_BY_FIFO_R_EMP_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, LCA_BY_FIFO_W_FULL_PD)) {
			vin_err("isp%d bayer fifo of LCA write full\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, LCA_BY_FIFO_W_FULL_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, D3D_K_FIFO_W_FULL_PD)) {
			vin_err("isp%d write fifo of D3D K data full\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, D3D_K_FIFO_W_FULL_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, D3D_RAW_FIFO_W_FULL_PD)) {
			vin_err("isp%d write fifo of D3D RAW data full\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, D3D_RAW_FIFO_W_FULL_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, D3D_K_FIFO_R_EMP_PD)) {
			vin_err("isp%d read fifo of D3D K data empty\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, D3D_K_FIFO_R_EMP_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, D3D_REF_FIFO_R_EMP_PD)) {
			vin_err("isp%d read fifo of D3D REF data empty\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, D3D_REF_FIFO_R_EMP_PD);
		}
		if (bsp_isp_get_internal_status0(isp->id, D3D_LTF_FIFO_R_EMP_PD)) {
			vin_err("isp%d read fifo of D3D LTF data empty\n", isp->id);
			bsp_isp_clr_internal_status0(isp->id, D3D_LTF_FIFO_R_EMP_PD);
		}
#endif
		/*isp reset*/
		sunxi_isp_reset(isp);
	}
#endif
	if (bsp_isp_get_irq_status(isp->id, HB_SHORT_PD)) {
		vin_err("isp%d Hblanking is short (less than 96 cycles)\n", isp->id);
		bsp_isp_clr_irq_status(isp->id, HB_SHORT_PD);
	}

	if (bsp_isp_get_irq_status(isp->id, FRAME_ERROR_PD)) {
		bsp_isp_get_s0_ch_fmerr_cnt(isp->id, &isp->err_size);
		bsp_isp_get_s0_ch_hb_cnt(isp->id, &isp->hb_max, &isp->hb_min);
		vin_err("isp%d frame error, size %d %d, hblank max %d min %d!!\n", isp->id,
			isp->err_size.width, isp->err_size.height, isp->hb_max, isp->hb_min);
		bsp_isp_clr_irq_status(isp->id, FRAME_ERROR_PD);
		sunxi_isp_reset(isp);
	}

	if (bsp_isp_get_irq_status(isp->id, FRAME_LOST_PD)) {
		vin_err("isp%d frame lost\n", isp->id);
		bsp_isp_clr_irq_status(isp->id, FRAME_LOST_PD);
		sunxi_isp_reset(isp);
	}

#if defined CONFIG_ARCH_SUN8IW19P1
	if (bsp_isp_get_irq_status(isp->id, S0_BTYPE_ERROR_PD)) {
		vin_err("isp%d input btype error\n", isp->id);
		bsp_isp_clr_irq_status(isp->id, S0_BTYPE_ERROR_PD);
	}

	if (bsp_isp_get_irq_status(isp->id, ADDR_ERROR_PD)) {
		vin_err("isp%d aligned addr error\n", isp->id);
		bsp_isp_clr_irq_status(isp->id, ADDR_ERROR_PD);
	}

	if (bsp_isp_get_irq_status(isp->id, LBC_ERROR_PD)) {
		vin_err("isp%d LBC de-compress error\n", isp->id);
		bsp_isp_clr_irq_status(isp->id, LBC_ERROR_PD);
		if (bsp_isp_get_lbc_internal_status(isp->id, WDR_LBC_DEC_ERR_PD)) {
			vin_err("isp%d WDR LBC decode error\n", isp->id);
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_MSQ_DEC_ERR_PD << WDR_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d msq decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_MSQ_DEC_ERR_PD << WDR_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_DTS_DEC_ERR_PD << WDR_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d dts decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_DTS_DEC_ERR_PD << WDR_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_QP_DEC_ERR_PD << WDR_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d qp decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_QP_DEC_ERR_PD << WDR_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_CODEC_BIT_LOST_PD << WDR_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d codec bit lost error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_CODEC_BIT_LOST_PD << WDR_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_CODEC_RED_ERR_PD << WDR_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d codec redundancy error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_CODEC_RED_ERR_PD << WDR_LBC_DEC_ERR_OFF);
			}
		}
		if (bsp_isp_get_lbc_internal_status(isp->id, D3D_K_LBC_DEC_ERR_PD)) {
			vin_err("isp%d D3D K LBC decode error\n", isp->id);
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_MSQ_DEC_ERR_PD << D3D_K_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d msq decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_MSQ_DEC_ERR_PD << D3D_K_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_DTS_DEC_ERR_PD << D3D_K_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d dts decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_DTS_DEC_ERR_PD << D3D_K_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_QP_DEC_ERR_PD << D3D_K_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d qp decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_QP_DEC_ERR_PD << D3D_K_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_CODEC_BIT_LOST_PD << D3D_K_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d codec bit lost error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_CODEC_BIT_LOST_PD << D3D_K_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_CODEC_RED_ERR_PD << D3D_K_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d codec redundancy error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_CODEC_RED_ERR_PD << D3D_K_LBC_DEC_ERR_OFF);
			}
		}
		if (bsp_isp_get_lbc_internal_status(isp->id, D3D_REF_LBC_DEC_ERR_PD)) {
			vin_err("isp%d D3D reference frame LBC decode error\n", isp->id);
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_MSQ_DEC_ERR_PD << D3D_REF_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d msq decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_MSQ_DEC_ERR_PD << D3D_REF_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_DTS_DEC_ERR_PD << D3D_REF_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d dts decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_DTS_DEC_ERR_PD << D3D_REF_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_QP_DEC_ERR_PD << D3D_REF_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d qp decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_QP_DEC_ERR_PD << D3D_REF_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_CODEC_BIT_LOST_PD << D3D_REF_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d codec bit lost error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_CODEC_BIT_LOST_PD << D3D_REF_LBC_DEC_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_CODEC_RED_ERR_PD << D3D_REF_LBC_DEC_ERR_OFF)) {
				vin_err("isp%d codec redundancy error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_CODEC_RED_ERR_PD << D3D_REF_LBC_DEC_ERR_OFF);
			}
		}
		if (bsp_isp_get_lbc_internal_status(isp->id, D3D_LTF_LBC_DEV_ERR_PD)) {
			vin_err("isp%d D3D long time reference frame LBC decode error\n", isp->id);
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_MSQ_DEC_ERR_PD << D3D_LTF_LBC_DEV_ERR_OFF)) {
				vin_err("isp%d msq decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_MSQ_DEC_ERR_PD << D3D_LTF_LBC_DEV_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_DTS_DEC_ERR_PD << D3D_LTF_LBC_DEV_ERR_OFF)) {
				vin_err("isp%d dts decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_DTS_DEC_ERR_PD << D3D_LTF_LBC_DEV_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_QP_DEC_ERR_PD << D3D_LTF_LBC_DEV_ERR_OFF)) {
				vin_err("isp%d qp decode error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_QP_DEC_ERR_PD << D3D_LTF_LBC_DEV_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_CODEC_BIT_LOST_PD << D3D_LTF_LBC_DEV_ERR_OFF)) {
				vin_err("isp%d codec bit lost error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_CODEC_BIT_LOST_PD << D3D_LTF_LBC_DEV_ERR_OFF);
			}
			if (bsp_isp_get_lbc_internal_status(isp->id, LBC_CODEC_RED_ERR_PD << D3D_LTF_LBC_DEV_ERR_OFF)) {
				vin_err("isp%d codec redundancy error\n", isp->id);
				bsp_isp_clr_lbc_internal_status(isp->id, LBC_CODEC_RED_ERR_PD << D3D_LTF_LBC_DEV_ERR_OFF);
			}
		}
	}
#endif

	if (bsp_isp_get_irq_status(isp->id, PARA_LOAD_PD)) {
		bsp_isp_clr_irq_status(isp->id, PARA_LOAD_PD);
		if (isp->ptn_type) {
			spin_unlock_irqrestore(&isp->slock, flags);
			return IRQ_HANDLED;
		}
		bsp_isp_set_para_ready(isp->id, PARA_NOT_READY);
		if (isp->load_flag)
			memcpy(isp->isp_load.vir_addr, &isp->load_shadow[0], ISP_LOAD_DRAM_SIZE);
		isp->isp_ob.set_cnt++;
		load_val = bsp_isp_load_update_flag(isp->id);
		if (isp->wdr_mode != ISP_NORMAL_MODE)
			isp_wdr_pingpong_set(isp);
		else {
			vin_log(VIN_LOG_ISP, "please close wdr in normal mode!!\n");
			load_val = load_val & ~WDR_UPDATE;
			bsp_isp_module_disable(isp->id, WDR_EN);
#if !defined CONFIG_ARCH_SUN8IW16P1 && !defined CONFIG_ARCH_SUN8IW19P1 && !defined CONFIG_ARCH_SUN50IW10P1
			bsp_isp_set_wdr_mode(isp->id, ISP_NORMAL_MODE);
#endif
		}
		if (isp->large_image == 2) {
			bsp_isp_module_disable(isp->id, PLTM_EN | D3D_EN | AE_EN | AWB_EN | AF_EN | HIST_EN);
			if (isp->left_right == 1) {
				isp->left_right = 0;
				isp->isp_ob.ob_start.hor -= isp->isp_ob.ob_valid.width - LARGE_IMAGE_OFF * 2;
			} else {
				isp->left_right = 1;
				isp->isp_ob.ob_start.hor += isp->isp_ob.ob_valid.width - LARGE_IMAGE_OFF * 2;
			}
		}
#if !defined CONFIG_D3D
		bsp_isp_module_disable(isp->id, D3D_EN);
		load_val = load_val & ~D3D_UPDATE;
#endif
		bsp_isp_set_size(isp->id, &isp->isp_ob);
		bsp_isp_update_table(isp->id, (unsigned short)load_val);
#if defined CONFIG_ARCH_SUN8IW12P1 || defined CONFIG_ARCH_SUN8IW17P1 || defined CONFIG_ARCH_SUN8IW19P1
		isp_3d_pingpong_update(isp);
#endif
		bsp_isp_set_para_ready(isp->id, PARA_READY);
	}
	spin_unlock_irqrestore(&isp->slock, flags);

	if (bsp_isp_get_irq_status(isp->id, FINISH_PD)) {
		isp->isp_frame_number++;
		bsp_isp_clr_irq_status(isp->id, FINISH_PD);
#ifdef SUPPORT_PTN
		if (isp->ptn_type) {
			bsp_isp_set_para_ready(isp->id, PARA_NOT_READY);
			memcpy(isp->isp_load.vir_addr, &isp->load_shadow[0] + (isp->ptn_isp_cnt%3) * ISP_LOAD_DRAM_SIZE, ISP_LOAD_DRAM_SIZE);
			isp->ptn_isp_cnt++;
			load_val = bsp_isp_load_update_flag(isp->id);
			bsp_isp_set_size(isp->id, &isp->isp_ob);
			bsp_isp_update_table(isp->id, (unsigned short)load_val);
			isp_3d_pingpong_update(isp);
			bsp_isp_set_para_ready(isp->id, PARA_READY);
		}
#endif
		if (!isp->f1_after_librun) {
			sunxi_isp_frame_sync_isr(&isp->subdev);
			if (isp->h3a_stat.stat_en_flag)
				isp->f1_after_librun = 1;
		} else {
			if (isp->load_flag || (isp->event_lost_cnt == 10)) {
				sunxi_isp_frame_sync_isr(&isp->subdev);
				isp->event_lost_cnt = 0;
			} else {
				isp->event_lost_cnt++;
			}
		}
		isp->load_flag = 0;
	}

	return IRQ_HANDLED;
}

static unsigned int isp_id;

static int isp_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct isp_dev *isp = NULL;
	int ret = 0;

	if (np == NULL) {
		vin_err("ISP failed to get of node\n");
		return -ENODEV;
	}

	isp = kzalloc(sizeof(struct isp_dev), GFP_KERNEL);
	if (!isp) {
		ret = -ENOMEM;
		goto ekzalloc;
	}

	of_property_read_u32(np, "device_id", &pdev->id);
	if (pdev->id < 0) {
		vin_err("ISP failed to get device id\n");
		ret = -EINVAL;
		goto freedev;
	}

	isp->id = pdev->id;
	isp->pdev = pdev;
	isp->nosend_ispoff = 0;

	if (isp->id > 0xf0) {
		isp->is_empty = 1;
		isp->id = isp_id;
		isp_id++;
	} else {
		isp->base = of_iomap(np, 0);
		isp->is_empty = 0;
		/*get irq resource */
		isp->irq = irq_of_parse_and_map(np, 0);
		if (isp->irq <= 0) {
			vin_err("failed to get ISP IRQ resource\n");
			goto unmap;
		}

		ret = request_irq(isp->irq, isp_isr, IRQF_SHARED, isp->pdev->name, isp);
		if (ret) {
			vin_err("isp%d request irq failed\n", isp->id);
			goto unmap;
		}
		if (isp_resource_alloc(isp) < 0) {
			ret = -ENOMEM;
			goto freeirq;
		}
		bsp_isp_map_reg_addr(isp->id, (unsigned long)isp->base);
		bsp_isp_map_load_dram_addr(isp->id, (unsigned long)isp->isp_load.vir_addr);
	}

	__isp_init_subdev(isp);

	spin_lock_init(&isp->slock);

	ret = vin_isp_h3a_init(isp);
	if (ret < 0) {
		vin_err("VIN H3A initialization failed\n");
			goto free_res;
	}

	INIT_WORK(&isp->s_sensor_stby_task, __isp_s_sensor_stby_handle);

	platform_set_drvdata(pdev, isp);
	glb_isp[isp->id] = isp;

	vin_log(VIN_LOG_ISP, "isp%d probe end!\n", isp->id);
	return 0;
free_res:
	isp_resource_free(isp);
freeirq:
	if (!isp->is_empty)
		free_irq(isp->irq, isp);
unmap:
	if (!isp->is_empty)
		iounmap(isp->base);
	else
		kfree(isp->base);
freedev:
	kfree(isp);
ekzalloc:
	vin_err("isp probe err!\n");
	return ret;
}

static int isp_remove(struct platform_device *pdev)
{
	struct isp_dev *isp = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = &isp->subdev;

	platform_set_drvdata(pdev, NULL);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	v4l2_set_subdevdata(sd, NULL);

	if (!isp->is_empty) {
		isp_resource_free(isp);
		free_irq(isp->irq, isp);
		if (isp->base)
			iounmap(isp->base);
	}
	vin_isp_h3a_cleanup(isp);
	media_entity_cleanup(&isp->subdev.entity);
	kfree(isp);
	return 0;
}

static const struct of_device_id sunxi_isp_match[] = {
	{.compatible = "allwinner,sunxi-isp",},
	{},
};

static struct platform_driver isp_platform_driver = {
	.probe = isp_probe,
	.remove = isp_remove,
	.driver = {
		   .name = ISP_MODULE_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = sunxi_isp_match,
		   },
};

void sunxi_isp_sensor_type(struct v4l2_subdev *sd, int use_isp)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	isp->use_isp = use_isp;
	if (isp->is_empty)
		isp->use_isp = 0;
}

void sunxi_isp_sensor_fps(struct v4l2_subdev *sd, int fps)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	isp->h3a_stat.sensor_fps = fps;
}

void sunxi_isp_debug(struct v4l2_subdev *sd, struct isp_debug_mode *isp_debug)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);

	isp->isp_dbg = *isp_debug;
}

void sunxi_isp_ptn(struct v4l2_subdev *sd, unsigned int ptn_type)
{
	struct isp_dev *isp = v4l2_get_subdevdata(sd);
	isp->ptn_type = ptn_type;
}

struct v4l2_subdev *sunxi_isp_get_subdev(int id)
{
	if (id < VIN_MAX_ISP)
		return &glb_isp[id]->subdev;
	else
		return NULL;
}

struct v4l2_subdev *sunxi_stat_get_subdev(int id)
{
	if (id < VIN_MAX_ISP && glb_isp[id])
		return &glb_isp[id]->h3a_stat.sd;
	else
		return NULL;
}

int sunxi_isp_platform_register(void)
{
	return platform_driver_register(&isp_platform_driver);
}

void sunxi_isp_platform_unregister(void)
{
	platform_driver_unregister(&isp_platform_driver);
	vin_log(VIN_LOG_ISP, "isp_exit end\n");
}
