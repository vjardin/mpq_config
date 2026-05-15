/* SPDX-License-Identifier: BSD-4-Clause */
/* Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr> */
/*
 * Register inventory for the MPS MPQ8785 / MPQ8646.
 *
 * One entry per chip register we know about.
 *   clone_safe = false   dumped on read but SKIPPED on write
 *                        (unless --force).
 *   skip_dump  = true    excluded from the default dump but can
 *                        be opted in via `read --all`.
 */

#include "mpq_config.h"

const struct mpq_reg mpq_regs[] = {
	/*  PMBus 1.3 standard control / VOUT / VIN / IOUT cal */
	{ 0x01, MPQ_W_BYTE, "OPERATION",            false, false, NULL,                "output on/off + margin (PMBus 1.3 §11.1)" },
	{ 0x02, MPQ_W_BYTE, "ON_OFF_CONFIG",        true,  false, dec_on_off_config,   "" },
	{ 0x10, MPQ_W_BYTE, "WRITE_PROTECT",        true,  false, dec_write_protect,   "" },
	{ 0x20, MPQ_W_BYTE, "VOUT_MODE",            false, true,  dec_vout_mode,       "informational" },
	{ 0x21, MPQ_W_WORD, "VOUT_COMMAND",         true,  false, dec_vout_mv,         "VID/DIRECT-encoded" },
	{ 0x22, MPQ_W_WORD, "VOUT_TRIM",            true,  false, NULL,                "" },
	{ 0x24, MPQ_W_WORD, "VOUT_MAX",             true,  false, dec_vout_mv,         "" },
	{ 0x25, MPQ_W_WORD, "VOUT_MARGIN_HIGH",     true,  false, dec_vout_mv,         "" },
	{ 0x26, MPQ_W_WORD, "VOUT_MARGIN_LOW",      true,  false, dec_vout_mv,         "" },
	{ 0x29, MPQ_W_WORD, "VOUT_SCALE_LOOP",      true,  false, NULL,                "feedback divider" },
	{ 0x2B, MPQ_W_WORD, "VOUT_MIN",             true,  false, dec_vout_mv,         "" },
	{ 0x35, MPQ_W_WORD, "VIN_ON",               true,  false, dec_vin_mv,          "" },
	{ 0x36, MPQ_W_WORD, "VIN_OFF",              true,  false, dec_vin_mv,          "" },
	{ 0x38, MPQ_W_WORD, "IOUT_CAL_GAIN",        true,  false, NULL,                "" },
	{ 0x39, MPQ_W_WORD, "IOUT_CAL_OFFSET",      true,  false, NULL,                "" },

	/* Standard threshold limits */
	{ 0x40, MPQ_W_WORD, "VOUT_OV_FAULT_LIMIT",  true,  false, dec_vout_mv,         "" },
	{ 0x42, MPQ_W_WORD, "VOUT_OV_WARN_LIMIT",   true,  false, dec_vout_mv,         "" },
	{ 0x43, MPQ_W_WORD, "VOUT_UV_WARN_LIMIT",   true,  false, dec_vout_mv,         "" },
	{ 0x44, MPQ_W_WORD, "VOUT_UV_FAULT_LIMIT",  true,  false, dec_vout_mv,         "" },
	{ 0x46, MPQ_W_WORD, "IOUT_OC_FAULT_LIMIT",  true,  false, dec_iout_ma,         "" },
	{ 0x4A, MPQ_W_WORD, "IOUT_OC_WARN_LIMIT",   true,  false, dec_iout_ma,         "" },
	{ 0x4D, MPQ_W_WORD, "VBOOT_SET_FOR_X0h_ADDR", true, false, dec_vout_mv,        "VBOOT for ADDR=0x10 strap" },
	{ 0x4F, MPQ_W_WORD, "OT_FAULT_LIMIT",       true,  false, dec_temp_c,          "" },
	{ 0x51, MPQ_W_WORD, "OT_WARN_LIMIT",        true,  false, dec_temp_c,          "" },
	{ 0x55, MPQ_W_WORD, "VIN_OV_FAULT_LIMIT",   true,  false, dec_vin_mv,          "" },
	{ 0x57, MPQ_W_WORD, "VIN_OV_WARN_LIMIT",    true,  false, dec_vin_mv,          "" },
	{ 0x58, MPQ_W_WORD, "VIN_UV_WARN_LIMIT",    true,  false, dec_vin_mv,          "" },
	{ 0x59, MPQ_W_WORD, "VIN_UV_FAULT_LIMIT",   true,  false, dec_vin_mv,          "" },
	{ 0x5E, MPQ_W_WORD, "VBOOT_SET_FOR_X4h_ADDR", true, false, dec_vout_mv,        "VBOOT for ADDR=0x14 strap" },
	{ 0x5F, MPQ_W_WORD, "VBOOT_SET_FOR_X8h_ADDR", true, false, dec_vout_mv,        "VBOOT for ADDR=0x18 strap" },

	/* Soft-start / soft-stop */
	{ 0x60, MPQ_W_WORD, "TON_DELAY",            true,  false, dec_linear11_us,     "" },
	{ 0x61, MPQ_W_WORD, "TON_RISE",             true,  false, dec_linear11_us,     "" },
	{ 0x64, MPQ_W_WORD, "TOFF_DELAY",           true,  false, dec_linear11_us,     "" },
	{ 0x65, MPQ_W_WORD, "TOFF_FALL",            true,  false, dec_linear11_us,     "" },
	{ 0x6A, MPQ_W_WORD, "VBOOT_SET_FOR_XEh_ADDR", true, false, dec_vout_mv,        "VBOOT for ADDR=0x1E strap" },

	/* Telemetry (dump-only, exclude from default --no-all) */
	{ 0x79, MPQ_W_WORD, "STATUS_WORD",          false, true,  dec_status_word,     "live state" },
	{ 0x88, MPQ_W_WORD, "READ_VIN",             false, true,  dec_vin_mv,          "telemetry" },
	{ 0x8B, MPQ_W_WORD, "READ_VOUT",            false, true,  dec_vout_mv,         "telemetry" },
	{ 0x8C, MPQ_W_WORD, "READ_IOUT",            false, true,  dec_iout_ma,         "telemetry" },
	{ 0x8D, MPQ_W_WORD, "READ_TEMPERATURE_1",   false, true,  dec_temp_c,          "telemetry" },
	{ 0x98, MPQ_W_BYTE, "PMBUS_REVISION",       false, true,  NULL,                "spec version" },
	{ 0x9B, MPQ_W_BYTE, "MFR_REVISION",         false, true,  NULL,                "" },

	/* MPS identity */
	{ 0xC0, MPQ_W_WORD, "MFR_CONFIG_ID",        false, false, NULL,                "board-specific NVM ID" },
	{ 0xC1, MPQ_W_WORD, "MFR_CONFIG_CODE_REV",  false, false, NULL,                "MPS NVM image rev" },
	{ 0xC2, MPQ_W_WORD, "MFR_PRODUCT_REV_USER", true,  false, NULL,                "operator-settable" },
	{ 0xC3, MPQ_W_BYTE, "MFR_SILICON_REV",      false, false, NULL,                "die rev" },
	{ 0xC5, MPQ_W_WORD, "MFR_APS_LEVEL",        true,  false, NULL,                "AVS level" },

	/* MPS mode / loop / timing config */
	{ 0xD0, MPQ_W_WORD, "MFR_CONFIG_A",         true,  false, NULL,                "" },
	{ 0xD1, MPQ_W_WORD, "MFR_FS_CFG",           true,  false, NULL,                "switching freq" },
	{ 0xD2, MPQ_W_BYTE, "MFR_ADDR_PMBUS",       false, false, NULL,                "would re-address target" },
	{ 0xD3, MPQ_W_WORD, "MFR_VOUT_RATE",        true,  false, NULL,                "" },
	{ 0xD4, MPQ_W_WORD, "MFR_PWM_TIME_CFG",     true,  false, NULL,                "" },
	{ 0xD5, MPQ_W_WORD, "MFR_PWM_TIME_CFG2",    true,  false, NULL,                "" },
	{ 0xD6, MPQ_W_WORD, "MFR_PHASE_BLANK_TIME", true,  false, NULL,                "" },
	{ 0xD7, MPQ_W_WORD, "MFR_PHASE_SLOPE_BLANK_TIME", true, false, NULL,           "" },
	{ 0xD8, MPQ_W_WORD, "MFR_SLOPE_BLANK_TIME", true,  false, NULL,                "" },
	{ 0xD9, MPQ_W_WORD, "MFR_BLANK_TIME_LV",    true,  false, NULL,                "" },
	{ 0xDA, MPQ_W_WORD, "MFR_SLOPE_CNT_DCM",    true,  false, NULL,                "" },
	{ 0xDB, MPQ_W_WORD, "MFR_SLOPE_SR_DCM",     true,  false, NULL,                "" },
	{ 0xDC, MPQ_W_WORD, "MFR_SW_BLOCK_LIMIT",   true,  false, NULL,                "" },
	{ 0xDD, MPQ_W_WORD, "MFR_VCOMP",            true,  false, NULL,                "compensation" },
	{ 0xDE, MPQ_W_WORD, "MFR_DROOP_CFG",        true,  false, NULL,                "load-line" },
	{ 0xDF, MPQ_W_WORD, "MFR_CONFIG_B",         true,  false, NULL,                "" },
	{ 0xE0, MPQ_W_WORD, "MFR_DC_LOOP_CTRL",     true,  false, NULL,                "" },
	{ 0xE1, MPQ_W_WORD, "MFR_CB_LOOP_CTRL",     true,  false, NULL,                "" },
	{ 0xE2, MPQ_W_WORD, "MFR_FS_LOOP_CTRL",     true,  false, NULL,                "" },
	{ 0xE3, MPQ_W_WORD, "MFR_VIN_CFG",          true,  false, NULL,                "" },
	{ 0xE4, MPQ_W_WORD, "MFR_VIN_SCALE",        true,  false, NULL,                "" },
	{ 0xE5, MPQ_W_WORD, "MFR_TEMP_TUNE",        true,  false, NULL,                "" },
	{ 0xE6, MPQ_W_WORD, "MFR_PROTECT_CFG",      true,  false, NULL,                "" },
	{ 0xE7, MPQ_W_WORD, "MFR_PROTECT_LEVEL",    true,  false, NULL,                "" },
	{ 0xE8, MPQ_W_WORD, "MFR_PRT_DELAY",        true,  false, NULL,                "" },
	{ 0xE9, MPQ_W_WORD, "SMBALERT_MASK",        true,  false, NULL,                "fault summary mask" },
	{ 0xEA, MPQ_W_WORD, "MFR_NOCP_OCP_SET",     true,  false, NULL,                "" },
	{ 0xEB, MPQ_W_WORD, "MFR_LEVEL_SEL2",       true,  false, NULL,                "" },
	{ 0xEC, MPQ_W_WORD, "MFR_PG_CFG",           true,  false, NULL,                "PG pin behaviour" },
	{ 0xED, MPQ_W_WORD, "MFR_PS_CTRL",          true,  false, NULL,                "" },
	{ 0xEE, MPQ_W_WORD, "MFR_PMBUS_LOCK",       true,  false, dec_mfr_pmbus_lock,  "bits[1:0] gate writes" },
	{ 0xEF, MPQ_W_WORD, "MFR_SET_SYNC_CFG",     true,  false, NULL,                "" },
	{ 0xF0, MPQ_W_WORD, "MFR_SLAVE_PROTECT",    true,  false, NULL,                "slave-phase write gate" },
	{ 0xF1, MPQ_W_WORD, "MFR_CTRL",             true,  false, NULL,                "" },
	{ 0xF2, MPQ_W_WORD, "MFR_AUTO_SLOPE_CFG",   true,  false, NULL,                "" },
	{ 0xF3, MPQ_W_WORD, "MFR_SLOPE_DELTA_LIMIT", true, false, NULL,                "" },

	/* Protection / strap */
	{ 0xF4, MPQ_W_WORD, "MFR_RETRY_TIMES",      false, false, NULL,                "retry counter, live state" },
	{ 0xF5, MPQ_W_WORD, "MFR_CFG_EXT",          true,  false, dec_mfr_cfg_ext,     "bit6 = CLEAR_LAST gate" },
	{ 0xF6, MPQ_W_WORD, "MFR_CDROOP_SET",       true,  false, NULL,                "droop config" },
	{ 0xF7, MPQ_W_WORD, "MFR_CFG_BACKUP",       true,  false, NULL,                "" },
	{ 0xF8, MPQ_W_WORD, "CRC_USER",             false, false, NULL,                "chip-computed NVM CRC, read-only" },
	{ 0xFB, MPQ_W_WORD, "PROTECTION_LAST",      false, false, dec_protection_last, "NVM post-mortem, per-chip-event" },
	{ 0xFC, MPQ_W_WORD, "MFR_VBOOT_CFG",        false, false, NULL,                "ADDR/VBOOT strap-latched" },
};

const size_t mpq_num_regs = sizeof(mpq_regs) / sizeof(mpq_regs[0]);

const struct mpq_reg *
lookup_reg(uint8_t reg) {
	for (size_t i = 0; i < mpq_num_regs; i++)
		if (mpq_regs[i].reg == reg)
			return &mpq_regs[i];

	return NULL;
}
