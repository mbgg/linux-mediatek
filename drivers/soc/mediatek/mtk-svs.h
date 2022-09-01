/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __DRV_SVS_MTK_H
#define __DRV_SVS_MTK_H

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/reset.h>

/* svs bank 1-line software id */
#define SVSB_CPU_LITTLE			BIT(0)
#define SVSB_CPU_BIG			BIT(1)
#define SVSB_CCI			BIT(2)
#define SVSB_GPU			BIT(3)

/* svs bank 2-line type */
#define SVSB_LOW			BIT(8)
#define SVSB_HIGH			BIT(9)

/* svs bank mode support */
#define SVSB_MODE_ALL_DISABLE		0
#define SVSB_MODE_INIT01		BIT(1)
#define SVSB_MODE_INIT02		BIT(2)
#define SVSB_MODE_MON			BIT(3)

/* svs bank volt flags */
#define SVSB_INIT01_PD_REQ		BIT(0)
#define SVSB_INIT01_VOLT_IGNORE		BIT(1)
#define SVSB_INIT01_VOLT_INC_ONLY	BIT(2)
#define SVSB_MON_VOLT_IGNORE		BIT(16)
#define SVSB_REMOVE_DVTFIXED_VOLT	BIT(24)

/**
 * enum svsb_phase - svs bank phase enumeration
 * @SVSB_PHASE_ERROR: svs bank encounters unexpected condition
 * @SVSB_PHASE_INIT01: svs bank basic init for data calibration
 * @SVSB_PHASE_INIT02: svs bank can provide voltages to opp table
 * @SVSB_PHASE_MON: svs bank can provide voltages with thermal effect
 * @SVSB_PHASE_MAX: total number of svs bank phase (debug purpose)
 *
 * Each svs bank has its own independent phase and we enable each svs bank by
 * running their phase orderly. However, when svs bank encounters unexpected
 * condition, it will fire an irq (PHASE_ERROR) to inform svs software.
 *
 * svs bank general phase-enabled order:
 * SVSB_PHASE_INIT01 -> SVSB_PHASE_INIT02 -> SVSB_PHASE_MON
 */
enum svsb_phase {
	SVSB_PHASE_ERROR = 0,
	SVSB_PHASE_INIT01,
	SVSB_PHASE_INIT02,
	SVSB_PHASE_MON,
	SVSB_PHASE_MAX,
};

enum svs_reg_index {
	DESCHAR = 0,
	TEMPCHAR,
	DETCHAR,
	AGECHAR,
	DCCONFIG,
	AGECONFIG,
	FREQPCT30,
	FREQPCT74,
	LIMITVALS,
	VBOOT,
	DETWINDOW,
	CONFIG,
	TSCALCS,
	RUNCONFIG,
	SVSEN,
	INIT2VALS,
	DCVALUES,
	AGEVALUES,
	VOP30,
	VOP74,
	TEMP,
	INTSTS,
	INTSTSRAW,
	INTEN,
	CHKINT,
	CHKSHIFT,
	STATUS,
	VDESIGN30,
	VDESIGN74,
	DVT30,
	DVT74,
	AGECOUNT,
	SMSTATE0,
	SMSTATE1,
	CTL0,
	DESDETSEC,
	TEMPAGESEC,
	CTRLSPARE0,
	CTRLSPARE1,
	CTRLSPARE2,
	CTRLSPARE3,
	CORESEL,
	THERMINTST,
	INTST,
	THSTAGE0ST,
	THSTAGE1ST,
	THSTAGE2ST,
	THAHBST0,
	THAHBST1,
	SPARE0,
	SPARE1,
	SPARE2,
	SPARE3,
	THSLPEVEB,
	SVS_REG_MAX,
};

/**
 * struct svs_platform - svs platform control
 * @base: svs platform register base
 * @dev: svs platform device
 * @main_clk: main clock for svs bank
 * @pbank: svs bank pointer needing to be protected by spin_lock section
 * @banks: svs banks that svs platform supports
 * @rst: svs platform reset control
 * @efuse_max: total number of svs efuse
 * @tefuse_max: total number of thermal efuse
 * @regs: svs platform registers map
 * @bank_max: total number of svs banks
 * @efuse: svs efuse data received from NVMEM framework
 * @tefuse: thermal efuse data received from NVMEM framework
 */
struct svs_platform {
	void __iomem *base;
	struct device *dev;
	struct clk *main_clk;
	struct svs_bank *pbank;
	struct svs_bank *banks;
	struct reset_control *rst;
	size_t efuse_max;
	size_t tefuse_max;
	const u32 *regs;
	u32 bank_max;
	u32 *efuse;
	u32 *tefuse;
};

#define MAX_OPP_ENTRIES			16

/**
 * struct svs_bank - svs bank representation
 * @dev: bank device
 * @opp_dev: device for opp table/buck control
 * @init_completion: the timeout completion for bank init
 * @buck: regulator used by opp_dev
 * @tzd: thermal zone device for getting temperature
 * @lock: mutex lock to protect voltage update process
 * @set_freq_pct: function pointer to set bank frequency percent table
 * @get_volts: function pointer to get bank voltages
 * @name: bank name
 * @buck_name: regulator name
 * @tzone_name: thermal zone name
 * @phase: bank current phase
 * @volt_od: bank voltage overdrive
 * @reg_data: bank register data in different phase for debug purpose
 * @pm_runtime_enabled_count: bank pm runtime enabled count
 * @mode_support: bank mode support.
 * @freq_base: reference frequency for bank init
 * @turn_freq_base: refenrece frequency for 2-line turn point
 * @vboot: voltage request for bank init01 only
 * @opp_dfreq: default opp frequency table
 * @opp_dvolt: default opp voltage table
 * @freq_pct: frequency percent table for bank init
 * @volt: bank voltage table
 * @volt_step: bank voltage step
 * @volt_base: bank voltage base
 * @volt_flags: bank voltage flags
 * @vmax: bank voltage maximum
 * @vmin: bank voltage minimum
 * @age_config: bank age configuration
 * @age_voffset_in: bank age voltage offset
 * @dc_config: bank dc configuration
 * @dc_voffset_in: bank dc voltage offset
 * @dvt_fixed: bank dvt fixed value
 * @vco: bank VCO value
 * @chk_shift: bank chicken shift
 * @core_sel: bank selection
 * @opp_count: bank opp count
 * @int_st: bank interrupt identification
 * @sw_id: bank software identification
 * @cpu_id: cpu core id for SVS CPU bank use only
 * @ctl0: TS-x selection
 * @temp: bank temperature
 * @tzone_htemp: thermal zone high temperature threshold
 * @tzone_htemp_voffset: thermal zone high temperature voltage offset
 * @tzone_ltemp: thermal zone low temperature threshold
 * @tzone_ltemp_voffset: thermal zone low temperature voltage offset
 * @bts: svs efuse data
 * @mts: svs efuse data
 * @bdes: svs efuse data
 * @mdes: svs efuse data
 * @mtdes: svs efuse data
 * @dcbdet: svs efuse data
 * @dcmdet: svs efuse data
 * @turn_pt: 2-line turn point tells which opp_volt calculated by high/low bank
 * @type: bank type to represent it is 2-line (high/low) bank or 1-line bank
 *
 * Svs bank will generate suitalbe voltages by below general math equation
 * and provide these voltages to opp voltage table.
 *
 * opp_volt[i] = (volt[i] * volt_step) + volt_base;
 */
struct svs_bank {
	struct device *dev;
	struct device *opp_dev;
	struct completion init_completion;
	struct regulator *buck;
	struct thermal_zone_device *tzd;
	struct mutex lock;	/* lock to protect voltage update process */
	void (*set_freq_pct)(struct svs_platform *svsp);
	void (*get_volts)(struct svs_platform *svsp);
	char *name;
	char *buck_name;
	char *tzone_name;
	enum svsb_phase phase;
	s32 volt_od;
	u32 reg_data[SVSB_PHASE_MAX][SVS_REG_MAX];
	u32 pm_runtime_enabled_count;
	u32 mode_support;
	u32 freq_base;
	u32 turn_freq_base;
	u32 vboot;
	u32 opp_dfreq[MAX_OPP_ENTRIES];
	u32 opp_dvolt[MAX_OPP_ENTRIES];
	u32 freq_pct[MAX_OPP_ENTRIES];
	u32 volt[MAX_OPP_ENTRIES];
	u32 volt_step;
	u32 volt_base;
	u32 volt_flags;
	u32 vmax;
	u32 vmin;
	u32 age_config;
	u32 age_voffset_in;
	u32 dc_config;
	u32 dc_voffset_in;
	u32 dvt_fixed;
	u32 vco;
	u32 chk_shift;
	u32 core_sel;
	u32 opp_count;
	u32 int_st;
	u32 sw_id;
	u32 cpu_id;
	u32 ctl0;
	u32 temp;
	u32 tzone_htemp;
	u32 tzone_htemp_voffset;
	u32 tzone_ltemp;
	u32 tzone_ltemp_voffset;
	u32 bts;
	u32 mts;
	u32 bdes;
	u32 mdes;
	u32 mtdes;
	u32 dcbdet;
	u32 dcmdet;
	u32 turn_pt;
	u32 type;
};

struct device *svs_add_device_link(struct svs_platform *svsp,
					  const char *node_name);
struct device *svs_get_subsys_device(struct svs_platform *svsp,
					    const char *node_name);
int svs_thermal_efuse_get_data(struct svs_platform *svsp);

void svs_get_bank_volts_v2(struct svs_platform *svsp);
void svs_set_bank_freq_pct_v2(struct svs_platform *svsp);

void svs_get_bank_volts_v3(struct svs_platform *svsp);
void svs_set_bank_freq_pct_v3(struct svs_platform *svsp);
#endif
