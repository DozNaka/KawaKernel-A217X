/*
 * linux/drivers/video/fbdev/exynos/panel/nt36575/nt36575.h
 *
 * Header file for S6E3HAB Dimming Driver
 *
 * Copyright (c) 2016 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __NT36575_H__
#define __NT36575_H__

#include <linux/types.h>
#include <linux/kernel.h>

/*
 * OFFSET ==> OFS means N-param - 1
 * <example>
 * XXX 1st param => NT36575_XXX_OFS (0)
 * XXX 2nd param => NT36575_XXX_OFS (1)
 * XXX 36th param => NT36575_XXX_OFS (35)
 */

#define NT36575_ID_REG				0x04 /* no use */
#define NT36575_ID_OFS				0
#define NT36575_ID_LEN				(PANEL_ID_LEN)

#define NT36575_ID_DA_REG				0xDA
#define NT36575_ID_DA_OFS				0
#define NT36575_ID_DA_LEN				1

#define NT36575_ID_DB_REG				0xDB
#define NT36575_ID_DB_OFS				0
#define NT36575_ID_DB_LEN				1

#define NT36575_ID_DC_REG				0xDC
#define NT36575_ID_DC_OFS				0
#define NT36575_ID_DC_LEN				1

#define NT36575_NR_LUMINANCE		(256)
#define NT36575_NR_HBM_LUMINANCE	(51)

#define NT36575_TOTAL_NR_LUMINANCE (NT36575_NR_LUMINANCE + NT36575_NR_HBM_LUMINANCE)

enum {
	BRT_MAPTBL,
	MAX_MAPTBL,
};

enum {
	READ_ID,
	READ_ID_DA,
	READ_ID_DB,
	READ_ID_DC,
};

enum {
	RES_ID,
};

static u8 NT36575_ID[NT36575_ID_LEN];

static struct rdinfo nt36575_rditbl[] = {
	[READ_ID_DA] = RDINFO_INIT(id, DSI_PKT_TYPE_RD, NT36575_ID_DA_REG, NT36575_ID_DA_OFS, NT36575_ID_DA_LEN),
	[READ_ID_DB] = RDINFO_INIT(id, DSI_PKT_TYPE_RD, NT36575_ID_DB_REG, NT36575_ID_DB_OFS, NT36575_ID_DB_LEN),
	[READ_ID_DC] = RDINFO_INIT(id, DSI_PKT_TYPE_RD, NT36575_ID_DC_REG, NT36575_ID_DC_OFS, NT36575_ID_DC_LEN),

};

static DECLARE_RESUI(id) = {
	{.rditbl = &nt36575_rditbl[READ_ID_DA], .offset = 0},
	{.rditbl = &nt36575_rditbl[READ_ID_DB], .offset = 1},
	{.rditbl = &nt36575_rditbl[READ_ID_DC], .offset = 2},
};

static struct resinfo nt36575_restbl[] = {
	[RES_ID] = RESINFO_INIT(id, NT36575_ID, RESUI(id)),
};

static int init_brightness_table(struct maptbl *tbl);
static int getidx_brt_table(struct maptbl *);
static void copy_common_maptbl(struct maptbl *, u8 *);

#endif /* __NT36575_H__ */
