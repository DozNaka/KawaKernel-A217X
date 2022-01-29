/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License Version 2 as publised
 * by the Free Software Foundation.
 *
 * BTS Bus Traffic Shaper device driver operation functions set
 *
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <dt-bindings/soc/samsung/exynos-bts.h>

#include "regs-bts9630.h"
#include "bts.h"

static void init_trexbts(void __iomem *base)
{
}

static void init_sciqfull(void __iomem *base)
{
}

static void init_smcqbusy(void __iomem *base)
{
}

static int set_drexbts(void __iomem *base, struct bts_stat *stat)
{
	int i;

	if (!base || !stat)
		return -ENODATA;

	if (stat->drex_on) {
		__raw_writel(stat->write_flush_config_0, base + WRITE_FLUSH_CONFIG_0);
		__raw_writel(stat->write_flush_config_1, base + WRITE_FLUSH_CONFIG_1);

		for (i = 0; i < MAX_QOS + 1; i++)
			__raw_writel(stat->drex_timeout[i],
					base + QOS_TIMER_0 + (4 * i));

		for (i = 0; i < VC_TIMER_TH_NR; i++)
			__raw_writel(stat->vc_timer_th[i],
					base + VC_TIMER_TH_0 + (4 * i));

		__raw_writel(stat->cutoff_con, base + CUTOFF_CON);
		__raw_writel(stat->brb_cutoff_con, base + BRB_CUTOFF_CON);
		__raw_writel(stat->wdbuf_cutoff_con, base + WDBUF_CUTOFF_CON);
	}

	if (stat->drex_pf_on) {
		__raw_writel(stat->pf_rreq_thrt_con, base + RREQ_THRT_CON);
		__raw_writel(stat->allow_mo_for_region, base + RREQ_THRT_MO_P2);

		for (i = 0; i < PF_TIMER_NR; i++)
			__raw_writel(stat->pf_qos_timer[i],
					base + PF_QOS_TIMER_0 + (4 * i));
	}

	return 0;
}
static int get_drexbts(void __iomem *base, struct bts_stat *stat)
{
	int i;

	if (!base || !stat)
		return -ENODATA;

	if (stat->drex_on) {
		stat->write_flush_config_0 = __raw_readl(base + WRITE_FLUSH_CONFIG_0);
		stat->write_flush_config_1 = __raw_readl(base + WRITE_FLUSH_CONFIG_1);

		for (i = 0; i < MAX_QOS + 1; i++)
			stat->drex_timeout[i] =
					__raw_readl(base + QOS_TIMER_0 + (4 * i));

		for (i = 0; i < VC_TIMER_TH_NR; i++)
			stat->vc_timer_th[i] =
					__raw_readl(base + VC_TIMER_TH_0 + (4 * i));

		stat->cutoff_con = __raw_readl(base + CUTOFF_CON);
		stat->brb_cutoff_con = __raw_readl(base + BRB_CUTOFF_CON);
		stat->wdbuf_cutoff_con = __raw_readl(base + WDBUF_CUTOFF_CON);
	}

	if (stat->drex_pf_on) {
		stat->pf_rreq_thrt_con = __raw_readl(base + RREQ_THRT_CON);
		stat->allow_mo_for_region = __raw_readl(base + RREQ_THRT_MO_P2);

		for (i = 0; i < PF_TIMER_NR; i++)
			stat->pf_qos_timer[i] =
					__raw_readl(base + PF_QOS_TIMER_0 + (4 * i));
	}

	return 0;
}

static int set_buscbts_qmax_threshold(void __iomem *base, unsigned int r_thd, unsigned int w_thd)
{
	unsigned int tmp_reg_r = 0, tmp_reg_w = 0;

	if (!base)
		return -ENODATA;

	if (r_thd > 0xFFFF)
		r_thd = 0xFFFF;

	tmp_reg_r = __raw_readl(base + QMAX_THRESHOLD_R);
	tmp_reg_r &= ~(0xFFFF << 0);
	tmp_reg_r |= (r_thd << 0);

	if (w_thd > 0xFFFF)
		w_thd = 0xFFFF;

	tmp_reg_w = __raw_readl(base + QMAX_THRESHOLD_W);
	tmp_reg_w &= ~(0xFFFF << 0);
	tmp_reg_w |= (w_thd << 0);

	__raw_writel(tmp_reg_r, base + QMAX_THRESHOLD_R);
	__raw_writel(tmp_reg_w, base + QMAX_THRESHOLD_W);

	return 0;
}

static int get_buscbts_qmax_threshold(void __iomem *base, unsigned int *r_thd, unsigned int *w_thd)
{
	unsigned int tmp_reg_r, tmp_reg_w;

	if (!base)
		return -ENODATA;

	tmp_reg_r = __raw_readl(base + QMAX_THRESHOLD_R);
	tmp_reg_w = __raw_readl(base + QMAX_THRESHOLD_W);

	*r_thd = tmp_reg_r & 0xFFFF;
	*w_thd = tmp_reg_w & 0xFFFF;

	return 0;
}

static void init_qmax(void __iomem *base)
{
	unsigned int threshold = 0;
	unsigned int tmp_reg;

	if (!base)
		return;

	/* Read threshold set */
	threshold = DEFAULT_QMAX_RD_TH;

	if (threshold > 0xFFFF)
		threshold = 0xFFFF;

	tmp_reg = __raw_readl(base + QMAX_THRESHOLD_R);
	tmp_reg &= ~(0xFFFF << 0);
	tmp_reg |= (threshold << 0);

	__raw_writel(tmp_reg, base + QMAX_THRESHOLD_R);

	/* Write threshold set */
	threshold = DEFAULT_QMAX_WR_TH;

	if (threshold > 0xFFFF)
		threshold = 0xFFFF;

	tmp_reg = __raw_readl(base + QMAX_THRESHOLD_W);
	tmp_reg &= ~(0xFFFF << 0);
	tmp_reg |= (threshold << 0);

	__raw_writel(tmp_reg, base + QMAX_THRESHOLD_W);
}

static int set_ipbts_qos(void __iomem *base, struct bts_stat *stat)
{
	unsigned int tmp_reg_r = 0;
	unsigned int tmp_reg_w = 0;
	unsigned int tmp_reg = 0;

	if (!base || !stat)
		return -ENODATA;

	/* xCON[8] = AxQoS override *1:override, 0:bypass */
	if (stat->bypass) {
		tmp_reg_r = __raw_readl(base + RCON);
		tmp_reg_r = tmp_reg_r & ~(0x1 << AXQOS_BYPASS);
		tmp_reg_w = __raw_readl(base + WCON);
		tmp_reg_w = tmp_reg_w & ~(0x1 << AXQOS_BYPASS);

		__raw_writel(tmp_reg_r, base + RCON);
		__raw_writel(tmp_reg_w, base + WCON);

		return 0;
	}

	if (stat->arqos > MAX_QOS)
		stat->arqos = MAX_QOS;

	/* xCON[15:12] = AxQoS value */
	tmp_reg_r = __raw_readl(base + RCON);
	tmp_reg_r = tmp_reg_r & ~((0x1 << AXQOS_BYPASS) | (MAX_QOS << AXQOS_VAL));
	tmp_reg_r = tmp_reg_r | (0x1 << AXQOS_BYPASS) | (stat->arqos << AXQOS_VAL);

	if (stat->awqos > MAX_QOS)
		stat->awqos = MAX_QOS;

	/* xCON[15:12] = AxQoS_value */
	tmp_reg_w = __raw_readl(base + WCON);
	tmp_reg_w = tmp_reg_w & ~((0x1 << AXQOS_BYPASS) | (MAX_QOS << AXQOS_VAL));
	tmp_reg_w = tmp_reg_w | (0x1 << AXQOS_BYPASS) | (stat->awqos << AXQOS_VAL);

	__raw_writel(tmp_reg_r, base + RCON);
	__raw_writel(tmp_reg_w, base + WCON);

	/* CONTROL[0] = QoS on/off */
	tmp_reg = __raw_readl(base + CON);
	tmp_reg = tmp_reg & ~(0x1 << AXQOS_ONOFF);
	tmp_reg = tmp_reg | (0x1 << AXQOS_ONOFF);
	__raw_writel(tmp_reg, base + CON);

	return 0;
}

static int get_ipbts_qos(void __iomem *base, struct bts_stat *stat)
{
	unsigned int tmp_reg_r = 0;
	unsigned int tmp_reg_w = 0;

	if (!base || !stat)
		return -ENODATA;

	tmp_reg_r = __raw_readl(base + RCON);
	tmp_reg_r = (tmp_reg_r & (0x1 << AXQOS_BYPASS)) >> AXQOS_BYPASS;
	tmp_reg_w = __raw_readl(base + WCON);
	tmp_reg_w = (tmp_reg_w & (0x1 << AXQOS_BYPASS)) >> AXQOS_BYPASS;

	if (!tmp_reg_r || !tmp_reg_w)
		stat->bypass = true;
	else
		stat->bypass = false;

	tmp_reg_r = __raw_readl(base + RCON);
	tmp_reg_r = (tmp_reg_r & (MAX_QOS << AXQOS_VAL)) >> AXQOS_VAL;

	tmp_reg_w = __raw_readl(base + WCON);
	tmp_reg_w = (tmp_reg_w & (MAX_QOS << AXQOS_VAL)) >> AXQOS_VAL;

	stat->arqos = tmp_reg_r;
	stat->awqos = tmp_reg_w;

	return 0;
}

static int set_ipbts_mo(void __iomem *base, struct bts_stat *stat)
{
	unsigned int tmp_reg_r = 0;
	unsigned int tmp_reg_w = 0;

	if (!base || !stat)
		return -ENODATA;

	if (stat->rmo > MAX_MO || !stat->rmo)
		stat->rmo = MAX_MO;

	tmp_reg_r = __raw_readl(base + RBLK_UPPER);
	tmp_reg_r = tmp_reg_r & ~(MAX_MO << BLOCK_UPPER);
	tmp_reg_r = tmp_reg_r | (stat->rmo << BLOCK_UPPER);

	if (stat->wmo > MAX_MO || !stat->wmo)
		stat->wmo = MAX_MO;

	tmp_reg_w = __raw_readl(base + WBLK_UPPER);
	tmp_reg_w = tmp_reg_w & ~(MAX_MO << BLOCK_UPPER);
	tmp_reg_w = tmp_reg_w | (stat->wmo << BLOCK_UPPER);

	__raw_writel(tmp_reg_r, base + RBLK_UPPER);
	__raw_writel(tmp_reg_w, base + WBLK_UPPER);

	return 0;
}

static int get_ipbts_mo(void __iomem *base, struct bts_stat *stat)
{
	unsigned int tmp_reg_r = 0;
	unsigned int tmp_reg_w = 0;

	if (!base || !stat)
		return -ENODATA;

	tmp_reg_r = __raw_readl(base + RBLK_UPPER);
	tmp_reg_r = (tmp_reg_r & (MAX_MO << BLOCK_UPPER)) >> BLOCK_UPPER;

	tmp_reg_w = __raw_readl(base + WBLK_UPPER);
	tmp_reg_w = (tmp_reg_w & (MAX_MO << BLOCK_UPPER)) >> BLOCK_UPPER;

	stat->rmo = tmp_reg_r;
	stat->wmo = tmp_reg_w;

	return 0;
}

static int set_ipbts_urgent(void __iomem *base, struct bts_stat *stat)
{
	unsigned int tmp_reg = 0;

	if (!base || !stat)
		return -ENODATA;

	if (stat->qurgent_th_r > MAX_QUTH)
		stat->qurgent_th_r = MAX_QUTH;
	if (stat->qurgent_th_w > MAX_QUTH)
		stat->qurgent_th_w = MAX_QUTH;

	tmp_reg = __raw_readl(base + TIMEOUT);
	tmp_reg = tmp_reg & ~((MAX_QUTH << TIMEOUT_CNT_R) | (MAX_QUTH << TIMEOUT_CNT_W));
	tmp_reg = tmp_reg | (stat->qurgent_th_r << TIMEOUT_CNT_R) | (stat->qurgent_th_w << TIMEOUT_CNT_W);

	__raw_writel(tmp_reg, base + TIMEOUT);

	tmp_reg = __raw_readl(base + CON);
	tmp_reg = tmp_reg & ~(0x1 << QURGENT_EN);

	if (stat->qurgent_on)
		tmp_reg = tmp_reg | (0x1 << QURGENT_EN);

	__raw_writel(tmp_reg, base + CON);

	return 0;
}

static int get_ipbts_urgent(void __iomem *base, struct bts_stat *stat)
{
	unsigned int tmp_reg = 0;

	if (!base || !stat)
		return -ENODATA;

	tmp_reg = __raw_readl(base + TIMEOUT);
	stat->qurgent_th_r = (tmp_reg & (MAX_QUTH << TIMEOUT_CNT_R)) >> TIMEOUT_CNT_R;
	stat->qurgent_th_w = (tmp_reg & (MAX_QUTH << TIMEOUT_CNT_W)) >> TIMEOUT_CNT_W;

	tmp_reg = __raw_readl(base + CON);
	stat->qurgent_on = (tmp_reg & (0x1 << QURGENT_EN)) >> QURGENT_EN;

	return 0;
}

static int set_ipbts_blocking(void __iomem *base, struct bts_stat *stat)
{
	unsigned int tmp_reg_r = 0;
	unsigned int tmp_reg_w = 0;

	if (!base || !stat)
		return -ENODATA;

	if (stat->blocking_on) {
		if (stat->qfull_limit_r > MAX_MO)
			stat->qfull_limit_r = MAX_MO;
		if (stat->qfull_limit_w > MAX_MO)
			stat->qfull_limit_w = MAX_MO;

		tmp_reg_r = __raw_readl(base + RBLK_UPPER_FULL);
		tmp_reg_r = tmp_reg_r & ~(MAX_MO << BLOCK_UPPER);
		tmp_reg_r = tmp_reg_r | (stat->qfull_limit_r << BLOCK_UPPER);

		tmp_reg_w = __raw_readl(base + WBLK_UPPER_FULL);
		tmp_reg_w = tmp_reg_w & ~(MAX_MO << BLOCK_UPPER);
		tmp_reg_w = tmp_reg_w | (stat->qfull_limit_w << BLOCK_UPPER);

		__raw_writel(tmp_reg_r, base + RBLK_UPPER_FULL);
		__raw_writel(tmp_reg_w, base + WBLK_UPPER_FULL);

		if (stat->qbusy_limit_r > MAX_MO)
			stat->qbusy_limit_r = MAX_MO;
		if (stat->qbusy_limit_w > MAX_MO)
			stat->qbusy_limit_w = MAX_MO;

		tmp_reg_r = __raw_readl(base + RBLK_UPPER_BUSY);
		tmp_reg_r = tmp_reg_r & ~(MAX_MO << BLOCK_UPPER);
		tmp_reg_r = tmp_reg_r | (stat->qbusy_limit_r << BLOCK_UPPER);

		tmp_reg_w = __raw_readl(base + WBLK_UPPER_BUSY);
		tmp_reg_w = tmp_reg_w & ~(MAX_MO << BLOCK_UPPER);
		tmp_reg_w = tmp_reg_w | (stat->qbusy_limit_w << BLOCK_UPPER);

		__raw_writel(tmp_reg_r, base + RBLK_UPPER_BUSY);
		__raw_writel(tmp_reg_w, base + WBLK_UPPER_BUSY);

		if (stat->qmax0_limit_r > MAX_MO)
			stat->qmax0_limit_r = MAX_MO;
		if (stat->qmax0_limit_w > MAX_MO)
			stat->qmax0_limit_w = MAX_MO;

		if (stat->qmax1_limit_r > MAX_MO)
			stat->qmax1_limit_r = MAX_MO;
		if (stat->qmax1_limit_w > MAX_MO)
			stat->qmax1_limit_w = MAX_MO;

		tmp_reg_r = __raw_readl(base + RBLK_UPPER_MAX);
		tmp_reg_r &= ~((MAX_MO << BLOCK_UPPER1) | (MAX_MO << BLOCK_UPPER0));
		tmp_reg_r |= ((stat->qmax1_limit_r << BLOCK_UPPER1) | (stat->qmax0_limit_r << BLOCK_UPPER0));

		tmp_reg_w = __raw_readl(base + WBLK_UPPER_MAX);
		tmp_reg_w &= ~((MAX_MO << BLOCK_UPPER1) | (MAX_MO << BLOCK_UPPER0));
		tmp_reg_w |= ((stat->qmax1_limit_w << BLOCK_UPPER1) | (stat->qmax0_limit_w << BLOCK_UPPER0));

		__raw_writel(tmp_reg_r, base + RBLK_UPPER_MAX);
		__raw_writel(tmp_reg_w, base + WBLK_UPPER_MAX);

		tmp_reg_r = __raw_readl(base + RCON);
		tmp_reg_r = tmp_reg_r & ~(0x1 << BLOCKING_EN);
		tmp_reg_r = tmp_reg_r | (0x1 << BLOCKING_EN);
		__raw_writel(tmp_reg_r, base + RCON);

		tmp_reg_w = __raw_readl(base + WCON);
		tmp_reg_w = tmp_reg_w & ~(0x1 << BLOCKING_EN);
		tmp_reg_w = tmp_reg_w | (0x1 << BLOCKING_EN);
		__raw_writel(tmp_reg_r, base + WCON);
	} else {
		tmp_reg_r = __raw_readl(base + RCON);
		tmp_reg_r = tmp_reg_r & ~(0x1 << BLOCKING_EN);
		__raw_writel(tmp_reg_r, base + RCON);

		tmp_reg_w = __raw_readl(base + WCON);
		tmp_reg_w = tmp_reg_w & ~(0x1 << BLOCKING_EN);
		__raw_writel(tmp_reg_r, base + WCON);
	}

	return 0;
}

static int get_ipbts_blocking(void __iomem *base, struct bts_stat *stat)
{
	unsigned int tmp_reg = 0;

	tmp_reg = __raw_readl(base + RCON);
	stat->blocking_on = (tmp_reg & (0x1 << BLOCKING_EN)) ? true : false;

	if (!stat->blocking_on)
		return 0;

	tmp_reg = __raw_readl(base + RBLK_UPPER_FULL);
	stat->qfull_limit_r = (tmp_reg & (MAX_MO << BLOCK_UPPER)) >> BLOCK_UPPER;

	tmp_reg = __raw_readl(base + WBLK_UPPER_FULL);
	stat->qfull_limit_w = (tmp_reg & (MAX_MO << BLOCK_UPPER)) >> BLOCK_UPPER;

	tmp_reg = __raw_readl(base + RBLK_UPPER_BUSY);
	stat->qbusy_limit_r = (tmp_reg & (MAX_MO << BLOCK_UPPER)) >> BLOCK_UPPER;

	tmp_reg = __raw_readl(base + WBLK_UPPER_BUSY);
	stat->qbusy_limit_w = (tmp_reg & (MAX_MO << BLOCK_UPPER)) >> BLOCK_UPPER;

	tmp_reg = __raw_readl(base + RBLK_UPPER_MAX);
	stat->qmax0_limit_r = (tmp_reg & (MAX_MO << BLOCK_UPPER0)) >> BLOCK_UPPER0;
	stat->qmax1_limit_r = (tmp_reg & (MAX_MO << BLOCK_UPPER1)) >> BLOCK_UPPER1;

	tmp_reg = __raw_readl(base + WBLK_UPPER_MAX);
	stat->qmax0_limit_w = (tmp_reg & (MAX_MO << BLOCK_UPPER0)) >> BLOCK_UPPER0;
	stat->qmax1_limit_w = (tmp_reg & (MAX_MO << BLOCK_UPPER1)) >> BLOCK_UPPER1;

	return 0;
}

static int set_ipbts(void __iomem *base, struct bts_stat *stat)
{
	int ret = 0;

	if (!base || !stat)
		return -ENODATA;

	if (!stat->stat_on)
		return 0;

	ret = set_ipbts_qos(base, stat);
	if (ret) {
		pr_err("set_ipbts_qos failed! ret=%d\n", ret);
		return ret;
	}
	ret = set_ipbts_mo(base, stat);
	if (ret) {
		pr_err("set_ipbts_mo failed! ret=%d\n", ret);
		return ret;
	}
	ret = set_ipbts_urgent(base, stat);
	if (ret) {
		pr_err("set_ipbts_urgent failed! ret=%d\n", ret);
		return ret;
	}
	ret = set_ipbts_blocking(base, stat);
	if (ret) {
		pr_err("set_ipbts_blocking failed! ret=%d\n", ret);
		return ret;
	}

	return ret;
}

static int get_ipbts(void __iomem *base, struct bts_stat *stat)
{
	int ret = 0;

	if (!base || !stat)
		return -ENODATA;

	ret = get_ipbts_qos(base, stat);
	if (ret) {
		pr_err("get_ipbts_qos failed! ret=%d\n", ret);
		return ret;
	}
	ret = get_ipbts_mo(base, stat);
	if (ret) {
		pr_err("get_ipbts_mo failed! ret=%d\n", ret);
		return ret;
	}
	ret = get_ipbts_urgent(base, stat);
	if (ret) {
		pr_err("get_ipbts_urgent failed! ret=%d\n", ret);
		return ret;
	}
	ret = get_ipbts_blocking(base, stat);
	if (ret) {
		pr_err("get_ipbts_blocking failed! ret=%d\n", ret);
		return ret;
	}

	return 0;
}

static int set_drexbts_write_config(void __iomem *base, struct bts_stat *stat)
{
	if (!base || !stat)
		return -EINVAL;

	__raw_writel(stat->write_flush_config_0, base + WRITE_FLUSH_CONFIG_0);
	__raw_writel(stat->write_flush_config_1, base + WRITE_FLUSH_CONFIG_1);

	return 0;
}

static int get_drexbts_write_config(void __iomem *base, struct bts_stat *stat)
{
	if (!base || !stat)
		return -EINVAL;

	stat->write_flush_config_0 = __raw_readl(base + WRITE_FLUSH_CONFIG_0);
	stat->write_flush_config_1 = __raw_readl(base + WRITE_FLUSH_CONFIG_1);

	return 0;
}

static int set_drexbts_drex_timeout(void __iomem *base, struct bts_stat *stat,
		unsigned int index)
{
	if (!base || !stat)
		return -EINVAL;
	if (index > MAX_QOS)
		return -EINVAL;

	__raw_writel(stat->drex_timeout[index], base + QOS_TIMER_0 + (4 * index));

	return 0;
}

static int get_drexbts_drex_timeout(void __iomem *base, struct bts_stat *stat)
{
	int i;

	if (!base || !stat)
		return -EINVAL;

	for (i = 0; i < MAX_QOS + 1; i++)
		stat->drex_timeout[i] =
			__raw_readl(base + QOS_TIMER_0 + (4 * i));

	return 0;
}

static int set_drexbts_vc_timer_th(void __iomem *base, struct bts_stat *stat,
		unsigned int index)
{
	if (!base || !stat)
		return -EINVAL;
	if (index >= VC_TIMER_TH_NR)
		return -EINVAL;

	__raw_writel(stat->vc_timer_th[index], base + VC_TIMER_TH_0 + (4 * index));

	return 0;
}

static int get_drexbts_vc_timer_th(void __iomem *base, struct bts_stat *stat)
{
	int i;

	if (!base || !stat)
		return -EINVAL;

	for (i = 0; i < VC_TIMER_TH_NR; i++)
		stat->vc_timer_th[i] =
			__raw_readl(base + VC_TIMER_TH_0 + (4 * i));

	return 0;
}

static int set_drexbts_cutoff(void __iomem *base, struct bts_stat *stat)
{
	if (!base || !stat)
		return -EINVAL;

	__raw_writel(stat->cutoff_con, base + CUTOFF_CON);
	__raw_writel(stat->brb_cutoff_con, base + BRB_CUTOFF_CON);
	__raw_writel(stat->wdbuf_cutoff_con, base + WDBUF_CUTOFF_CON);

	return 0;
}

static int get_drexbts_cutoff(void __iomem *base, struct bts_stat *stat)
{
	if (!base || !stat)
		return -EINVAL;

	stat->cutoff_con = __raw_readl(base + CUTOFF_CON);
	stat->brb_cutoff_con = __raw_readl(base + BRB_CUTOFF_CON);
	stat->wdbuf_cutoff_con = __raw_readl(base + WDBUF_CUTOFF_CON);

	return 0;
}

static int set_drexbts_pf_rreq_thrt_con(void __iomem *base, struct bts_stat *stat)
{
	if (!base || !stat)
		return -EINVAL;

	__raw_writel(stat->pf_rreq_thrt_con, base + RREQ_THRT_CON);

	return 0;
}

static int get_drexbts_pf_rreq_thrt_con(void __iomem *base, struct bts_stat *stat)
{
	if (!base || !stat)
		return -EINVAL;

	stat->pf_rreq_thrt_con = __raw_readl(base + RREQ_THRT_CON);

	return 0;
}

static int set_drexbts_allow_mo_for_region(void __iomem *base, struct bts_stat *stat)
{
	if (!base || !stat)
		return -EINVAL;

	__raw_writel(stat->allow_mo_for_region, base + RREQ_THRT_MO_P2);

	return 0;
}

static int get_drexbts_allow_mo_for_region(void __iomem *base, struct bts_stat *stat)
{
	if (!base || !stat)
		return -EINVAL;

	stat->allow_mo_for_region = __raw_readl(base + RREQ_THRT_MO_P2);

	return 0;
}

static int set_drexbts_pf_qos_timer(void __iomem *base, struct bts_stat *stat,
		unsigned int index)
{
	if (!base || !stat)
		return -EINVAL;
	if (index >= PF_TIMER_NR)
		return -EINVAL;

	__raw_writel(stat->pf_qos_timer[index], base + PF_QOS_TIMER_0 + (4 * index));

	return 0;
}

static int get_drexbts_pf_qos_timer(void __iomem *base, struct bts_stat *stat)
{
	int i;

	if (!base || !stat)
		return -EINVAL;

	for (i = 0; i < PF_TIMER_NR; i++)
		stat->pf_qos_timer[i] =
			__raw_readl(base + PF_QOS_TIMER_0 + (4 * i));

	return 0;
}

int register_btsops(struct bts_info *info)
{
	unsigned int type = info->type;

	info->ops = kmalloc(sizeof(struct bts_ops), GFP_KERNEL);
	if (info->ops == NULL)
		return -ENOMEM;

	switch (type) {
	case IP_BTS:
		info->ops->init_bts = NULL;
		info->ops->set_bts = set_ipbts;
		info->ops->get_bts = get_ipbts;
		info->ops->set_qos = set_ipbts_qos;
		info->ops->get_qos = get_ipbts_qos;
		info->ops->set_mo = set_ipbts_mo;
		info->ops->get_mo = get_ipbts_mo;
		info->ops->set_urgent = set_ipbts_urgent;
		info->ops->get_urgent = get_ipbts_urgent;
		info->ops->set_blocking = set_ipbts_blocking;
		info->ops->get_blocking = get_ipbts_blocking;
		info->ops->get_write_config = NULL;
		info->ops->set_write_config = NULL;
		info->ops->get_drex_timeout = NULL;
		info->ops->set_drex_timeout = NULL;
		info->ops->get_vc_timer_th = NULL;
		info->ops->set_vc_timer_th = NULL;
		info->ops->get_cutoff = NULL;
		info->ops->set_cutoff = NULL;
		info->ops->get_pf_rreq_thrt_con = NULL;
		info->ops->set_pf_rreq_thrt_con = NULL;
		info->ops->get_allow_mo_for_region = NULL;
		info->ops->set_allow_mo_for_region = NULL;
		info->ops->get_pf_qos_timer = NULL;
		info->ops->set_pf_qos_timer = NULL;
		info->ops->get_qmax_threshold = NULL;
		info->ops->set_qmax_threshold = NULL;
		break;
	case TREX_BTS:
		info->ops->init_bts = init_trexbts;
		info->ops->set_bts = NULL;
		info->ops->get_bts = NULL;
		info->ops->set_qos = NULL;
		info->ops->get_qos = NULL;
		info->ops->set_mo = NULL;
		info->ops->get_mo = NULL;
		info->ops->set_urgent = NULL;
		info->ops->get_urgent = NULL;
		info->ops->set_blocking = NULL;
		info->ops->get_blocking = NULL;
		info->ops->get_write_config = NULL;
		info->ops->set_write_config = NULL;
		info->ops->get_drex_timeout = NULL;
		info->ops->set_drex_timeout = NULL;
		info->ops->get_vc_timer_th = NULL;
		info->ops->set_vc_timer_th = NULL;
		info->ops->get_cutoff = NULL;
		info->ops->set_cutoff = NULL;
		info->ops->get_pf_rreq_thrt_con = NULL;
		info->ops->set_pf_rreq_thrt_con = NULL;
		info->ops->get_allow_mo_for_region = NULL;
		info->ops->set_allow_mo_for_region = NULL;
		info->ops->get_pf_qos_timer = NULL;
		info->ops->set_pf_qos_timer = NULL;
		info->ops->get_qmax_threshold = NULL;
		info->ops->set_qmax_threshold = NULL;
		break;
	case SCI_BTS:
		info->ops->init_bts = init_sciqfull;
		info->ops->set_bts = NULL;
		info->ops->get_bts = NULL;
		info->ops->set_qos = NULL;
		info->ops->get_qos = NULL;
		info->ops->set_mo = NULL;
		info->ops->get_mo = NULL;
		info->ops->set_urgent = NULL;
		info->ops->get_urgent = NULL;
		info->ops->set_blocking = NULL;
		info->ops->get_blocking = NULL;
		info->ops->get_write_config = NULL;
		info->ops->set_write_config = NULL;
		info->ops->get_drex_timeout = NULL;
		info->ops->set_drex_timeout = NULL;
		info->ops->get_vc_timer_th = NULL;
		info->ops->set_vc_timer_th = NULL;
		info->ops->get_cutoff = NULL;
		info->ops->set_cutoff = NULL;
		info->ops->get_pf_rreq_thrt_con = NULL;
		info->ops->set_pf_rreq_thrt_con = NULL;
		info->ops->get_allow_mo_for_region = NULL;
		info->ops->set_allow_mo_for_region = NULL;
		info->ops->get_pf_qos_timer = NULL;
		info->ops->set_pf_qos_timer = NULL;
		info->ops->get_qmax_threshold = NULL;
		info->ops->set_qmax_threshold = NULL;
		break;
	case SMC_BTS:
		info->ops->init_bts = init_smcqbusy;
		info->ops->set_bts = NULL;
		info->ops->get_bts = NULL;
		info->ops->set_qos = NULL;
		info->ops->get_qos = NULL;
		info->ops->set_mo = NULL;
		info->ops->get_mo = NULL;
		info->ops->set_urgent = NULL;
		info->ops->get_urgent = NULL;
		info->ops->set_blocking = NULL;
		info->ops->get_blocking = NULL;
		info->ops->get_write_config = NULL;
		info->ops->set_write_config = NULL;
		info->ops->get_drex_timeout = NULL;
		info->ops->set_drex_timeout = NULL;
		info->ops->get_vc_timer_th = NULL;
		info->ops->set_vc_timer_th = NULL;
		info->ops->get_cutoff = NULL;
		info->ops->set_cutoff = NULL;
		info->ops->get_pf_rreq_thrt_con = NULL;
		info->ops->set_pf_rreq_thrt_con = NULL;
		info->ops->get_allow_mo_for_region = NULL;
		info->ops->set_allow_mo_for_region = NULL;
		info->ops->get_pf_qos_timer = NULL;
		info->ops->set_pf_qos_timer = NULL;
		info->ops->get_qmax_threshold = NULL;
		info->ops->set_qmax_threshold = NULL;
		break;
	case BUSC_BTS:
		info->ops->init_bts = init_qmax;
		info->ops->set_bts = NULL;
		info->ops->get_bts = NULL;
		info->ops->set_qos = NULL;
		info->ops->get_qos = NULL;
		info->ops->set_mo = NULL;
		info->ops->get_mo = NULL;
		info->ops->set_urgent = NULL;
		info->ops->get_urgent = NULL;
		info->ops->set_blocking = NULL;
		info->ops->get_blocking = NULL;
		info->ops->get_write_config = NULL;
		info->ops->set_write_config = NULL;
		info->ops->get_drex_timeout = NULL;
		info->ops->set_drex_timeout = NULL;
		info->ops->get_vc_timer_th = NULL;
		info->ops->set_vc_timer_th = NULL;
		info->ops->get_cutoff = NULL;
		info->ops->set_cutoff = NULL;
		info->ops->get_pf_rreq_thrt_con = NULL;
		info->ops->set_pf_rreq_thrt_con = NULL;
		info->ops->get_allow_mo_for_region = NULL;
		info->ops->set_allow_mo_for_region = NULL;
		info->ops->get_pf_qos_timer = NULL;
		info->ops->set_pf_qos_timer = NULL;
		info->ops->get_qmax_threshold = get_buscbts_qmax_threshold;
		info->ops->set_qmax_threshold = set_buscbts_qmax_threshold;
		break;
	case DREX_BTS:
		info->ops->init_bts = NULL;
		info->ops->set_bts = set_drexbts;
		info->ops->get_bts = get_drexbts;
		info->ops->set_qos = NULL;
		info->ops->get_qos = NULL;
		info->ops->set_mo = NULL;
		info->ops->get_mo = NULL;
		info->ops->set_urgent = NULL;
		info->ops->get_urgent = NULL;
		info->ops->set_blocking = NULL;
		info->ops->get_blocking = NULL;
		info->ops->get_write_config = get_drexbts_write_config;
		info->ops->set_write_config = set_drexbts_write_config;
		info->ops->get_drex_timeout = get_drexbts_drex_timeout;
		info->ops->set_drex_timeout = set_drexbts_drex_timeout;
		info->ops->get_vc_timer_th = get_drexbts_vc_timer_th;
		info->ops->set_vc_timer_th = set_drexbts_vc_timer_th;
		info->ops->get_cutoff = get_drexbts_cutoff;
		info->ops->set_cutoff = set_drexbts_cutoff;
		info->ops->get_pf_rreq_thrt_con = get_drexbts_pf_rreq_thrt_con;
		info->ops->set_pf_rreq_thrt_con = set_drexbts_pf_rreq_thrt_con;
		info->ops->get_allow_mo_for_region = get_drexbts_allow_mo_for_region;
		info->ops->set_allow_mo_for_region = set_drexbts_allow_mo_for_region;
		info->ops->get_pf_qos_timer = get_drexbts_pf_qos_timer;
		info->ops->set_pf_qos_timer = set_drexbts_pf_qos_timer;
		info->ops->get_qmax_threshold = NULL;
		info->ops->set_qmax_threshold = NULL;
		break;
	case INTERNAL_BTS:
		info->ops->init_bts = NULL;
		info->ops->set_bts = NULL;
		info->ops->get_bts = NULL;
		info->ops->set_qos = NULL;
		info->ops->get_qos = NULL;
		info->ops->set_mo = NULL;
		info->ops->get_mo = NULL;
		info->ops->set_urgent = NULL;
		info->ops->get_urgent = NULL;
		info->ops->set_blocking = NULL;
		info->ops->get_blocking = NULL;
		info->ops->get_write_config = NULL;
		info->ops->set_write_config = NULL;
		info->ops->get_drex_timeout = NULL;
		info->ops->set_drex_timeout = NULL;
		info->ops->get_vc_timer_th = NULL;
		info->ops->set_vc_timer_th = NULL;
		info->ops->get_cutoff = NULL;
		info->ops->set_cutoff = NULL;
		info->ops->get_pf_rreq_thrt_con = NULL;
		info->ops->set_pf_rreq_thrt_con = NULL;
		info->ops->get_allow_mo_for_region = NULL;
		info->ops->set_allow_mo_for_region = NULL;
		info->ops->get_pf_qos_timer = NULL;
		info->ops->set_pf_qos_timer = NULL;
		info->ops->get_qmax_threshold = NULL;
		info->ops->set_qmax_threshold = NULL;
		break;
	default:
		break;
	}

	return 0;
}
