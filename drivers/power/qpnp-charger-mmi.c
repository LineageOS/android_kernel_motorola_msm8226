/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/spmi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/radix-tree.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/power_supply.h>
#include <linux/bitops.h>
#include <linux/ratelimit.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/of_batterydata.h>
#include <linux/qpnp-revid.h>
#ifndef CONFIG_RTC_INTF_ALARM
#include <linux/alarmtimer.h>
#else
#include <linux/android_alarm.h>
#endif
#include <linux/spinlock.h>
#include <linux/batterydata-lib.h>
#include <linux/usb/msm_ext_chg.h>
#include <linux/ctype.h>
#include <linux/dropbox.h>

#define DROPBOX_CHARGING_ISSUE "charging_issue"
#define USBLPM_DROPBOX_MSG "USB PSY not in low power when USBIN not present"

/* Interrupt offsets */
#define INT_RT_STS(base)			(base + 0x10)
#define INT_SET_TYPE(base)			(base + 0x11)
#define INT_POLARITY_HIGH(base)			(base + 0x12)
#define INT_POLARITY_LOW(base)			(base + 0x13)
#define INT_LATCHED_CLR(base)			(base + 0x14)
#define INT_EN_SET(base)			(base + 0x15)
#define INT_EN_CLR(base)			(base + 0x16)
#define INT_LATCHED_STS(base)			(base + 0x18)
#define INT_PENDING_STS(base)			(base + 0x19)
#define INT_MID_SEL(base)			(base + 0x1A)
#define INT_PRIORITY(base)			(base + 0x1B)

/* Peripheral register offsets */
#define CHGR_CHG_OPTION				0x08
#define CHGR_ATC_STATUS				0x0A
#define CHGR_VBAT_STATUS			0x0B
#define CHGR_IBAT_BMS				0x0C
#define CHGR_IBAT_STS				0x0D
#define CHGR_VDD_MAX				0x40
#define CHGR_VDD_SAFE				0x41
#define CHGR_VDD_MAX_STEP			0x42
#define CHGR_IBAT_MAX				0x44
#define CHGR_IBAT_SAFE				0x45
#define CHGR_VIN_MIN				0x47
#define CHGR_VIN_MIN_STEP			0x48
#define CHGR_CHG_CTRL				0x49
#define CHGR_CHG_FAILED				0x4A
#define CHGR_ATC_CTRL				0x4B
#define CHGR_ATC_FAILED				0x4C
#define CHGR_VBAT_TRKL				0x50
#define CHGR_VBAT_WEAK				0x52
#define CHGR_IBAT_ATC_A				0x54
#define CHGR_IBAT_ATC_B				0x55
#define CHGR_IBAT_TERM_CHGR			0x5B
#define CHGR_IBAT_TERM_BMS			0x5C
#define CHGR_VBAT_DET				0x5D
#define CHGR_TTRKL_MAX_EN			0x5E
#define CHGR_TTRKL_MAX				0x5F
#define CHGR_TCHG_MAX_EN			0x60
#define CHGR_TCHG_MAX				0x61
#define CHGR_CHG_WDOG_TIME			0x62
#define CHGR_CHG_WDOG_DLY			0x63
#define CHGR_CHG_WDOG_PET			0x64
#define CHGR_CHG_WDOG_EN			0x65
#define CHGR_IR_DROP_COMPEN			0x67
#define CHGR_I_MAX_REG			0x44
#define CHGR_USB_USB_SUSP			0x47
#define CHGR_USB_USB_OTG_CTL			0x48
#define CHGR_USB_ENUM_T_STOP			0x4E
#define CHGR_USB_TRIM				0xF1
#define CHGR_CHG_TEMP_THRESH			0x66
#define CHGR_BAT_IF_PRES_STATUS			0x08
#define CHGR_STATUS				0x09
#define CHGR_BAT_IF_VCP				0x42
#define CHGR_BAT_IF_BATFET_CTRL1		0x90
#define CHGR_BAT_IF_BATFET_CTRL4		0x93
#define CHGR_BAT_IF_SPARE			0xDF
#define CHGR_MISC_BOOT_DONE			0x42
#define CHGR_BUCK_PSTG_CTRL			0x73
#define CHGR_BUCK_COMPARATOR_OVRIDE_1		0xEB
#define CHGR_BUCK_COMPARATOR_OVRIDE_3		0xED
#define CHGR_BUCK_BCK_VBAT_REG_MODE		0x74
#define MISC_REVISION2				0x01
#define USB_OVP_CTL				0x42
#define USB_CHG_GONE_REV_BST			0xED
#define BUCK_VCHG_OV				0x77
#define BUCK_TEST_SMBC_MODES			0xE6
#define BUCK_CTRL_TRIM1				0xF1
#define BUCK_CTRL_TRIM3				0xF3
#define SEC_ACCESS				0xD0
#define BAT_IF_VREF_BAT_THM_CTRL		0x4A
#define BAT_IF_BPD_CTRL				0x48
#define BOOST_VSET				0x41
#define BOOST_ENABLE_CONTROL			0x46
#define COMP_OVR1				0xEA
#define BAT_IF_BTC_CTRL				0x49
#define USB_OCP_THR				0x52
#define USB_OCP_CLR				0x53
#define BAT_IF_TEMP_STATUS			0x09
#define BOOST_ILIM				0x78
#define USB_SPARE				0xDF
#define DC_COMP_OVR1				0xE9

#define REG_OFFSET_PERP_SUBTYPE			0x05

/* SMBB peripheral subtype values */
#define SMBB_CHGR_SUBTYPE			0x01
#define SMBB_BUCK_SUBTYPE			0x02
#define SMBB_BAT_IF_SUBTYPE			0x03
#define SMBB_USB_CHGPTH_SUBTYPE			0x04
#define SMBB_DC_CHGPTH_SUBTYPE			0x05
#define SMBB_BOOST_SUBTYPE			0x06
#define SMBB_MISC_SUBTYPE			0x07

/* SMBB peripheral subtype values */
#define SMBBP_CHGR_SUBTYPE			0x31
#define SMBBP_BUCK_SUBTYPE			0x32
#define SMBBP_BAT_IF_SUBTYPE			0x33
#define SMBBP_USB_CHGPTH_SUBTYPE		0x34
#define SMBBP_BOOST_SUBTYPE			0x36
#define SMBBP_MISC_SUBTYPE			0x37

/* SMBCL peripheral subtype values */
#define SMBCL_CHGR_SUBTYPE			0x41
#define SMBCL_BUCK_SUBTYPE			0x42
#define SMBCL_BAT_IF_SUBTYPE			0x43
#define SMBCL_USB_CHGPTH_SUBTYPE		0x44
#define SMBCL_MISC_SUBTYPE			0x47

#define QPNP_CHARGER_DEV_NAME	"qcom,qpnp-charger"

/* Status bits and masks */
#define CHGR_BOOT_DONE			BIT(7)
#define CHGR_CHG_EN			BIT(7)
#define CHGR_ON_BAT_FORCE_BIT		BIT(0)
#define USB_VALID_DEB_20MS		0x03
#define BUCK_VBAT_REG_NODE_SEL_BIT	BIT(0)
#define VREF_BATT_THERM_FORCE_ON	0xC0
#define BAT_IF_BPD_CTRL_SEL		0x03
#define VREF_BAT_THM_ENABLED_FSM	0x80
#define REV_BST_DETECTED		BIT(0)
#define BAT_THM_EN			BIT(1)
#define BAT_ID_EN			BIT(0)
#define BOOST_PWR_EN			BIT(7)
#define OCP_CLR_BIT			BIT(7)
#define OCP_THR_MASK			0x03
#define OCP_THR_900_MA			0x02
#define OCP_THR_500_MA			0x01
#define OCP_THR_200_MA			0x00

/* Interrupt definitions */
/* smbb_chg_interrupts */
#define CHG_DONE_IRQ			BIT(7)
#define CHG_FAILED_IRQ			BIT(6)
#define FAST_CHG_ON_IRQ			BIT(5)
#define TRKL_CHG_ON_IRQ			BIT(4)
#define STATE_CHANGE_ON_IR		BIT(3)
#define CHGWDDOG_IRQ			BIT(2)
#define VBAT_DET_HI_IRQ			BIT(1)
#define VBAT_DET_LOW_IRQ		BIT(0)

/* smbb_buck_interrupts */
#define VDD_LOOP_IRQ			BIT(6)
#define IBAT_LOOP_IRQ			BIT(5)
#define ICHG_LOOP_IRQ			BIT(4)
#define VCHG_LOOP_IRQ			BIT(3)
#define OVERTEMP_IRQ			BIT(2)
#define VREF_OV_IRQ			BIT(1)
#define VBAT_OV_IRQ			BIT(0)

/* smbb_bat_if_interrupts */
#define PSI_IRQ				BIT(4)
#define VCP_ON_IRQ			BIT(3)
#define BAT_FET_ON_IRQ			BIT(2)
#define BAT_TEMP_OK_IRQ			BIT(1)
#define BATT_PRES_IRQ			BIT(0)

/* smbb_usb_interrupts */
#define CHG_GONE_IRQ			BIT(2)
#define USBIN_VALID_IRQ			BIT(1)
#define COARSE_DET_USB_IRQ		BIT(0)

/* smbb_dc_interrupts */
#define DCIN_VALID_IRQ			BIT(1)
#define COARSE_DET_DC_IRQ		BIT(0)

/* smbb_boost_interrupts */
#define LIMIT_ERROR_IRQ			BIT(1)
#define BOOST_PWR_OK_IRQ		BIT(0)

/* smbb_misc_interrupts */
#define TFTWDOG_IRQ			BIT(0)

/* SMBB types */
#define SMBB				BIT(1)
#define SMBBP				BIT(2)
#define SMBCL				BIT(3)

/* Workaround flags */
#define CHG_FLAGS_VCP_WA		BIT(0)
#define BOOST_FLASH_WA			BIT(1)
#define POWER_STAGE_WA			BIT(2)

/* Die Temperature Alarm Registers */
#define TEMP_ALARM_SHUTDOWN_CTL1 0x2440
#define ALM_SD_TEMP_THRESH_CNTRL 0x03

struct qpnp_chg_irq {
	int		irq;
	unsigned long		disabled;
};

struct qpnp_chg_regulator {
	struct regulator_desc			rdesc;
	struct regulator_dev			*rdev;
};

/**
 * struct qpnp_chg_chip - device information
 * @dev:			device pointer to access the parent
 * @spmi:			spmi pointer to access spmi information
 * @chgr_base:			charger peripheral base address
 * @buck_base:			buck  peripheral base address
 * @bat_if_base:		battery interface  peripheral base address
 * @usb_chgpth_base:		USB charge path peripheral base address
 * @dc_chgpth_base:		DC charge path peripheral base address
 * @boost_base:			boost peripheral base address
 * @misc_base:			misc peripheral base address
 * @freq_base:			freq peripheral base address
 * @bat_is_cool:		indicates that battery is cool
 * @bat_is_warm:		indicates that battery is warm
 * @bat_therm_is_warm:          indicates that battery is warm due to batt_therm
 * @bat_hotspot_is_warm:        indicates that batt is warm due to hot spot
 * @ext_hi_temp:		indicates Extended Temp Range High Threshold
 * @out_of_temp:		indicates battery is Out of Temperature Range
 * @chg_done:			indicates that charging is completed
 * @usb_present:		present status of usb
 * @dc_present:			present status of dc
 * @batt_present:		present status of battery
 * @use_default_batt_values:	flag to report default battery properties
 * @btc_disabled		Flag to disable btc (disables hot and cold irqs)
 * @max_voltage_mv:		the max volts the batt should be charged up to
 * @min_voltage_mv:		min battery voltage before turning the FET on
 * @batt_weak_voltage_mv:	Weak battery voltage threshold
 * @vbatdet_max_err_mv		resume voltage hysterisis
 * @max_bat_chg_current:	maximum battery charge current in mA
 * @warm_bat_chg_ma:	warm battery maximum charge current in mA
 * @cool_bat_chg_ma:	cool battery maximum charge current in mA
 * @warm_bat_mv:		warm temperature battery target voltage
 * @warm_bat_soc:		warm temperature battery target soc
 * @resume_delta_soc:           soc delta at which battery resumes charging
 * @cool_bat_mv:		cool temperature battery target voltage
 * @resume_delta_mv:		voltage delta at which battery resumes charging
 * @term_current:		the charging based term current
 * @safe_current:		battery safety current setting
 * @maxinput_usb_ma:		Maximum Input current USB
 * @maxinput_dc_ma:		Maximum Input current DC
 * @hot_batt_p			Hot battery threshold setting
 * @cold_batt_p			Cold battery threshold setting
 * @warm_bat_decidegc		Warm battery temperature in degree Celsius
 * @cool_bat_decidegc		Cool battery temperature in degree Celsius
 * @revision:			PMIC revision
 * @type:			SMBB type
 * @tchg_mins			maximum allowed software initiated charge time
 * @thermal_levels		amount of thermal mitigation levels
 * @thermal_mitigation		thermal mitigation level values
 * @therm_lvl_sel		thermal mitigation level selection
 * @dc_psy			power supply to export information to userspace
 * @usb_psy			power supply to export information to userspace
 * @bms_psy			power supply to export information to userspace
 * @batt_psy:			power supply to export information to userspace
 * @ float_charge_timer         Predetermined length of time to do Float charge
 * @ float_charge_start_time    Timestamp when it reaches Taper point
 * @flags:			flags to activate specific workarounds
 *				throughout the driver
 * @use_max_vdd:		Flag to indicate Max Vdd can be set
 * @step_charge_mv:		Voltage threshold for High Voltage batteries to
 *                              charge at a reduced rate
 * @step_charge_soc:		SOC threshold for High Voltage batteries to
 *                              charge at a reduced rate
 * @step_charge_ma:             Reduced charge rate for High Voltage batteries
 *                              which are above a certain Voltage threshold
 * @cutoff_mv:                  cutoff voltage where the battery is considered
 *                              dead
 * @ float_timer_start          Indicates float timer has started.
 * @ weak_chg_attached          Indicates that a Weak Charger is attached.
 * @ hv_chg_attached            Indicates that a HV Charger is attached.
 * @ usb_limit_ma               Current Limit Sent from USB.
 * @ weak_check_work            Work for Detecting Weak Charger.
 * @ battery_model_sysfs        Indicates if battery model sysfs is required to
 *                              be updated
 * @ battery_info               buff to hold battery make and model info
 *
 */
struct qpnp_chg_chip {
	struct device			*dev;
	struct spmi_device		*spmi;
	u16				chgr_base;
	u16				buck_base;
	u16				bat_if_base;
	u16				usb_chgpth_base;
	u16				dc_chgpth_base;
	u16				boost_base;
	u16				misc_base;
	u16				freq_base;
	struct qpnp_chg_irq		usbin_valid;
	struct qpnp_chg_irq		usb_ocp;
	struct qpnp_chg_irq		dcin_valid;
	struct qpnp_chg_irq		chg_gone;
	struct qpnp_chg_irq		chg_fastchg;
	struct qpnp_chg_irq		chg_trklchg;
	struct qpnp_chg_irq		chg_failed;
	struct qpnp_chg_irq		chg_vbatdet_lo;
	struct qpnp_chg_irq		batt_pres;
	struct qpnp_chg_irq		batt_temp_ok;
	struct qpnp_chg_irq		coarse_det_usb;
	bool				bat_is_cool;
	bool				bat_is_warm;
	bool                            bat_therm_is_warm;
	bool                            bat_hotspot_is_warm;
	int                             bat_hotspot_temp;
	int                             bat_hotspot_thrs;
	bool				ext_hi_temp;
	bool				out_of_temp;
	struct mutex                    update_ext_hi_state_lock;
	bool				chg_done;
	bool				charger_monitor_checked;
	bool				usb_present;
	u8				usbin_health;
	bool				usb_coarse_det;
	bool				dc_present;
	bool				batt_present;
	bool				charging_disabled;
	bool				ovp_monitor_enable;
	bool				usb_valid_check_ovp;
	bool				btc_disabled;
	bool				use_default_batt_values;
	bool				duty_cycle_100p;
	bool				ibat_calibration_enabled;
	bool				aicl_settled;
	bool				use_external_rsense;
	bool				fastchg_on;
	bool				parallel_ovp_mode;
	unsigned int			bpd_detection;
	unsigned int			max_bat_chg_current;
	unsigned int			warm_bat_chg_ma;
	unsigned int			cool_bat_chg_ma;
	unsigned int			safe_voltage_mv;
	unsigned int			max_voltage_mv;
	unsigned int			min_voltage_mv;
	unsigned int			batt_weak_voltage_mv;
	unsigned int			vbatdet_max_err_mv;
	int				prev_usb_max_ma;
	int				set_vddmax_mv;
	int				delta_vddmax_mv;
	u8				trim_center;
	unsigned int			warm_bat_mv;
	unsigned int			warm_bat_soc;
	unsigned int			resume_delta_soc;
	unsigned int			cool_bat_mv;
	unsigned int			resume_delta_mv;
	int				insertion_ocv_uv;
	int				term_current;
	int				soc_resume_limit;
	bool				resuming_charging;
	int				charger_enable_set;
	unsigned int			maxinput_usb_ma;
	unsigned int			maxinput_dc_ma;
	unsigned int			hot_batt_p;
	unsigned int			cold_batt_p;
	int				warm_bat_decidegc;
	int				cool_bat_decidegc;
	int				batt_therm_offset;
	int				fake_battery_soc;
	unsigned int			safe_current;
	unsigned int			revision;
	unsigned int			type;
	unsigned int			tchg_mins;
	unsigned int			thermal_levels;
	unsigned int			therm_lvl_sel;
	unsigned int			*thermal_mitigation;
	struct power_supply		dc_psy;
	struct power_supply		*usb_psy;
	struct power_supply		*bms_psy;
	struct power_supply		batt_psy;
	uint32_t			flags;
	struct qpnp_adc_tm_btm_param	adc_param;
	struct work_struct		adc_measure_work;
	struct work_struct		adc_disable_work;
	struct delayed_work		arb_stop_work;
	struct delayed_work		eoc_work;
	struct delayed_work		usbin_health_check;
	struct work_struct		soc_check_work;
	struct delayed_work		aicl_check_work;
	struct work_struct		insertion_ocv_work;
	struct work_struct		ocp_clear_work;
	struct delayed_work		update_heartbeat_work;
	struct qpnp_chg_regulator	otg_vreg;
	struct qpnp_chg_regulator	boost_vreg;
	struct qpnp_chg_regulator	batfet_vreg;
	bool				batfet_ext_en;
	struct work_struct		batfet_lcl_work;
	struct qpnp_vadc_chip		*vadc_dev;
	struct qpnp_iadc_chip		*iadc_dev;
	struct qpnp_adc_tm_chip		*adc_tm_dev;
	struct mutex			jeita_configure_lock;
	spinlock_t			usbin_health_monitor_lock;
	struct mutex			batfet_vreg_lock;
	struct alarm			reduce_power_stage_alarm;
	struct work_struct		reduce_power_stage_work;
	bool				power_stage_workaround_running;
	bool				power_stage_workaround_enable;
	char				shutdown_needed;
	bool                            use_step_charge;
	bool                            use_max_vdd;
	unsigned int			step_charge_mv;
	unsigned int			step_charge_soc;
	unsigned int			step_charge_ma;
	unsigned int			cutoff_mv;
	struct delayed_work		chrg_ocv_work;
	unsigned int			fcc_mah;
	struct pc_temp_ocv_lut		*pc_temp_ocv_lut;
	int				chrg_ocv_state;
	int				chrg_ocv_ocv_time;
	int				chrg_ocv_low_soc_thr;
	int				chrg_ocv_high_soc_thr;
	int				chrg_ocv_dchrg_thr_ma;
	int				chrg_ocv_cc_thr_uah;
	int				chrg_ocv_time;
	int				chrg_ocv_cc_bf_uah;
	int				chrg_ocv_cc_af_uah;
	int				chrg_ocv_cc_ef_uah;
	int				chrg_ocv_bv_mv;
	bool				maint_chrg;
	unsigned int			ir_drop_comp;
	unsigned int			float_charge_timer;
	unsigned long			float_charge_start_time;
	bool				float_timer_start;
	bool				weak_chg_attached;
	bool				hv_chg_attached;
	int				usb_limit_ma;
	struct delayed_work		weak_check_work;
	bool				battery_model_sysfs;
	char				battery_info[255];
	bool				demo_mode;
	bool                            usblpm_dropbox_sent;
	bool				demo_suspend_usb;
};

static struct qpnp_chg_chip *the_chip;

static struct of_device_id qpnp_charger_match_table[] = {
	{ .compatible = QPNP_CHARGER_DEV_NAME, },
	{}
};

enum bpd_type {
	BPD_TYPE_BAT_ID,
	BPD_TYPE_BAT_THM,
	BPD_TYPE_BAT_THM_BAT_ID,
};

enum chrg_ocv_states {
	CHRG_OCV_NO_CHRG,
	CHRG_OCV_OCV_WAIT,
	CHRG_OCV_FULL_WAIT,
};

static const char * const bpd_label[] = {
	[BPD_TYPE_BAT_ID] = "bpd_id",
	[BPD_TYPE_BAT_THM] = "bpd_thm",
	[BPD_TYPE_BAT_THM_BAT_ID] = "bpd_thm_id",
};

enum btc_type {
	HOT_THD_25_PCT = 25,
	HOT_THD_35_PCT = 35,
	COLD_THD_70_PCT = 70,
	COLD_THD_80_PCT = 80,
};

static u8 btc_value[] = {
	[HOT_THD_25_PCT] = 0x0,
	[HOT_THD_35_PCT] = BIT(0),
	[COLD_THD_70_PCT] = 0x0,
	[COLD_THD_80_PCT] = BIT(1),
};

enum usbin_health {
	USBIN_UNKNOW,
	USBIN_OK,
	USBIN_OVP,
};

static int bms_chrg_capacity;
module_param(bms_chrg_capacity, int, 0644);
static int bms_chrg_capacity_temp;
module_param(bms_chrg_capacity_temp, int, 0644);
static int chrg_timestamp;
module_param(chrg_timestamp, int, 0644);
MODULE_PARM_DESC(chrg_timestamp,
		 "Epoch format timestamp chg_capacity time");

static void
qpnp_chg_set_appropriate_vddmax(struct qpnp_chg_chip *chip);

static int
qpnp_chg_vbatdet_set(struct qpnp_chg_chip *chip, int vbatdet_mv);

static void
qpnp_chg_weak_check(struct qpnp_chg_chip *chip);

static const char *qpnp_charger_mmi_battid(void);

static inline int
get_bpd(const char *name)
{
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(bpd_label); i++) {
		if (strcmp(bpd_label[i], name) == 0)
			return i;
	}
	return -EINVAL;
}

static bool
is_within_range(int value, int left, int right)
{
	if (left >= right && left >= value && value >= right)
		return 1;
	if (left <= right && left <= value && value <= right)
		return 1;
	return 0;
}

static int
qpnp_chg_read(struct qpnp_chg_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return -EINVAL;
	}

	rc = spmi_ext_register_readl(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("SPMI read failed base=0x%02x sid=0x%02x rc=%d\n", base,
				spmi->sid, rc);
		return rc;
	}
	return 0;
}

static int
qpnp_chg_write(struct qpnp_chg_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return -EINVAL;
	}

	rc = spmi_ext_register_writel(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("write failed base=0x%02x sid=0x%02x rc=%d\n",
			base, spmi->sid, rc);
		return rc;
	}

	return 0;
}

static int
qpnp_chg_masked_write(struct qpnp_chg_chip *chip, u16 base,
						u8 mask, u8 val, int count)
{
	int rc;
	u8 reg;

	rc = qpnp_chg_read(chip, &reg, base, count);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n", base, rc);
		return rc;
	}
	pr_debug("addr = 0x%x read 0x%x\n", base, reg);

	reg &= ~mask;
	reg |= val & mask;

	pr_debug("Writing 0x%x\n", reg);

	rc = qpnp_chg_write(chip, &reg, base, count);
	if (rc) {
		pr_err("spmi write failed: addr=%03X, rc=%d\n", base, rc);
		return rc;
	}

	return 0;
}

static void
qpnp_chg_enable_irq(struct qpnp_chg_irq *irq)
{
	if (__test_and_clear_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		enable_irq(irq->irq);
	}
}

static void
qpnp_chg_disable_irq(struct qpnp_chg_irq *irq)
{
	if (!__test_and_set_bit(0, &irq->disabled)) {
		pr_debug("number = %d\n", irq->irq);
		disable_irq_nosync(irq->irq);
	}
}

#define USB_OTG_EN_BIT	BIT(0)
static int
qpnp_chg_is_otg_en_set(struct qpnp_chg_chip *chip)
{
	u8 usb_otg_en;
	int rc;

	rc = qpnp_chg_read(chip, &usb_otg_en,
				 chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
				 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}
	pr_debug("usb otg en 0x%x\n", usb_otg_en);

	return (usb_otg_en & USB_OTG_EN_BIT) ? 1 : 0;
}

static int
qpnp_chg_is_boost_en_set(struct qpnp_chg_chip *chip)
{
	u8 boost_en_ctl;
	int rc;

	rc = qpnp_chg_read(chip, &boost_en_ctl,
		chip->boost_base + BOOST_ENABLE_CONTROL, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->boost_base + BOOST_ENABLE_CONTROL, rc);
		return rc;
	}

	pr_debug("boost en 0x%x\n", boost_en_ctl);

	return (boost_en_ctl & BOOST_PWR_EN) ? 1 : 0;
}

static int
qpnp_chg_is_batt_temp_ok(struct qpnp_chg_chip *chip)
{
	u8 batt_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &batt_rt_sts,
				 INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->bat_if_base), rc);
		return rc;
	}

	return (batt_rt_sts & BAT_TEMP_OK_IRQ) ? 1 : 0;
}

static int
qpnp_chg_is_batt_present(struct qpnp_chg_chip *chip)
{
	u8 batt_pres_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &batt_pres_rt_sts,
				 INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->bat_if_base), rc);
		return rc;
	}

	return (batt_pres_rt_sts & BATT_PRES_IRQ) ? 1 : 0;
}

static int
qpnp_chg_is_batfet_closed(struct qpnp_chg_chip *chip)
{
	u8 batfet_closed_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &batfet_closed_rt_sts,
				 INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->bat_if_base), rc);
		return rc;
	}

	return (batfet_closed_rt_sts & BAT_FET_ON_IRQ) ? 1 : 0;
}

#define USB_VALID_BIT	BIT(7)
static int
qpnp_chg_is_usb_chg_plugged_in(struct qpnp_chg_chip *chip)
{
	u8 usbin_valid_rt_sts;
	int rc;

	rc = qpnp_chg_read(chip, &usbin_valid_rt_sts,
				 chip->usb_chgpth_base + CHGR_STATUS , 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}
	pr_debug("chgr usb sts 0x%x\n", usbin_valid_rt_sts);

	return (usbin_valid_rt_sts & USB_VALID_BIT) ? 1 : 0;
}

static bool
qpnp_chg_is_ibat_loop_active(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 buck_sts;

	rc = qpnp_chg_read(chip, &buck_sts,
			INT_RT_STS(chip->buck_base), 1);
	if (rc) {
		pr_err("failed to read buck RT status rc=%d\n", rc);
		return 0;
	}

	return !!(buck_sts & IBAT_LOOP_IRQ);
}

#define USB_VALID_MASK 0xC0
#define USB_COARSE_DET 0x10
#define USB_VALID_UVP_VALUE    0x00
#define USB_VALID_OVP_VALUE    0x40
static int
qpnp_chg_check_usb_coarse_det(struct qpnp_chg_chip *chip)
{
	u8 usbin_chg_rt_sts;
	int rc;
	rc = qpnp_chg_read(chip, &usbin_chg_rt_sts,
		chip->usb_chgpth_base + CHGR_STATUS , 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
			chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}
	return (usbin_chg_rt_sts & USB_COARSE_DET) ? 1 : 0;
}

static int
qpnp_chg_check_usbin_health(struct qpnp_chg_chip *chip)
{
	u8 usbin_chg_rt_sts, usbin_health = 0;
	int rc;

	rc = qpnp_chg_read(chip, &usbin_chg_rt_sts,
		chip->usb_chgpth_base + CHGR_STATUS , 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
		chip->usb_chgpth_base + CHGR_STATUS, rc);
		return rc;
	}

	pr_debug("chgr usb sts 0x%x\n", usbin_chg_rt_sts);
	if ((usbin_chg_rt_sts & USB_COARSE_DET) == USB_COARSE_DET) {
		if ((usbin_chg_rt_sts & USB_VALID_MASK)
			 == USB_VALID_OVP_VALUE) {
			usbin_health = USBIN_OVP;
			pr_err("Over voltage charger inserted\n");
		} else if ((usbin_chg_rt_sts & USB_VALID_BIT) != 0) {
			usbin_health = USBIN_OK;
			pr_debug("Valid charger inserted\n");
		}
	} else {
		usbin_health = USBIN_UNKNOW;
		pr_debug("Charger plug out\n");
	}

	return usbin_health;
}

static int
qpnp_chg_is_dc_chg_plugged_in(struct qpnp_chg_chip *chip)
{
	u8 dcin_valid_rt_sts;
	int rc;

	if (!chip->dc_chgpth_base)
		return 0;

	rc = qpnp_chg_read(chip, &dcin_valid_rt_sts,
				 INT_RT_STS(chip->dc_chgpth_base), 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->dc_chgpth_base), rc);
		return rc;
	}

	return (dcin_valid_rt_sts & DCIN_VALID_IRQ) ? 1 : 0;
}

static int
qpnp_chg_is_ichg_loop_active(struct qpnp_chg_chip *chip)
{
	u8 buck_sts;
	int rc;

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->buck_base), rc);
		return rc;
	}
	pr_debug("buck usb sts 0x%x\n", buck_sts);

	return (buck_sts & ICHG_LOOP_IRQ) ? 1 : 0;
}

#define QPNP_CHG_I_MAX_MIN_100		100
#define QPNP_CHG_I_MAX_MIN_150		150
#define QPNP_CHG_I_MAX_MIN_MA		200
#define QPNP_CHG_I_MAX_MAX_MA		2500
#define QPNP_CHG_I_MAXSTEP_MA		100
static int
qpnp_chg_idcmax_set(struct qpnp_chg_chip *chip, int mA)
{
	int rc = 0;
	u8 dc = 0;

	if (mA < QPNP_CHG_I_MAX_MIN_100
			|| mA > QPNP_CHG_I_MAX_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	if (mA == QPNP_CHG_I_MAX_MIN_100) {
		dc = 0x00;
		pr_debug("current=%d setting %02x\n", mA, dc);
		return qpnp_chg_write(chip, &dc,
			chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);
	} else if (mA == QPNP_CHG_I_MAX_MIN_150) {
		dc = 0x01;
		pr_debug("current=%d setting %02x\n", mA, dc);
		return qpnp_chg_write(chip, &dc,
			chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);
	}

	dc = mA / QPNP_CHG_I_MAXSTEP_MA;

	pr_debug("current=%d setting 0x%x\n", mA, dc);
	rc = qpnp_chg_write(chip, &dc,
		chip->dc_chgpth_base + CHGR_I_MAX_REG, 1);

	return rc;
}

static int
qpnp_chg_iusb_trim_get(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	u8 trim_reg;

	rc = qpnp_chg_read(chip, &trim_reg,
			chip->usb_chgpth_base + CHGR_USB_TRIM, 1);
	if (rc) {
		pr_err("failed to read USB_TRIM rc=%d\n", rc);
		return 0;
	}

	return trim_reg;
}

static int
qpnp_chg_iusb_trim_set(struct qpnp_chg_chip *chip, int trim)
{
	int rc = 0;

	rc = qpnp_chg_masked_write(chip,
		chip->usb_chgpth_base + SEC_ACCESS,
		0xFF,
		0xA5, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->usb_chgpth_base + CHGR_USB_TRIM,
		0xFF,
		trim, 1);
	if (rc) {
		pr_err("failed to write USB TRIM rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int
qpnp_chg_iusbmax_set(struct qpnp_chg_chip *chip, int mA)
{
	int rc = 0;
	u8 usb_reg = 0, temp = 8;

	if (mA < 0 || mA > QPNP_CHG_I_MAX_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", mA);
		return -EINVAL;
	}

	if (mA <= QPNP_CHG_I_MAX_MIN_100) {
		usb_reg = 0x00;
		pr_debug("current=%d setting %02x\n", mA, usb_reg);
		return qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	} else if (mA == QPNP_CHG_I_MAX_MIN_150) {
		usb_reg = 0x01;
		pr_debug("current=%d setting %02x\n", mA, usb_reg);
		return qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	}

	/* Impose input current limit */
	if (chip->maxinput_usb_ma)
		mA = (chip->maxinput_usb_ma) <= mA ? chip->maxinput_usb_ma : mA;

	usb_reg = mA / QPNP_CHG_I_MAXSTEP_MA;

	if (chip->flags & CHG_FLAGS_VCP_WA) {
		temp = 0xA5;
		rc =  qpnp_chg_write(chip, &temp,
			chip->buck_base + SEC_ACCESS, 1);
		rc =  qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_3,
			0x0C, 0x0C, 1);
	}

	pr_debug("current=%d setting 0x%x\n", mA, usb_reg);
	rc = qpnp_chg_write(chip, &usb_reg,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);

	if (chip->flags & CHG_FLAGS_VCP_WA) {
		temp = 0xA5;
		udelay(200);
		rc =  qpnp_chg_write(chip, &temp,
			chip->buck_base + SEC_ACCESS, 1);
		rc =  qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_3,
			0x0C, 0x00, 1);
	}

	return rc;
}

#define QPNP_CHG_VINMIN_MIN_MV		4200
#define QPNP_CHG_VINMIN_HIGH_MIN_MV	5600
#define QPNP_CHG_VINMIN_HIGH_MIN_VAL	0x2B
#define QPNP_CHG_VINMIN_MAX_MV		9600
#define QPNP_CHG_VINMIN_STEP_MV		50
#define QPNP_CHG_VINMIN_STEP_HIGH_MV	200
#define QPNP_CHG_VINMIN_MASK		0x3F
#define QPNP_CHG_VINMIN_MIN_VAL	0x10
static int
qpnp_chg_vinmin_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp;

	if (voltage < QPNP_CHG_VINMIN_MIN_MV
			|| voltage > QPNP_CHG_VINMIN_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	if (voltage >= QPNP_CHG_VINMIN_HIGH_MIN_MV) {
		temp = QPNP_CHG_VINMIN_HIGH_MIN_VAL;
		temp += (voltage - QPNP_CHG_VINMIN_HIGH_MIN_MV)
			/ QPNP_CHG_VINMIN_STEP_HIGH_MV;
	} else {
		temp = QPNP_CHG_VINMIN_MIN_VAL;
		temp += (voltage - QPNP_CHG_VINMIN_MIN_MV)
			/ QPNP_CHG_VINMIN_STEP_MV;
	}

	pr_debug("voltage=%d setting %02x\n", voltage, temp);
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_VIN_MIN,
			QPNP_CHG_VINMIN_MASK, temp, 1);
}

static int
qpnp_chg_vinmin_get(struct qpnp_chg_chip *chip)
{
	int rc, vin_min_mv;
	u8 vin_min;

	rc = qpnp_chg_read(chip, &vin_min, chip->chgr_base + CHGR_VIN_MIN, 1);
	if (rc) {
		pr_err("failed to read VIN_MIN rc=%d\n", rc);
		return 0;
	}

	if (vin_min == 0)
		vin_min_mv = QPNP_CHG_I_MAX_MIN_100;
	else if (vin_min >= QPNP_CHG_VINMIN_HIGH_MIN_VAL)
		vin_min_mv = QPNP_CHG_VINMIN_HIGH_MIN_MV +
			(vin_min - QPNP_CHG_VINMIN_HIGH_MIN_VAL)
				* QPNP_CHG_VINMIN_STEP_HIGH_MV;
	else
		vin_min_mv = QPNP_CHG_VINMIN_MIN_MV +
			(vin_min - QPNP_CHG_VINMIN_MIN_VAL)
				* QPNP_CHG_VINMIN_STEP_MV;
	pr_debug("vin_min= 0x%02x, ma = %d\n", vin_min, vin_min_mv);

	return vin_min_mv;
}

#define QPNP_CHG_VBATWEAK_MIN_MV	2100
#define QPNP_CHG_VBATWEAK_MAX_MV	3600
#define QPNP_CHG_VBATWEAK_STEP_MV	100
static int
qpnp_chg_vbatweak_set(struct qpnp_chg_chip *chip, int vbatweak_mv)
{
	u8 temp;

	if (vbatweak_mv < QPNP_CHG_VBATWEAK_MIN_MV
			|| vbatweak_mv > QPNP_CHG_VBATWEAK_MAX_MV)
		return -EINVAL;

	temp = (vbatweak_mv - QPNP_CHG_VBATWEAK_MIN_MV)
			/ QPNP_CHG_VBATWEAK_STEP_MV;

	pr_debug("voltage=%d setting %02x\n", vbatweak_mv, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VBAT_WEAK, 1);
}

static int
qpnp_chg_usb_iusbmax_get(struct qpnp_chg_chip *chip)
{
	int rc, iusbmax_ma;
	u8 iusbmax;

	rc = qpnp_chg_read(chip, &iusbmax,
		chip->usb_chgpth_base + CHGR_I_MAX_REG, 1);
	if (rc) {
		pr_err("failed to read IUSB_MAX rc=%d\n", rc);
		return 0;
	}

	if (iusbmax == 0)
		iusbmax_ma = QPNP_CHG_I_MAX_MIN_100;
	else if (iusbmax == 0x01)
		iusbmax_ma = QPNP_CHG_I_MAX_MIN_150;
	else
		iusbmax_ma = iusbmax * QPNP_CHG_I_MAXSTEP_MA;

	pr_debug("iusbmax = 0x%02x, ma = %d\n", iusbmax, iusbmax_ma);

	return iusbmax_ma;
}

#define ILIMIT_OVR_0	0x02
static int
override_dcin_ilimit(struct qpnp_chg_chip *chip, bool override)
{
	int rc;

	pr_debug("override %d\n", override);
	rc = qpnp_chg_masked_write(chip,
			chip->dc_chgpth_base + SEC_ACCESS,
			0xA5,
			0xA5, 1);
	rc |= qpnp_chg_masked_write(chip,
			chip->dc_chgpth_base + DC_COMP_OVR1,
			0xFF,
			override ? ILIMIT_OVR_0 : 0, 1);
	if (rc) {
		pr_err("Failed to override dc ilimit rc = %d\n", rc);
		return rc;
	}

	return rc;
}

#define DUAL_PATH_EN	BIT(7)
static int
switch_parallel_ovp_mode(struct qpnp_chg_chip *chip, bool enable)
{
	int rc = 0;

	if (!chip->usb_chgpth_base || !chip->dc_chgpth_base)
		return rc;

	pr_debug("enable %d\n", enable);
	rc = override_dcin_ilimit(chip, 1);
	udelay(10);

	/* enable/disable dual path mode */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xA5,
			0xA5, 1);
	rc |= qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_SPARE,
			0xFF,
			enable ? DUAL_PATH_EN : 0, 1);
	if (rc) {
		pr_err("Failed to turn on usb ovp rc = %d\n", rc);
		return rc;
	}

	if (enable)
		rc = override_dcin_ilimit(chip, 0);
	return rc;
}

#define USB_SUSPEND_BIT	BIT(0)
static int
qpnp_chg_usb_suspend_enable(struct qpnp_chg_chip *chip, int enable)
{
	if (chip->demo_mode)
		return 0;

	/* Turn off DC OVP FET when going into USB suspend */
	if (chip->parallel_ovp_mode && enable)
		switch_parallel_ovp_mode(chip, 0);

	return qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_SUSP,
			USB_SUSPEND_BIT,
			enable ? USB_SUSPEND_BIT : 0, 1);
}

static int
qpnp_chg_demo_usb_suspend_enable(struct qpnp_chg_chip *chip, int enable)
{
	if ((!chip->demo_mode) ||
	    (chip->demo_suspend_usb && enable) ||
	    (!chip->demo_suspend_usb && !enable))
		return 0;

	/* Turn off DC OVP FET when going into USB suspend */
	if (chip->parallel_ovp_mode && enable)
		switch_parallel_ovp_mode(chip, 0);
	else if (chip->parallel_ovp_mode && !enable)
		switch_parallel_ovp_mode(chip, 1);

	chip->demo_suspend_usb = enable;

	return qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_SUSP,
			USB_SUSPEND_BIT,
			enable ? USB_SUSPEND_BIT : 0, 1);
}

static int
qpnp_chg_charge_en(struct qpnp_chg_chip *chip, int enable)
{
	if (chip->insertion_ocv_uv == 0 && enable) {
		pr_debug("Battery not present, skipping\n");
		return 0;
	}
	pr_debug("charging %s\n", enable ? "enabled" : "disabled");

	if (!chip->charger_enable_set && enable)
		qpnp_chg_vbatdet_set(chip, chip->max_voltage_mv
			+ chip->resume_delta_mv);
	chip->charger_enable_set = enable;

	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_CHG_CTRL,
			CHGR_CHG_EN,
			enable ? CHGR_CHG_EN : 0, 1);
}

static int
qpnp_chg_force_run_on_batt(struct qpnp_chg_chip *chip, int disable)
{
	/* Don't run on battery for batteryless hardware */
	if (chip->use_default_batt_values) {
		pr_err("force on: Using default batt values\n");
		return 0;
	}

	/* Don't force on battery if battery is not present */
	if (!qpnp_chg_is_batt_present(chip)) {
		pr_err("force on: No battery present\n");
		return 0;
	}

	/* This bit forces the charger to run off of the battery rather
	 * than a connected charger */
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_CHG_CTRL,
			CHGR_ON_BAT_FORCE_BIT,
			disable ? CHGR_ON_BAT_FORCE_BIT : 0, 1);
}

#define BUCK_DUTY_MASK_100P	0x30
static int
qpnp_buck_set_100_duty_cycle_enable(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	pr_debug("enable: %d\n", enable);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS, 0xA5, 0xA5, 1);
	if (rc) {
		pr_debug("failed to write sec access rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + BUCK_TEST_SMBC_MODES,
			BUCK_DUTY_MASK_100P, enable ? 0x00 : 0x10, 1);
	if (rc) {
		pr_debug("failed enable 100p duty cycle rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#define COMPATATOR_OVERRIDE_0	0x80
static int
qpnp_chg_toggle_chg_done_logic(struct qpnp_chg_chip *chip, int enable)
{
	int rc;

	pr_debug("toggle: %d\n", enable);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS, 0xA5, 0xA5, 1);
	if (rc) {
		pr_debug("failed to write sec access rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + CHGR_BUCK_COMPARATOR_OVRIDE_1,
			0xC0, enable ? 0x00 : COMPATATOR_OVERRIDE_0, 1);
	if (rc) {
		pr_debug("failed to toggle chg done override rc=%d\n", rc);
		return rc;
	}

	return rc;
}

#define QPNP_CHG_VBATDET_MIN_MV	3240
#define QPNP_CHG_VBATDET_MAX_MV	5780
#define QPNP_CHG_VBATDET_STEP_MV	20
static int
qpnp_chg_vbatdet_set(struct qpnp_chg_chip *chip, int vbatdet_mv)
{
	u8 temp;

	if (vbatdet_mv < QPNP_CHG_VBATDET_MIN_MV
			|| vbatdet_mv > QPNP_CHG_VBATDET_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", vbatdet_mv);
		return -EINVAL;
	}
	temp = (vbatdet_mv - QPNP_CHG_VBATDET_MIN_MV)
			/ QPNP_CHG_VBATDET_STEP_MV;

	pr_debug("voltage=%d setting %02x\n", vbatdet_mv, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VBAT_DET, 1);
}

static void
qpnp_chg_set_appropriate_vbatdet(struct qpnp_chg_chip *chip)
{
	if (chip->demo_mode)
		qpnp_chg_vbatdet_set(chip, 4000 - chip->resume_delta_mv);
	else if (chip->bat_is_cool && !chip->resuming_charging)
		qpnp_chg_vbatdet_set(chip, chip->cool_bat_mv
			- chip->resume_delta_mv);
	else if (chip->bat_is_warm && !chip->resuming_charging)
		qpnp_chg_vbatdet_set(chip, chip->warm_bat_mv
			- chip->resume_delta_mv);
	else if (chip->resuming_charging)
		qpnp_chg_vbatdet_set(chip, chip->max_voltage_mv
			+ chip->resume_delta_mv);
	else
		qpnp_chg_vbatdet_set(chip, chip->max_voltage_mv
			- chip->resume_delta_mv);
}

static void
qpnp_arb_stop_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, arb_stop_work);

	if (!chip->chg_done || !chip->out_of_temp || !chip->ext_hi_temp)
		qpnp_chg_charge_en(chip, !chip->charging_disabled &&
				   (chip->chrg_ocv_state !=
				    CHRG_OCV_OCV_WAIT));
	qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
}

static void
qpnp_bat_if_adc_measure_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, adc_measure_work);

	if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev, &chip->adc_param))
		pr_err("request ADC error\n");
}

static void
qpnp_bat_if_adc_disable_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, adc_disable_work);

	qpnp_adc_tm_disable_chan_meas(chip->adc_tm_dev, &chip->adc_param);
}

#define EOC_CHECK_PERIOD_MS	10000
static irqreturn_t
qpnp_chg_vbatdet_lo_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	u8 chg_sts = 0;
	int rc;

	pr_debug("vbatdet-lo triggered\n");

	rc = qpnp_chg_read(chip, &chg_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc)
		pr_err("failed to read chg_sts rc=%d\n", rc);

	pr_debug("chg_done chg_sts: 0x%x triggered\n", chg_sts);
	if (!chip->charging_disabled && (chg_sts & FAST_CHG_ON_IRQ)) {
		chip->float_timer_start = false;
		schedule_delayed_work(&chip->eoc_work,
			msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
		pm_stay_awake(chip->dev);
	}
	qpnp_chg_disable_irq(&chip->chg_vbatdet_lo);

	pr_debug("psy changed usb_psy\n");
	power_supply_changed(chip->usb_psy);
	if (chip->dc_chgpth_base) {
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
	}
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}
	return IRQ_HANDLED;
}

#define ARB_STOP_WORK_MS	1000
static irqreturn_t
qpnp_chg_usb_chg_gone_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	u8 usb_sts;
	int rc;

	rc = qpnp_chg_read(chip, &usb_sts,
			INT_RT_STS(chip->usb_chgpth_base), 1);
	if (rc)
		pr_err("failed to read usb_chgpth_sts rc=%d\n", rc);

	pr_debug("chg_gone triggered\n");
	if ((qpnp_chg_is_usb_chg_plugged_in(chip)
			|| qpnp_chg_is_dc_chg_plugged_in(chip))
			&& (usb_sts & CHG_GONE_IRQ)) {
		qpnp_chg_charge_en(chip, 0);
		qpnp_chg_force_run_on_batt(chip, 1);
		chip->resuming_charging = true;
		qpnp_chg_set_appropriate_vbatdet(chip);
		schedule_delayed_work(&chip->arb_stop_work,
			msecs_to_jiffies(ARB_STOP_WORK_MS));
	}

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_usb_usb_ocp_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_debug("usb-ocp triggered\n");

	schedule_work(&chip->ocp_clear_work);

	return IRQ_HANDLED;
}

#define BOOST_ILIMIT_MIN	0x07
#define BOOST_ILIMIT_DEF	0x02
#define BOOST_ILIMT_MASK	0xFF
static void
qpnp_chg_ocp_clear_work(struct work_struct *work)
{
	int rc;
	u8 usb_sts;
	struct qpnp_chg_chip *chip = container_of(work,
		struct qpnp_chg_chip, ocp_clear_work);

	if (chip->type == SMBBP) {
		rc = qpnp_chg_masked_write(chip,
				chip->boost_base + BOOST_ILIM,
				BOOST_ILIMT_MASK,
				BOOST_ILIMIT_MIN, 1);
		if (rc) {
			pr_err("Failed to turn configure ilim rc = %d\n", rc);
			return;
		}
	}

	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_OCP_CLR,
			OCP_CLR_BIT,
			OCP_CLR_BIT, 1);
	if (rc)
		pr_err("Failed to clear OCP bit rc = %d\n", rc);

	/* force usb ovp fet off */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			USB_OTG_EN_BIT, 1);
	if (rc)
		pr_err("Failed to turn off usb ovp rc = %d\n", rc);

	if (chip->type == SMBBP) {
		/* Wait for OCP circuitry to be powered up */
		msleep(100);
		rc = qpnp_chg_read(chip, &usb_sts,
				INT_RT_STS(chip->usb_chgpth_base), 1);
		if (rc) {
			pr_err("failed to read interrupt sts %d\n", rc);
			return;
		}

		if (usb_sts & COARSE_DET_USB_IRQ) {
			rc = qpnp_chg_masked_write(chip,
				chip->boost_base + BOOST_ILIM,
				BOOST_ILIMT_MASK,
				BOOST_ILIMIT_DEF, 1);
			if (rc) {
				pr_err("Failed to set ilim rc = %d\n", rc);
				return;
			}
		} else {
			pr_warn_ratelimited("USB short to GND detected!\n");
		}
	}
}

#define QPNP_CHG_VDDMAX_MIN		3400
#define QPNP_CHG_V_MIN_MV		3240
#define QPNP_CHG_V_MAX_MV		4500
#define QPNP_CHG_V_STEP_MV		10
#define QPNP_CHG_BUCK_TRIM1_STEP	10
#define QPNP_CHG_BUCK_VDD_TRIM_MASK	0xF0
static int
qpnp_chg_vddmax_and_trim_set(struct qpnp_chg_chip *chip,
		int voltage, int trim_mv)
{
	int rc, trim_set;
	u8 vddmax = 0, trim = 0;

	if (voltage < QPNP_CHG_VDDMAX_MIN
			|| voltage > QPNP_CHG_V_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}

	vddmax = (voltage - QPNP_CHG_V_MIN_MV) / QPNP_CHG_V_STEP_MV;
	rc = qpnp_chg_write(chip, &vddmax, chip->chgr_base + CHGR_VDD_MAX, 1);
	if (rc) {
		pr_err("Failed to write vddmax: %d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS,
		0xFF,
		0xA5, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		return rc;
	}
	trim_set = clamp((int)chip->trim_center
			+ (trim_mv / QPNP_CHG_BUCK_TRIM1_STEP),
			0, 0xF);
	trim = (u8)trim_set << 4;
	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + BUCK_CTRL_TRIM1,
		QPNP_CHG_BUCK_VDD_TRIM_MASK,
		trim, 1);
	if (rc) {
		pr_err("Failed to write buck trim1: %d\n", rc);
		return rc;
	}
	pr_debug("voltage=%d+%d setting vddmax: %02x, trim: %02x\n",
			voltage, trim_mv, vddmax, trim);
	return 0;
}

static int
qpnp_chg_vddmax_get(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 vddmax = 0;

	rc = qpnp_chg_read(chip, &vddmax, chip->chgr_base + CHGR_VDD_MAX, 1);
	if (rc) {
		pr_err("Failed to write vddmax: %d\n", rc);
		return rc;
	}

	return QPNP_CHG_V_MIN_MV + (int)vddmax * QPNP_CHG_V_STEP_MV;
}

/* JEITA compliance logic */
static void
qpnp_chg_set_appropriate_vddmax(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 ir_comp;

	if (chip->demo_mode) {
		qpnp_chg_vddmax_and_trim_set(chip, 4000,
					     chip->delta_vddmax_mv);
		ir_comp = 0x00;
	} else if (chip->bat_is_cool &&
		 !chip->out_of_temp &&
		 !chip->ext_hi_temp) {
		qpnp_chg_vddmax_and_trim_set(chip, chip->cool_bat_mv,
				chip->delta_vddmax_mv);
		ir_comp = 0x84;
	} else if (chip->bat_is_warm && !chip->out_of_temp &&
		   !chip->ext_hi_temp && !chip->warm_bat_soc) {
		qpnp_chg_vddmax_and_trim_set(chip, chip->warm_bat_mv,
				chip->delta_vddmax_mv);
		ir_comp = 0x84;
	} else if (!chip->use_max_vdd) {
		qpnp_chg_vddmax_and_trim_set(chip, chip->step_charge_mv,
				chip->delta_vddmax_mv);
		ir_comp = 0x84;
	} else {
		qpnp_chg_vddmax_and_trim_set(chip, chip->max_voltage_mv,
				chip->delta_vddmax_mv);
		ir_comp = 0x83;
	}

	rc = qpnp_chg_masked_write(chip,
				   chip->chgr_base + CHGR_IR_DROP_COMPEN, 0xFF,
				   chip->ir_drop_comp ? ir_comp : 0x00, 1);
	if (rc)
		pr_err("failed to enable IR drop comp rc=%d\n", rc);
}

static void
qpnp_usbin_health_check_work(struct work_struct *work)
{
	int usbin_health = 0;
	u8 psy_health_sts = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, usbin_health_check);

	usbin_health = qpnp_chg_check_usbin_health(chip);
	spin_lock(&chip->usbin_health_monitor_lock);
	if (chip->usbin_health != usbin_health) {
		pr_debug("health_check_work: pr_usbin_health = %d, usbin_health = %d",
			chip->usbin_health, usbin_health);
		chip->usbin_health = usbin_health;
		if (usbin_health == USBIN_OVP)
			psy_health_sts = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else if (usbin_health == USBIN_OK)
			psy_health_sts = POWER_SUPPLY_HEALTH_GOOD;
		power_supply_set_health_state(chip->usb_psy, psy_health_sts);
		power_supply_changed(chip->usb_psy);
	}
	/* enable OVP monitor in usb valid after coarse-det complete */
	chip->usb_valid_check_ovp = true;
	spin_unlock(&chip->usbin_health_monitor_lock);
	return;
}

#define USB_VALID_DEBOUNCE_TIME_MASK		0x3
#define USB_DEB_BYPASS		0x0
#define USB_DEB_5MS			0x1
#define USB_DEB_10MS		0x2
#define USB_DEB_20MS		0x3
static irqreturn_t
qpnp_chg_coarse_det_usb_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int host_mode, rc = 0;
	int debounce[] = {
		[USB_DEB_BYPASS] = 0,
		[USB_DEB_5MS] = 5,
		[USB_DEB_10MS] = 10,
		[USB_DEB_20MS] = 20 };
	u8 ovp_ctl;
	bool usb_coarse_det;

	host_mode = qpnp_chg_is_otg_en_set(chip);
	usb_coarse_det = qpnp_chg_check_usb_coarse_det(chip);
	pr_debug("usb coarse-det triggered: %d host_mode: %d\n",
			usb_coarse_det, host_mode);

	if (host_mode)
		return IRQ_HANDLED;
	/* ignore to monitor OVP in usbin valid irq handler
	 if the coarse-det fired first, do the OVP state monitor
	 in the usbin_health_check work, and after the work,
	 enable monitor OVP in usbin valid irq handler */
	chip->usb_valid_check_ovp = false;
	if (chip->usb_coarse_det ^ usb_coarse_det) {
		chip->usb_coarse_det = usb_coarse_det;
		if (usb_coarse_det) {
			/* usb coarse-det rising edge, check the usbin_valid
			debounce time setting, and start a delay work to
			check the OVP status*/
			rc = qpnp_chg_read(chip, &ovp_ctl,
					chip->usb_chgpth_base + USB_OVP_CTL, 1);

			if (rc) {
				pr_err("spmi read failed: addr=%03X, rc=%d\n",
					chip->usb_chgpth_base + USB_OVP_CTL,
					rc);
				return rc;
			}
			ovp_ctl = ovp_ctl & USB_VALID_DEBOUNCE_TIME_MASK;
			schedule_delayed_work(&chip->usbin_health_check,
					msecs_to_jiffies(debounce[ovp_ctl]));
		} else {
			/* usb coarse-det rising edge, set the usb psy health
			status to unknown */
			pr_debug("usb coarse det clear, set usb health to unknown\n");
			chip->usbin_health = USBIN_UNKNOW;
			power_supply_set_health_state(chip->usb_psy,
				POWER_SUPPLY_HEALTH_UNKNOWN);
			power_supply_changed(chip->usb_psy);
		}

	}
	return IRQ_HANDLED;
}

#define BATFET_LPM_MASK		0xC0
#define BATFET_LPM		0x40
#define BATFET_NO_LPM		0x00
static int
qpnp_chg_regulator_batfet_set(struct qpnp_chg_chip *chip, bool enable)
{
	int rc = 0;

	if (chip->charging_disabled || !chip->bat_if_base)
		return rc;

	if (!enable)
		return rc;

	if (chip->type == SMBB)
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + CHGR_BAT_IF_SPARE,
			BATFET_LPM_MASK,
			enable ? BATFET_NO_LPM : BATFET_LPM, 1);
	else
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + CHGR_BAT_IF_BATFET_CTRL4,
			BATFET_LPM_MASK,
			enable ? BATFET_NO_LPM : BATFET_LPM, 1);

	return rc;
}

#define SECS_PER_WEEK 604800
#define SECS_PER_HR 3600
#define ENUM_T_STOP_BIT		BIT(0)
static irqreturn_t
qpnp_chg_usb_usbin_valid_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int usb_present, host_mode, usbin_health;
	u8 psy_health_sts;
	struct timeval tv;
	int chrg_ocv_time = chrg_timestamp;
	static int chrg_ocv_try_time;

	do_gettimeofday(&tv);
	chrg_ocv_time += SECS_PER_WEEK;

	usb_present = qpnp_chg_is_usb_chg_plugged_in(chip);
	host_mode = qpnp_chg_is_otg_en_set(chip);
	pr_debug("usbin-valid triggered: %d host_mode: %d\n",
		usb_present, host_mode);

	/* In host mode notifications cmoe from USB supply */
	if (host_mode)
		return IRQ_HANDLED;

	if (chip->usb_present ^ usb_present) {
		chip->usb_present = usb_present;
		if (!usb_present) {
			chip->use_max_vdd = true;
			/* when a valid charger inserted, and increase the
			 *  charger voltage to OVP threshold, then
			 *  usb_in_valid falling edge interrupt triggers.
			 *  So we handle the OVP monitor here, and ignore
			 *  other health state changes */
			if (chip->ovp_monitor_enable &&
				       (chip->usb_valid_check_ovp)) {
				usbin_health =
					qpnp_chg_check_usbin_health(chip);
				if ((chip->usbin_health != usbin_health)
					&& (usbin_health == USBIN_OVP)) {
					chip->usbin_health = usbin_health;
					psy_health_sts =
					POWER_SUPPLY_HEALTH_OVERVOLTAGE;
					power_supply_set_health_state(
						chip->usb_psy,
						psy_health_sts);
					power_supply_changed(chip->usb_psy);
				}
			}
			if (!qpnp_chg_is_dc_chg_plugged_in(chip)) {
				chip->delta_vddmax_mv = 0;
				qpnp_chg_set_appropriate_vddmax(chip);
				chip->chg_done = false;
				chip->maint_chrg = false;
				chip->float_timer_start = false;
			}
			qpnp_chg_usb_suspend_enable(chip, 0);
			qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
			qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
			chip->prev_usb_max_ma = -EINVAL;
			chip->aicl_settled = false;
			chip->weak_chg_attached = false;
			chip->hv_chg_attached = false;
			chip->chrg_ocv_state = CHRG_OCV_NO_CHRG;
			chip->float_charge_start_time = 0;
			power_supply_set_online(chip->usb_psy, 0);
			chip->usblpm_dropbox_sent = false;
		} else {
			/* when OVP clamped usbin, and then decrease
			 * the charger voltage to lower than the OVP
			 * threshold, a usbin_valid rising edge
			 * interrupt triggered. So we change the usb
			 * psy health state back to good */
			if (chip->ovp_monitor_enable &&
				       (chip->usb_valid_check_ovp)) {
				usbin_health =
					qpnp_chg_check_usbin_health(chip);
				if ((chip->usbin_health != usbin_health)
					&& (usbin_health == USBIN_OK)) {
					chip->usbin_health = usbin_health;
					psy_health_sts =
						POWER_SUPPLY_HEALTH_GOOD;
					power_supply_set_health_state(
						chip->usb_psy,
						psy_health_sts);
					power_supply_changed(chip->usb_psy);
				}
			}

			qpnp_chg_force_run_on_batt(chip,
					chip->charging_disabled);
			if (!qpnp_chg_is_dc_chg_plugged_in(chip)) {
				chip->delta_vddmax_mv = 0;
				qpnp_chg_set_appropriate_vddmax(chip);
				chip->resuming_charging = true;
				qpnp_chg_set_appropriate_vbatdet(chip);
				chip->resuming_charging = false;
			}
			if ((tv.tv_sec >= chrg_ocv_time) &&
			    (tv.tv_sec >= (chrg_ocv_try_time + SECS_PER_HR)) &&
			    !chip->bat_is_cool &&
			    !chip->bat_is_warm) {
				chip->chrg_ocv_cc_bf_uah = 0;
				chip->chrg_ocv_time = 0;
				chrg_ocv_try_time = tv.tv_sec;
				qpnp_chg_charge_en(chip, 0);
				chip->chrg_ocv_state = CHRG_OCV_OCV_WAIT;
				pr_info("Start Chrg OCV calculation\n");
			}
			pm_stay_awake(chip->dev);
			schedule_delayed_work(&chip->chrg_ocv_work,
				msecs_to_jiffies(0));
			chip->float_timer_start = false;
			schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
			schedule_work(&chip->soc_check_work);
		}

		power_supply_set_present(chip->usb_psy, chip->usb_present);
		schedule_work(&chip->batfet_lcl_work);
	}

	return IRQ_HANDLED;
}

#define TEST_EN_SMBC_LOOP		0xE5
#define IBAT_REGULATION_DISABLE		BIT(2)
static irqreturn_t
qpnp_chg_bat_if_batt_temp_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int batt_temp_good, rc;

	batt_temp_good = qpnp_chg_is_batt_temp_ok(chip);
	pr_debug("batt-temp triggered: %d\n", batt_temp_good);

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + SEC_ACCESS,
		0xFF,
		0xA5, 1);
	if (rc) {
		pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip,
		chip->buck_base + TEST_EN_SMBC_LOOP,
		IBAT_REGULATION_DISABLE,
		batt_temp_good ? 0 : IBAT_REGULATION_DISABLE, 1);
	if (rc) {
		pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
		return rc;
	}

	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_bat_if_batt_pres_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int batt_present;

	batt_present = qpnp_chg_is_batt_present(chip);
	pr_debug("batt-pres triggered: %d\n", batt_present);

	if (chip->batt_present ^ batt_present) {
		if (batt_present) {
			schedule_work(&chip->insertion_ocv_work);
		} else {
			chip->insertion_ocv_uv = 0;
			qpnp_chg_charge_en(chip, 0);
		}
		chip->batt_present = batt_present;
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
		pr_debug("psy changed usb_psy\n");
		power_supply_changed(chip->usb_psy);

		if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
						&& batt_present) {
			pr_debug("enabling vadc notifications\n");
			schedule_work(&chip->adc_measure_work);
		}
#if 0
		else if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
				&& !batt_present) {
			schedule_work(&chip->adc_disable_work);
			pr_debug("disabling vadc notifications\n");
		}
#endif
	}

	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_dc_dcin_valid_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int dc_present;

	dc_present = qpnp_chg_is_dc_chg_plugged_in(chip);
	pr_debug("dcin-valid triggered: %d\n", dc_present);

	if (chip->dc_present ^ dc_present) {
		chip->dc_present = dc_present;
		chip->float_timer_start = false;
		if (qpnp_chg_is_otg_en_set(chip))
			qpnp_chg_force_run_on_batt(chip, !dc_present ? 1 : 0);
		if (!dc_present && !qpnp_chg_is_usb_chg_plugged_in(chip)) {
			chip->use_max_vdd = true;
			chip->delta_vddmax_mv = 0;
			qpnp_chg_set_appropriate_vddmax(chip);
			chip->chg_done = false;
			chip->maint_chrg = false;
		} else {
			if (!qpnp_chg_is_usb_chg_plugged_in(chip)) {
				chip->delta_vddmax_mv = 0;
				qpnp_chg_set_appropriate_vddmax(chip);
			}
			pm_stay_awake(chip->dev);
			schedule_delayed_work(&chip->eoc_work,
				msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
			schedule_work(&chip->soc_check_work);
		}
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
		schedule_work(&chip->batfet_lcl_work);
	}

	return IRQ_HANDLED;
}

#define CHGR_CHG_FAILED_BIT	BIT(7)
static irqreturn_t
qpnp_chg_chgr_chg_failed_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	int rc;

	pr_debug("chg_failed triggered\n");

	rc = qpnp_chg_masked_write(chip,
		chip->chgr_base + CHGR_CHG_FAILED,
		CHGR_CHG_FAILED_BIT,
		CHGR_CHG_FAILED_BIT, 1);
	if (rc)
		pr_err("Failed to write chg_fail clear bit!\n");

	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}
	pr_debug("psy changed usb_psy\n");
	power_supply_changed(chip->usb_psy);
	if (chip->dc_chgpth_base) {
		pr_debug("psy changed dc_psy\n");
		power_supply_changed(&chip->dc_psy);
	}
	return IRQ_HANDLED;
}

static irqreturn_t
qpnp_chg_chgr_chg_trklchg_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;

	pr_debug("TRKL IRQ triggered\n");

	chip->chg_done = false;
	chip->float_timer_start = false;
	if (chip->bat_if_base) {
		pr_debug("psy changed batt_psy\n");
		power_supply_changed(&chip->batt_psy);
	}

	return IRQ_HANDLED;
}

static int qpnp_chg_is_fastchg_on(struct qpnp_chg_chip *chip)
{
	u8 chgr_sts;
	int rc;

	rc = qpnp_chg_read(chip, &chgr_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt status %d\n", rc);
		return rc;
	}
	pr_debug("chgr_sts 0x%x\n", chgr_sts);
	return (chgr_sts & FAST_CHG_ON_IRQ) ? 1 : 0;
}

static irqreturn_t
qpnp_chg_chgr_chg_fastchg_irq_handler(int irq, void *_chip)
{
	struct qpnp_chg_chip *chip = _chip;
	bool fastchg_on = false;

	fastchg_on = qpnp_chg_is_fastchg_on(chip);

	pr_debug("FAST_CHG IRQ triggered, fastchg_on: %d\n", fastchg_on);

	if (chip->fastchg_on ^ fastchg_on) {
		chip->fastchg_on = fastchg_on;
		if (chip->bat_if_base) {
			pr_debug("psy changed batt_psy\n");
			power_supply_changed(&chip->batt_psy);
		}

		pr_debug("psy changed usb_psy\n");
		power_supply_changed(chip->usb_psy);

		if (chip->dc_chgpth_base) {
			pr_debug("psy changed dc_psy\n");
			power_supply_changed(&chip->dc_psy);
		}

		if (fastchg_on) {
			chip->chg_done = false;
			chip->float_timer_start = false;

			if (!chip->charging_disabled) {
				schedule_delayed_work(&chip->weak_check_work,
						      msecs_to_jiffies(1000));
				schedule_delayed_work(&chip->eoc_work,
					msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
				pm_stay_awake(chip->dev);
			}
			if (chip->parallel_ovp_mode)
				switch_parallel_ovp_mode(chip, 1);
		} else {
			if (chip->parallel_ovp_mode)
				switch_parallel_ovp_mode(chip, 0);
		}
	}

	qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);

	return IRQ_HANDLED;
}

static int
qpnp_dc_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static int
qpnp_batt_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_COOL_TEMP:
	case POWER_SUPPLY_PROP_WARM_TEMP:
	case POWER_SUPPLY_PROP_CAPACITY:
		return 1;
	default:
		break;
	}

	return 0;
}

static int
switch_usb_to_charge_mode(struct qpnp_chg_chip *chip)
{
	int rc;

	pr_debug("switch to charge mode\n");
	if (!qpnp_chg_is_otg_en_set(chip))
		return 0;

	if (chip->type == SMBBP) {
		rc = qpnp_chg_masked_write(chip,
			chip->boost_base + BOOST_ILIM,
			BOOST_ILIMT_MASK,
			BOOST_ILIMIT_DEF, 1);
		if (rc) {
			pr_err("Failed to set ilim rc = %d\n", rc);
			return rc;
		}
	}

	/* enable usb ovp fet */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			0, 1);
	if (rc) {
		pr_err("Failed to turn on usb ovp rc = %d\n", rc);
		return rc;
	}

	rc = qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
	if (rc) {
		pr_err("Failed re-enable charging rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int
switch_usb_to_host_mode(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 usb_sts;

	pr_debug("switch to host mode\n");
	if (qpnp_chg_is_otg_en_set(chip))
		return 0;

	if (chip->parallel_ovp_mode)
		switch_parallel_ovp_mode(chip, 0);

	if (chip->type == SMBBP) {
		rc = qpnp_chg_masked_write(chip,
				chip->boost_base + BOOST_ILIM,
				BOOST_ILIMT_MASK,
				BOOST_ILIMIT_MIN, 1);
		if (rc) {
			pr_err("Failed to turn configure ilim rc = %d\n", rc);
			return rc;
		}
	}

	if (!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		rc = qpnp_chg_force_run_on_batt(chip, 1);
		if (rc) {
			pr_err("Failed to disable charging rc = %d\n", rc);
			return rc;
		}
	}

	/* Clear any OCP condition before turning off OVP FET */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_OCP_CLR,
			OCP_CLR_BIT,
			OCP_CLR_BIT, 1);
	if (rc)
		pr_err("Failed to clear ocp latch event rc = %d\n", rc);

	/* force usb ovp fet off */
	rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_USB_OTG_CTL,
			USB_OTG_EN_BIT,
			USB_OTG_EN_BIT, 1);
	if (rc) {
		pr_err("Failed to turn off usb ovp rc = %d\n", rc);
		return rc;
	}

	if (chip->type == SMBBP) {
		/* Wait for OCP circuitry to be powered up */
		msleep(100);
		rc = qpnp_chg_read(chip, &usb_sts,
				INT_RT_STS(chip->usb_chgpth_base), 1);
		if (rc) {
			pr_err("failed to read interrupt sts %d\n", rc);
			return rc;
		}

		if (usb_sts & COARSE_DET_USB_IRQ) {
			rc = qpnp_chg_masked_write(chip,
				chip->boost_base + BOOST_ILIM,
				BOOST_ILIMT_MASK,
				BOOST_ILIMIT_DEF, 1);
			if (rc) {
				pr_err("Failed to set ilim rc = %d\n", rc);
				return rc;
			}
		} else {
			pr_warn_ratelimited("USB short to GND detected!\n");
		}
	}

	return 0;
}

static enum power_supply_property pm_power_props_mains[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static enum power_supply_property msm_batt_power_props[] = {
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_EMPTY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_INPUT_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_COOL_TEMP,
	POWER_SUPPLY_PROP_WARM_TEMP,
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
	POWER_SUPPLY_PROP_NUM_SYSTEM_TEMP_LEVELS,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_RATE,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static char *pm_power_supplied_to[] = {
	"battery",
};

static char *pm_batt_supplied_to[] = {
	"bms",
};

static int charger_monitor;
module_param(charger_monitor, int, 0644);

static int ext_ovp_present;
module_param(ext_ovp_present, int, 0444);

#define USB_WALL_THRESHOLD_MA	500
#define USB_WEAK_THRESHOLD_MA	400
#define OVP_USB_WALL_THRESHOLD_MA	200
static int
qpnp_power_get_property_mains(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								dc_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		if (chip->charging_disabled)
			return 0;

		val->intval = qpnp_chg_is_dc_chg_plugged_in(chip);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chip->maxinput_dc_ma * 1000;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void
qpnp_weak_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, weak_check_work);

	qpnp_chg_weak_check(chip);
}

static void
qpnp_aicl_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, aicl_check_work);
	union power_supply_propval ret = {0,};

	if (!charger_monitor && qpnp_chg_is_usb_chg_plugged_in(chip)) {
		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);
		if ((ret.intval / 1000) > USB_WALL_THRESHOLD_MA) {
			pr_debug("no charger_monitor present set iusbmax %d\n",
					ret.intval / 1000);
			qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
		}
	} else {
		pr_debug("charger_monitor is present\n");
	}
	chip->charger_monitor_checked = true;
}

static int
get_prop_battery_voltage_now(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	if (chip->revision == 0 && chip->type == SMBB) {
		pr_err("vbat reading not supported for 1.0 rc=%d\n", rc);
		return 0;
	} else {
		rc = qpnp_vadc_read(chip->vadc_dev, VBAT_SNS, &results);
		if (rc) {
			pr_err("Unable to read vbat rc=%d\n", rc);
			return 0;
		}
		return results.physical;
	}
}

#define BATT_PRES_BIT BIT(7)
static int
get_prop_batt_present(struct qpnp_chg_chip *chip)
{
#ifdef QCOM_WA
	u8 batt_present;
	int rc;

	rc = qpnp_chg_read(chip, &batt_present,
				chip->bat_if_base + CHGR_BAT_IF_PRES_STATUS, 1);
	if (rc) {
		pr_err("Couldn't read battery status read failed rc=%d\n", rc);
		return 0;
	};
	return (batt_present & BATT_PRES_BIT) ? 1 : 0;
#else
	return 1;
#endif
}

#define BATT_TEMP_HOT	BIT(6)
#define BATT_TEMP_OK	BIT(7)
static int
get_prop_batt_health(struct qpnp_chg_chip *chip)
{
	u8 batt_health;
	int rc;

	rc = qpnp_chg_read(chip, &batt_health,
				chip->bat_if_base + CHGR_STATUS, 1);
	if (rc) {
		pr_err("Couldn't read battery health read failed rc=%d\n", rc);
		return POWER_SUPPLY_HEALTH_UNKNOWN;
	};

	if (BATT_TEMP_OK & batt_health)
		return POWER_SUPPLY_HEALTH_GOOD;
	if (BATT_TEMP_HOT & batt_health)
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	else
		return POWER_SUPPLY_HEALTH_COLD;
}

static int
get_prop_charge_type(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 chgr_sts;

	if (!get_prop_batt_present(chip))
		return POWER_SUPPLY_CHARGE_TYPE_NONE;

	rc = qpnp_chg_read(chip, &chgr_sts,
				INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	if (chgr_sts & TRKL_CHG_ON_IRQ)
		return POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
	if (chgr_sts & FAST_CHG_ON_IRQ)
		return POWER_SUPPLY_CHARGE_TYPE_FAST;

	return POWER_SUPPLY_CHARGE_TYPE_NONE;
}

#define DEFAULT_CAPACITY	50
static int
get_batt_capacity(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->fake_battery_soc >= 0)
		return chip->fake_battery_soc;
	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_CAPACITY;
	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);
		return ret.intval;
	}
	return DEFAULT_CAPACITY;
}

static int
get_prop_batt_status(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 chgr_sts, bat_if_sts;

	if ((qpnp_chg_is_usb_chg_plugged_in(chip) ||
		qpnp_chg_is_dc_chg_plugged_in(chip)) && chip->chg_done) {
		return POWER_SUPPLY_STATUS_FULL;
	}

	rc = qpnp_chg_read(chip, &chgr_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	rc = qpnp_chg_read(chip, &bat_if_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("failed to read bat_if sts %d\n", rc);
		return POWER_SUPPLY_CHARGE_TYPE_NONE;
	}

	if ((chgr_sts & TRKL_CHG_ON_IRQ) && !(bat_if_sts & BAT_FET_ON_IRQ))
		return POWER_SUPPLY_STATUS_CHARGING;
	if (chgr_sts & FAST_CHG_ON_IRQ && bat_if_sts & BAT_FET_ON_IRQ)
		return POWER_SUPPLY_STATUS_CHARGING;

	/* report full if state of charge is 100 and a charger is connected */
	if ((qpnp_chg_is_usb_chg_plugged_in(chip) ||
		qpnp_chg_is_dc_chg_plugged_in(chip))
			&& get_batt_capacity(chip) == 100) {
		return POWER_SUPPLY_STATUS_FULL;
	}

	if (chip->chrg_ocv_state == CHRG_OCV_OCV_WAIT)
		return POWER_SUPPLY_STATUS_CHARGING;

	return POWER_SUPPLY_STATUS_DISCHARGING;
}

static int
get_prop_current_now(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CURRENT_NOW, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

static int
get_prop_full_design(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

static int
get_prop_charge_full(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CHARGE_FULL, &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

static int
get_prop_charge_counter(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
					    POWER_SUPPLY_PROP_CHARGE_COUNTER,
					    &ret);
		return ret.intval;
	} else {
		pr_debug("No BMS supply registered return 0\n");
	}

	return 0;
}

static int
get_prop_capacity(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};
	int battery_status, bms_status, soc, charger_in;

	if (chip->fake_battery_soc >= 0)
		return chip->fake_battery_soc;

	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_CAPACITY;

	if (chip->bms_psy) {
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);
		soc = ret.intval;
		battery_status = get_prop_batt_status(chip);
		chip->bms_psy->get_property(chip->bms_psy,
				POWER_SUPPLY_PROP_STATUS, &ret);
		bms_status = ret.intval;
		charger_in = qpnp_chg_is_usb_chg_plugged_in(chip) ||
			qpnp_chg_is_dc_chg_plugged_in(chip);

		if (soc <= chip->soc_resume_limit) {
			chip->maint_chrg = false;
			chip->chg_done = false;
		}

		if (battery_status != POWER_SUPPLY_STATUS_CHARGING
				&& bms_status != POWER_SUPPLY_STATUS_CHARGING
				&& !chip->out_of_temp
				&& !chip->ext_hi_temp
				&& charger_in
				&& !chip->bat_is_cool
				&& !chip->bat_is_warm
				&& !chip->charging_disabled
				&& chip->soc_resume_limit
				&& soc <= chip->soc_resume_limit) {
			pr_debug("resuming charging at %d%% soc\n", soc);
			chip->resuming_charging = true;
			qpnp_chg_set_appropriate_vbatdet(chip);
			chip->resuming_charging = false;
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		}
		if (soc == 0) {
			if (!chip->shutdown_needed) {
				power_supply_set_present(chip->usb_psy, 0);
				power_supply_set_online(chip->usb_psy, 0);
			}
			chip->shutdown_needed = 1;
			if (!qpnp_chg_is_usb_chg_plugged_in(chip)
				&& !qpnp_chg_is_usb_chg_plugged_in(chip))
				pr_warn_ratelimited("Battery 0, CHG absent\n");
		}
		return soc;
	} else {
		pr_debug("No BMS supply registered return 50\n");
	}

	/* return default capacity to avoid userspace
	 * from shutting down unecessarily */
	return DEFAULT_CAPACITY;
}

#define DEFAULT_TEMP		250
#define MAX_TOLERABLE_BATT_TEMP_DDC	680
static int
get_prop_batt_temp(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	if (chip->use_default_batt_values || !get_prop_batt_present(chip))
		return DEFAULT_TEMP;

	rc = qpnp_vadc_read(chip->vadc_dev, LR_MUX1_BATT_THERM, &results);
	if (rc) {
		pr_debug("Unable to read batt temperature rc=%d\n", rc);
		return 0;
	}
	pr_debug("get_bat_temp %d, %lld\n",
		results.adc_code, results.physical);

	return (int)results.physical + chip->batt_therm_offset;
}

static int get_prop_cycle_count(struct qpnp_chg_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->bms_psy)
		chip->bms_psy->get_property(chip->bms_psy,
			  POWER_SUPPLY_PROP_CYCLE_COUNT, &ret);
	return ret.intval;
}

static int get_prop_vchg_loop(struct qpnp_chg_chip *chip)
{
	u8 buck_sts;
	int rc;

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);

	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n",
				INT_RT_STS(chip->buck_base), rc);
		return rc;
	}
	pr_debug("buck usb sts 0x%x\n", buck_sts);

	return (buck_sts & VCHG_LOOP_IRQ) ? 1 : 0;
}

static int get_prop_online(struct qpnp_chg_chip *chip)
{
	return qpnp_chg_is_batfet_closed(chip);
}

static void
qpnp_chg_weak_check(struct qpnp_chg_chip *chip)
{
	bool vchg_loop = get_prop_vchg_loop(chip);
	union power_supply_propval ret = {0,};

	chip->usb_psy->get_property(chip->usb_psy,
				    POWER_SUPPLY_PROP_TYPE, &ret);

	if (!vchg_loop ||
	    (ret.intval != POWER_SUPPLY_TYPE_USB_DCP) ||
	    chip->hv_chg_attached ||
	    !qpnp_chg_is_usb_chg_plugged_in(chip))
		return;

	pr_debug("Find Weak Charger\n");
	qpnp_chg_iusbmax_set(chip, USB_WALL_THRESHOLD_MA);
	msleep(100);

	if (!qpnp_chg_is_usb_chg_plugged_in(chip))
		return;

	vchg_loop = get_prop_vchg_loop(chip);
	if (vchg_loop) {
		qpnp_chg_iusbmax_set(chip, USB_WEAK_THRESHOLD_MA);
		pr_warn("Weak Charger Detected\n");
		chip->weak_chg_attached = true;
		power_supply_changed(&chip->batt_psy);
	} else {
		qpnp_chg_iusbmax_set(chip, chip->usb_limit_ma);
	}
}

static void
qpnp_batt_external_power_changed(struct power_supply *psy)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);
	union power_supply_propval ret = {0,};

	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("bms");

	chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_ONLINE, &ret);

	/* Only honour requests while USB is present */
	if (qpnp_chg_is_usb_chg_plugged_in(chip)) {
		chip->usb_psy->get_property(chip->usb_psy,
					    POWER_SUPPLY_PROP_POWER_NOW, &ret);
		if ((USB_REQUEST_MODE_9V == ret.intval) ||
			(USB_REQUEST_MODE_PENDING == ret.intval)) {
			chip->hv_chg_attached = true;
			qpnp_chg_vinmin_set(chip, 7000);
			qpnp_chg_iusbmax_set(chip, 1600);
			chip->prev_usb_max_ma = 1600;
			pr_info("HVDCP 9V Detected VINMIN 7.0V\n");
			goto skip_set_iusb_max;
		} else {
			qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
		}

		ret.intval = 0;

		chip->usb_psy->get_property(chip->usb_psy,
			  POWER_SUPPLY_PROP_CURRENT_MAX, &ret);

		if (chip->prev_usb_max_ma == ret.intval)
			goto skip_set_iusb_max;

		chip->prev_usb_max_ma = ret.intval;
		chip->usb_limit_ma = ret.intval / 1000;
		if (ret.intval <= 2 && !chip->use_default_batt_values &&
						get_prop_batt_present(chip)) {
			if (ret.intval ==  2)
				qpnp_chg_usb_suspend_enable(chip, 1);
			qpnp_chg_iusbmax_set(chip, QPNP_CHG_I_MAX_MIN_100);
		} else {
			qpnp_chg_usb_suspend_enable(chip, 0);
			if (((ret.intval / 1000) > USB_WALL_THRESHOLD_MA)
					&& (charger_monitor ||
					!chip->charger_monitor_checked)) {
				if (!ext_ovp_present)
					qpnp_chg_iusbmax_set(chip,
						USB_WALL_THRESHOLD_MA);
				else
					qpnp_chg_iusbmax_set(chip,
						OVP_USB_WALL_THRESHOLD_MA);
			} else {
				qpnp_chg_iusbmax_set(chip, ret.intval / 1000);
			}

			if ((chip->flags & POWER_STAGE_WA)
			&& ((ret.intval / 1000) > USB_WALL_THRESHOLD_MA)
			&& !chip->power_stage_workaround_running
			&& chip->power_stage_workaround_enable) {
				chip->power_stage_workaround_running = true;
				pr_debug("usb wall chg inserted starting power stage workaround charger_monitor = %d\n",
						charger_monitor);
				schedule_work(&chip->reduce_power_stage_work);
			}
		}
	}

skip_set_iusb_max:
	pr_debug("end of power supply changed\n");
	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
}

static int
get_prop_charge_rate(struct qpnp_chg_chip *chip)
{

	if (chip->weak_chg_attached)
		return POWER_SUPPLY_CHARGE_RATE_WEAK;

	else if (chip->hv_chg_attached)
		return POWER_SUPPLY_CHARGE_RATE_TURBO;

	if (!qpnp_chg_is_usb_chg_plugged_in(chip))
		return POWER_SUPPLY_CHARGE_RATE_NONE;

	return POWER_SUPPLY_CHARGE_RATE_NORMAL;
}

static int
qpnp_batt_power_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if ((qpnp_chg_is_usb_chg_plugged_in(chip) ||
		     qpnp_chg_is_dc_chg_plugged_in(chip)) &&
		    (chip->bat_is_cool  || chip->bat_is_warm))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if ((qpnp_chg_is_usb_chg_plugged_in(chip) ||
			  qpnp_chg_is_dc_chg_plugged_in(chip)) &&
			 chip->maint_chrg)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = get_prop_batt_status(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = get_prop_charge_type(chip);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = get_prop_batt_health(chip);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = get_prop_batt_present(chip);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = chip->max_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = chip->min_voltage_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_EMPTY:
		val->intval = chip->cutoff_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->battery_info;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_prop_battery_voltage_now(chip);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = chip->insertion_ocv_uv;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = get_prop_batt_temp(chip);
		break;
	case POWER_SUPPLY_PROP_COOL_TEMP:
		val->intval = chip->cool_bat_decidegc;
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		val->intval = chip->warm_bat_decidegc;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_prop_capacity(chip);
		if ((qpnp_chg_is_usb_chg_plugged_in(chip) ||
		     qpnp_chg_is_dc_chg_plugged_in(chip)) &&
		    chip->maint_chrg)
			val->intval = 100;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = get_prop_current_now(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = get_prop_full_design(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = get_prop_charge_full(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		val->intval = !(chip->charging_disabled);
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		val->intval = chip->therm_lvl_sel;
		break;
	case POWER_SUPPLY_PROP_NUM_SYSTEM_TEMP_LEVELS:
		val->intval = chip->thermal_levels;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = get_prop_cycle_count(chip);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		val->intval = get_prop_vchg_loop(chip);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		val->intval = qpnp_chg_usb_iusbmax_get(chip) * 1000;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM:
		val->intval = qpnp_chg_iusb_trim_get(chip);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		val->intval = chip->aicl_settled;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = qpnp_chg_vinmin_get(chip) * 1000;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = get_prop_online(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = get_prop_charge_counter(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_RATE:
		val->intval = get_prop_charge_rate(chip);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define BTC_CONFIG_ENABLED	BIT(7)
#define BTC_COLD		BIT(1)
#define BTC_HOT			BIT(0)
static int
qpnp_chg_bat_if_configure_btc(struct qpnp_chg_chip *chip)
{
	u8 btc_cfg = 0, mask = 0;

	/* Do nothing if battery peripheral not present */
	if (!chip->bat_if_base)
		return 0;

	if ((chip->hot_batt_p == HOT_THD_25_PCT)
			|| (chip->hot_batt_p == HOT_THD_35_PCT)) {
		btc_cfg |= btc_value[chip->hot_batt_p];
		mask |= BTC_HOT;
	}

	if ((chip->cold_batt_p == COLD_THD_70_PCT) ||
			(chip->cold_batt_p == COLD_THD_80_PCT)) {
		btc_cfg |= btc_value[chip->cold_batt_p];
		mask |= BTC_COLD;
	}

	if (chip->btc_disabled)
		mask |= BTC_CONFIG_ENABLED;

	return qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_BTC_CTRL,
			mask, btc_cfg, 1);
}

#define QPNP_CHG_IBATSAFE_MIN_MA		100
#define QPNP_CHG_IBATSAFE_MAX_MA		3250
#define QPNP_CHG_I_STEP_MA		50
#define QPNP_CHG_I_MIN_MA		100
#define QPNP_CHG_I_MASK			0x3F
static int
qpnp_chg_ibatsafe_set(struct qpnp_chg_chip *chip, int safe_current)
{
	u8 temp;

	if (safe_current < QPNP_CHG_IBATSAFE_MIN_MA
			|| safe_current > QPNP_CHG_IBATSAFE_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", safe_current);
		return -EINVAL;
	}

	temp = safe_current / QPNP_CHG_I_STEP_MA;
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_IBAT_SAFE,
			QPNP_CHG_I_MASK, temp, 1);
}

#define QPNP_CHG_ITERM_MIN_MA		100
#define QPNP_CHG_ITERM_MAX_MA		250
#define QPNP_CHG_ITERM_STEP_MA		50
#define QPNP_CHG_ITERM_MASK			0x03
static int
qpnp_chg_ibatterm_set(struct qpnp_chg_chip *chip, int term_current)
{
	u8 temp;

	if (term_current < QPNP_CHG_ITERM_MIN_MA
			|| term_current > QPNP_CHG_ITERM_MAX_MA) {
		pr_err("bad mA=%d asked to set\n", term_current);
		return -EINVAL;
	}

	temp = (term_current - QPNP_CHG_ITERM_MIN_MA)
				/ QPNP_CHG_ITERM_STEP_MA;
	return qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_IBAT_TERM_CHGR,
			QPNP_CHG_ITERM_MASK, temp, 1);
}

#define QPNP_CHG_IBATMAX_MIN	50
#define QPNP_CHG_IBATMAX_MAX	3250
static int
qpnp_chg_ibatmax_set(struct qpnp_chg_chip *chip, int chg_current)
{
	u8 temp;

	if (chg_current < QPNP_CHG_IBATMAX_MIN
			|| chg_current > QPNP_CHG_IBATMAX_MAX) {
		pr_err("bad mA=%d asked to set\n", chg_current);
		return -EINVAL;
	}
	temp = chg_current / QPNP_CHG_I_STEP_MA;
	return qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_IBAT_MAX,
			QPNP_CHG_I_MASK, temp, 1);
}

static int
qpnp_chg_ibatmax_get(struct qpnp_chg_chip *chip, int *chg_current)
{
	int rc;
	u8 temp;

	*chg_current = 0;
	rc = qpnp_chg_read(chip, &temp, chip->chgr_base + CHGR_IBAT_MAX, 1);
	if (rc) {
		pr_err("failed read ibat_max rc=%d\n", rc);
		return rc;
	}

	*chg_current = ((temp & QPNP_CHG_I_MASK) * QPNP_CHG_I_STEP_MA);

	return 0;
}

#define QPNP_CHG_TCHG_MASK	0x7F
#define QPNP_CHG_TCHG_EN_MASK	0x80
#define QPNP_CHG_TCHG_MIN	4
#define QPNP_CHG_TCHG_MAX	512
#define QPNP_CHG_TCHG_STEP	4
static int qpnp_chg_tchg_max_set(struct qpnp_chg_chip *chip, int minutes)
{
	u8 temp;
	int rc;

	if (minutes < QPNP_CHG_TCHG_MIN || minutes > QPNP_CHG_TCHG_MAX) {
		pr_err("bad max minutes =%d asked to set\n", minutes);
		return -EINVAL;
	}

	rc = qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX_EN,
			QPNP_CHG_TCHG_EN_MASK, 0, 1);
	if (rc) {
		pr_err("failed write tchg_max_en rc=%d\n", rc);
		return rc;
	}

	temp = minutes / QPNP_CHG_TCHG_STEP - 1;

	rc = qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX,
			QPNP_CHG_TCHG_MASK, temp, 1);
	if (rc) {
		pr_err("failed write tchg_max_en rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_masked_write(chip, chip->chgr_base + CHGR_TCHG_MAX_EN,
			QPNP_CHG_TCHG_EN_MASK, QPNP_CHG_TCHG_EN_MASK, 1);
	if (rc) {
		pr_err("failed write tchg_max_en rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int
qpnp_chg_vddsafe_set(struct qpnp_chg_chip *chip, int voltage)
{
	u8 temp;

	if (voltage < QPNP_CHG_V_MIN_MV
			|| voltage > QPNP_CHG_V_MAX_MV) {
		pr_err("bad mV=%d asked to set\n", voltage);
		return -EINVAL;
	}
	temp = (voltage - QPNP_CHG_V_MIN_MV) / QPNP_CHG_V_STEP_MV;
	pr_debug("voltage=%d setting %02x\n", voltage, temp);
	return qpnp_chg_write(chip, &temp,
		chip->chgr_base + CHGR_VDD_SAFE, 1);
}

#define IBAT_TRIM_TGT_MA		500
#define IBAT_TRIM_OFFSET_MASK		0x7F
#define IBAT_TRIM_GOOD_BIT		BIT(7)
#define IBAT_TRIM_LOW_LIM		20
#define IBAT_TRIM_HIGH_LIM		114
#define IBAT_TRIM_MEAN			64

static void
qpnp_chg_trim_ibat(struct qpnp_chg_chip *chip, u8 ibat_trim)
{
	int ibat_now_ma, ibat_diff_ma, rc;
	struct qpnp_iadc_result i_result;
	enum qpnp_iadc_channels iadc_channel;

	iadc_channel = chip->use_external_rsense ?
				EXTERNAL_RSENSE : INTERNAL_RSENSE;
	rc = qpnp_iadc_read(chip->iadc_dev, iadc_channel, &i_result);
	if (rc) {
		pr_err("Unable to read bat rc=%d\n", rc);
		return;
	}

	ibat_now_ma = i_result.result_ua / 1000;

	if (qpnp_chg_is_ibat_loop_active(chip)) {
		ibat_diff_ma = ibat_now_ma - IBAT_TRIM_TGT_MA;

		if (abs(ibat_diff_ma) > 50) {
			ibat_trim += (ibat_diff_ma / 20);
			ibat_trim &= IBAT_TRIM_OFFSET_MASK;
			/* reject new ibat_trim if it is outside limits */
			if (!is_within_range(ibat_trim, IBAT_TRIM_LOW_LIM,
						IBAT_TRIM_HIGH_LIM))
				return;
		}
		ibat_trim |= IBAT_TRIM_GOOD_BIT;
		rc = qpnp_chg_write(chip, &ibat_trim,
				chip->buck_base + BUCK_CTRL_TRIM3, 1);
		if (rc)
			pr_err("failed to set IBAT_TRIM rc=%d\n", rc);

		pr_debug("ibat_now=%dmA, itgt=%dmA, ibat_diff=%dmA, ibat_trim=%x\n",
					ibat_now_ma, IBAT_TRIM_TGT_MA,
					ibat_diff_ma, ibat_trim);
	} else {
		pr_debug("ibat loop not active - cannot calibrate ibat\n");
	}
}

static int
qpnp_chg_input_current_settled(struct qpnp_chg_chip *chip)
{
	int rc, ibat_max_ma;
	u8 reg, chgr_sts, ibat_trim, i;

	chip->aicl_settled = true;

	/*
	 * Perform the ibat calibration.
	 * This is for devices which have a IBAT_TRIM error
	 * which can show IBAT_MAX out of spec.
	 */
	if (!chip->ibat_calibration_enabled)
		return 0;

	if (chip->type != SMBB)
		return 0;

	rc = qpnp_chg_read(chip, &reg,
			chip->buck_base + BUCK_CTRL_TRIM3, 1);
	if (rc) {
		pr_err("failed to read BUCK_CTRL_TRIM3 rc=%d\n", rc);
		return rc;
	}
	if (reg & IBAT_TRIM_GOOD_BIT) {
		pr_debug("IBAT_TRIM_GOOD bit already set. Quitting!\n");
		return 0;
	}
	ibat_trim = reg & IBAT_TRIM_OFFSET_MASK;

	if (!is_within_range(ibat_trim, IBAT_TRIM_LOW_LIM,
					IBAT_TRIM_HIGH_LIM)) {
		pr_debug("Improper ibat_trim value=%x setting to value=%x\n",
						ibat_trim, IBAT_TRIM_MEAN);
		ibat_trim = IBAT_TRIM_MEAN;
		rc = qpnp_chg_masked_write(chip,
				chip->buck_base + BUCK_CTRL_TRIM3,
				IBAT_TRIM_OFFSET_MASK, ibat_trim, 1);
		if (rc) {
			pr_err("failed to set ibat_trim to %x rc=%d\n",
						IBAT_TRIM_MEAN, rc);
			return rc;
		}
	}

	rc = qpnp_chg_read(chip, &chgr_sts,
				INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read interrupt sts rc=%d\n", rc);
		return rc;
	}
	if (!(chgr_sts & FAST_CHG_ON_IRQ)) {
		pr_debug("Not in fastchg\n");
		return rc;
	}

	/* save the ibat_max to restore it later */
	rc = qpnp_chg_ibatmax_get(chip, &ibat_max_ma);
	if (rc) {
		pr_debug("failed to save ibatmax rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_ibatmax_set(chip, IBAT_TRIM_TGT_MA);
	if (rc) {
		pr_err("failed to set ibatmax rc=%d\n", rc);
		return rc;
	}

	for (i = 0; i < 3; i++) {
		/*
		 * ibat settling delay - to make sure the BMS controller
		 * has sufficient time to sample ibat for the configured
		 * ibat_max
		 */
		msleep(20);
		if (qpnp_chg_is_ibat_loop_active(chip))
			qpnp_chg_trim_ibat(chip, ibat_trim);
		else
			pr_debug("ibat loop not active\n");

		/* read the adjusted ibat_trim for further adjustments */
		rc = qpnp_chg_read(chip, &ibat_trim,
			chip->buck_base + BUCK_CTRL_TRIM3, 1);
		if (rc) {
			pr_err("failed to read BUCK_CTRL_TRIM3 rc=%d\n", rc);
			break;
		}
	}

	/* restore IBATMAX */
	rc = qpnp_chg_ibatmax_set(chip, ibat_max_ma);
	if (rc)
		pr_err("failed to restore ibatmax rc=%d\n", rc);

	return rc;
}


#define BOOST_MIN_UV	4200000
#define BOOST_MAX_UV	5500000
#define BOOST_STEP_UV	50000
#define BOOST_MIN	16
#define N_BOOST_V	((BOOST_MAX_UV - BOOST_MIN_UV) / BOOST_STEP_UV + 1)
static int
qpnp_boost_vset(struct qpnp_chg_chip *chip, int voltage)
{
	u8 reg = 0;

	if (voltage < BOOST_MIN_UV || voltage > BOOST_MAX_UV) {
		pr_err("invalid voltage requested %d uV\n", voltage);
		return -EINVAL;
	}

	reg = DIV_ROUND_UP(voltage - BOOST_MIN_UV, BOOST_STEP_UV) + BOOST_MIN;

	pr_debug("voltage=%d setting %02x\n", voltage, reg);
	return qpnp_chg_write(chip, &reg, chip->boost_base + BOOST_VSET, 1);
}

static int
qpnp_boost_vget_uv(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 boost_reg;

	rc = qpnp_chg_read(chip, &boost_reg,
		 chip->boost_base + BOOST_VSET, 1);
	if (rc) {
		pr_err("failed to read BOOST_VSET rc=%d\n", rc);
		return rc;
	}

	if (boost_reg < BOOST_MIN) {
		pr_err("Invalid reading from 0x%x\n", boost_reg);
		return -EINVAL;
	}

	return BOOST_MIN_UV + ((boost_reg - BOOST_MIN) * BOOST_STEP_UV);
}

static void
qpnp_chg_set_appropriate_battery_current(struct qpnp_chg_chip *chip)
{
	unsigned int chg_current = chip->max_bat_chg_current;

	if (chip->bat_is_cool)
		chg_current = min(chg_current, chip->cool_bat_chg_ma);

	if (chip->bat_is_warm)
		chg_current = min(chg_current, chip->warm_bat_chg_ma);

	if (chip->use_step_charge)
		chg_current = min(chg_current, chip->step_charge_ma);

	if (chip->therm_lvl_sel != 0 && chip->thermal_mitigation)
		chg_current = min(chg_current,
			chip->thermal_mitigation[chip->therm_lvl_sel]);

	pr_debug("setting %d mA\n", chg_current);
	qpnp_chg_ibatmax_set(chip, chg_current);
}

static void
qpnp_batt_system_temp_level_set(struct qpnp_chg_chip *chip, int lvl_sel)
{
	if (lvl_sel >= 0 && lvl_sel < chip->thermal_levels) {
		chip->therm_lvl_sel = lvl_sel;
		qpnp_chg_set_appropriate_battery_current(chip);
	} else {
		pr_err("Unsupported level selected %d\n", lvl_sel);
	}
}

static bool
qpnp_update_ext_hi_state(struct qpnp_chg_chip *chip)
{
	int batt_mv;
	int batt_soc;
	bool ext_hi_temp = false;
	bool resuming = false;
	unsigned int cool_resume_mv;
	unsigned int warm_resume_mv;

	cool_resume_mv = chip->cool_bat_mv - chip->resume_delta_mv;
	warm_resume_mv = chip->warm_bat_mv - chip->resume_delta_mv;

	mutex_lock(&chip->update_ext_hi_state_lock);
	chip->bat_is_warm =
		chip->bat_therm_is_warm || chip->bat_hotspot_is_warm;

	batt_mv = get_prop_battery_voltage_now(chip) / 1000;
	batt_soc = get_prop_capacity(chip);

	if (chip->bat_is_cool) {
		if (chip->ext_hi_temp) {
			if (batt_mv <= cool_resume_mv) {
				ext_hi_temp = false;
				resuming = true;
			} else
				ext_hi_temp = true;
		} else if ((batt_mv > chip->cool_bat_mv))
			ext_hi_temp = true;
	} else if (chip->bat_is_warm) {
		if (chip->warm_bat_soc) {
			if (chip->ext_hi_temp) {
				if (batt_soc <= (chip->warm_bat_soc
					- chip->resume_delta_soc)) {
					ext_hi_temp = false;
					resuming = true;
				} else
					ext_hi_temp = true;
			} else if (batt_soc > chip->warm_bat_soc)
				ext_hi_temp = true;
		} else {
			if (chip->ext_hi_temp) {
				if (batt_mv <= warm_resume_mv) {
					ext_hi_temp = false;
					resuming = true;
				} else
					ext_hi_temp = true;
			} else if ((batt_mv > chip->warm_bat_mv))
				ext_hi_temp = true;
		}
	}

	if (resuming) {
		chip->resuming_charging = true;
		qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
	} else
		chip->resuming_charging = false;

	chip->ext_hi_temp = ext_hi_temp;
	qpnp_chg_set_appropriate_vddmax(chip);
	qpnp_chg_set_appropriate_battery_current(chip);
	qpnp_chg_set_appropriate_vbatdet(chip);
	chip->resuming_charging = false;
	qpnp_chg_charge_en(chip, (!chip->charging_disabled &&
				  !chip->out_of_temp &&
				  !chip->ext_hi_temp &&
				  (chip->chrg_ocv_state !=
				   CHRG_OCV_OCV_WAIT)));

	mutex_unlock(&chip->update_ext_hi_state_lock);

	return resuming;
}

#define CHG_SHOW_MAX_SIZE 50
static ssize_t hotspot_temp_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	int err;
	long hsp;
	bool is_hotspot = false;
	int temp;

	err = kstrtol(buf, 0, &hsp);
	if (err) {
		pr_err("Invalid hotspot temp value = %d\n", (int)hsp);
		return err;
	}

	if (chip->use_default_batt_values || !get_prop_batt_present(chip)) {
		pr_err("hotspot disabled\n");
		return -ENODEV;
	}

	chip->bat_hotspot_temp = hsp;

	temp = get_prop_batt_temp(chip);
	temp *= 100;

	if ((temp > chip->cool_bat_decidegc) &&
	    (chip->bat_hotspot_temp > temp) &&
	    (chip->bat_hotspot_temp >= chip->bat_hotspot_thrs)) {
		pr_debug("hotspot detected:%d, threshold=%d\n",
			 chip->bat_hotspot_temp, chip->bat_hotspot_thrs);
		is_hotspot = true;
	} else {
		pr_debug("hotspot cleared:%d, threshold=%d\n",
		       chip->bat_hotspot_temp, chip->bat_hotspot_thrs);
		is_hotspot = false;
	}


	if (chip->bat_hotspot_is_warm ^ is_hotspot) {
		chip->bat_hotspot_is_warm = is_hotspot;
		qpnp_update_ext_hi_state(chip);
	}

	return count;
}

static ssize_t hotspot_temp_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	return scnprintf(buf, CHG_SHOW_MAX_SIZE, "%d\n",
			 chip->bat_hotspot_temp);
}

static DEVICE_ATTR(hotspot_temp, 0664,
		   hotspot_temp_show,
		   hotspot_temp_store);

/* OTG regulator operations */
static int
qpnp_chg_regulator_otg_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return switch_usb_to_host_mode(chip);
}

static int
qpnp_chg_regulator_otg_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return switch_usb_to_charge_mode(chip);
}

static int
qpnp_chg_regulator_otg_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_chg_is_otg_en_set(chip);
}

static int
qpnp_chg_regulator_boost_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;

	if (qpnp_chg_is_usb_chg_plugged_in(chip) &&
			(chip->flags & BOOST_FLASH_WA)) {
		qpnp_chg_usb_suspend_enable(chip, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}
		if (chip->type != SMBBP) {
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + COMP_OVR1,
				0xFF,
				0x2F, 1);
			if (rc) {
				pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
				return rc;
			}
		}
	}

	return qpnp_chg_masked_write(chip,
		chip->boost_base + BOOST_ENABLE_CONTROL,
		BOOST_PWR_EN,
		BOOST_PWR_EN, 1);
}

/* Boost regulator operations */
#define ABOVE_VBAT_WEAK		BIT(1)
static int
qpnp_chg_regulator_boost_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc;
	u8 vbat_sts;

	rc = qpnp_chg_masked_write(chip,
		chip->boost_base + BOOST_ENABLE_CONTROL,
		BOOST_PWR_EN,
		0, 1);
	if (rc) {
		pr_err("failed to disable boost rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_chg_read(chip, &vbat_sts,
			chip->chgr_base + CHGR_VBAT_STATUS, 1);
	if (rc) {
		pr_err("failed to read bat sts rc=%d\n", rc);
		return rc;
	}

	if (!(vbat_sts & ABOVE_VBAT_WEAK) && (chip->flags & BOOST_FLASH_WA)) {
		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + COMP_OVR1,
			0xFF,
			0x20, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}

		usleep(2000);

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + COMP_OVR1,
			0xFF,
			0x00, 1);
		if (rc) {
			pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
			return rc;
		}
	}

	if (qpnp_chg_is_usb_chg_plugged_in(chip)
			&& (chip->flags & BOOST_FLASH_WA)) {
		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);
		if (rc) {
			pr_err("failed to write SEC_ACCESS rc=%d\n", rc);
			return rc;
		}
		if (chip->type != SMBBP) {
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + COMP_OVR1,
				0xFF,
				0x00, 1);
			if (rc) {
				pr_err("failed to write COMP_OVR1 rc=%d\n", rc);
				return rc;
			}
		}
		usleep(1000);

		qpnp_chg_usb_suspend_enable(chip, 0);
	}

	return rc;
}

static int
qpnp_chg_regulator_boost_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_chg_is_boost_en_set(chip);
}

static int
qpnp_chg_regulator_boost_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	int uV = min_uV;
	int rc;
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	if (uV < BOOST_MIN_UV && max_uV >= BOOST_MIN_UV)
		uV = BOOST_MIN_UV;


	if (uV < BOOST_MIN_UV || uV > BOOST_MAX_UV) {
		pr_err("request %d uV is out of bounds\n", uV);
		return -EINVAL;
	}

	*selector = DIV_ROUND_UP(uV - BOOST_MIN_UV, BOOST_STEP_UV);
	if ((*selector * BOOST_STEP_UV + BOOST_MIN_UV) > max_uV) {
		pr_err("no available setpoint [%d, %d] uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	rc = qpnp_boost_vset(chip, uV);

	return rc;
}

static int
qpnp_chg_regulator_boost_get_voltage(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return qpnp_boost_vget_uv(chip);
}

static int
qpnp_chg_regulator_boost_list_voltage(struct regulator_dev *rdev,
			unsigned selector)
{
	if (selector >= N_BOOST_V)
		return 0;

	return BOOST_MIN_UV + (selector * BOOST_STEP_UV);
}

static struct regulator_ops qpnp_chg_otg_reg_ops = {
	.enable			= qpnp_chg_regulator_otg_enable,
	.disable		= qpnp_chg_regulator_otg_disable,
	.is_enabled		= qpnp_chg_regulator_otg_is_enabled,
};

static struct regulator_ops qpnp_chg_boost_reg_ops = {
	.enable			= qpnp_chg_regulator_boost_enable,
	.disable		= qpnp_chg_regulator_boost_disable,
	.is_enabled		= qpnp_chg_regulator_boost_is_enabled,
	.set_voltage		= qpnp_chg_regulator_boost_set_voltage,
	.get_voltage		= qpnp_chg_regulator_boost_get_voltage,
	.list_voltage		= qpnp_chg_regulator_boost_list_voltage,
};

static int
qpnp_chg_bat_if_batfet_reg_enabled(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	u8 reg = 0;

	if (!chip->bat_if_base)
		return rc;

	if (chip->type == SMBB)
		rc = qpnp_chg_read(chip, &reg,
				chip->bat_if_base + CHGR_BAT_IF_SPARE, 1);
	else
		rc = qpnp_chg_read(chip, &reg,
			chip->bat_if_base + CHGR_BAT_IF_BATFET_CTRL4, 1);

	if (rc) {
		pr_err("failed to read batt_if rc=%d\n", rc);
		return rc;
	}

	if ((reg & BATFET_LPM_MASK) == BATFET_NO_LPM)
		return 1;

	return 0;
}

static int
qpnp_chg_regulator_batfet_enable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc = 0;

	mutex_lock(&chip->batfet_vreg_lock);
	/* Only enable if not already enabled */
	if (!qpnp_chg_bat_if_batfet_reg_enabled(chip)) {
		rc = qpnp_chg_regulator_batfet_set(chip, 1);
		if (rc)
			pr_err("failed to write to batt_if rc=%d\n", rc);
	}

	chip->batfet_ext_en = true;
	mutex_unlock(&chip->batfet_vreg_lock);

	return rc;
}

static int
qpnp_chg_regulator_batfet_disable(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);
	int rc = 0;

	mutex_lock(&chip->batfet_vreg_lock);
	/* Don't allow disable if charger connected */
	if (!qpnp_chg_is_usb_chg_plugged_in(chip) &&
			!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		rc = qpnp_chg_regulator_batfet_set(chip, 0);
		if (rc)
			pr_err("failed to write to batt_if rc=%d\n", rc);
	}

	chip->batfet_ext_en = false;
	mutex_unlock(&chip->batfet_vreg_lock);

	return rc;
}

static int
qpnp_chg_regulator_batfet_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_chg_chip *chip = rdev_get_drvdata(rdev);

	return chip->batfet_ext_en;
}

static struct regulator_ops qpnp_chg_batfet_vreg_ops = {
	.enable			= qpnp_chg_regulator_batfet_enable,
	.disable		= qpnp_chg_regulator_batfet_disable,
	.is_enabled		= qpnp_chg_regulator_batfet_is_enabled,
};

#define MIN_DELTA_MV_TO_INCREASE_VDD_MAX	8
#define MAX_DELTA_VDD_MAX_MV			80
#define VDD_MAX_CENTER_OFFSET			4
static void
qpnp_chg_adjust_vddmax(struct qpnp_chg_chip *chip, int vbat_mv)
{
#ifdef QCOM_WA
	int delta_mv, closest_delta_mv, sign;

	delta_mv = chip->max_voltage_mv - VDD_MAX_CENTER_OFFSET - vbat_mv;
	if (delta_mv > 0 && delta_mv < MIN_DELTA_MV_TO_INCREASE_VDD_MAX) {
		pr_debug("vbat is not low enough to increase vdd\n");
		return;
	}

	sign = delta_mv > 0 ? 1 : -1;
	closest_delta_mv = ((delta_mv + sign * QPNP_CHG_BUCK_TRIM1_STEP / 2)
			/ QPNP_CHG_BUCK_TRIM1_STEP) * QPNP_CHG_BUCK_TRIM1_STEP;
	pr_debug("max_voltage = %d, vbat_mv = %d, delta_mv = %d, closest = %d\n",
			chip->max_voltage_mv, vbat_mv,
			delta_mv, closest_delta_mv);
	chip->delta_vddmax_mv = clamp(chip->delta_vddmax_mv + closest_delta_mv,
			-MAX_DELTA_VDD_MAX_MV, MAX_DELTA_VDD_MAX_MV);
	pr_debug("using delta_vddmax_mv = %d\n", chip->delta_vddmax_mv);
	qpnp_chg_set_appropriate_vddmax(chip);
#else
	return;
#endif
}

static int calculate_pc(struct qpnp_chg_chip *chip, int ocv_uv,
							int batt_temp)
{
	int pc;

	pc = interpolate_pc(chip->pc_temp_ocv_lut,
			batt_temp / 10, ocv_uv / 1000);
	pr_debug("pc = %u %% for ocv = %d uv batt_temp = %d\n",
					pc, ocv_uv, batt_temp);
	return pc;
}

void qpnp_chg_calculate_chrg_fcc(struct qpnp_chg_chip *chip,
				 int ocv_mv,
				 int ocv_cc_uah,
				 int full_cc_uah,
				 int full_temp_c)
{
	struct timeval tv;
	int ocv_soc = calculate_pc(chip, (ocv_mv * 1000), full_temp_c);
	unsigned int chrg_ocv_fcc = 100 * abs(full_cc_uah - ocv_cc_uah);
	pr_info("ocv_mv = %d, ocv_soc = %d, ocv_cc = %d, full_cc = %d\n",
		ocv_mv, ocv_soc, ocv_cc_uah, full_cc_uah);

	/* Calculate Percentage of Initial FCC */
	chrg_ocv_fcc /= (100 - ocv_soc);
	chrg_ocv_fcc /= 10;
	chrg_ocv_fcc /= chip->fcc_mah;
	pr_info("Actual chrg_ocv_fcc = %d\n", chrg_ocv_fcc);

	/* Round up, 5 percent Resolution */
	if ((chrg_ocv_fcc % 10) > 5) {
		chrg_ocv_fcc /= 10;
		chrg_ocv_fcc *= 10;
		chrg_ocv_fcc += 10;
	} else if (chrg_ocv_fcc % 10) {
		chrg_ocv_fcc /= 10;
		chrg_ocv_fcc *= 10;
		chrg_ocv_fcc += 5;
	}

	/* Max of 100 */
	if (chrg_ocv_fcc > 100)
		chrg_ocv_fcc = 100;

	/* Mark Time Stamp */
	do_gettimeofday(&tv);
	chrg_timestamp = tv.tv_sec;

	bms_chrg_capacity = (int)chrg_ocv_fcc;
	bms_chrg_capacity_temp = full_temp_c;

	pr_info("bms_chrg_capacity = %d, bms_chrg_capacity_temp = %d\n",
		bms_chrg_capacity, bms_chrg_capacity_temp);
}

#define CHRG_OCV_WORK_MS 1000
static void
qpnp_chrg_ocv_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, chrg_ocv_work);
	int ibat_ma, vbat_mv = 0;
	int cc;
	int percent_soc;
	int batt_temp;
	int curr_time;
#ifndef CONFIG_RTC_INTF_ALARM
	struct timespec ts;

	get_monotonic_boottime(&ts);
	curr_time = ts.tv_sec;
#else
	curr_time = ktime_to_timespec(alarm_get_elapsed_realtime()).tv_sec;
#endif

	if (!chip->bms_psy || !qpnp_chg_is_usb_chg_plugged_in(chip)) {
		chip->chrg_ocv_state = CHRG_OCV_NO_CHRG;
		return;
	}

	ibat_ma = get_prop_current_now(chip) / 1000;
	vbat_mv = get_prop_battery_voltage_now(chip) / 1000;
	percent_soc = get_prop_capacity(chip);
	batt_temp = get_prop_batt_temp(chip);
	cc = get_prop_charge_counter(chip);

	if (chip->chrg_ocv_state == CHRG_OCV_OCV_WAIT) {
		if (chip->chrg_ocv_time == 0) {
			chip->chrg_ocv_cc_bf_uah = cc;
			chip->chrg_ocv_time = curr_time;
			if ((percent_soc > chip->chrg_ocv_high_soc_thr) ||
			    (percent_soc < chip->chrg_ocv_low_soc_thr)) {
				chip->chrg_ocv_time = 0;
				chip->chrg_ocv_state = CHRG_OCV_NO_CHRG;
				qpnp_chg_charge_en(chip,
						   (!chip->charging_disabled &&
						    !chip->out_of_temp &&
						    !chip->ext_hi_temp));
				pr_info("High SOC or Low SOC - NO CHRG\n");
				goto reschedule;
			}
		}
		if (curr_time < (chip->chrg_ocv_ocv_time +
				 chip->chrg_ocv_time)) {
			pr_debug("OCV = %d, ibat_ma = %d\n", vbat_mv, ibat_ma);
			goto reschedule;
		} else {
			chip->chrg_ocv_time = 0;
			chip->chrg_ocv_cc_af_uah = cc;
			chip->chrg_ocv_bv_mv = vbat_mv;
			pr_info("chrg_ocv_bv_mv = %d\n", chip->chrg_ocv_bv_mv);
			pr_debug("ibat_ma = %d\n", ibat_ma);
			pr_debug("af_uah = %d\n", chip->chrg_ocv_cc_af_uah);
			pr_debug("bf_uah = %d\n", chip->chrg_ocv_cc_bf_uah);
			qpnp_chg_charge_en(chip, (!chip->charging_disabled &&
						  !chip->out_of_temp &&
						  !chip->ext_hi_temp));
			if ((abs(ibat_ma) >= chip->chrg_ocv_dchrg_thr_ma) ||
			    (abs(chip->chrg_ocv_cc_af_uah -
				 chip->chrg_ocv_cc_bf_uah) >=
			     chip->chrg_ocv_cc_thr_uah)){
				chip->chrg_ocv_state = CHRG_OCV_NO_CHRG;
				pr_info("CHRG_OCV_OCV_WAIT - NO CHRG\n");
				return;
			} else {
				pr_debug("CHRG_OCV_OCV_WAIT - Done\n");
				chip->chrg_ocv_state = CHRG_OCV_FULL_WAIT;
			}
		}
	}

	if ((chip->chrg_ocv_state == CHRG_OCV_FULL_WAIT) && chip->chg_done) {
		qpnp_chg_calculate_chrg_fcc(chip,
					    chip->chrg_ocv_bv_mv,
					    chip->chrg_ocv_cc_af_uah,
					    chip->chrg_ocv_cc_ef_uah,
					    batt_temp);
		chip->chrg_ocv_state = CHRG_OCV_NO_CHRG;
		power_supply_changed(&chip->batt_psy);
		return;
	}

reschedule:
	schedule_delayed_work(&chip->chrg_ocv_work,
		msecs_to_jiffies(CHRG_OCV_WORK_MS));
	return;
}
#define CONSECUTIVE_COUNT	3
#define VBATDET_MAX_ERR_MV	50
#define CV_DELTA 75
static void
qpnp_eoc_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, eoc_work);
	static int count;
	static int vbat_low_count;
	int ibat_ma, vbat_mv, rc = 0;
	struct timespec bootup_time;
	unsigned long float_timestamp;
	u8 batt_sts = 0, buck_sts = 0, chg_sts = 0;

#ifdef QCOM_WA
	bool vbat_lower_than_vbatdet;
#endif
	static int count_taper;

	get_monotonic_boottime(&bootup_time);
	float_timestamp = bootup_time.tv_sec;
	pm_stay_awake(chip->dev);

	if ((chip->step_charge_mv < chip->max_voltage_mv) &&
	    (chip->step_charge_mv > chip->cutoff_mv) &&
	    (chip->step_charge_soc == 0)) {
		if ((get_prop_battery_voltage_now(chip)/1000) >=
			chip->step_charge_mv) {
			pr_debug("Step Rate used Batt V = %d\n",
				(get_prop_battery_voltage_now(chip)/1000));
			chip->use_step_charge = true;
		} else {
			pr_debug("Step Rate NOT used Batt V = %d\n",
				(get_prop_battery_voltage_now(chip)/1000));
			chip->use_step_charge = false;
		}
		qpnp_chg_set_appropriate_battery_current(chip);
	} else if ((chip->step_charge_soc < 100) &&
		(chip->step_charge_soc > 0)) {
		if (get_prop_capacity(chip) >= chip->step_charge_soc) {
			pr_debug("Step Rate used SOC = %d\n",
				 get_prop_capacity(chip));
			chip->use_step_charge = true;
			chip->use_max_vdd = true;
			count_taper = 0;
		} else {
			pr_debug("Step Rate NOT used SOC = %d\n",
				get_prop_capacity(chip));
			chip->use_step_charge = false;
		}
		qpnp_chg_set_appropriate_battery_current(chip);
	}

	qpnp_chg_charge_en(chip, (!chip->charging_disabled &&
				  !chip->out_of_temp &&
				  !chip->ext_hi_temp &&
				  (chip->chrg_ocv_state != CHRG_OCV_OCV_WAIT)));

	if (chip->chrg_ocv_state == CHRG_OCV_OCV_WAIT)
		goto check_again_later;

	rc = qpnp_chg_read(chip, &batt_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc) {
		pr_err("failed to read batt_if rc=%d\n", rc);
		return;
	}

	rc = qpnp_chg_read(chip, &buck_sts, INT_RT_STS(chip->buck_base), 1);
	if (rc) {
		pr_err("failed to read buck rc=%d\n", rc);
		return;
	}

	rc = qpnp_chg_read(chip, &chg_sts, INT_RT_STS(chip->chgr_base), 1);
	if (rc) {
		pr_err("failed to read chg_sts rc=%d\n", rc);
		return;
	}

	pr_debug("chgr: 0x%x, bat_if: 0x%x, buck: 0x%x\n",
		chg_sts, batt_sts, buck_sts);

	if (!qpnp_chg_is_usb_chg_plugged_in(chip) &&
			!qpnp_chg_is_dc_chg_plugged_in(chip)) {
		pr_debug("no chg connected, stopping\n");
		goto stop_eoc;
	}

	if (((batt_sts & BAT_FET_ON_IRQ) && (chg_sts & FAST_CHG_ON_IRQ
					     || chg_sts & TRKL_CHG_ON_IRQ)) ||
	    (chip->bat_is_cool || chip->bat_is_warm)) {
		ibat_ma = get_prop_current_now(chip) / 1000;
		vbat_mv = get_prop_battery_voltage_now(chip) / 1000;

		pr_debug("ibat_ma = %d vbat_mv = %d term_current_ma = %d\n",
				ibat_ma, vbat_mv, chip->term_current);

#ifdef QCOM_WA
		vbat_lower_than_vbatdet = !(chg_sts & VBAT_DET_LOW_IRQ);
		if (vbat_lower_than_vbatdet && vbat_mv <
				(chip->max_voltage_mv - chip->resume_delta_mv
				 - chip->vbatdet_max_err_mv)) {
			vbat_low_count++;
			pr_debug("woke up too early vbat_mv = %d, max_mv = %d, resume_mv = %d tolerance_mv = %d low_count = %d\n",
					vbat_mv, chip->max_voltage_mv,
					chip->resume_delta_mv,
					chip->vbatdet_max_err_mv,
					vbat_low_count);
			if (vbat_low_count >= CONSECUTIVE_COUNT) {
				pr_debug("woke up too early stopping\n");
				qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
				goto stop_eoc;
			} else {
				goto check_again_later;
			}
		} else {
			vbat_low_count = 0;
		}
#endif
		if (chip->bat_is_cool) {
			chip->use_max_vdd = true;
			count_taper = 0;
			if (qpnp_update_ext_hi_state(chip))
				goto stop_eoc;
		} else if (!chip->use_step_charge &&
			   (count_taper < CONSECUTIVE_COUNT) &&
			   (chip->step_charge_mv < chip->max_voltage_mv) &&
			   (chip->step_charge_mv > chip->cutoff_mv) &&
			   (chip->step_charge_soc < 100) &&
			   (chip->step_charge_soc > 0)) {
			if (!(buck_sts & VDD_LOOP_IRQ) ||
			    (vbat_mv < (chip->step_charge_mv - CV_DELTA))) {
				pr_debug("Step Taper Not in CV\n");
				count_taper = 0;
				chip->use_max_vdd = false;
			} else if ((ibat_ma * -1) > chip->step_charge_ma) {
				pr_debug("Not at Step Taper!\n");
				count_taper = 0;
				chip->use_max_vdd = false;
			} else if (ibat_ma > 0) {
				pr_debug("system demand increased\n");
				count_taper = 0;
				chip->use_max_vdd = false;
			} else {
				if (count_taper >= CONSECUTIVE_COUNT) {
					chip->use_max_vdd = true;
				} else {
					count_taper += 1;
					chip->use_max_vdd = false;
					pr_debug("Step Taper count = %d\n",
						 count_taper);
				}
			}
		}
		qpnp_chg_set_appropriate_vddmax(chip);

		if (chip->bat_is_warm)
			if (qpnp_update_ext_hi_state(chip))
				goto stop_eoc;

		if ((buck_sts & VDD_LOOP_IRQ) &&
		    (vbat_mv >= (chip->max_voltage_mv - CV_DELTA)))
			qpnp_chg_adjust_vddmax(chip, vbat_mv);

		if (!(buck_sts & VDD_LOOP_IRQ) ||
		    (vbat_mv < (chip->max_voltage_mv - CV_DELTA))) {
			pr_debug("Not in CV\n");
			count = 0;
			chip->float_timer_start = false;
		} else if (((ibat_ma * -1) > chip->term_current) &&
			   (!chip->float_timer_start)) {
			pr_debug("Not at EOC, battery current too high\n");
			count = 0;
			chip->float_timer_start = false;
		} else if (ibat_ma > 0) {
			pr_debug("Charging but system demand increased\n");
			count = 0;
			chip->float_timer_start = false;
		} else if (!chip->float_timer_start) {
			if (count == CONSECUTIVE_COUNT) {
				if (!chip->bat_is_cool && !chip->bat_is_warm) {
					pr_info("Start of Float Charging\n");
					chip->chg_done = true;
					chip->float_charge_start_time =
						float_timestamp;
					chip->float_timer_start = true;
				} else {
					pr_info("stop charging: battery is %s, vddmax = %d reached\n",
						chip->bat_is_cool
						? "cool" : "warm",
						qpnp_chg_vddmax_get(chip));
				}
				chip->delta_vddmax_mv = 0;
				qpnp_chg_set_appropriate_vddmax(chip);
				chip->maint_chrg = true;
				chip->chrg_ocv_cc_ef_uah =
				get_prop_charge_counter(chip);
#ifdef QCOM_WA
				/* sleep for a second before enabling */
				msleep(2000);
				qpnp_chg_charge_en(chip,
						!chip->charging_disabled);
#endif
				pr_debug("psy changed batt_psy\n");
				power_supply_changed(&chip->batt_psy);
				if (!chip->float_timer_start)
					goto stop_eoc;
				else
					goto check_again_later;
			} else {
				count += 1;
				pr_debug("EOC count = %d\n", count);
			}
		} else {
			if (((float_timestamp - chip->float_charge_start_time)
			     >= chip->float_charge_timer)) {
				pr_info("End of Charging\n");
				chip->float_timer_start = false;
				qpnp_chg_enable_irq(&chip->chg_vbatdet_lo);
				qpnp_chg_charge_en(chip, 0);
				goto stop_eoc;
			}
		}
	} else {
		pr_debug("not charging\n");
		goto stop_eoc;
	}

check_again_later:
	schedule_delayed_work(&chip->eoc_work,
		msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
	return;

stop_eoc:
	vbat_low_count = 0;
	count = 0;
	count_taper = 0;
	chip->float_charge_start_time = 0;
	chip->float_timer_start = false;
	pm_relax(chip->dev);
}

static void
qpnp_chg_insertion_ocv_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, insertion_ocv_work);
	u8 bat_if_sts = 0, charge_en = 0;
	int rc;

	chip->insertion_ocv_uv = get_prop_battery_voltage_now(chip);

	rc = qpnp_chg_read(chip, &bat_if_sts, INT_RT_STS(chip->bat_if_base), 1);
	if (rc)
		pr_err("failed to read bat_if sts %d\n", rc);

	rc = qpnp_chg_read(chip, &charge_en,
			chip->chgr_base + CHGR_CHG_CTRL, 1);
	if (rc)
		pr_err("failed to read bat_if sts %d\n", rc);

	pr_debug("batfet sts = %02x, charge_en = %02x ocv = %d\n",
			bat_if_sts, charge_en, chip->insertion_ocv_uv);
	qpnp_chg_charge_en(chip, !chip->charging_disabled);
	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
}

static void
qpnp_chg_soc_check_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, soc_check_work);

	get_prop_capacity(chip);
}

#define HYSTERISIS_DECIDEGC 20
#define MAX_TEMP_HOT 600
#define MAX_TEMP_COLD -200
static void
qpnp_chg_adc_notification(enum qpnp_tm_state state, void *ctx)
{
	struct qpnp_chg_chip *chip = ctx;
	bool bat_warm = 0, bat_cool = 0, out_temp = 0;
	int temp, batt_volt_mv;

	if (state >= ADC_TM_STATE_NUM) {
		pr_err("invalid notification %d\n", state);
		return;
	}

	temp = get_prop_batt_temp(chip);
	batt_volt_mv = get_prop_battery_voltage_now(chip) / 1000;

	pr_debug("batt_v_mv = %d temp = %d state = %s\n", batt_volt_mv, temp,
			state == ADC_TM_WARM_STATE ? "warm" : "cool");

	if (state == ADC_TM_WARM_STATE) {
		if (temp > MAX_TEMP_HOT) {
			/* warm to Hot */
			bat_warm = true;
			bat_cool = false;
			out_temp = true;
			chip->adc_param.low_temp =
				MAX_TEMP_HOT - HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
				ADC_TM_COOL_THR_ENABLE;
		} else if (temp >= chip->warm_bat_decidegc) {
			/* Normal to warm */
			bat_warm = true;
			bat_cool = false;
			out_temp = false;
			chip->adc_param.low_temp =
				chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC;
			chip->adc_param.high_temp = MAX_TEMP_HOT;
			chip->adc_param.state_request =
				ADC_TM_HIGH_LOW_THR_ENABLE;
		} else if (temp >=
				chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC){
			/* Cool to normal */
			bat_warm = false;
			bat_cool = false;
			out_temp = false;
			chip->adc_param.low_temp = chip->cool_bat_decidegc;
			chip->adc_param.high_temp = chip->warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		} else if (temp >
				MAX_TEMP_COLD + HYSTERISIS_DECIDEGC){
			/* Cold to Cool */
			bat_warm = false;
			bat_cool = true;
			out_temp = false;
			chip->adc_param.low_temp = MAX_TEMP_COLD;
			chip->adc_param.high_temp =
				chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		}
	} else {
		if (temp < MAX_TEMP_COLD) {
			/* Cool to cold */
			bat_warm = false;
			bat_cool = true;
			out_temp = true;
			chip->adc_param.high_temp =
				MAX_TEMP_COLD + HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
				ADC_TM_WARM_THR_ENABLE;
		} else if (temp <= chip->cool_bat_decidegc) {
			/* Normal to cool */
			bat_warm = false;
			bat_cool = true;
			out_temp = false;
			chip->adc_param.low_temp = MAX_TEMP_COLD;
			chip->adc_param.high_temp =
				chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		} else if (temp <=
				chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC){
			/* Warm to normal */
			bat_warm = false;
			bat_cool = false;
			out_temp = false;
			chip->adc_param.low_temp = chip->cool_bat_decidegc;
			chip->adc_param.high_temp = chip->warm_bat_decidegc;
			chip->adc_param.state_request =
					ADC_TM_HIGH_LOW_THR_ENABLE;
		} else if (temp <
				MAX_TEMP_HOT - HYSTERISIS_DECIDEGC){
			/* Hot to Warm */
			bat_warm = true;
			bat_cool = false;
			out_temp = false;
			chip->adc_param.low_temp =
				chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC;
			chip->adc_param.high_temp = MAX_TEMP_HOT;
			chip->adc_param.state_request =
				ADC_TM_HIGH_LOW_THR_ENABLE;
		}
	}

	if (chip->bat_is_cool ^ bat_cool ||
	    chip->bat_therm_is_warm ^ bat_warm ||
	    chip->out_of_temp ^ out_temp) {
		chip->bat_is_cool = bat_cool;
		chip->bat_therm_is_warm = bat_warm;
		chip->out_of_temp = out_temp;

		qpnp_update_ext_hi_state(chip);
	}

	pr_debug("warm %d, cool %d, low = %d deciDegC, high = %d deciDegC\n",
			chip->bat_therm_is_warm, chip->bat_is_cool,
			chip->adc_param.low_temp, chip->adc_param.high_temp);

	if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev, &chip->adc_param))
		pr_err("request ADC error\n");
}

/**
 * update_heartbeat - internal function to update userspace
 *              per update_time minutes
 *
 */
#define UPDATE_HEARTBEAT_MS		60000
#define DEMO_MODE_MAX_SOC 35
#define DEMO_MODE_HYS_SOC 5
static void update_heartbeat(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct qpnp_chg_chip *chip = container_of(dwork,
				struct qpnp_chg_chip, update_heartbeat_work);
	int temp = 0;
	int usb_present, host_mode;
	union power_supply_propval ret = {1,};

	if (chip->demo_mode) {
		pr_warn("Battery in Demo Mode charging Limited\n");
		if (!chip->demo_suspend_usb &&
		    (get_prop_capacity(chip) >= DEMO_MODE_MAX_SOC))
			qpnp_chg_demo_usb_suspend_enable(chip, true);
		else if (chip->demo_suspend_usb &&
			 (get_prop_capacity(chip) <=
			  (DEMO_MODE_MAX_SOC - DEMO_MODE_HYS_SOC)))
			qpnp_chg_demo_usb_suspend_enable(chip, false);
	}

	/*
	 * In pm8110/pm8226 there is automated BTM which takes care of
	 * cool and warm updates of comparators
	 */
	temp = get_prop_batt_temp(chip);

	if (temp >= MAX_TOLERABLE_BATT_TEMP_DDC) {
		pr_err("BATT_TEMP= %d >= 68degC, device will be shut-down\n",
		       temp);
		power_supply_changed(&chip->batt_psy);
	}
	usb_present = qpnp_chg_is_usb_chg_plugged_in(chip);
	host_mode = qpnp_chg_is_otg_en_set(chip);
	if (chip->usb_psy)
		chip->usb_psy->get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_LOW_POWER, &ret);

	/* Handle Missed Disconnects in the heartbeat */
	if (chip->usb_present && !usb_present) {
		pr_err("Missed USB_IN irq, hostmode = %d\n", host_mode);
		qpnp_chg_usb_usbin_valid_irq_handler(chip->usbin_valid.irq,
							chip);
	} else if (!usb_present && !host_mode && !ret.intval) {
		pr_err("USB PSY not in low power without being connected\n");
		if (!chip->usblpm_dropbox_sent) {
			dropbox_queue_event_text(DROPBOX_CHARGING_ISSUE,
				USBLPM_DROPBOX_MSG, strlen(USBLPM_DROPBOX_MSG));
			chip->usblpm_dropbox_sent = true;
		}
	} else if (usb_present)
		qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);

	schedule_delayed_work(&chip->update_heartbeat_work,
		msecs_to_jiffies(UPDATE_HEARTBEAT_MS));
}

#define MIN_COOL_TEMP	-300
#define MAX_WARM_TEMP	1000

static int
qpnp_chg_configure_jeita(struct qpnp_chg_chip *chip,
		enum power_supply_property psp, int temp_degc)
{
	int rc = 0;

	if ((temp_degc < MIN_COOL_TEMP) || (temp_degc > MAX_WARM_TEMP)) {
		pr_err("Bad temperature request %d\n", temp_degc);
		return -EINVAL;
	}

	mutex_lock(&chip->jeita_configure_lock);
	switch (psp) {
	case POWER_SUPPLY_PROP_COOL_TEMP:
		if (temp_degc >=
			(chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC)) {
			pr_err("Can't set cool %d higher than warm %d - hysterisis %d\n",
					temp_degc, chip->warm_bat_decidegc,
					HYSTERISIS_DECIDEGC);
			rc = -EINVAL;
			goto mutex_unlock;
		}
		if (chip->bat_is_cool)
			chip->adc_param.high_temp =
				temp_degc + HYSTERISIS_DECIDEGC;
		else if (!chip->bat_therm_is_warm)
			chip->adc_param.low_temp = temp_degc;

		chip->cool_bat_decidegc = temp_degc;
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		if (temp_degc <=
			(chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC)) {
			pr_err("Can't set warm %d higher than cool %d + hysterisis %d\n",
					temp_degc, chip->warm_bat_decidegc,
					HYSTERISIS_DECIDEGC);
			rc = -EINVAL;
			goto mutex_unlock;
		}
		if (chip->bat_therm_is_warm)
			chip->adc_param.low_temp =
				temp_degc - HYSTERISIS_DECIDEGC;
		else if (!chip->bat_is_cool)
			chip->adc_param.high_temp = temp_degc;

		chip->warm_bat_decidegc = temp_degc;
		break;
	default:
		rc = -EINVAL;
		goto mutex_unlock;
	}

	schedule_work(&chip->adc_measure_work);

mutex_unlock:
	mutex_unlock(&chip->jeita_configure_lock);
	return rc;
}

#define POWER_STAGE_REDUCE_CHECK_PERIOD_SECONDS		20
#define POWER_STAGE_REDUCE_MAX_VBAT_UV			3900000
#define POWER_STAGE_REDUCE_MIN_VCHG_UV			4800000
#define POWER_STAGE_SEL_MASK				0x0F
#define POWER_STAGE_REDUCED				0x01
#define POWER_STAGE_DEFAULT				0x0F
static bool
qpnp_chg_is_power_stage_reduced(struct qpnp_chg_chip *chip)
{
	int rc;
	u8 reg;

	rc = qpnp_chg_read(chip, &reg,
				 chip->buck_base + CHGR_BUCK_PSTG_CTRL,
				 1);
	if (rc) {
		pr_err("Error %d reading power stage register\n", rc);
		return false;
	}

	if ((reg & POWER_STAGE_SEL_MASK) == POWER_STAGE_DEFAULT)
		return false;

	return true;
}

static int
qpnp_chg_power_stage_set(struct qpnp_chg_chip *chip, bool reduce)
{
	int rc;
	u8 reg = 0xA5;

	rc = qpnp_chg_write(chip, &reg,
				 chip->buck_base + SEC_ACCESS,
				 1);
	if (rc) {
		pr_err("Error %d writing 0xA5 to buck's 0x%x reg\n",
				rc, SEC_ACCESS);
		return rc;
	}

	reg = POWER_STAGE_DEFAULT;
	if (reduce)
		reg = POWER_STAGE_REDUCED;
	rc = qpnp_chg_write(chip, &reg,
				 chip->buck_base + CHGR_BUCK_PSTG_CTRL,
				 1);

	if (rc)
		pr_err("Error %d writing 0x%x power stage register\n", rc, reg);
	return rc;
}

static int
qpnp_chg_get_vusbin_uv(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct qpnp_vadc_result results;

	rc = qpnp_vadc_read(chip->vadc_dev, USBIN, &results);
	if (rc) {
		pr_err("Unable to read vbat rc=%d\n", rc);
		return 0;
	}
	return results.physical;
}

static
int get_vusb_averaged(struct qpnp_chg_chip *chip, int sample_count)
{
	int vusb_uv = 0;
	int i;

	/* avoid  overflows */
	if (sample_count > 256)
		sample_count = 256;

	for (i = 0; i < sample_count; i++)
		vusb_uv += qpnp_chg_get_vusbin_uv(chip);

	vusb_uv = vusb_uv / sample_count;
	return vusb_uv;
}

static
int get_vbat_averaged(struct qpnp_chg_chip *chip, int sample_count)
{
	int vbat_uv = 0;
	int i;

	/* avoid  overflows */
	if (sample_count > 256)
		sample_count = 256;

	for (i = 0; i < sample_count; i++)
		vbat_uv += get_prop_battery_voltage_now(chip);

	vbat_uv = vbat_uv / sample_count;
	return vbat_uv;
}

static void
qpnp_chg_reduce_power_stage(struct qpnp_chg_chip *chip)
{
	bool power_stage_reduced_in_hw = qpnp_chg_is_power_stage_reduced(chip);
	bool reduce_power_stage = false;
	int vbat_uv = get_vbat_averaged(chip, 16);
	int vusb_uv = get_vusb_averaged(chip, 16);
	bool fast_chg =
		(get_prop_charge_type(chip) == POWER_SUPPLY_CHARGE_TYPE_FAST);
	static int count_restore_power_stage;
	static int count_reduce_power_stage;
	bool vchg_loop = get_prop_vchg_loop(chip);
	bool ichg_loop = qpnp_chg_is_ichg_loop_active(chip);
	bool usb_present = qpnp_chg_is_usb_chg_plugged_in(chip);
	bool usb_ma_above_wall =
		(qpnp_chg_usb_iusbmax_get(chip) > USB_WALL_THRESHOLD_MA);

	if (fast_chg
		&& usb_present
		&& usb_ma_above_wall
		&& vbat_uv < POWER_STAGE_REDUCE_MAX_VBAT_UV
		&& vusb_uv > POWER_STAGE_REDUCE_MIN_VCHG_UV)
		reduce_power_stage = true;

	if ((usb_present && usb_ma_above_wall)
		&& (vchg_loop || ichg_loop))
		reduce_power_stage = true;

	if (power_stage_reduced_in_hw && !reduce_power_stage) {
		count_restore_power_stage++;
		count_reduce_power_stage = 0;
	} else if (!power_stage_reduced_in_hw && reduce_power_stage) {
		count_reduce_power_stage++;
		count_restore_power_stage = 0;
	} else if (power_stage_reduced_in_hw == reduce_power_stage) {
		count_restore_power_stage = 0;
		count_reduce_power_stage = 0;
	}

	pr_debug("power_stage_hw = %d reduce_power_stage = %d usb_present = %d usb_ma_above_wall = %d vbat_uv(16) = %d vusb_uv(16) = %d fast_chg = %d , ichg = %d, vchg = %d, restore,reduce = %d, %d\n",
			power_stage_reduced_in_hw, reduce_power_stage,
			usb_present, usb_ma_above_wall,
			vbat_uv, vusb_uv, fast_chg,
			ichg_loop, vchg_loop,
			count_restore_power_stage, count_reduce_power_stage);

	if (!power_stage_reduced_in_hw && reduce_power_stage) {
		if (count_reduce_power_stage >= 2) {
			qpnp_chg_power_stage_set(chip, true);
			power_stage_reduced_in_hw = true;
		}
	}

	if (power_stage_reduced_in_hw && !reduce_power_stage) {
		if (count_restore_power_stage >= 6
				|| (!usb_present || !usb_ma_above_wall)) {
			qpnp_chg_power_stage_set(chip, false);
			power_stage_reduced_in_hw = false;
		}
	}

	if (usb_present && usb_ma_above_wall) {
#ifndef CONFIG_RTC_INTF_ALARM
		ktime_t start;
		u64 ns = (u64) POWER_STAGE_REDUCE_CHECK_PERIOD_SECONDS *
				NSEC_PER_SEC;

		start = ns_to_ktime(ns);
		alarm_start_relative(&chip->reduce_power_stage_alarm, start);
#else
		struct timespec ts;

		getnstimeofday(&ts);
		ts.tv_sec += POWER_STAGE_REDUCE_CHECK_PERIOD_SECONDS;
		alarm_start_range(&chip->reduce_power_stage_alarm,
					timespec_to_ktime(ts),
					timespec_to_ktime(ts));
#endif
	} else {
		pr_debug("stopping power stage workaround\n");
		chip->power_stage_workaround_running = false;
	}
}

static void
qpnp_chg_batfet_lcl_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, batfet_lcl_work);

	mutex_lock(&chip->batfet_vreg_lock);
	if (qpnp_chg_is_usb_chg_plugged_in(chip) ||
			qpnp_chg_is_dc_chg_plugged_in(chip)) {
		qpnp_chg_regulator_batfet_set(chip, 1);
		pr_debug("disabled ULPM\n");
	} else if (!chip->batfet_ext_en && !qpnp_chg_is_usb_chg_plugged_in(chip)
			&& !qpnp_chg_is_dc_chg_plugged_in(chip)) {
		qpnp_chg_regulator_batfet_set(chip, 0);
		pr_debug("enabled ULPM\n");
	}
	mutex_unlock(&chip->batfet_vreg_lock);
}

static void
qpnp_chg_reduce_power_stage_work(struct work_struct *work)
{
	struct qpnp_chg_chip *chip = container_of(work,
				struct qpnp_chg_chip, reduce_power_stage_work);

	qpnp_chg_reduce_power_stage(chip);
}

#ifndef CONFIG_RTC_INTF_ALARM
static enum alarmtimer_restart
qpnp_chg_reduce_power_stage_callback(struct alarm *alarm, ktime_t now)
{
	struct qpnp_chg_chip *chip = container_of(alarm, struct qpnp_chg_chip,
						reduce_power_stage_alarm);

	schedule_work(&chip->reduce_power_stage_work);

	return ALARMTIMER_NORESTART;
}
#else
static void
qpnp_chg_reduce_power_stage_callback(struct alarm *alarm)
{
	struct qpnp_chg_chip *chip = container_of(alarm, struct qpnp_chg_chip,
						reduce_power_stage_alarm);

	schedule_work(&chip->reduce_power_stage_work);
}
#endif

static int
qpnp_dc_power_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								dc_psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (!val->intval)
			break;

		rc = qpnp_chg_idcmax_set(chip, val->intval / 1000);
		if (rc) {
			pr_err("Error setting idcmax property %d\n", rc);
			return rc;
		}
		chip->maxinput_dc_ma = (val->intval / 1000);

		break;
	default:
		return -EINVAL;
	}

	pr_debug("psy changed dc_psy\n");
	power_supply_changed(&chip->dc_psy);
	return rc;
}

static int
qpnp_batt_power_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	struct qpnp_chg_chip *chip = container_of(psy, struct qpnp_chg_chip,
								batt_psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_COOL_TEMP:
		rc = qpnp_chg_configure_jeita(chip, psp, val->intval);
		break;
	case POWER_SUPPLY_PROP_WARM_TEMP:
		rc = qpnp_chg_configure_jeita(chip, psp, val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		chip->fake_battery_soc = val->intval;
		power_supply_changed(&chip->batt_psy);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		chip->charging_disabled = !(val->intval);
		if (chip->charging_disabled) {
			/* disable charging */
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
			qpnp_chg_force_run_on_batt(chip,
						chip->charging_disabled);
		} else {
			/* enable charging */
			qpnp_chg_force_run_on_batt(chip,
					chip->charging_disabled);
			qpnp_chg_charge_en(chip, !chip->charging_disabled);
		}
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		qpnp_batt_system_temp_level_set(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		if (qpnp_chg_is_usb_chg_plugged_in(chip))
			qpnp_chg_iusbmax_set(chip, val->intval / 1000);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM:
		qpnp_chg_iusb_trim_set(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		qpnp_chg_input_current_settled(chip);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		qpnp_chg_vinmin_set(chip, val->intval / 1000);
		break;
	default:
		return -EINVAL;
	}

	pr_debug("psy changed batt_psy\n");
	power_supply_changed(&chip->batt_psy);
	return rc;
}

static int
qpnp_chg_setup_flags(struct qpnp_chg_chip *chip)
{
	if (chip->revision > 0 && chip->type == SMBB)
		chip->flags |= CHG_FLAGS_VCP_WA;
	if (chip->type == SMBB)
		chip->flags |= BOOST_FLASH_WA;
	if (chip->type == SMBBP) {
		struct device_node *revid_dev_node;
		struct pmic_revid_data *revid_data;

		chip->flags |=  BOOST_FLASH_WA;

		revid_dev_node = of_parse_phandle(chip->spmi->dev.of_node,
						"qcom,pmic-revid", 0);
		if (!revid_dev_node) {
			pr_err("Missing qcom,pmic-revid property\n");
			return -EINVAL;
		}
		revid_data = get_revid_data(revid_dev_node);
		if (IS_ERR(revid_data)) {
			pr_err("Couldnt get revid data rc = %ld\n",
						PTR_ERR(revid_data));
			return PTR_ERR(revid_data);
		}

		if (revid_data->rev4 < PM8226_V2P1_REV4
			|| ((revid_data->rev4 == PM8226_V2P1_REV4)
				&& (revid_data->rev3 <= PM8226_V2P1_REV3))) {
			chip->flags |= POWER_STAGE_WA;
		}
	}
	return 0;
}

static int
qpnp_chg_request_irqs(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	struct resource *resource;
	struct spmi_resource *spmi_resource;
	u8 subtype;
	struct spmi_device *spmi = chip->spmi;

	spmi_for_each_container_dev(spmi_resource, chip->spmi) {
		if (!spmi_resource) {
				pr_err("qpnp_chg: spmi resource absent\n");
			return rc;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			return rc;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			return rc;
		}

		switch (subtype) {
		case SMBB_CHGR_SUBTYPE:
		case SMBBP_CHGR_SUBTYPE:
		case SMBCL_CHGR_SUBTYPE:
			chip->chg_fastchg.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "fast-chg-on");
			if (chip->chg_fastchg.irq < 0) {
				pr_err("Unable to get fast-chg-on irq\n");
				return rc;
			}

			chip->chg_trklchg.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "trkl-chg-on");
			if (chip->chg_trklchg.irq < 0) {
				pr_err("Unable to get trkl-chg-on irq\n");
				return rc;
			}

			chip->chg_failed.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "chg-failed");
			if (chip->chg_failed.irq < 0) {
				pr_err("Unable to get chg_failed irq\n");
				return rc;
			}

			chip->chg_vbatdet_lo.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "vbat-det-lo");
			if (chip->chg_vbatdet_lo.irq < 0) {
				pr_err("Unable to get fast-chg-on irq\n");
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_failed.irq,
				qpnp_chg_chgr_chg_failed_irq_handler,
				IRQF_TRIGGER_RISING, "chg-failed", chip);
			if (rc < 0) {
				pr_err("Can't request %d chg-failed: %d\n",
						chip->chg_failed.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_fastchg.irq,
					qpnp_chg_chgr_chg_fastchg_irq_handler,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
					"fast-chg-on", chip);
			if (rc < 0) {
				pr_err("Can't request %d fast-chg-on: %d\n",
						chip->chg_fastchg.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev, chip->chg_trklchg.irq,
				qpnp_chg_chgr_chg_trklchg_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"trkl-chg-on", chip);
			if (rc < 0) {
				pr_err("Can't request %d trkl-chg-on: %d\n",
						chip->chg_trklchg.irq, rc);
				return rc;
			}

			rc |= devm_request_irq(chip->dev,
				chip->chg_vbatdet_lo.irq,
				qpnp_chg_vbatdet_lo_irq_handler,
				IRQF_TRIGGER_RISING,
				"vbat-det-lo", chip);
			if (rc < 0) {
				pr_err("Can't request %d vbat-det-lo: %d\n",
						chip->chg_vbatdet_lo.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->chg_trklchg.irq);
			enable_irq_wake(chip->chg_failed.irq);
			qpnp_chg_disable_irq(&chip->chg_vbatdet_lo);
			enable_irq_wake(chip->chg_vbatdet_lo.irq);

			break;
		case SMBB_BAT_IF_SUBTYPE:
		case SMBBP_BAT_IF_SUBTYPE:
		case SMBCL_BAT_IF_SUBTYPE:
			chip->batt_pres.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "batt-pres");
			if (chip->batt_pres.irq < 0) {
				pr_err("Unable to get batt-pres irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->batt_pres.irq,
				qpnp_chg_bat_if_batt_pres_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
				| IRQF_SHARED | IRQF_ONESHOT,
				"batt-pres", chip);
			if (rc < 0) {
				pr_err("Can't request %d batt-pres irq: %d\n",
						chip->batt_pres.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->batt_pres.irq);

			chip->batt_temp_ok.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "bat-temp-ok");
			if (chip->batt_temp_ok.irq < 0) {
				pr_err("Unable to get bat-temp-ok irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->batt_temp_ok.irq,
				qpnp_chg_bat_if_batt_temp_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"bat-temp-ok", chip);
			if (rc < 0) {
				pr_err("Can't request %d bat-temp-ok irq: %d\n",
						chip->batt_temp_ok.irq, rc);
				return rc;
			}
			qpnp_chg_bat_if_batt_temp_irq_handler(0, chip);

			enable_irq_wake(chip->batt_temp_ok.irq);

			break;
		case SMBB_BUCK_SUBTYPE:
		case SMBBP_BUCK_SUBTYPE:
		case SMBCL_BUCK_SUBTYPE:
			break;

		case SMBB_USB_CHGPTH_SUBTYPE:
		case SMBBP_USB_CHGPTH_SUBTYPE:
		case SMBCL_USB_CHGPTH_SUBTYPE:
			if (chip->ovp_monitor_enable) {
				chip->coarse_det_usb.irq =
					spmi_get_irq_byname(spmi,
					spmi_resource, "coarse-det-usb");
				if (chip->coarse_det_usb.irq < 0) {
					pr_err("Can't get coarse-det irq\n");
					return rc;
				}
				rc = devm_request_irq(chip->dev,
					chip->coarse_det_usb.irq,
					qpnp_chg_coarse_det_usb_irq_handler,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
					"coarse-det-usb", chip);
				if (rc < 0) {
					pr_err("Can't req %d coarse-det: %d\n",
						chip->coarse_det_usb.irq, rc);
					return rc;
				}
			}

			chip->usbin_valid.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "usbin-valid");
			if (chip->usbin_valid.irq < 0) {
				pr_err("Unable to get usbin irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->usbin_valid.irq,
				qpnp_chg_usb_usbin_valid_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
					"usbin-valid", chip);
			if (rc < 0) {
				pr_err("Can't request %d usbin-valid: %d\n",
						chip->usbin_valid.irq, rc);
				return rc;
			}

			chip->chg_gone.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "chg-gone");
			if (chip->chg_gone.irq < 0) {
				pr_err("Unable to get chg-gone irq\n");
				return rc;
			}
			rc = devm_request_irq(chip->dev, chip->chg_gone.irq,
				qpnp_chg_usb_chg_gone_irq_handler,
				IRQF_TRIGGER_RISING,
					"chg-gone", chip);
			if (rc < 0) {
				pr_err("Can't request %d chg-gone: %d\n",
						chip->chg_gone.irq, rc);
				return rc;
			}

			if ((subtype == SMBBP_USB_CHGPTH_SUBTYPE) ||
				(subtype == SMBCL_USB_CHGPTH_SUBTYPE)) {
				chip->usb_ocp.irq = spmi_get_irq_byname(spmi,
						spmi_resource, "usb-ocp");
				if (chip->usb_ocp.irq < 0) {
					pr_err("Unable to get usbin irq\n");
					return rc;
				}
				rc = devm_request_irq(chip->dev,
					chip->usb_ocp.irq,
					qpnp_chg_usb_usb_ocp_irq_handler,
					IRQF_TRIGGER_RISING, "usb-ocp", chip);
				if (rc < 0) {
					pr_err("Can't request %d usb-ocp: %d\n",
							chip->usb_ocp.irq, rc);
					return rc;
				}

				enable_irq_wake(chip->usb_ocp.irq);
			}

			enable_irq_wake(chip->usbin_valid.irq);
			enable_irq_wake(chip->chg_gone.irq);
			break;
		case SMBB_DC_CHGPTH_SUBTYPE:
			chip->dcin_valid.irq = spmi_get_irq_byname(spmi,
					spmi_resource, "dcin-valid");
			if (chip->dcin_valid.irq < 0) {
				pr_err("Unable to get dcin irq\n");
				return -rc;
			}
			rc = devm_request_irq(chip->dev, chip->dcin_valid.irq,
				qpnp_chg_dc_dcin_valid_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"dcin-valid", chip);
			if (rc < 0) {
				pr_err("Can't request %d dcin-valid: %d\n",
						chip->dcin_valid.irq, rc);
				return rc;
			}

			enable_irq_wake(chip->dcin_valid.irq);
			break;
		}
	}

	return rc;
}

static void get_battery_make_info(const char *name, char *model)
{
	int i = 0, count = 0;
	char *key = NULL;

	if (NULL == name) {
		pr_err("battery name is NULL\n");
		return;
	}

	key = strnchr(name, strlen(name) - 1, ',');

	if (NULL == key) {
		pr_err("battery name key is NULL\n");
		return;
	}
	/* copy batt model and make info from
	 * string "mmi,ey30-lg-batterydata"
	 * the output string would be "EY30-LG"
	 */
	key++;
	do {
		if (*key == '-')
			count++;
		if (count == 2)
			break;
		model[i++] = toupper(*key++);

	} while (*key != '\0');
	model[i--] = '\0';

	return;
}

static int
qpnp_chg_load_battery_data(struct qpnp_chg_chip *chip)
{
	struct bms_battery_data batt_data;
	struct device_node *node;
	struct qpnp_vadc_result result;
	int rc;

	node = of_find_node_by_name(chip->spmi->dev.of_node,
			"qcom,battery-data");
	if (node) {
		memset(&batt_data, 0, sizeof(struct bms_battery_data));
		rc = qpnp_vadc_read(chip->vadc_dev, LR_MUX2_BAT_ID, &result);
		if (rc) {
			pr_err("error reading batt id channel = %d, rc = %d\n",
						LR_MUX2_BAT_ID, rc);
			return rc;
		}
		chip->pc_temp_ocv_lut =
			kzalloc(sizeof(struct pc_temp_ocv_lut), GFP_KERNEL);
		batt_data.pc_temp_ocv_lut = chip->pc_temp_ocv_lut;
		batt_data.max_voltage_uv = -1;
		batt_data.iterm_ua = -1;
		batt_data.max_current_ma = -1;
		rc = of_batterydata_read_data(node,
					      &batt_data, result.physical,
					      qpnp_charger_mmi_battid());
		if (rc) {
			pr_err("battery not found, using palladium 1500\n");
			kfree(chip->pc_temp_ocv_lut);
			chip->pc_temp_ocv_lut =
				palladium_1500_data.pc_temp_ocv_lut;
			if (chip->battery_model_sysfs)
				strlcpy(chip->battery_info, "PALLADIUM_1500",
						sizeof(chip->battery_info));
			chip->cutoff_mv = 3400;
			return 0;
		}
		if (chip->battery_model_sysfs)
			get_battery_make_info(batt_data.name,
						chip->battery_info);

		pr_info("batt_id = %d uV, max_voltage = %d uV, term_curr = %d uA\n",
			(int)result.physical,
			(int)batt_data.max_voltage_uv,
			(int)batt_data.iterm_ua);

		chip->fcc_mah = batt_data.fcc;
		if (batt_data.max_voltage_uv >= 0) {
			chip->max_voltage_mv = batt_data.max_voltage_uv / 1000;
			chip->safe_voltage_mv = chip->max_voltage_mv
				+ MAX_DELTA_VDD_MAX_MV;
		}
		if (batt_data.iterm_ua >= 0)
			chip->term_current = batt_data.iterm_ua / 1000;

		if (batt_data.cutoff_uv >= 0)
			chip->cutoff_mv = batt_data.cutoff_uv / 1000;
		batt_data.pc_temp_ocv_lut = NULL;
		if (batt_data.max_current_ma >= 0)
			chip->max_bat_chg_current = batt_data.max_current_ma;
	}

	return 0;
}

#define WDOG_EN_BIT	BIT(7)
static int
qpnp_chg_hwinit(struct qpnp_chg_chip *chip, u8 subtype,
				struct spmi_resource *spmi_resource)
{
	int rc = 0;
	u8 reg = 0;
	struct regulator_init_data *init_data;
	struct regulator_desc *rdesc;

	switch (subtype) {
	case SMBB_CHGR_SUBTYPE:
	case SMBBP_CHGR_SUBTYPE:
	case SMBCL_CHGR_SUBTYPE:
		qpnp_chg_vbatweak_set(chip, chip->batt_weak_voltage_mv);

		rc = qpnp_chg_vinmin_set(chip, chip->min_voltage_mv);
		if (rc) {
			pr_debug("failed setting  min_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_vddsafe_set(chip, chip->safe_voltage_mv);
		if (rc) {
			pr_debug("failed setting safe_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_vbatdet_set(chip,
				chip->max_voltage_mv - chip->resume_delta_mv);
		if (rc) {
			pr_debug("failed setting resume_voltage rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_ibatmax_set(chip, chip->max_bat_chg_current);
		if (rc) {
			pr_debug("failed setting ibatmax rc=%d\n", rc);
			return rc;
		}
		if (chip->term_current) {
			rc = qpnp_chg_ibatterm_set(chip, chip->term_current);
			if (rc) {
				pr_debug("failed setting ibatterm rc=%d\n", rc);
				return rc;
			}
		}
		rc = qpnp_chg_ibatsafe_set(chip, chip->safe_current);
		if (rc) {
			pr_debug("failed setting ibat_Safe rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_chg_tchg_max_set(chip, chip->tchg_mins);
		if (rc) {
			pr_debug("failed setting tchg_mins rc=%d\n", rc);
			return rc;
		}

		/* HACK: Disable wdog */
		rc = qpnp_chg_masked_write(chip, chip->chgr_base + 0x62,
			0xFF, 0xA0, 1);

		/* HACK: use analog EOC */
		rc = qpnp_chg_masked_write(chip, chip->chgr_base +
			CHGR_IBAT_TERM_CHGR,
			0xFF, 0x08, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->chgr_base + CHGR_IR_DROP_COMPEN, 0xFF,
			chip->ir_drop_comp ? 0x83 : 0x00, 1);
		if (rc) {
			pr_debug("failed to enable IR drop comp rc=%d\n", rc);
			return rc;
		}

		break;
	case SMBB_BUCK_SUBTYPE:
	case SMBBP_BUCK_SUBTYPE:
	case SMBCL_BUCK_SUBTYPE:
		rc = qpnp_chg_toggle_chg_done_logic(chip, 0);
		if (rc)
			return rc;

		rc = qpnp_chg_masked_write(chip,
			chip->buck_base + CHGR_BUCK_BCK_VBAT_REG_MODE,
			BUCK_VBAT_REG_NODE_SEL_BIT,
			chip->ir_drop_comp ? 0x0 : BUCK_VBAT_REG_NODE_SEL_BIT,
			1);
		if (rc) {
			pr_debug("failed to enable IR drop %s rc=%d\n",
				 chip->ir_drop_comp ? "mode" : "comp", rc);
			return rc;
		}

		rc = qpnp_chg_read(chip, &chip->trim_center,
				chip->buck_base + BUCK_CTRL_TRIM1, 1);
		if (rc) {
			pr_debug("failed to read trim center rc=%d\n", rc);
			return rc;
		}
		chip->trim_center >>= 4;
		pr_debug("trim center = %02x\n", chip->trim_center);
		break;
	case SMBB_BAT_IF_SUBTYPE:
	case SMBBP_BAT_IF_SUBTYPE:
	case SMBCL_BAT_IF_SUBTYPE:
		/* Select battery presence detection */
		switch (chip->bpd_detection) {
		case BPD_TYPE_BAT_THM:
			reg = BAT_THM_EN;
			break;
		case BPD_TYPE_BAT_ID:
			reg = BAT_ID_EN;
			break;
		case BPD_TYPE_BAT_THM_BAT_ID:
			reg = BAT_THM_EN | BAT_ID_EN;
			break;
		default:
			reg = BAT_THM_EN;
			break;
		}

		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_BPD_CTRL,
			BAT_IF_BPD_CTRL_SEL,
			reg, 1);
		if (rc) {
			pr_debug("failed to chose BPD rc=%d\n", rc);
			return rc;
		}
		/* Force on VREF_BAT_THM */
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BATT_THERM_FORCE_ON, 1);
		if (rc) {
			pr_debug("failed to force on VREF_BAT_THM rc=%d\n", rc);
			return rc;
		}

		init_data = of_get_regulator_init_data(chip->dev,
					       spmi_resource->of_node);

		if (init_data->constraints.name) {
			rdesc			= &(chip->batfet_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_batfet_vreg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS;

			chip->batfet_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->batfet_vreg.rdev)) {
				rc = PTR_ERR(chip->batfet_vreg.rdev);
				chip->batfet_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("batfet reg failed, rc=%d\n",
							rc);
				return rc;
			}
		}
		break;
	case SMBB_USB_CHGPTH_SUBTYPE:
	case SMBBP_USB_CHGPTH_SUBTYPE:
	case SMBCL_USB_CHGPTH_SUBTYPE:
		if (qpnp_chg_is_usb_chg_plugged_in(chip)) {
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + CHGR_USB_ENUM_T_STOP,
				ENUM_T_STOP_BIT,
				ENUM_T_STOP_BIT, 1);
			if (rc) {
				pr_err("failed to write enum stop rc=%d\n", rc);
				return -ENXIO;
			}
		}

		init_data = of_get_regulator_init_data(chip->dev,
						       spmi_resource->of_node);
		if (!init_data) {
			pr_err("unable to allocate memory\n");
			return -ENOMEM;
		}

		if (init_data->constraints.name) {
			if (of_get_property(chip->dev->of_node,
						"otg-parent-supply", NULL))
				init_data->supply_regulator = "otg-parent";

			rdesc			= &(chip->otg_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_otg_reg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS;

			chip->otg_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->otg_vreg.rdev)) {
				rc = PTR_ERR(chip->otg_vreg.rdev);
				chip->otg_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("OTG reg failed, rc=%d\n", rc);
				return rc;
			}
		}

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_OVP_CTL,
			USB_VALID_DEB_20MS,
			USB_VALID_DEB_20MS, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + CHGR_USB_ENUM_T_STOP,
			ENUM_T_STOP_BIT,
			ENUM_T_STOP_BIT, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + SEC_ACCESS,
			0xFF,
			0xA5, 1);

		rc = qpnp_chg_masked_write(chip,
			chip->usb_chgpth_base + USB_CHG_GONE_REV_BST,
			0xFF,
			0x80, 1);

		if ((subtype == SMBBP_USB_CHGPTH_SUBTYPE) ||
			(subtype == SMBCL_USB_CHGPTH_SUBTYPE)) {
			rc = qpnp_chg_masked_write(chip,
				chip->usb_chgpth_base + USB_OCP_THR,
				OCP_THR_MASK,
				OCP_THR_900_MA, 1);
			if (rc)
				pr_err("Failed to configure OCP rc = %d\n", rc);
		}

		break;
	case SMBB_DC_CHGPTH_SUBTYPE:
		break;
	case SMBB_BOOST_SUBTYPE:
	case SMBBP_BOOST_SUBTYPE:
		init_data = of_get_regulator_init_data(chip->dev,
					       spmi_resource->of_node);
		if (!init_data) {
			pr_err("unable to allocate memory\n");
			return -ENOMEM;
		}

		if (init_data->constraints.name) {
			if (of_get_property(chip->dev->of_node,
						"boost-parent-supply", NULL))
				init_data->supply_regulator = "boost-parent";

			rdesc			= &(chip->boost_vreg.rdesc);
			rdesc->owner		= THIS_MODULE;
			rdesc->type		= REGULATOR_VOLTAGE;
			rdesc->ops		= &qpnp_chg_boost_reg_ops;
			rdesc->name		= init_data->constraints.name;

			init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_STATUS
					| REGULATOR_CHANGE_VOLTAGE;

			chip->boost_vreg.rdev = regulator_register(rdesc,
					chip->dev, init_data, chip,
					spmi_resource->of_node);
			if (IS_ERR(chip->boost_vreg.rdev)) {
				rc = PTR_ERR(chip->boost_vreg.rdev);
				chip->boost_vreg.rdev = NULL;
				if (rc != -EPROBE_DEFER)
					pr_err("boost reg failed, rc=%d\n", rc);
				return rc;
			}
		}
		break;
	case SMBB_MISC_SUBTYPE:
	case SMBBP_MISC_SUBTYPE:
	case SMBCL_MISC_SUBTYPE:
		if (subtype == SMBB_MISC_SUBTYPE)
			chip->type = SMBB;
		else if (subtype == SMBBP_MISC_SUBTYPE)
			chip->type = SMBBP;
		else if (subtype == SMBCL_MISC_SUBTYPE)
			chip->type = SMBCL;

		pr_debug("Setting BOOT_DONE\n");
		rc = qpnp_chg_masked_write(chip,
			chip->misc_base + CHGR_MISC_BOOT_DONE,
			CHGR_BOOT_DONE, CHGR_BOOT_DONE, 1);
		rc = qpnp_chg_read(chip, &reg,
				 chip->misc_base + MISC_REVISION2, 1);
		if (rc) {
			pr_err("failed to read revision register rc=%d\n", rc);
			return rc;
		}

		chip->revision = reg;
		break;
	default:
		pr_err("Invalid peripheral subtype\n");
	}
	return rc;
}

#define OF_PROP_READ(chip, prop, qpnp_dt_property, retval, optional)	\
do {									\
	if (retval)							\
		break;							\
									\
	retval = of_property_read_u32(chip->spmi->dev.of_node,		\
					"qcom," qpnp_dt_property,	\
					&chip->prop);			\
									\
	if ((retval == -EINVAL) && optional)				\
		retval = 0;						\
	else if (retval)						\
		pr_err("Error reading " #qpnp_dt_property		\
				" property rc = %d\n", rc);		\
} while (0)

static int
qpnp_charger_read_dt_props(struct qpnp_chg_chip *chip)
{
	int rc = 0;
	const char *bpd;

	OF_PROP_READ(chip, max_voltage_mv, "vddmax-mv", rc, 0);
	OF_PROP_READ(chip, min_voltage_mv, "vinmin-mv", rc, 0);
	OF_PROP_READ(chip, safe_voltage_mv, "vddsafe-mv", rc, 0);
	OF_PROP_READ(chip, resume_delta_mv, "vbatdet-delta-mv", rc, 0);
	OF_PROP_READ(chip, safe_current, "ibatsafe-ma", rc, 0);
	OF_PROP_READ(chip, max_bat_chg_current, "ibatmax-ma", rc, 0);
	if (rc)
		pr_err("failed to read required dt parameters %d\n", rc);

	OF_PROP_READ(chip, term_current, "ibatterm-ma", rc, 1);
	OF_PROP_READ(chip, maxinput_dc_ma, "maxinput-dc-ma", rc, 1);
	OF_PROP_READ(chip, maxinput_usb_ma, "maxinput-usb-ma", rc, 1);
	OF_PROP_READ(chip, warm_bat_decidegc, "warm-bat-decidegc", rc, 1);
	OF_PROP_READ(chip, cool_bat_decidegc, "cool-bat-decidegc", rc, 1);
	OF_PROP_READ(chip, batt_therm_offset, "batt-therm-offset", rc, 1);
	OF_PROP_READ(chip, tchg_mins, "tchg-mins", rc, 1);
	OF_PROP_READ(chip, hot_batt_p, "batt-hot-percentage", rc, 1);
	OF_PROP_READ(chip, cold_batt_p, "batt-cold-percentage", rc, 1);
	OF_PROP_READ(chip, soc_resume_limit, "resume-soc", rc, 1);
	OF_PROP_READ(chip, batt_weak_voltage_mv, "vbatweak-mv", rc, 1);
	OF_PROP_READ(chip, vbatdet_max_err_mv, "vbatdet-maxerr-mv", rc, 1);
	OF_PROP_READ(chip, step_charge_mv, "step-charge-voltage", rc, 1);
	OF_PROP_READ(chip, step_charge_soc, "step-charge-soc", rc, 1);
	OF_PROP_READ(chip, step_charge_ma, "step-charge-current", rc, 1);
	OF_PROP_READ(chip, ir_drop_comp, "ir-comp", rc, 1);
	OF_PROP_READ(chip, float_charge_timer, "float-charge-timer", rc, 1);

	if (rc)
		return rc;

	rc = of_property_read_string(chip->spmi->dev.of_node,
		"qcom,bpd-detection", &bpd);
	if (rc) {
		/* Select BAT_THM as default BPD scheme */
		chip->bpd_detection = BPD_TYPE_BAT_THM;
		rc = 0;
	} else {
		chip->bpd_detection = get_bpd(bpd);
		if (chip->bpd_detection < 0) {
			pr_err("failed to determine bpd schema %d\n", rc);
			return rc;
		}
	}

	if (!chip->vbatdet_max_err_mv)
		chip->vbatdet_max_err_mv = VBATDET_MAX_ERR_MV;

	/* Look up JEITA compliance parameters if cool and warm temp provided */
	if (chip->cool_bat_decidegc || chip->warm_bat_decidegc) {
		chip->adc_tm_dev = qpnp_get_adc_tm(chip->dev, "chg");
		if (IS_ERR(chip->adc_tm_dev)) {
			rc = PTR_ERR(chip->adc_tm_dev);
			if (rc != -EPROBE_DEFER)
				pr_err("adc-tm not ready, defer probe\n");
			return rc;
		}

		OF_PROP_READ(chip, warm_bat_chg_ma, "ibatmax-warm-ma", rc, 1);
		OF_PROP_READ(chip, cool_bat_chg_ma, "ibatmax-cool-ma", rc, 1);
		OF_PROP_READ(chip, warm_bat_mv, "warm-bat-mv", rc, 1);
		OF_PROP_READ(chip, warm_bat_soc, "warm-bat-soc", rc, 1);
		OF_PROP_READ(chip, resume_delta_soc, "resume-delta-soc", rc, 1);
		OF_PROP_READ(chip, cool_bat_mv, "cool-bat-mv", rc, 1);
		if (rc)
			return rc;
	}

	chip->battery_model_sysfs =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,battery-model-sysfs");

	/* Get the use-external-rsense property */
	chip->use_external_rsense = of_property_read_bool(
			chip->spmi->dev.of_node,
			"qcom,use-external-rsense");

	/* Get the btc-disabled property */
	chip->btc_disabled = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,btc-disabled");

	ext_ovp_present = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,ext-ovp-present");

	/* Get the charging-disabled property */
	chip->charging_disabled = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,charging-disabled");

	chip->ovp_monitor_enable = of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,ovp-monitor-en");

	/* Get the duty-cycle-100p property */
	chip->duty_cycle_100p = of_property_read_bool(
					chip->spmi->dev.of_node,
					"qcom,duty-cycle-100p");

	/* Get the fake-batt-values property */
	chip->use_default_batt_values =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,use-default-batt-values");

	/* Disable charging when faking battery values */
	if (chip->use_default_batt_values)
		chip->charging_disabled = true;

	chip->power_stage_workaround_enable =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,power-stage-reduced");

	chip->ibat_calibration_enabled =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,ibat-calibration-enabled");
	chip->parallel_ovp_mode =
			of_property_read_bool(chip->spmi->dev.of_node,
					"qcom,parallel-ovp-mode");

	of_property_read_u32(chip->spmi->dev.of_node,
			     "qcom,hotspot-thresh",
			     &(chip->bat_hotspot_thrs));

	of_get_property(chip->spmi->dev.of_node, "qcom,thermal-mitigation",
		&(chip->thermal_levels));

	if (chip->thermal_levels > sizeof(int)) {
		chip->thermal_mitigation = devm_kzalloc(chip->dev,
			chip->thermal_levels,
			GFP_KERNEL);

		if (chip->thermal_mitigation == NULL) {
			pr_err("thermal mitigation kzalloc() failed.\n");
			return -ENOMEM;
		}

		chip->thermal_levels /= sizeof(int);
		rc = of_property_read_u32_array(chip->spmi->dev.of_node,
				"qcom,thermal-mitigation",
				chip->thermal_mitigation, chip->thermal_levels);
		if (rc) {
			pr_err("qcom,thermal-mitigation missing in dt\n");
			return rc;
		}
	}

	return rc;
}

#define CHG_SHOW_MAX_SIZE 50
static ssize_t force_demo_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long r;
	unsigned long mode;

	r = kstrtoul(buf, 0, &mode);
	if (r) {
		pr_err("Invalid usb suspend mode value = %lu\n", mode);
		return -EINVAL;
	}

	if (!the_chip) {
		pr_err("chip not valid\n");
		return -ENODEV;
	}

	if (mode)
		pr_warn("Battery in Demo Mode charging Limited\n");
	else
		pr_warn("Battery in Demo mode DISABLED\n");

	the_chip->demo_mode = (mode) ? true : false;

	return r ? r : count;
}

static ssize_t force_demo_mode_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	int state;

	if (!the_chip) {
		pr_err("chip not valid\n");
		return -ENODEV;
	}

	state = (the_chip->demo_mode) ? 1 : 0;

	return scnprintf(buf, CHG_SHOW_MAX_SIZE, "%d\n", state);
}

static DEVICE_ATTR(force_demo_mode, 0644,
		force_demo_mode_show,
		force_demo_mode_store);

static bool __devinit qpnp_charger_mmi_factory(void)
{
	struct device_node *np = of_find_node_by_path("/chosen");
	bool factory = false;

	if (np)
		factory = of_property_read_bool(np, "mmi,factory-cable");

	of_node_put(np);

	return factory;
}

static const char *qpnp_charger_mmi_battid(void)
{
	struct device_node *np = of_find_node_by_path("/chosen");
	const char *battid_buf;
	int retval;

	if (!np)
		return NULL;

	pr_err("Charger Reading Battid at powerup!\n");

	retval = of_property_read_string(np, "mmi,battid",
						 &battid_buf);

	if ((retval == -EINVAL) || !battid_buf) {
		pr_err("Battid unused\n");
		of_node_put(np);
		return NULL;
	} else
		pr_err("Battid = %s\n", battid_buf);

	of_node_put(np);

	return battid_buf;
}

static int __devinit
qpnp_charger_probe(struct spmi_device *spmi)
{
	u8 subtype;
	struct qpnp_chg_chip	*chip;
	struct resource *resource;
	struct spmi_resource *spmi_resource;
	int rc = 0;

	if (qpnp_charger_mmi_factory()) {
		pr_info("Factory Mode Disabling!\n");
		return -ENODEV;
	}

	chip = devm_kzalloc(&spmi->dev,
			sizeof(struct qpnp_chg_chip), GFP_KERNEL);
	if (chip == NULL) {
		pr_err("kzalloc() failed.\n");
		return -ENOMEM;
	}

	chip->prev_usb_max_ma = -EINVAL;
	chip->fake_battery_soc = -EINVAL;
	chip->dev = &(spmi->dev);
	chip->spmi = spmi;
	chip->charger_enable_set = 0;

	chip->chrg_ocv_state = CHRG_OCV_NO_CHRG;
	chip->chrg_ocv_ocv_time = 240;
	chip->chrg_ocv_low_soc_thr = 20;
	chip->chrg_ocv_high_soc_thr = 60;
	chip->chrg_ocv_dchrg_thr_ma = 20;
	chip->chrg_ocv_cc_thr_uah = 1320;
	chip->chrg_ocv_time = 0;
	chip->chrg_ocv_cc_bf_uah = 0;
	chip->chrg_ocv_cc_af_uah = 0;
	chip->chrg_ocv_cc_ef_uah = 0;
	chip->chrg_ocv_bv_mv = 0;
	chip->maint_chrg = false;
	chip->float_timer_start = false;
	chip->bat_hotspot_thrs = 50000;
	chip->demo_mode = false;
	chip->demo_suspend_usb = false;

	strlcpy(chip->battery_info, "UNKNOWN", sizeof(chip->battery_info));
	chip->usb_psy = power_supply_get_by_name("usb");
	if (!chip->usb_psy) {
		pr_err("usb supply not found deferring probe\n");
		rc = -EPROBE_DEFER;
		goto fail_chg_enable;
	}

	mutex_init(&chip->jeita_configure_lock);
	spin_lock_init(&chip->usbin_health_monitor_lock);
#ifndef CONFIG_RTC_INTF_ALARM
	alarm_init(&chip->reduce_power_stage_alarm, ALARM_REALTIME,
			qpnp_chg_reduce_power_stage_callback);
#else
	alarm_init(&chip->reduce_power_stage_alarm, ANDROID_ALARM_RTC_WAKEUP,
			qpnp_chg_reduce_power_stage_callback);
#endif
	INIT_WORK(&chip->reduce_power_stage_work,
			qpnp_chg_reduce_power_stage_work);
	mutex_init(&chip->batfet_vreg_lock);
	INIT_WORK(&chip->ocp_clear_work,
			qpnp_chg_ocp_clear_work);
	INIT_WORK(&chip->batfet_lcl_work,
			qpnp_chg_batfet_lcl_work);
	INIT_WORK(&chip->insertion_ocv_work,
			qpnp_chg_insertion_ocv_work);
	mutex_init(&chip->update_ext_hi_state_lock);

	/* Get all device tree properties */
	rc = qpnp_charger_read_dt_props(chip);
	if (rc)
		return rc;

	/*
	 * Check if bat_if is set in DT and make sure VADC is present
	 * Also try loading the battery data profile if bat_if exists
	 */
	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("qpnp_chg: spmi resource absent\n");
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			goto fail_chg_enable;
		}

		if (subtype == SMBB_BAT_IF_SUBTYPE ||
			subtype == SMBBP_BAT_IF_SUBTYPE ||
			subtype == SMBCL_BAT_IF_SUBTYPE) {
			chip->vadc_dev = qpnp_get_vadc(chip->dev, "chg");
			if (IS_ERR(chip->vadc_dev)) {
				rc = PTR_ERR(chip->vadc_dev);
				if (rc != -EPROBE_DEFER)
					pr_err("vadc property missing\n");
				goto fail_chg_enable;
			}

			if (subtype == SMBB_BAT_IF_SUBTYPE) {
				chip->iadc_dev = qpnp_get_iadc(chip->dev,
						"chg");
				if (IS_ERR(chip->iadc_dev)) {
					rc = PTR_ERR(chip->iadc_dev);
					if (rc != -EPROBE_DEFER)
						pr_err("iadc property missing\n");
					goto fail_chg_enable;
				}
			}

			rc = qpnp_chg_load_battery_data(chip);
			if (rc)
				goto fail_chg_enable;
		}
	}

	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("qpnp_chg: spmi resource absent\n");
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		rc = qpnp_chg_read(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			goto fail_chg_enable;
		}

		switch (subtype) {
		case SMBB_CHGR_SUBTYPE:
		case SMBBP_CHGR_SUBTYPE:
		case SMBCL_CHGR_SUBTYPE:
			chip->chgr_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_BUCK_SUBTYPE:
		case SMBBP_BUCK_SUBTYPE:
		case SMBCL_BUCK_SUBTYPE:
			chip->buck_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + SEC_ACCESS,
				0xFF,
				0xA5, 1);

			rc = qpnp_chg_masked_write(chip,
				chip->buck_base + BUCK_VCHG_OV,
				0xff,
				0x00, 1);

			if (chip->duty_cycle_100p) {
				rc = qpnp_buck_set_100_duty_cycle_enable(chip,
						1);
				if (rc) {
					pr_err("failed to set duty cycle %d\n",
						rc);
					goto fail_chg_enable;
				}
			}

			break;
		case SMBB_BAT_IF_SUBTYPE:
		case SMBBP_BAT_IF_SUBTYPE:
		case SMBCL_BAT_IF_SUBTYPE:
			chip->bat_if_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_USB_CHGPTH_SUBTYPE:
		case SMBBP_USB_CHGPTH_SUBTYPE:
		case SMBCL_USB_CHGPTH_SUBTYPE:
			chip->usb_chgpth_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				if (rc != -EPROBE_DEFER)
					pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_DC_CHGPTH_SUBTYPE:
			chip->dc_chgpth_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_BOOST_SUBTYPE:
		case SMBBP_BOOST_SUBTYPE:
			chip->boost_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				if (rc != -EPROBE_DEFER)
					pr_err("Failed to init subtype 0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		case SMBB_MISC_SUBTYPE:
		case SMBBP_MISC_SUBTYPE:
		case SMBCL_MISC_SUBTYPE:
			chip->misc_base = resource->start;
			rc = qpnp_chg_hwinit(chip, subtype, spmi_resource);
			if (rc) {
				pr_err("Failed to init subtype=0x%x rc=%d\n",
						subtype, rc);
				goto fail_chg_enable;
			}
			break;
		default:
			pr_err("Invalid peripheral subtype=0x%x\n", subtype);
			rc = -EINVAL;
			goto fail_chg_enable;
		}
	}

	/* Configure Die Temp Alarms to Max for HVDCP */
	rc = qpnp_chg_masked_write(chip,
				   TEMP_ALARM_SHUTDOWN_CTL1,
				   ALM_SD_TEMP_THRESH_CNTRL,
				   ALM_SD_TEMP_THRESH_CNTRL, 1);
	if (rc)
		pr_debug("failed to set Temp Alarm Thresholds rc=%d\n", rc);

	dev_set_drvdata(&spmi->dev, chip);
	device_init_wakeup(&spmi->dev, 1);

	chip->insertion_ocv_uv = -EINVAL;
	chip->batt_present = qpnp_chg_is_batt_present(chip);
	if (chip->bat_if_base) {
		chip->batt_psy.name = "battery";
		chip->batt_psy.type = POWER_SUPPLY_TYPE_BATTERY;
		chip->batt_psy.properties = msm_batt_power_props;
		chip->batt_psy.num_properties =
			ARRAY_SIZE(msm_batt_power_props);
		chip->batt_psy.get_property = qpnp_batt_power_get_property;
		chip->batt_psy.set_property = qpnp_batt_power_set_property;
		chip->batt_psy.property_is_writeable =
				qpnp_batt_property_is_writeable;
		chip->batt_psy.external_power_changed =
				qpnp_batt_external_power_changed;
		chip->batt_psy.supplied_to = pm_batt_supplied_to;
		chip->batt_psy.num_supplicants =
				ARRAY_SIZE(pm_batt_supplied_to);

		rc = power_supply_register(chip->dev, &chip->batt_psy);
		if (rc < 0) {
			pr_err("batt failed to register rc = %d\n", rc);
			goto fail_chg_enable;
		}
		INIT_WORK(&chip->adc_measure_work,
			qpnp_bat_if_adc_measure_work);
		INIT_WORK(&chip->adc_disable_work,
			qpnp_bat_if_adc_disable_work);
	}

	INIT_DELAYED_WORK(&chip->eoc_work, qpnp_eoc_work);
	INIT_DELAYED_WORK(&chip->chrg_ocv_work, qpnp_chrg_ocv_work);
	INIT_DELAYED_WORK(&chip->arb_stop_work, qpnp_arb_stop_work);
	INIT_DELAYED_WORK(&chip->usbin_health_check,
			qpnp_usbin_health_check_work);
	INIT_WORK(&chip->soc_check_work, qpnp_chg_soc_check_work);
	INIT_DELAYED_WORK(&chip->aicl_check_work, qpnp_aicl_check_work);
	INIT_DELAYED_WORK(&chip->weak_check_work, qpnp_weak_check_work);
	INIT_DELAYED_WORK(&chip->update_heartbeat_work, update_heartbeat);

	if (chip->dc_chgpth_base) {
		chip->dc_psy.name = "qpnp-dc";
		chip->dc_psy.type = POWER_SUPPLY_TYPE_MAINS;
		chip->dc_psy.supplied_to = pm_power_supplied_to;
		chip->dc_psy.num_supplicants = ARRAY_SIZE(pm_power_supplied_to);
		chip->dc_psy.properties = pm_power_props_mains;
		chip->dc_psy.num_properties = ARRAY_SIZE(pm_power_props_mains);
		chip->dc_psy.get_property = qpnp_power_get_property_mains;
		chip->dc_psy.set_property = qpnp_dc_power_set_property;
		chip->dc_psy.property_is_writeable =
				qpnp_dc_property_is_writeable;

		rc = power_supply_register(chip->dev, &chip->dc_psy);
		if (rc < 0) {
			pr_err("power_supply_register dc failed rc=%d\n", rc);
			goto unregister_batt;
		}
	}

	rc = device_create_file(&spmi->dev,
				&dev_attr_hotspot_temp);
	if (rc) {
		pr_err("couldn't create hotspot_temp\n");
		goto unregister_dc_psy;
	}

	/* Turn on appropriate workaround flags */
	rc = qpnp_chg_setup_flags(chip);
	if (rc < 0) {
		pr_err("failed to setup flags rc=%d\n", rc);
		goto unregister_dc_psy;
	}

	if (chip->maxinput_dc_ma && chip->dc_chgpth_base) {
		rc = qpnp_chg_idcmax_set(chip, chip->maxinput_dc_ma);
		if (rc) {
			pr_err("Error setting idcmax property %d\n", rc);
			goto unregister_dc_psy;
		}
	}

	if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
							&& chip->bat_if_base) {
		chip->adc_param.low_temp = chip->cool_bat_decidegc;
		chip->adc_param.high_temp = chip->warm_bat_decidegc;
		chip->adc_param.timer_interval = ADC_MEAS2_INTERVAL_1S;
		chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
		chip->adc_param.btm_ctx = chip;
		chip->adc_param.threshold_notification =
						qpnp_chg_adc_notification;
		chip->adc_param.channel = LR_MUX1_BATT_THERM;

		if (get_prop_batt_present(chip)) {
			rc = qpnp_adc_tm_channel_measure(chip->adc_tm_dev,
							&chip->adc_param);
			if (rc) {
				pr_err("request ADC error %d\n", rc);
				goto unregister_dc_psy;
			}
		}
	}
	rc = qpnp_chg_bat_if_configure_btc(chip);
	if (rc) {
		pr_err("failed to configure btc %d\n", rc);
		goto unregister_dc_psy;
	}
	chip->use_max_vdd = true;
	qpnp_chg_charge_en(chip, !chip->charging_disabled);
	qpnp_chg_force_run_on_batt(chip, chip->charging_disabled);
	qpnp_chg_set_appropriate_vddmax(chip);

	if (chip->parallel_ovp_mode) {
		rc = override_dcin_ilimit(chip, 1);
		if (rc) {
			pr_err("Override DCIN LLIMIT %d\n", rc);
			goto unregister_dc_psy;
		}
	}

	rc = qpnp_chg_request_irqs(chip);
	if (rc) {
		pr_err("failed to request interrupts %d\n", rc);
		goto unregister_dc_psy;
	}

	qpnp_chg_usb_chg_gone_irq_handler(chip->chg_gone.irq, chip);
	qpnp_chg_usb_usbin_valid_irq_handler(chip->usbin_valid.irq, chip);
	qpnp_chg_dc_dcin_valid_irq_handler(chip->dcin_valid.irq, chip);
	power_supply_set_present(chip->usb_psy,
			qpnp_chg_is_usb_chg_plugged_in(chip));

	the_chip = chip;

	rc = device_create_file(chip->dev,
				&dev_attr_force_demo_mode);
	if (rc) {
		pr_err("couldn't create force_demo_mode\n");
		goto unregister_dc_psy;
	}

	/* Set USB psy online to avoid userspace from shutting down if battery
	 * capacity is at zero and no chargers online. */
	if (qpnp_chg_is_usb_chg_plugged_in(chip))
		power_supply_set_online(chip->usb_psy, 1);

	schedule_delayed_work(&chip->aicl_check_work,
		msecs_to_jiffies(EOC_CHECK_PERIOD_MS));
	schedule_delayed_work(&chip->update_heartbeat_work,
		msecs_to_jiffies(UPDATE_HEARTBEAT_MS));
	pm_wakeup_event(chip->dev, 15000);
	qpnp_chg_regulator_batfet_set(chip, 1);
	pr_info("success chg_dis = %d, bpd = %d, usb = %d, dc = %d b_health = %d batt_present = %d\n",
			chip->charging_disabled,
			chip->bpd_detection,
			qpnp_chg_is_usb_chg_plugged_in(chip),
			qpnp_chg_is_dc_chg_plugged_in(chip),
			get_prop_batt_present(chip),
			get_prop_batt_health(chip));
	return 0;

unregister_dc_psy:
	if (chip->dc_chgpth_base)
		power_supply_unregister(&chip->dc_psy);
unregister_batt:
	if (chip->bat_if_base)
		power_supply_unregister(&chip->batt_psy);
fail_chg_enable:
	regulator_unregister(chip->otg_vreg.rdev);
	regulator_unregister(chip->boost_vreg.rdev);
	the_chip = NULL;
	return rc;
}

static int __devexit
qpnp_charger_remove(struct spmi_device *spmi)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(&spmi->dev);
	if ((chip->cool_bat_decidegc || chip->warm_bat_decidegc)
						&& chip->batt_present) {
		qpnp_adc_tm_disable_chan_meas(chip->adc_tm_dev,
							&chip->adc_param);
	}

	cancel_delayed_work_sync(&chip->aicl_check_work);
	device_remove_file(&spmi->dev, &dev_attr_hotspot_temp);
	power_supply_unregister(&chip->dc_psy);
	cancel_work_sync(&chip->soc_check_work);
	cancel_delayed_work_sync(&chip->usbin_health_check);
	cancel_delayed_work_sync(&chip->arb_stop_work);
	cancel_delayed_work_sync(&chip->eoc_work);
	cancel_work_sync(&chip->adc_disable_work);
	cancel_work_sync(&chip->adc_measure_work);
	power_supply_unregister(&chip->batt_psy);
	cancel_work_sync(&chip->batfet_lcl_work);
	cancel_work_sync(&chip->insertion_ocv_work);
	cancel_work_sync(&chip->reduce_power_stage_work);
	alarm_cancel(&chip->reduce_power_stage_alarm);

	mutex_destroy(&chip->batfet_vreg_lock);
	mutex_destroy(&chip->jeita_configure_lock);
	mutex_destroy(&chip->update_ext_hi_state_lock);

	cancel_delayed_work_sync(&chip->chrg_ocv_work);
	cancel_delayed_work_sync(&chip->update_heartbeat_work);
	regulator_unregister(chip->otg_vreg.rdev);
	regulator_unregister(chip->boost_vreg.rdev);

	device_remove_file(chip->dev,
			   &dev_attr_force_demo_mode);

	kfree(chip->pc_temp_ocv_lut);
	return 0;
}

static int qpnp_chg_resume(struct device *dev)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	cancel_delayed_work_sync(&chip->update_heartbeat_work);

	if (chip->bat_if_base) {
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BATT_THERM_FORCE_ON, 1);
		if (rc)
			pr_debug("failed to force on VREF_BAT_THM rc=%d\n", rc);

		schedule_delayed_work(&chip->update_heartbeat_work, 0);
	}

	return rc;
}

static int qpnp_chg_suspend(struct device *dev)
{
	struct qpnp_chg_chip *chip = dev_get_drvdata(dev);
	int rc = 0;

	if (chip->bat_if_base) {
		rc = qpnp_chg_masked_write(chip,
			chip->bat_if_base + BAT_IF_VREF_BAT_THM_CTRL,
			VREF_BATT_THERM_FORCE_ON,
			VREF_BAT_THM_ENABLED_FSM, 1);
		if (rc)
			pr_debug("failed to set FSM VREF_BAT_THM rc=%d\n", rc);
	}

	return rc;
}

static const struct dev_pm_ops qpnp_chg_pm_ops = {
	.resume		= qpnp_chg_resume,
	.suspend	= qpnp_chg_suspend,
};

static struct spmi_driver qpnp_charger_driver = {
	.probe		= qpnp_charger_probe,
	.remove		= __devexit_p(qpnp_charger_remove),
	.driver		= {
		.name		= QPNP_CHARGER_DEV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= qpnp_charger_match_table,
		.pm		= &qpnp_chg_pm_ops,
	},
};

/**
 * qpnp_chg_init() - register spmi driver for qpnp-chg
 */
int __init
qpnp_chg_init(void)
{
	return spmi_driver_register(&qpnp_charger_driver);
}
module_init(qpnp_chg_init);

static void __exit
qpnp_chg_exit(void)
{
	spmi_driver_unregister(&qpnp_charger_driver);
}
module_exit(qpnp_chg_exit);


MODULE_DESCRIPTION("QPNP charger driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" QPNP_CHARGER_DEV_NAME);
