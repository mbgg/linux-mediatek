// SPDX-License-Identifier: GPL-2.0-only

#include "mtk-svs.h"

bool svs_mt8183_efuse_parsing(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	int format[6], x_roomt[6], o_vtsmcu[5], o_vtsabb, tb_roomt = 0;
	int adc_ge_t, adc_oe_t, ge, oe, gain, degc_cali, adc_cali_en_t;
	int o_slope, o_slope_sign, ts_id;
	u32 idx, i, ft_pgm, mts, temp0, temp1, temp2;
	int ret;

	for (i = 0; i < svsp->efuse_max; i++)
		if (svsp->efuse[i])
			dev_info(svsp->dev, "M_HW_RES%d: 0x%08x\n",
				 i, svsp->efuse[i]);

	if (!svsp->efuse[2]) {
		dev_notice(svsp->dev, "svs_efuse[2] = 0x0?\n");
		return false;
	}

	/* Svs efuse parsing */
	ft_pgm = (svsp->efuse[0] >> 4) & GENMASK(3, 0);

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (ft_pgm <= 1)
			svsb->volt_flags |= SVSB_INIT01_VOLT_IGNORE;

		switch (svsb->sw_id) {
		case SVSB_CPU_LITTLE:
			svsb->bdes = svsp->efuse[16] & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[16] >> 8) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[16] >> 16) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[16] >> 24) & GENMASK(7, 0);
			svsb->mtdes  = (svsp->efuse[17] >> 16) & GENMASK(7, 0);

			if (ft_pgm <= 3)
				svsb->volt_od += 10;
			else
				svsb->volt_od += 2;
			break;
		case SVSB_CPU_BIG:
			svsb->bdes = svsp->efuse[18] & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[18] >> 8) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[18] >> 16) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[18] >> 24) & GENMASK(7, 0);
			svsb->mtdes  = svsp->efuse[17] & GENMASK(7, 0);

			if (ft_pgm <= 3)
				svsb->volt_od += 15;
			else
				svsb->volt_od += 12;
			break;
		case SVSB_CCI:
			svsb->bdes = svsp->efuse[4] & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[4] >> 8) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[4] >> 16) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[4] >> 24) & GENMASK(7, 0);
			svsb->mtdes  = (svsp->efuse[5] >> 16) & GENMASK(7, 0);

			if (ft_pgm <= 3)
				svsb->volt_od += 10;
			else
				svsb->volt_od += 2;
			break;
		case SVSB_GPU:
			svsb->bdes = svsp->efuse[6] & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[6] >> 8) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[6] >> 16) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[6] >> 24) & GENMASK(7, 0);
			svsb->mtdes  = svsp->efuse[5] & GENMASK(7, 0);

			if (ft_pgm >= 2) {
				svsb->freq_base = 800000000; /* 800MHz */
				svsb->dvt_fixed = 2;
			}
			break;
		default:
			dev_err(svsb->dev, "unknown sw_id: %u\n", svsb->sw_id);
			return false;
		}
	}

	ret = svs_thermal_efuse_get_data(svsp);
	if (ret)
		return false;

	/* Thermal efuse parsing */
	adc_ge_t = (svsp->tefuse[1] >> 22) & GENMASK(9, 0);
	adc_oe_t = (svsp->tefuse[1] >> 12) & GENMASK(9, 0);

	o_vtsmcu[0] = (svsp->tefuse[0] >> 17) & GENMASK(8, 0);
	o_vtsmcu[1] = (svsp->tefuse[0] >> 8) & GENMASK(8, 0);
	o_vtsmcu[2] = svsp->tefuse[1] & GENMASK(8, 0);
	o_vtsmcu[3] = (svsp->tefuse[2] >> 23) & GENMASK(8, 0);
	o_vtsmcu[4] = (svsp->tefuse[2] >> 5) & GENMASK(8, 0);
	o_vtsabb = (svsp->tefuse[2] >> 14) & GENMASK(8, 0);

	degc_cali = (svsp->tefuse[0] >> 1) & GENMASK(5, 0);
	adc_cali_en_t = svsp->tefuse[0] & BIT(0);
	o_slope_sign = (svsp->tefuse[0] >> 7) & BIT(0);

	ts_id = (svsp->tefuse[1] >> 9) & BIT(0);
	o_slope = (svsp->tefuse[0] >> 26) & GENMASK(5, 0);

	if (adc_cali_en_t == 1) {
		if (!ts_id)
			o_slope = 0;

		if (adc_ge_t < 265 || adc_ge_t > 758 ||
		    adc_oe_t < 265 || adc_oe_t > 758 ||
		    o_vtsmcu[0] < -8 || o_vtsmcu[0] > 484 ||
		    o_vtsmcu[1] < -8 || o_vtsmcu[1] > 484 ||
		    o_vtsmcu[2] < -8 || o_vtsmcu[2] > 484 ||
		    o_vtsmcu[3] < -8 || o_vtsmcu[3] > 484 ||
		    o_vtsmcu[4] < -8 || o_vtsmcu[4] > 484 ||
		    o_vtsabb < -8 || o_vtsabb > 484 ||
		    degc_cali < 1 || degc_cali > 63) {
			dev_err(svsp->dev, "bad thermal efuse, no mon mode\n");
			goto remove_mt8183_svsb_mon_mode;
		}
	} else {
		dev_err(svsp->dev, "no thermal efuse, no mon mode\n");
		goto remove_mt8183_svsb_mon_mode;
	}

	ge = ((adc_ge_t - 512) * 10000) / 4096;
	oe = (adc_oe_t - 512);
	gain = (10000 + ge);

	format[0] = (o_vtsmcu[0] + 3350 - oe);
	format[1] = (o_vtsmcu[1] + 3350 - oe);
	format[2] = (o_vtsmcu[2] + 3350 - oe);
	format[3] = (o_vtsmcu[3] + 3350 - oe);
	format[4] = (o_vtsmcu[4] + 3350 - oe);
	format[5] = (o_vtsabb + 3350 - oe);

	for (i = 0; i < 6; i++)
		x_roomt[i] = (((format[i] * 10000) / 4096) * 10000) / gain;

	temp0 = (10000 * 100000 / gain) * 15 / 18;

	if (!o_slope_sign)
		mts = (temp0 * 10) / (1534 + o_slope * 10);
	else
		mts = (temp0 * 10) / (1534 - o_slope * 10);

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];
		svsb->mts = mts;

		switch (svsb->sw_id) {
		case SVSB_CPU_LITTLE:
			tb_roomt = x_roomt[3];
			break;
		case SVSB_CPU_BIG:
			tb_roomt = x_roomt[4];
			break;
		case SVSB_CCI:
			tb_roomt = x_roomt[3];
			break;
		case SVSB_GPU:
			tb_roomt = x_roomt[1];
			break;
		default:
			dev_err(svsb->dev, "unknown sw_id: %u\n", svsb->sw_id);
			goto remove_mt8183_svsb_mon_mode;
		}

		temp0 = (degc_cali * 10 / 2);
		temp1 = ((10000 * 100000 / 4096 / gain) *
			 oe + tb_roomt * 10) * 15 / 18;

		if (!o_slope_sign)
			temp2 = temp1 * 100 / (1534 + o_slope * 10);
		else
			temp2 = temp1 * 100 / (1534 - o_slope * 10);

		svsb->bts = (temp0 + temp2 - 250) * 4 / 10;
	}

	return true;

remove_mt8183_svsb_mon_mode:
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];
		svsb->mode_support &= ~SVSB_MODE_MON;
	}

	return true;
}

int svs_mt8183_platform_probe(struct svs_platform *svsp)
{
	struct device *dev;
	struct svs_bank *svsb;
	u32 idx;

	dev = svs_add_device_link(svsp, "thermal");
	if (IS_ERR(dev))
		return dev_err_probe(svsp->dev, PTR_ERR(dev),
				     "failed to get thermal device\n");

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		switch (svsb->sw_id) {
		case SVSB_CPU_LITTLE:
		case SVSB_CPU_BIG:
			svsb->opp_dev = get_cpu_device(svsb->cpu_id);
			break;
		case SVSB_CCI:
			svsb->opp_dev = svs_add_device_link(svsp, "cci");
			break;
		case SVSB_GPU:
			svsb->opp_dev = svs_add_device_link(svsp, "gpu");
			break;
		default:
			dev_err(svsb->dev, "unknown sw_id: %u\n", svsb->sw_id);
			return -EINVAL;
		}

		if (IS_ERR(svsb->opp_dev))
			return dev_err_probe(svsp->dev, PTR_ERR(svsb->opp_dev),
					     "failed to get OPP device for bank %d\n",
					     idx);
	}

	return 0;
}
