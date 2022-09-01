// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/cpuidle.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

#include "mtk-svs.h"
#include "mt8183-svs.h"
#include "mt8192-svs.h"

/* svs bank register fields and common configuration */
#define SVSB_PTPCONFIG_DETMAX		GENMASK(15, 0)
#define SVSB_DET_MAX			FIELD_PREP(SVSB_PTPCONFIG_DETMAX, 0xffff)
#define SVSB_DET_WINDOW			0xa28

/* DESCHAR */
#define SVSB_DESCHAR_FLD_MDES		GENMASK(7, 0)
#define SVSB_DESCHAR_FLD_BDES		GENMASK(15, 8)

/* TEMPCHAR */
#define SVSB_TEMPCHAR_FLD_DVT_FIXED	GENMASK(7, 0)
#define SVSB_TEMPCHAR_FLD_MTDES		GENMASK(15, 8)
#define SVSB_TEMPCHAR_FLD_VCO		GENMASK(23, 16)

/* DETCHAR */
#define SVSB_DETCHAR_FLD_DCMDET		GENMASK(7, 0)
#define SVSB_DETCHAR_FLD_DCBDET		GENMASK(15, 8)

/* SVSEN (PTPEN) */
#define SVSB_PTPEN_INIT01		BIT(0)
#define SVSB_PTPEN_MON			BIT(1)
#define SVSB_PTPEN_INIT02		(SVSB_PTPEN_INIT01 | BIT(2))
#define SVSB_PTPEN_OFF			0x0

/* FREQPCTS */
#define SVSB_FREQPCTS_FLD_PCT0_4	GENMASK(7, 0)
#define SVSB_FREQPCTS_FLD_PCT1_5	GENMASK(15, 8)
#define SVSB_FREQPCTS_FLD_PCT2_6	GENMASK(23, 16)
#define SVSB_FREQPCTS_FLD_PCT3_7	GENMASK(31, 24)

/* INTSTS */
#define SVSB_INTSTS_VAL_CLEAN		0x00ffffff
#define SVSB_INTSTS_F0_COMPLETE		BIT(0)
#define SVSB_INTSTS_FLD_MONVOP		GENMASK(23, 16)
#define SVSB_RUNCONFIG_DEFAULT		0x80000000

/* LIMITVALS */
#define SVSB_LIMITVALS_FLD_DTLO		GENMASK(7, 0)
#define SVSB_LIMITVALS_FLD_DTHI		GENMASK(15, 8)
#define SVSB_LIMITVALS_FLD_VMIN		GENMASK(23, 16)
#define SVSB_LIMITVALS_FLD_VMAX		GENMASK(31, 24)
#define SVSB_VAL_DTHI			0x1
#define SVSB_VAL_DTLO			0xfe

/* INTEN */
#define SVSB_INTEN_F0EN			BIT(0)
#define SVSB_INTEN_DACK0UPEN		BIT(8)
#define SVSB_INTEN_DC0EN		BIT(9)
#define SVSB_INTEN_DC1EN		BIT(10)
#define SVSB_INTEN_DACK0LOEN		BIT(11)
#define SVSB_INTEN_INITPROD_OVF_EN	BIT(12)
#define SVSB_INTEN_INITSUM_OVF_EN	BIT(14)
#define SVSB_INTEN_MONVOPEN		GENMASK(23, 16)
#define SVSB_INTEN_INIT0x		(SVSB_INTEN_F0EN | SVSB_INTEN_DACK0UPEN |	\
					 SVSB_INTEN_DC0EN | SVSB_INTEN_DC1EN |		\
					 SVSB_INTEN_DACK0LOEN |				\
					 SVSB_INTEN_INITPROD_OVF_EN |			\
					 SVSB_INTEN_INITSUM_OVF_EN)

/* TSCALCS */
#define SVSB_TSCALCS_FLD_MTS		GENMASK(11, 0)
#define SVSB_TSCALCS_FLD_BTS		GENMASK(23, 12)

/* INIT2VALS */
#define SVSB_INIT2VALS_FLD_DCVOFFSETIN	GENMASK(15, 0)
#define SVSB_INIT2VALS_FLD_AGEVOFFSETIN	GENMASK(31, 16)

/* VOPS */
#define SVSB_VOPS_FLD_VOP0_4		GENMASK(7, 0)
#define SVSB_VOPS_FLD_VOP1_5		GENMASK(15, 8)
#define SVSB_VOPS_FLD_VOP2_6		GENMASK(23, 16)
#define SVSB_VOPS_FLD_VOP3_7		GENMASK(31, 24)

/* svs bank related setting */
#define BITS8				8
#define REG_BYTES			4
#define SVSB_DC_SIGNED_BIT		BIT(15)
#define SVSB_DET_CLK_EN			BIT(31)
#define SVSB_TEMP_LOWER_BOUND		0xb2
#define SVSB_TEMP_UPPER_BOUND		0x64

static const u32 svs_regs_v2[] = {
	[DESCHAR]		= 0xc00,
	[TEMPCHAR]		= 0xc04,
	[DETCHAR]		= 0xc08,
	[AGECHAR]		= 0xc0c,
	[DCCONFIG]		= 0xc10,
	[AGECONFIG]		= 0xc14,
	[FREQPCT30]		= 0xc18,
	[FREQPCT74]		= 0xc1c,
	[LIMITVALS]		= 0xc20,
	[VBOOT]			= 0xc24,
	[DETWINDOW]		= 0xc28,
	[CONFIG]		= 0xc2c,
	[TSCALCS]		= 0xc30,
	[RUNCONFIG]		= 0xc34,
	[SVSEN]			= 0xc38,
	[INIT2VALS]		= 0xc3c,
	[DCVALUES]		= 0xc40,
	[AGEVALUES]		= 0xc44,
	[VOP30]			= 0xc48,
	[VOP74]			= 0xc4c,
	[TEMP]			= 0xc50,
	[INTSTS]		= 0xc54,
	[INTSTSRAW]		= 0xc58,
	[INTEN]			= 0xc5c,
	[CHKINT]		= 0xc60,
	[CHKSHIFT]		= 0xc64,
	[STATUS]		= 0xc68,
	[VDESIGN30]		= 0xc6c,
	[VDESIGN74]		= 0xc70,
	[DVT30]			= 0xc74,
	[DVT74]			= 0xc78,
	[AGECOUNT]		= 0xc7c,
	[SMSTATE0]		= 0xc80,
	[SMSTATE1]		= 0xc84,
	[CTL0]			= 0xc88,
	[DESDETSEC]		= 0xce0,
	[TEMPAGESEC]		= 0xce4,
	[CTRLSPARE0]		= 0xcf0,
	[CTRLSPARE1]		= 0xcf4,
	[CTRLSPARE2]		= 0xcf8,
	[CTRLSPARE3]		= 0xcfc,
	[CORESEL]		= 0xf00,
	[THERMINTST]		= 0xf04,
	[INTST]			= 0xf08,
	[THSTAGE0ST]		= 0xf0c,
	[THSTAGE1ST]		= 0xf10,
	[THSTAGE2ST]		= 0xf14,
	[THAHBST0]		= 0xf18,
	[THAHBST1]		= 0xf1c,
	[SPARE0]		= 0xf20,
	[SPARE1]		= 0xf24,
	[SPARE2]		= 0xf28,
	[SPARE3]		= 0xf2c,
	[THSLPEVEB]		= 0xf30,
};

static DEFINE_SPINLOCK(svs_lock);

#define debug_fops_ro(name)						\
	static int svs_##name##_debug_open(struct inode *inode,		\
					   struct file *filp)		\
	{								\
		return single_open(filp, svs_##name##_debug_show,	\
				   inode->i_private);			\
	}								\
	static const struct file_operations svs_##name##_debug_fops = {	\
		.owner = THIS_MODULE,					\
		.open = svs_##name##_debug_open,			\
		.read = seq_read,					\
		.llseek = seq_lseek,					\
		.release = single_release,				\
	}

#define debug_fops_rw(name)						\
	static int svs_##name##_debug_open(struct inode *inode,		\
					   struct file *filp)		\
	{								\
		return single_open(filp, svs_##name##_debug_show,	\
				   inode->i_private);			\
	}								\
	static const struct file_operations svs_##name##_debug_fops = {	\
		.owner = THIS_MODULE,					\
		.open = svs_##name##_debug_open,			\
		.read = seq_read,					\
		.write = svs_##name##_debug_write,			\
		.llseek = seq_lseek,					\
		.release = single_release,				\
	}

#define svs_dentry_data(name)	{__stringify(name), &svs_##name##_debug_fops}


struct svs_platform_data {
	char *name;
	struct svs_bank *banks;
	bool (*efuse_parsing)(struct svs_platform *svsp);
	int (*probe)(struct svs_platform *svsp);
	const u32 *regs;
	u32 bank_max;
};

static u32 percent(u32 numerator, u32 denominator)
{
	/* If not divide 1000, "numerator * 100" will have data overflow. */
	numerator /= 1000;
	denominator /= 1000;

	return DIV_ROUND_UP(numerator * 100, denominator);
}

static u32 svs_readl_relaxed(struct svs_platform *svsp, enum svs_reg_index rg_i)
{
	return readl_relaxed(svsp->base + svsp->regs[rg_i]);
}

static void svs_writel_relaxed(struct svs_platform *svsp, u32 val,
			       enum svs_reg_index rg_i)
{
	writel_relaxed(val, svsp->base + svsp->regs[rg_i]);
}

static void svs_switch_bank(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	svs_writel_relaxed(svsp, svsb->core_sel, CORESEL);
}

static u32 svs_bank_volt_to_opp_volt(u32 svsb_volt, u32 svsb_volt_step,
				     u32 svsb_volt_base)
{
	return (svsb_volt * svsb_volt_step) + svsb_volt_base;
}

static u32 svs_opp_volt_to_bank_volt(u32 opp_u_volt, u32 svsb_volt_step,
				     u32 svsb_volt_base)
{
	return (opp_u_volt - svsb_volt_base) / svsb_volt_step;
}

static int svs_sync_bank_volts_from_opp(struct svs_bank *svsb)
{
	struct dev_pm_opp *opp;
	u32 i, opp_u_volt;

	for (i = 0; i < svsb->opp_count; i++) {
		opp = dev_pm_opp_find_freq_exact(svsb->opp_dev,
						 svsb->opp_dfreq[i],
						 true);
		if (IS_ERR(opp)) {
			dev_err(svsb->dev, "cannot find freq = %u (%ld)\n",
				svsb->opp_dfreq[i], PTR_ERR(opp));
			return PTR_ERR(opp);
		}

		opp_u_volt = dev_pm_opp_get_voltage(opp);
		svsb->volt[i] = svs_opp_volt_to_bank_volt(opp_u_volt,
							  svsb->volt_step,
							  svsb->volt_base);
		dev_pm_opp_put(opp);
	}

	return 0;
}

static int svs_adjust_pm_opp_volts(struct svs_bank *svsb)
{
	int ret = -EPERM, tzone_temp = 0;
	u32 i, svsb_volt, opp_volt, temp_voffset = 0, opp_start, opp_stop;

	mutex_lock(&svsb->lock);

	/*
	 * 2-line bank updates its corresponding opp volts.
	 * 1-line bank updates all opp volts.
	 */
	if (svsb->type == SVSB_HIGH) {
		opp_start = 0;
		opp_stop = svsb->turn_pt;
	} else if (svsb->type == SVSB_LOW) {
		opp_start = svsb->turn_pt;
		opp_stop = svsb->opp_count;
	} else {
		opp_start = 0;
		opp_stop = svsb->opp_count;
	}

	/* Get thermal effect */
	if (svsb->phase == SVSB_PHASE_MON) {
		ret = thermal_zone_get_temp(svsb->tzd, &tzone_temp);
		if (ret || (svsb->temp > SVSB_TEMP_UPPER_BOUND &&
			    svsb->temp < SVSB_TEMP_LOWER_BOUND)) {
			dev_err(svsb->dev, "%s: %d (0x%x), run default volts\n",
				svsb->tzone_name, ret, svsb->temp);
			svsb->phase = SVSB_PHASE_ERROR;
		}

		if (tzone_temp >= svsb->tzone_htemp)
			temp_voffset += svsb->tzone_htemp_voffset;
		else if (tzone_temp <= svsb->tzone_ltemp)
			temp_voffset += svsb->tzone_ltemp_voffset;

		/* 2-line bank update all opp volts when running mon mode */
		if (svsb->type == SVSB_HIGH || svsb->type == SVSB_LOW) {
			opp_start = 0;
			opp_stop = svsb->opp_count;
		}
	}

	/* vmin <= svsb_volt (opp_volt) <= default opp voltage */
	for (i = opp_start; i < opp_stop; i++) {
		switch (svsb->phase) {
		case SVSB_PHASE_ERROR:
			opp_volt = svsb->opp_dvolt[i];
			break;
		case SVSB_PHASE_INIT01:
			/* do nothing */
			goto unlock_mutex;
		case SVSB_PHASE_INIT02:
			svsb_volt = max(svsb->volt[i], svsb->vmin);
			opp_volt = svs_bank_volt_to_opp_volt(svsb_volt,
							     svsb->volt_step,
							     svsb->volt_base);
			break;
		case SVSB_PHASE_MON:
			svsb_volt = max(svsb->volt[i] + temp_voffset, svsb->vmin);
			opp_volt = svs_bank_volt_to_opp_volt(svsb_volt,
							     svsb->volt_step,
							     svsb->volt_base);
			break;
		default:
			dev_err(svsb->dev, "unknown phase: %u\n", svsb->phase);
			ret = -EINVAL;
			goto unlock_mutex;
		}

		opp_volt = min(opp_volt, svsb->opp_dvolt[i]);
		ret = dev_pm_opp_adjust_voltage(svsb->opp_dev,
						svsb->opp_dfreq[i],
						opp_volt, opp_volt,
						svsb->opp_dvolt[i]);
		if (ret) {
			dev_err(svsb->dev, "set %uuV fail: %d\n",
				opp_volt, ret);
			goto unlock_mutex;
		}
	}

unlock_mutex:
	mutex_unlock(&svsb->lock);

	return ret;
}

static int svs_dump_debug_show(struct seq_file *m, void *p)
{
	struct svs_platform *svsp = (struct svs_platform *)m->private;
	struct svs_bank *svsb;
	unsigned long svs_reg_addr;
	u32 idx, i, j, bank_id;

	for (i = 0; i < svsp->efuse_max; i++)
		if (svsp->efuse && svsp->efuse[i])
			seq_printf(m, "M_HW_RES%d = 0x%08x\n",
				   i, svsp->efuse[i]);

	for (i = 0; i < svsp->tefuse_max; i++)
		if (svsp->tefuse)
			seq_printf(m, "THERMAL_EFUSE%d = 0x%08x\n",
				   i, svsp->tefuse[i]);

	for (bank_id = 0, idx = 0; idx < svsp->bank_max; idx++, bank_id++) {
		svsb = &svsp->banks[idx];

		for (i = SVSB_PHASE_INIT01; i <= SVSB_PHASE_MON; i++) {
			seq_printf(m, "Bank_number = %u\n", bank_id);

			if (i == SVSB_PHASE_INIT01 || i == SVSB_PHASE_INIT02)
				seq_printf(m, "mode = init%d\n", i);
			else if (i == SVSB_PHASE_MON)
				seq_puts(m, "mode = mon\n");
			else
				seq_puts(m, "mode = error\n");

			for (j = DESCHAR; j < SVS_REG_MAX; j++) {
				svs_reg_addr = (unsigned long)(svsp->base +
							       svsp->regs[j]);
				seq_printf(m, "0x%08lx = 0x%08x\n",
					   svs_reg_addr, svsb->reg_data[i][j]);
			}
		}
	}

	return 0;
}

debug_fops_ro(dump);

static int svs_enable_debug_show(struct seq_file *m, void *v)
{
	struct svs_bank *svsb = (struct svs_bank *)m->private;

	switch (svsb->phase) {
	case SVSB_PHASE_ERROR:
		seq_puts(m, "disabled\n");
		break;
	case SVSB_PHASE_INIT01:
		seq_puts(m, "init1\n");
		break;
	case SVSB_PHASE_INIT02:
		seq_puts(m, "init2\n");
		break;
	case SVSB_PHASE_MON:
		seq_puts(m, "mon mode\n");
		break;
	default:
		seq_puts(m, "unknown\n");
		break;
	}

	return 0;
}

static ssize_t svs_enable_debug_write(struct file *filp,
				      const char __user *buffer,
				      size_t count, loff_t *pos)
{
	struct svs_bank *svsb = file_inode(filp)->i_private;
	struct svs_platform *svsp = dev_get_drvdata(svsb->dev);
	unsigned long flags;
	int enabled, ret;
	char *buf = NULL;

	if (count >= PAGE_SIZE)
		return -EINVAL;

	buf = (char *)memdup_user_nul(buffer, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = kstrtoint(buf, 10, &enabled);
	if (ret)
		return ret;

	if (!enabled) {
		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;
		svsb->mode_support = SVSB_MODE_ALL_DISABLE;
		svs_switch_bank(svsp);
		svs_writel_relaxed(svsp, SVSB_PTPEN_OFF, SVSEN);
		svs_writel_relaxed(svsp, SVSB_INTSTS_VAL_CLEAN, INTSTS);
		spin_unlock_irqrestore(&svs_lock, flags);

		svsb->phase = SVSB_PHASE_ERROR;
		svs_adjust_pm_opp_volts(svsb);
	}

	kfree(buf);

	return count;
}

debug_fops_rw(enable);

static int svs_status_debug_show(struct seq_file *m, void *v)
{
	struct svs_bank *svsb = (struct svs_bank *)m->private;
	struct dev_pm_opp *opp;
	int tzone_temp = 0, ret;
	u32 i;

	ret = thermal_zone_get_temp(svsb->tzd, &tzone_temp);
	if (ret)
		seq_printf(m, "%s: temperature ignore, turn_pt = %u\n",
			   svsb->name, svsb->turn_pt);
	else
		seq_printf(m, "%s: temperature = %d, turn_pt = %u\n",
			   svsb->name, tzone_temp, svsb->turn_pt);

	for (i = 0; i < svsb->opp_count; i++) {
		opp = dev_pm_opp_find_freq_exact(svsb->opp_dev,
						 svsb->opp_dfreq[i], true);
		if (IS_ERR(opp)) {
			seq_printf(m, "%s: cannot find freq = %u (%ld)\n",
				   svsb->name, svsb->opp_dfreq[i],
				   PTR_ERR(opp));
			return PTR_ERR(opp);
		}

		seq_printf(m, "opp_freq[%02u]: %u, opp_volt[%02u]: %lu, ",
			   i, svsb->opp_dfreq[i], i,
			   dev_pm_opp_get_voltage(opp));
		seq_printf(m, "svsb_volt[%02u]: 0x%x, freq_pct[%02u]: %u\n",
			   i, svsb->volt[i], i, svsb->freq_pct[i]);
		dev_pm_opp_put(opp);
	}

	return 0;
}

debug_fops_ro(status);

static int svs_create_debug_cmds(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	struct dentry *svs_dir, *svsb_dir, *file_entry;
	const char *d = "/sys/kernel/debug/svs";
	u32 i, idx;

	struct svs_dentry {
		const char *name;
		const struct file_operations *fops;
	};

	struct svs_dentry svs_entries[] = {
		svs_dentry_data(dump),
	};

	struct svs_dentry svsb_entries[] = {
		svs_dentry_data(enable),
		svs_dentry_data(status),
	};

	svs_dir = debugfs_create_dir("svs", NULL);
	if (IS_ERR(svs_dir)) {
		dev_err(svsp->dev, "cannot create %s: %ld\n",
			d, PTR_ERR(svs_dir));
		return PTR_ERR(svs_dir);
	}

	for (i = 0; i < ARRAY_SIZE(svs_entries); i++) {
		file_entry = debugfs_create_file(svs_entries[i].name, 0664,
						 svs_dir, svsp,
						 svs_entries[i].fops);
		if (IS_ERR(file_entry)) {
			dev_err(svsp->dev, "cannot create %s/%s: %ld\n",
				d, svs_entries[i].name, PTR_ERR(file_entry));
			return PTR_ERR(file_entry);
		}
	}

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (svsb->mode_support == SVSB_MODE_ALL_DISABLE)
			continue;

		svsb_dir = debugfs_create_dir(svsb->name, svs_dir);
		if (IS_ERR(svsb_dir)) {
			dev_err(svsp->dev, "cannot create %s/%s: %ld\n",
				d, svsb->name, PTR_ERR(svsb_dir));
			return PTR_ERR(svsb_dir);
		}

		for (i = 0; i < ARRAY_SIZE(svsb_entries); i++) {
			file_entry = debugfs_create_file(svsb_entries[i].name,
							 0664, svsb_dir, svsb,
							 svsb_entries[i].fops);
			if (IS_ERR(file_entry)) {
				dev_err(svsp->dev, "no %s/%s/%s?: %ld\n",
					d, svsb->name, svsb_entries[i].name,
					PTR_ERR(file_entry));
				return PTR_ERR(file_entry);
			}
		}
	}

	return 0;
}

static u32 interpolate(u32 f0, u32 f1, u32 v0, u32 v1, u32 fx)
{
	u32 vx;

	if (v0 == v1 || f0 == f1)
		return v0;

	/* *100 to have decimal fraction factor */
	vx = (v0 * 100) - ((((v0 - v1) * 100) / (f0 - f1)) * (f0 - fx));

	return DIV_ROUND_UP(vx, 100);
}

void svs_get_bank_volts_v3(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;
	u32 i, j, *vop, vop74, vop30, turn_pt = svsb->turn_pt;
	u32 b_sft, shift_byte = 0, opp_start = 0, opp_stop = 0;
	u32 middle_index = (svsb->opp_count / 2);

	if (svsb->phase == SVSB_PHASE_MON &&
	    svsb->volt_flags & SVSB_MON_VOLT_IGNORE)
		return;

	vop74 = svs_readl_relaxed(svsp, VOP74);
	vop30 = svs_readl_relaxed(svsp, VOP30);

	/* Target is to set svsb->volt[] by algorithm */
	if (turn_pt < middle_index) {
		if (svsb->type == SVSB_HIGH) {
			/* volt[0] ~ volt[turn_pt - 1] */
			for (i = 0; i < turn_pt; i++) {
				b_sft = BITS8 * (shift_byte % REG_BYTES);
				vop = (shift_byte < REG_BYTES) ? &vop30 :
								 &vop74;
				svsb->volt[i] = (*vop >> b_sft) & GENMASK(7, 0);
				shift_byte++;
			}
		} else if (svsb->type == SVSB_LOW) {
			/* volt[turn_pt] + volt[j] ~ volt[opp_count - 1] */
			j = svsb->opp_count - 7;
			svsb->volt[turn_pt] = FIELD_GET(SVSB_VOPS_FLD_VOP0_4, vop30);
			shift_byte++;
			for (i = j; i < svsb->opp_count; i++) {
				b_sft = BITS8 * (shift_byte % REG_BYTES);
				vop = (shift_byte < REG_BYTES) ? &vop30 :
								 &vop74;
				svsb->volt[i] = (*vop >> b_sft) & GENMASK(7, 0);
				shift_byte++;
			}

			/* volt[turn_pt + 1] ~ volt[j - 1] by interpolate */
			for (i = turn_pt + 1; i < j; i++)
				svsb->volt[i] = interpolate(svsb->freq_pct[turn_pt],
							    svsb->freq_pct[j],
							    svsb->volt[turn_pt],
							    svsb->volt[j],
							    svsb->freq_pct[i]);
		}
	} else {
		if (svsb->type == SVSB_HIGH) {
			/* volt[0] + volt[j] ~ volt[turn_pt - 1] */
			j = turn_pt - 7;
			svsb->volt[0] = FIELD_GET(SVSB_VOPS_FLD_VOP0_4, vop30);
			shift_byte++;
			for (i = j; i < turn_pt; i++) {
				b_sft = BITS8 * (shift_byte % REG_BYTES);
				vop = (shift_byte < REG_BYTES) ? &vop30 :
								 &vop74;
				svsb->volt[i] = (*vop >> b_sft) & GENMASK(7, 0);
				shift_byte++;
			}

			/* volt[1] ~ volt[j - 1] by interpolate */
			for (i = 1; i < j; i++)
				svsb->volt[i] = interpolate(svsb->freq_pct[0],
							    svsb->freq_pct[j],
							    svsb->volt[0],
							    svsb->volt[j],
							    svsb->freq_pct[i]);
		} else if (svsb->type == SVSB_LOW) {
			/* volt[turn_pt] ~ volt[opp_count - 1] */
			for (i = turn_pt; i < svsb->opp_count; i++) {
				b_sft = BITS8 * (shift_byte % REG_BYTES);
				vop = (shift_byte < REG_BYTES) ? &vop30 :
								 &vop74;
				svsb->volt[i] = (*vop >> b_sft) & GENMASK(7, 0);
				shift_byte++;
			}
		}
	}

	if (svsb->type == SVSB_HIGH) {
		opp_start = 0;
		opp_stop = svsb->turn_pt;
	} else if (svsb->type == SVSB_LOW) {
		opp_start = svsb->turn_pt;
		opp_stop = svsb->opp_count;
	}

	for (i = opp_start; i < opp_stop; i++)
		if (svsb->volt_flags & SVSB_REMOVE_DVTFIXED_VOLT)
			svsb->volt[i] -= svsb->dvt_fixed;
}

void svs_set_bank_freq_pct_v3(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;
	u32 i, j, *freq_pct, freq_pct74 = 0, freq_pct30 = 0;
	u32 b_sft, shift_byte = 0, turn_pt;
	u32 middle_index = (svsb->opp_count / 2);

	for (i = 0; i < svsb->opp_count; i++) {
		if (svsb->opp_dfreq[i] <= svsb->turn_freq_base) {
			svsb->turn_pt = i;
			break;
		}
	}

	turn_pt = svsb->turn_pt;

	/* Target is to fill out freq_pct74 / freq_pct30 by algorithm */
	if (turn_pt < middle_index) {
		if (svsb->type == SVSB_HIGH) {
			/*
			 * If we don't handle this situation,
			 * SVSB_HIGH's FREQPCT74 / FREQPCT30 would keep "0"
			 * and this leads SVSB_LOW to work abnormally.
			 */
			if (turn_pt == 0)
				freq_pct30 = svsb->freq_pct[0];

			/* freq_pct[0] ~ freq_pct[turn_pt - 1] */
			for (i = 0; i < turn_pt; i++) {
				b_sft = BITS8 * (shift_byte % REG_BYTES);
				freq_pct = (shift_byte < REG_BYTES) ?
					   &freq_pct30 : &freq_pct74;
				*freq_pct |= (svsb->freq_pct[i] << b_sft);
				shift_byte++;
			}
		} else if (svsb->type == SVSB_LOW) {
			/*
			 * freq_pct[turn_pt] +
			 * freq_pct[opp_count - 7] ~ freq_pct[opp_count -1]
			 */
			freq_pct30 = svsb->freq_pct[turn_pt];
			shift_byte++;
			j = svsb->opp_count - 7;
			for (i = j; i < svsb->opp_count; i++) {
				b_sft = BITS8 * (shift_byte % REG_BYTES);
				freq_pct = (shift_byte < REG_BYTES) ?
					   &freq_pct30 : &freq_pct74;
				*freq_pct |= (svsb->freq_pct[i] << b_sft);
				shift_byte++;
			}
		}
	} else {
		if (svsb->type == SVSB_HIGH) {
			/*
			 * freq_pct[0] +
			 * freq_pct[turn_pt - 7] ~ freq_pct[turn_pt - 1]
			 */
			freq_pct30 = svsb->freq_pct[0];
			shift_byte++;
			j = turn_pt - 7;
			for (i = j; i < turn_pt; i++) {
				b_sft = BITS8 * (shift_byte % REG_BYTES);
				freq_pct = (shift_byte < REG_BYTES) ?
					   &freq_pct30 : &freq_pct74;
				*freq_pct |= (svsb->freq_pct[i] << b_sft);
				shift_byte++;
			}
		} else if (svsb->type == SVSB_LOW) {
			/* freq_pct[turn_pt] ~ freq_pct[opp_count - 1] */
			for (i = turn_pt; i < svsb->opp_count; i++) {
				b_sft = BITS8 * (shift_byte % REG_BYTES);
				freq_pct = (shift_byte < REG_BYTES) ?
					   &freq_pct30 : &freq_pct74;
				*freq_pct |= (svsb->freq_pct[i] << b_sft);
				shift_byte++;
			}
		}
	}

	svs_writel_relaxed(svsp, freq_pct74, FREQPCT74);
	svs_writel_relaxed(svsp, freq_pct30, FREQPCT30);
}

void svs_get_bank_volts_v2(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;
	u32 temp, i;

	temp = svs_readl_relaxed(svsp, VOP74);
	svsb->volt[14] = FIELD_GET(SVSB_VOPS_FLD_VOP3_7, temp);
	svsb->volt[12] = FIELD_GET(SVSB_VOPS_FLD_VOP2_6, temp);
	svsb->volt[10] = FIELD_GET(SVSB_VOPS_FLD_VOP1_5, temp);
	svsb->volt[8] = FIELD_GET(SVSB_VOPS_FLD_VOP0_4, temp);

	temp = svs_readl_relaxed(svsp, VOP30);
	svsb->volt[6] = FIELD_GET(SVSB_VOPS_FLD_VOP3_7, temp);
	svsb->volt[4] = FIELD_GET(SVSB_VOPS_FLD_VOP2_6, temp);
	svsb->volt[2] = FIELD_GET(SVSB_VOPS_FLD_VOP1_5, temp);
	svsb->volt[0] = FIELD_GET(SVSB_VOPS_FLD_VOP0_4, temp);

	for (i = 0; i <= 12; i += 2)
		svsb->volt[i + 1] = interpolate(svsb->freq_pct[i],
						svsb->freq_pct[i + 2],
						svsb->volt[i],
						svsb->volt[i + 2],
						svsb->freq_pct[i + 1]);

	svsb->volt[15] = interpolate(svsb->freq_pct[12],
				     svsb->freq_pct[14],
				     svsb->volt[12],
				     svsb->volt[14],
				     svsb->freq_pct[15]);

	for (i = 0; i < svsb->opp_count; i++)
		svsb->volt[i] += svsb->volt_od;
}

void svs_set_bank_freq_pct_v2(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;
	u32 freqpct74_val, freqpct30_val;

	freqpct74_val = FIELD_PREP(SVSB_FREQPCTS_FLD_PCT0_4, svsb->freq_pct[8]) |
			FIELD_PREP(SVSB_FREQPCTS_FLD_PCT1_5, svsb->freq_pct[10]) |
			FIELD_PREP(SVSB_FREQPCTS_FLD_PCT2_6, svsb->freq_pct[12]) |
			FIELD_PREP(SVSB_FREQPCTS_FLD_PCT3_7, svsb->freq_pct[14]);

	freqpct30_val = FIELD_PREP(SVSB_FREQPCTS_FLD_PCT0_4, svsb->freq_pct[0]) |
			FIELD_PREP(SVSB_FREQPCTS_FLD_PCT1_5, svsb->freq_pct[2]) |
			FIELD_PREP(SVSB_FREQPCTS_FLD_PCT2_6, svsb->freq_pct[4]) |
			FIELD_PREP(SVSB_FREQPCTS_FLD_PCT3_7, svsb->freq_pct[6]);

	svs_writel_relaxed(svsp, freqpct74_val, FREQPCT74);
	svs_writel_relaxed(svsp, freqpct30_val, FREQPCT30);
}

static void svs_set_bank_phase(struct svs_platform *svsp,
			       enum svsb_phase target_phase)
{
	struct svs_bank *svsb = svsp->pbank;
	u32 des_char, temp_char, det_char, limit_vals, init2vals, ts_calcs;

	svs_switch_bank(svsp);

	des_char = FIELD_PREP(SVSB_DESCHAR_FLD_BDES, svsb->bdes) |
		   FIELD_PREP(SVSB_DESCHAR_FLD_MDES, svsb->mdes);
	svs_writel_relaxed(svsp, des_char, DESCHAR);

	temp_char = FIELD_PREP(SVSB_TEMPCHAR_FLD_VCO, svsb->vco) |
		    FIELD_PREP(SVSB_TEMPCHAR_FLD_MTDES, svsb->mtdes) |
		    FIELD_PREP(SVSB_TEMPCHAR_FLD_DVT_FIXED, svsb->dvt_fixed);
	svs_writel_relaxed(svsp, temp_char, TEMPCHAR);

	det_char = FIELD_PREP(SVSB_DETCHAR_FLD_DCBDET, svsb->dcbdet) |
		   FIELD_PREP(SVSB_DETCHAR_FLD_DCMDET, svsb->dcmdet);
	svs_writel_relaxed(svsp, det_char, DETCHAR);

	svs_writel_relaxed(svsp, svsb->dc_config, DCCONFIG);
	svs_writel_relaxed(svsp, svsb->age_config, AGECONFIG);
	svs_writel_relaxed(svsp, SVSB_RUNCONFIG_DEFAULT, RUNCONFIG);

	svsb->set_freq_pct(svsp);

	limit_vals = FIELD_PREP(SVSB_LIMITVALS_FLD_DTLO, SVSB_VAL_DTLO) |
		     FIELD_PREP(SVSB_LIMITVALS_FLD_DTHI, SVSB_VAL_DTHI) |
		     FIELD_PREP(SVSB_LIMITVALS_FLD_VMIN, svsb->vmin) |
		     FIELD_PREP(SVSB_LIMITVALS_FLD_VMAX, svsb->vmax);
	svs_writel_relaxed(svsp, limit_vals, LIMITVALS);

	svs_writel_relaxed(svsp, SVSB_DET_WINDOW, DETWINDOW);
	svs_writel_relaxed(svsp, SVSB_DET_MAX, CONFIG);
	svs_writel_relaxed(svsp, svsb->chk_shift, CHKSHIFT);
	svs_writel_relaxed(svsp, svsb->ctl0, CTL0);
	svs_writel_relaxed(svsp, SVSB_INTSTS_VAL_CLEAN, INTSTS);

	switch (target_phase) {
	case SVSB_PHASE_INIT01:
		svs_writel_relaxed(svsp, svsb->vboot, VBOOT);
		svs_writel_relaxed(svsp, SVSB_INTEN_INIT0x, INTEN);
		svs_writel_relaxed(svsp, SVSB_PTPEN_INIT01, SVSEN);
		break;
	case SVSB_PHASE_INIT02:
		init2vals = FIELD_PREP(SVSB_INIT2VALS_FLD_AGEVOFFSETIN, svsb->age_voffset_in) |
			    FIELD_PREP(SVSB_INIT2VALS_FLD_DCVOFFSETIN, svsb->dc_voffset_in);
		svs_writel_relaxed(svsp, SVSB_INTEN_INIT0x, INTEN);
		svs_writel_relaxed(svsp, init2vals, INIT2VALS);
		svs_writel_relaxed(svsp, SVSB_PTPEN_INIT02, SVSEN);
		break;
	case SVSB_PHASE_MON:
		ts_calcs = FIELD_PREP(SVSB_TSCALCS_FLD_BTS, svsb->bts) |
			   FIELD_PREP(SVSB_TSCALCS_FLD_MTS, svsb->mts);
		svs_writel_relaxed(svsp, ts_calcs, TSCALCS);
		svs_writel_relaxed(svsp, SVSB_INTEN_MONVOPEN, INTEN);
		svs_writel_relaxed(svsp, SVSB_PTPEN_MON, SVSEN);
		break;
	default:
		dev_err(svsb->dev, "requested unknown target phase: %u\n",
			target_phase);
		break;
	}
}

static inline void svs_save_bank_register_data(struct svs_platform *svsp,
					       enum svsb_phase phase)
{
	struct svs_bank *svsb = svsp->pbank;
	enum svs_reg_index rg_i;

	for (rg_i = DESCHAR; rg_i < SVS_REG_MAX; rg_i++)
		svsb->reg_data[phase][rg_i] = svs_readl_relaxed(svsp, rg_i);
}

static inline void svs_error_isr_handler(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	dev_err(svsb->dev, "%s: CORESEL = 0x%08x\n",
		__func__, svs_readl_relaxed(svsp, CORESEL));
	dev_err(svsb->dev, "SVSEN = 0x%08x, INTSTS = 0x%08x\n",
		svs_readl_relaxed(svsp, SVSEN),
		svs_readl_relaxed(svsp, INTSTS));
	dev_err(svsb->dev, "SMSTATE0 = 0x%08x, SMSTATE1 = 0x%08x\n",
		svs_readl_relaxed(svsp, SMSTATE0),
		svs_readl_relaxed(svsp, SMSTATE1));
	dev_err(svsb->dev, "TEMP = 0x%08x\n", svs_readl_relaxed(svsp, TEMP));

	svs_save_bank_register_data(svsp, SVSB_PHASE_ERROR);

	svsb->phase = SVSB_PHASE_ERROR;
	svs_writel_relaxed(svsp, SVSB_PTPEN_OFF, SVSEN);
	svs_writel_relaxed(svsp, SVSB_INTSTS_VAL_CLEAN, INTSTS);
}

static inline void svs_init01_isr_handler(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	dev_info(svsb->dev, "%s: VDN74~30:0x%08x~0x%08x, DC:0x%08x\n",
		 __func__, svs_readl_relaxed(svsp, VDESIGN74),
		 svs_readl_relaxed(svsp, VDESIGN30),
		 svs_readl_relaxed(svsp, DCVALUES));

	svs_save_bank_register_data(svsp, SVSB_PHASE_INIT01);

	svsb->phase = SVSB_PHASE_INIT01;
	svsb->dc_voffset_in = ~(svs_readl_relaxed(svsp, DCVALUES) &
				GENMASK(15, 0)) + 1;
	if (svsb->volt_flags & SVSB_INIT01_VOLT_IGNORE ||
	    (svsb->dc_voffset_in & SVSB_DC_SIGNED_BIT &&
	     svsb->volt_flags & SVSB_INIT01_VOLT_INC_ONLY))
		svsb->dc_voffset_in = 0;

	svsb->age_voffset_in = svs_readl_relaxed(svsp, AGEVALUES) &
			       GENMASK(15, 0);

	svs_writel_relaxed(svsp, SVSB_PTPEN_OFF, SVSEN);
	svs_writel_relaxed(svsp, SVSB_INTSTS_F0_COMPLETE, INTSTS);
	svsb->core_sel &= ~SVSB_DET_CLK_EN;
}

static inline void svs_init02_isr_handler(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	dev_info(svsb->dev, "%s: VOP74~30:0x%08x~0x%08x, DC:0x%08x\n",
		 __func__, svs_readl_relaxed(svsp, VOP74),
		 svs_readl_relaxed(svsp, VOP30),
		 svs_readl_relaxed(svsp, DCVALUES));

	svs_save_bank_register_data(svsp, SVSB_PHASE_INIT02);

	svsb->phase = SVSB_PHASE_INIT02;
	svsb->get_volts(svsp);

	svs_writel_relaxed(svsp, SVSB_PTPEN_OFF, SVSEN);
	svs_writel_relaxed(svsp, SVSB_INTSTS_F0_COMPLETE, INTSTS);
}

static inline void svs_mon_mode_isr_handler(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	svs_save_bank_register_data(svsp, SVSB_PHASE_MON);

	svsb->phase = SVSB_PHASE_MON;
	svsb->get_volts(svsp);

	svsb->temp = svs_readl_relaxed(svsp, TEMP) & GENMASK(7, 0);
	svs_writel_relaxed(svsp, SVSB_INTSTS_FLD_MONVOP, INTSTS);
}

static irqreturn_t svs_isr(int irq, void *data)
{
	struct svs_platform *svsp = data;
	struct svs_bank *svsb = NULL;
	unsigned long flags;
	u32 idx, int_sts, svs_en;

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];
		WARN(!svsb, "%s: svsb(%s) is null", __func__, svsb->name);

		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;

		/* Find out which svs bank fires interrupt */
		if (svsb->int_st & svs_readl_relaxed(svsp, INTST)) {
			spin_unlock_irqrestore(&svs_lock, flags);
			continue;
		}

		svs_switch_bank(svsp);
		int_sts = svs_readl_relaxed(svsp, INTSTS);
		svs_en = svs_readl_relaxed(svsp, SVSEN);

		if (int_sts == SVSB_INTSTS_F0_COMPLETE &&
		    svs_en == SVSB_PTPEN_INIT01)
			svs_init01_isr_handler(svsp);
		else if (int_sts == SVSB_INTSTS_F0_COMPLETE &&
			 svs_en == SVSB_PTPEN_INIT02)
			svs_init02_isr_handler(svsp);
		else if (int_sts & SVSB_INTSTS_FLD_MONVOP)
			svs_mon_mode_isr_handler(svsp);
		else
			svs_error_isr_handler(svsp);

		spin_unlock_irqrestore(&svs_lock, flags);
		break;
	}

	svs_adjust_pm_opp_volts(svsb);

	if (svsb->phase == SVSB_PHASE_INIT01 ||
	    svsb->phase == SVSB_PHASE_INIT02)
		complete(&svsb->init_completion);

	return IRQ_HANDLED;
}

static int svs_init01(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	unsigned long flags, time_left;
	bool search_done;
	int ret = 0, r;
	u32 opp_freq, opp_vboot, buck_volt, idx, i;

	/* Keep CPUs' core power on for svs_init01 initialization */
	cpuidle_pause_and_lock();

	 /* Svs bank init01 preparation - power enable */
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT01))
			continue;

		ret = regulator_enable(svsb->buck);
		if (ret) {
			dev_err(svsb->dev, "%s enable fail: %d\n",
				svsb->buck_name, ret);
			goto svs_init01_resume_cpuidle;
		}

		/* Some buck doesn't support mode change. Show fail msg only */
		ret = regulator_set_mode(svsb->buck, REGULATOR_MODE_FAST);
		if (ret)
			dev_notice(svsb->dev, "set fast mode fail: %d\n", ret);

		if (svsb->volt_flags & SVSB_INIT01_PD_REQ) {
			if (!pm_runtime_enabled(svsb->opp_dev)) {
				pm_runtime_enable(svsb->opp_dev);
				svsb->pm_runtime_enabled_count++;
			}

			ret = pm_runtime_get_sync(svsb->opp_dev);
			if (ret < 0) {
				dev_err(svsb->dev, "mtcmos on fail: %d\n", ret);
				goto svs_init01_resume_cpuidle;
			}
		}
	}

	/*
	 * Svs bank init01 preparation - vboot voltage adjustment
	 * Sometimes two svs banks use the same buck. Therefore,
	 * we have to set each svs bank to target voltage(vboot) first.
	 */
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT01))
			continue;

		/*
		 * Find the fastest freq that can be run at vboot and
		 * fix to that freq until svs_init01 is done.
		 */
		search_done = false;
		opp_vboot = svs_bank_volt_to_opp_volt(svsb->vboot,
						      svsb->volt_step,
						      svsb->volt_base);

		for (i = 0; i < svsb->opp_count; i++) {
			opp_freq = svsb->opp_dfreq[i];
			if (!search_done && svsb->opp_dvolt[i] <= opp_vboot) {
				ret = dev_pm_opp_adjust_voltage(svsb->opp_dev,
								opp_freq,
								opp_vboot,
								opp_vboot,
								opp_vboot);
				if (ret) {
					dev_err(svsb->dev,
						"set opp %uuV vboot fail: %d\n",
						opp_vboot, ret);
					goto svs_init01_finish;
				}

				search_done = true;
			} else {
				ret = dev_pm_opp_disable(svsb->opp_dev,
							 svsb->opp_dfreq[i]);
				if (ret) {
					dev_err(svsb->dev,
						"opp %uHz disable fail: %d\n",
						svsb->opp_dfreq[i], ret);
					goto svs_init01_finish;
				}
			}
		}
	}

	/* Svs bank init01 begins */
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT01))
			continue;

		opp_vboot = svs_bank_volt_to_opp_volt(svsb->vboot,
						      svsb->volt_step,
						      svsb->volt_base);

		buck_volt = regulator_get_voltage(svsb->buck);
		if (buck_volt != opp_vboot) {
			dev_err(svsb->dev,
				"buck voltage: %uuV, expected vboot: %uuV\n",
				buck_volt, opp_vboot);
			ret = -EPERM;
			goto svs_init01_finish;
		}

		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;
		svs_set_bank_phase(svsp, SVSB_PHASE_INIT01);
		spin_unlock_irqrestore(&svs_lock, flags);

		time_left = wait_for_completion_timeout(&svsb->init_completion,
							msecs_to_jiffies(5000));
		if (!time_left) {
			dev_err(svsb->dev, "init01 completion timeout\n");
			ret = -EBUSY;
			goto svs_init01_finish;
		}
	}

svs_init01_finish:
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT01))
			continue;

		for (i = 0; i < svsb->opp_count; i++) {
			r = dev_pm_opp_enable(svsb->opp_dev,
					      svsb->opp_dfreq[i]);
			if (r)
				dev_err(svsb->dev, "opp %uHz enable fail: %d\n",
					svsb->opp_dfreq[i], r);
		}

		if (svsb->volt_flags & SVSB_INIT01_PD_REQ) {
			r = pm_runtime_put_sync(svsb->opp_dev);
			if (r)
				dev_err(svsb->dev, "mtcmos off fail: %d\n", r);

			if (svsb->pm_runtime_enabled_count > 0) {
				pm_runtime_disable(svsb->opp_dev);
				svsb->pm_runtime_enabled_count--;
			}
		}

		r = regulator_set_mode(svsb->buck, REGULATOR_MODE_NORMAL);
		if (r)
			dev_notice(svsb->dev, "set normal mode fail: %d\n", r);

		r = regulator_disable(svsb->buck);
		if (r)
			dev_err(svsb->dev, "%s disable fail: %d\n",
				svsb->buck_name, r);
	}

svs_init01_resume_cpuidle:
	cpuidle_resume_and_unlock();

	return ret;
}

static int svs_init02(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	unsigned long flags, time_left;
	u32 idx;

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT02))
			continue;

		reinit_completion(&svsb->init_completion);
		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;
		svs_set_bank_phase(svsp, SVSB_PHASE_INIT02);
		spin_unlock_irqrestore(&svs_lock, flags);

		time_left = wait_for_completion_timeout(&svsb->init_completion,
							msecs_to_jiffies(5000));
		if (!time_left) {
			dev_err(svsb->dev, "init02 completion timeout\n");
			return -EBUSY;
		}
	}

	/*
	 * 2-line high/low bank update its corresponding opp voltages only.
	 * Therefore, we sync voltages from opp for high/low bank voltages
	 * consistency.
	 */
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT02))
			continue;

		if (svsb->type == SVSB_HIGH || svsb->type == SVSB_LOW) {
			if (svs_sync_bank_volts_from_opp(svsb)) {
				dev_err(svsb->dev, "sync volt fail\n");
				return -EPERM;
			}
		}
	}

	return 0;
}

static void svs_mon_mode(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	unsigned long flags;
	u32 idx;

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_MON))
			continue;

		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;
		svs_set_bank_phase(svsp, SVSB_PHASE_MON);
		spin_unlock_irqrestore(&svs_lock, flags);
	}
}

static int svs_start(struct svs_platform *svsp)
{
	int ret;

	ret = svs_init01(svsp);
	if (ret)
		return ret;

	ret = svs_init02(svsp);
	if (ret)
		return ret;

	svs_mon_mode(svsp);

	return 0;
}

static int svs_suspend(struct device *dev)
{
	struct svs_platform *svsp = dev_get_drvdata(dev);
	struct svs_bank *svsb;
	unsigned long flags;
	int ret;
	u32 idx;

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		/* This might wait for svs_isr() process */
		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;
		svs_switch_bank(svsp);
		svs_writel_relaxed(svsp, SVSB_PTPEN_OFF, SVSEN);
		svs_writel_relaxed(svsp, SVSB_INTSTS_VAL_CLEAN, INTSTS);
		spin_unlock_irqrestore(&svs_lock, flags);

		svsb->phase = SVSB_PHASE_ERROR;
		svs_adjust_pm_opp_volts(svsb);
	}

	ret = reset_control_assert(svsp->rst);
	if (ret) {
		dev_err(svsp->dev, "cannot assert reset %d\n", ret);
		return ret;
	}

	clk_disable_unprepare(svsp->main_clk);

	return 0;
}

static int svs_resume(struct device *dev)
{
	struct svs_platform *svsp = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(svsp->main_clk);
	if (ret) {
		dev_err(svsp->dev, "cannot enable main_clk, disable svs\n");
		return ret;
	}

	ret = reset_control_deassert(svsp->rst);
	if (ret) {
		dev_err(svsp->dev, "cannot deassert reset %d\n", ret);
		goto out_of_resume;
	}

	ret = svs_init02(svsp);
	if (ret)
		goto out_of_resume;

	svs_mon_mode(svsp);

	return 0;

out_of_resume:
	clk_disable_unprepare(svsp->main_clk);
	return ret;
}

static int svs_bank_resource_setup(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	struct dev_pm_opp *opp;
	unsigned long freq;
	int count, ret;
	u32 idx, i;

	dev_set_drvdata(svsp->dev, svsp);

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		switch (svsb->sw_id) {
		case SVSB_CPU_LITTLE:
			svsb->name = "SVSB_CPU_LITTLE";
			break;
		case SVSB_CPU_BIG:
			svsb->name = "SVSB_CPU_BIG";
			break;
		case SVSB_CCI:
			svsb->name = "SVSB_CCI";
			break;
		case SVSB_GPU:
			if (svsb->type == SVSB_HIGH)
				svsb->name = "SVSB_GPU_HIGH";
			else if (svsb->type == SVSB_LOW)
				svsb->name = "SVSB_GPU_LOW";
			else
				svsb->name = "SVSB_GPU";
			break;
		default:
			dev_err(svsb->dev, "unknown sw_id: %u\n", svsb->sw_id);
			return -EINVAL;
		}

		svsb->dev = devm_kzalloc(svsp->dev, sizeof(*svsb->dev),
					 GFP_KERNEL);
		if (!svsb->dev)
			return -ENOMEM;

		ret = dev_set_name(svsb->dev, "%s", svsb->name);
		if (ret)
			return ret;

		dev_set_drvdata(svsb->dev, svsp);

		ret = devm_pm_opp_of_add_table(svsb->opp_dev);
		if (ret) {
			dev_err(svsb->dev, "add opp table fail: %d\n", ret);
			return ret;
		}

		mutex_init(&svsb->lock);
		init_completion(&svsb->init_completion);

		if (svsb->mode_support & SVSB_MODE_INIT01) {
			svsb->buck = devm_regulator_get_optional(svsb->opp_dev,
								 svsb->buck_name);
			if (IS_ERR(svsb->buck)) {
				dev_err(svsb->dev, "cannot get \"%s-supply\"\n",
					svsb->buck_name);
				return PTR_ERR(svsb->buck);
			}
		}

		if (svsb->mode_support & SVSB_MODE_MON) {
			svsb->tzd = thermal_zone_get_zone_by_name(svsb->tzone_name);
			if (IS_ERR(svsb->tzd)) {
				dev_err(svsb->dev, "cannot get \"%s\" thermal zone\n",
					svsb->tzone_name);
				return PTR_ERR(svsb->tzd);
			}
		}

		count = dev_pm_opp_get_opp_count(svsb->opp_dev);
		if (svsb->opp_count != count) {
			dev_err(svsb->dev,
				"opp_count not \"%u\" but get \"%d\"?\n",
				svsb->opp_count, count);
			return count;
		}

		for (i = 0, freq = U32_MAX; i < svsb->opp_count; i++, freq--) {
			opp = dev_pm_opp_find_freq_floor(svsb->opp_dev, &freq);
			if (IS_ERR(opp)) {
				dev_err(svsb->dev, "cannot find freq = %ld\n",
					PTR_ERR(opp));
				return PTR_ERR(opp);
			}

			svsb->opp_dfreq[i] = freq;
			svsb->opp_dvolt[i] = dev_pm_opp_get_voltage(opp);
			svsb->freq_pct[i] = percent(svsb->opp_dfreq[i],
						    svsb->freq_base);
			dev_pm_opp_put(opp);
		}
	}

	return 0;
}

int svs_thermal_efuse_get_data(struct svs_platform *svsp)
{
	struct nvmem_cell *cell;

	/* Thermal efuse parsing */
	cell = nvmem_cell_get(svsp->dev, "t-calibration-data");
	if (IS_ERR_OR_NULL(cell)) {
		dev_err(svsp->dev, "no \"t-calibration-data\"? %ld\n", PTR_ERR(cell));
		return PTR_ERR(cell);
	}

	svsp->tefuse = nvmem_cell_read(cell, &svsp->tefuse_max);
	if (IS_ERR(svsp->tefuse)) {
		dev_err(svsp->dev, "cannot read thermal efuse: %ld\n",
			PTR_ERR(svsp->tefuse));
		nvmem_cell_put(cell);
		return PTR_ERR(svsp->tefuse);
	}

	svsp->tefuse_max /= sizeof(u32);
	nvmem_cell_put(cell);

	return 0;
}

static bool svs_is_efuse_data_correct(struct svs_platform *svsp)
{
	struct nvmem_cell *cell;

	/* Get svs efuse by nvmem */
	cell = nvmem_cell_get(svsp->dev, "svs-calibration-data");
	if (IS_ERR(cell)) {
		dev_err(svsp->dev, "no \"svs-calibration-data\"? %ld\n",
			PTR_ERR(cell));
		return false;
	}

	svsp->efuse = nvmem_cell_read(cell, &svsp->efuse_max);
	if (IS_ERR(svsp->efuse)) {
		dev_err(svsp->dev, "cannot read svs efuse: %ld\n",
			PTR_ERR(svsp->efuse));
		nvmem_cell_put(cell);
		return false;
	}

	svsp->efuse_max /= sizeof(u32);
	nvmem_cell_put(cell);

	return true;
}

struct device *svs_get_subsys_device(struct svs_platform *svsp,
					    const char *node_name)
{
	struct platform_device *pdev;
	struct device_node *np;

	np = of_find_node_by_name(NULL, node_name);
	if (!np) {
		dev_err(svsp->dev, "cannot find %s node\n", node_name);
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		of_node_put(np);
		dev_err(svsp->dev, "cannot find pdev by %s\n", node_name);
		return ERR_PTR(-ENXIO);
	}

	of_node_put(np);

	return &pdev->dev;
}

struct device *svs_add_device_link(struct svs_platform *svsp,
					  const char *node_name)
{
	struct device *dev;
	struct device_link *sup_link;

	if (!node_name) {
		dev_err(svsp->dev, "node name cannot be null\n");
		return ERR_PTR(-EINVAL);
	}

	dev = svs_get_subsys_device(svsp, node_name);
	if (IS_ERR(dev))
		return dev;

	sup_link = device_link_add(svsp->dev, dev,
				   DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!sup_link) {
		dev_err(svsp->dev, "sup_link is NULL\n");
		return ERR_PTR(-EINVAL);
	}

	if (sup_link->supplier->links.status != DL_DEV_DRIVER_BOUND)
		return ERR_PTR(-EPROBE_DEFER);

	return dev;
}

static const struct svs_platform_data svs_mt8192_platform_data = {
	.name = "mt8192-svs",
	.banks = svs_mt8192_banks,
	.efuse_parsing = svs_mt8192_efuse_parsing,
	.probe = svs_mt8192_platform_probe,
	.regs = svs_regs_v2,
	.bank_max = ARRAY_SIZE(svs_mt8192_banks),
};

static const struct svs_platform_data svs_mt8183_platform_data = {
	.name = "mt8183-svs",
	.banks = svs_mt8183_banks,
	.efuse_parsing = svs_mt8183_efuse_parsing,
	.probe = svs_mt8183_platform_probe,
	.regs = svs_regs_v2,
	.bank_max = ARRAY_SIZE(svs_mt8183_banks),
};

static const struct of_device_id svs_of_match[] = {
	{
		.compatible = "mediatek,mt8192-svs",
		.data = &svs_mt8192_platform_data,
	}, {
		.compatible = "mediatek,mt8183-svs",
		.data = &svs_mt8183_platform_data,
	}, {
		/* Sentinel */
	},
};

static int svs_probe(struct platform_device *pdev)
{
	struct svs_platform *svsp;
	const struct svs_platform_data *svsp_data;
	int ret, svsp_irq;

	svsp_data = of_device_get_match_data(&pdev->dev);

	svsp = devm_kzalloc(&pdev->dev, sizeof(*svsp), GFP_KERNEL);
	if (!svsp)
		return -ENOMEM;

	svsp->dev = &pdev->dev;
	svsp->banks = svsp_data->banks;
	svsp->regs = svsp_data->regs;
	svsp->bank_max = svsp_data->bank_max;

	ret = svsp_data->probe(svsp);
	if (ret)
		return ret;

	if (!svs_is_efuse_data_correct(svsp)) {
		dev_notice(svsp->dev, "efuse data isn't correct\n");
		ret = -EPERM;
		goto svs_probe_free_efuse;
	}

	if (!svsp_data->efuse_parsing(svsp)) {
		dev_notice(svsp->dev, "efuse data parsing failed\n");
		ret = -EPERM;
		goto svs_probe_free_resource;
	}

	ret = svs_bank_resource_setup(svsp);
	if (ret) {
		dev_err(svsp->dev, "svs bank resource setup fail: %d\n", ret);
		goto svs_probe_free_resource;
	}

	svsp_irq = platform_get_irq(pdev, 0);
	if (svsp_irq < 0) {
		ret = svsp_irq;
		goto svs_probe_free_resource;
	}

	ret = devm_request_threaded_irq(svsp->dev, svsp_irq, NULL, svs_isr,
					IRQF_ONESHOT, svsp_data->name, svsp);
	if (ret) {
		dev_err(svsp->dev, "register irq(%d) failed: %d\n",
			svsp_irq, ret);
		goto svs_probe_free_resource;
	}

	svsp->main_clk = devm_clk_get(svsp->dev, "main");
	if (IS_ERR(svsp->main_clk)) {
		dev_err(svsp->dev, "failed to get clock: %ld\n",
			PTR_ERR(svsp->main_clk));
		ret = PTR_ERR(svsp->main_clk);
		goto svs_probe_free_resource;
	}

	ret = clk_prepare_enable(svsp->main_clk);
	if (ret) {
		dev_err(svsp->dev, "cannot enable main clk: %d\n", ret);
		goto svs_probe_free_resource;
	}

	svsp->base = of_iomap(svsp->dev->of_node, 0);
	if (IS_ERR_OR_NULL(svsp->base)) {
		dev_err(svsp->dev, "cannot find svs register base\n");
		ret = -EINVAL;
		goto svs_probe_clk_disable;
	}

	ret = svs_start(svsp);
	if (ret) {
		dev_err(svsp->dev, "svs start fail: %d\n", ret);
		goto svs_probe_iounmap;
	}

	ret = svs_create_debug_cmds(svsp);
	if (ret) {
		dev_err(svsp->dev, "svs create debug cmds fail: %d\n", ret);
		goto svs_probe_iounmap;
	}

	return 0;

svs_probe_iounmap:
	iounmap(svsp->base);

svs_probe_clk_disable:
	clk_disable_unprepare(svsp->main_clk);

svs_probe_free_resource:
	if (!IS_ERR_OR_NULL(svsp->tefuse))
		kfree(svsp->tefuse);

svs_probe_free_efuse:
	if (!IS_ERR_OR_NULL(svsp->efuse))
		kfree(svsp->efuse);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(svs_pm_ops, svs_suspend, svs_resume);

static struct platform_driver svs_driver = {
	.probe	= svs_probe,
	.driver	= {
		.name		= "mtk-svs",
		.pm		= &svs_pm_ops,
		.of_match_table	= svs_of_match,
	},
};

module_platform_driver(svs_driver);

MODULE_AUTHOR("Roger Lu <roger.lu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek SVS driver");
MODULE_LICENSE("GPL");
