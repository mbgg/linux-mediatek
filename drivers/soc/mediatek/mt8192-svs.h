/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __DRV_SVS_MT8192_H
#define __DRV_SVS_MT8192_H

static struct svs_bank svs_mt8192_banks[] = {
	{
		.sw_id			= SVSB_GPU,
		.type			= SVSB_LOW,
		.set_freq_pct		= svs_set_bank_freq_pct_v3,
		.get_volts		= svs_get_bank_volts_v3,
		.volt_flags		= SVSB_REMOVE_DVTFIXED_VOLT,
		.mode_support		= SVSB_MODE_INIT02,
		.opp_count		= MAX_OPP_ENTRIES,
		.freq_base		= 688000000,
		.turn_freq_base		= 688000000,
		.volt_step		= 6250,
		.volt_base		= 400000,
		.vmax			= 0x61,
		.vmin			= 0x1a,
		.age_config		= 0x555555,
		.dc_config		= 0x1,
		.dvt_fixed		= 0x1,
		.vco			= 0x18,
		.chk_shift		= 0x87,
		.core_sel		= 0x0fff0100,
		.int_st			= BIT(0),
		.ctl0			= 0x00540003,
	},
	{
		.sw_id			= SVSB_GPU,
		.type			= SVSB_HIGH,
		.set_freq_pct		= svs_set_bank_freq_pct_v3,
		.get_volts		= svs_get_bank_volts_v3,
		.tzone_name		= "gpu1",
		.volt_flags		= SVSB_REMOVE_DVTFIXED_VOLT |
					  SVSB_MON_VOLT_IGNORE,
		.mode_support		= SVSB_MODE_INIT02 | SVSB_MODE_MON,
		.opp_count		= MAX_OPP_ENTRIES,
		.freq_base		= 902000000,
		.turn_freq_base		= 688000000,
		.volt_step		= 6250,
		.volt_base		= 400000,
		.vmax			= 0x66,
		.vmin			= 0x1a,
		.age_config		= 0x555555,
		.dc_config		= 0x1,
		.dvt_fixed		= 0x6,
		.vco			= 0x18,
		.chk_shift		= 0x87,
		.core_sel		= 0x0fff0101,
		.int_st			= BIT(1),
		.ctl0			= 0x00540003,
		.tzone_htemp		= 85000,
		.tzone_htemp_voffset	= 0,
		.tzone_ltemp		= 25000,
		.tzone_ltemp_voffset	= 7,
	},
};

bool svs_mt8192_efuse_parsing(struct svs_platform *svsp);
int svs_mt8192_platform_probe(struct svs_platform *svsp);

#endif
