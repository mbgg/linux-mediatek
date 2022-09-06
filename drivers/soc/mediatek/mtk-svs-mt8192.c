// SPDX-License-Identifier: GPL-2.0-only

#include "mtk-svs.h"

bool svs_mt8192_efuse_parsing(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	u32 idx, i, vmin, golden_temp;
	int ret;

	for (i = 0; i < svsp->efuse_max; i++)
		if (svsp->efuse[i])
			dev_info(svsp->dev, "M_HW_RES%d: 0x%08x\n",
				 i, svsp->efuse[i]);

	if (!svsp->efuse[9]) {
		dev_notice(svsp->dev, "svs_efuse[9] = 0x0?\n");
		return false;
	}

	/* Svs efuse parsing */
	vmin = (svsp->efuse[19] >> 4) & GENMASK(1, 0);

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (vmin == 0x1)
			svsb->vmin = 0x1e;

		if (svsb->type == SVSB_LOW) {
			svsb->mtdes = svsp->efuse[10] & GENMASK(7, 0);
			svsb->bdes = (svsp->efuse[10] >> 16) & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[10] >> 24) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[17]) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[17] >> 8) & GENMASK(7, 0);
		} else if (svsb->type == SVSB_HIGH) {
			svsb->mtdes = svsp->efuse[9] & GENMASK(7, 0);
			svsb->bdes = (svsp->efuse[9] >> 16) & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[9] >> 24) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[17] >> 16) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[17] >> 24) & GENMASK(7, 0);
		}
	}

	ret = svs_thermal_efuse_get_data(svsp);
	if (ret)
		return false;

	for (i = 0; i < svsp->tefuse_max; i++)
		if (svsp->tefuse[i] != 0)
			break;

	if (i == svsp->tefuse_max)
		golden_temp = 50; /* All thermal efuse data are 0 */
	else
		golden_temp = (svsp->tefuse[0] >> 24) & GENMASK(7, 0);

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];
		svsb->mts = 500;
		svsb->bts = (((500 * golden_temp + 250460) / 1000) - 25) * 4;
	}

	return true;
}

int svs_mt8192_platform_probe(struct svs_platform *svsp)
{
	struct device *dev;
	struct svs_bank *svsb;
	u32 idx;

	svsp->rst = devm_reset_control_get_optional(svsp->dev, "svs_rst");
	if (IS_ERR(svsp->rst))
		return dev_err_probe(svsp->dev, PTR_ERR(svsp->rst),
				     "cannot get svs reset control\n");

	dev = svs_add_device_link(svsp, "lvts");
	if (IS_ERR(dev))
		return dev_err_probe(svsp->dev, PTR_ERR(dev),
				     "failed to get lvts device\n");

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (svsb->type == SVSB_HIGH)
			svsb->opp_dev = svs_add_device_link(svsp, "mali");
		else if (svsb->type == SVSB_LOW)
			svsb->opp_dev = svs_get_subsys_device(svsp, "mali");

		if (IS_ERR(svsb->opp_dev))
			return dev_err_probe(svsp->dev, PTR_ERR(svsb->opp_dev),
					     "failed to get OPP device for bank %d\n",
					     idx);
	}

	return 0;
}
