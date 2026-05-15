/* SPDX-License-Identifier: BSD-4-Clause */
/* Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr> */
/*
 * Per-register human-readable decoders, used by the `explain`
 * subcommand. NULL decoders fall back to raw hex (see cmd_explain
 * in mpq_subcmd.c).
 *
 * Some decoders (notably dec_vout_mv) need cross-register context:
 * VOUT_COMMAND / READ_VOUT / VOUT_*_LIMIT scaling depends on
 * VOUT_MODE (PMBus 1.3 Part II §8.3.1). The decoder typedef takes
 * a `const struct dump_file *dump` to let any decoder look up
 * companion registers.
 */

#include <stdio.h>
#include "mpq_config.h"

/*
 * VOUT decoding follows the PMBus VOUT_MODE register (20h):
 *
 *   mode bits[7:5] = 0 (LINEAR16): V = raw * 2^exp where exp is the
 *                    5-bit signed value in param bits[4:0]. The
 *                    MPQ8785/MPQ8646 on the target board ships
 *                    VOUT_MODE = 0x17 -> mode=0, param=0x17 -> exp=-9,
 *                    so 1 LSB = 1/512 V ~= 1.953 mV.
 *   mode bits[7:5] = 1 (VID): V = (raw - VID_offset) / VID_step.
 *                    MPQ-style VID maps raw/640 V (1.5625 mV/LSB).
 *                    Not used on the target board but kept as a fallback.
 *   mode bits[7:5] = 2 (DIRECT): V = (raw - b) / (m * 10^R).
 *                    DIRECT coefficients here match the kernel
 *                    driver's mpq8785_info table (m=64, b=0, R=1
 *                    in VID mode -> raw/640 V).
 *
 * MPQ8785/MPQ8646 DIRECT-format coefficients per the in-tree
 * pmbus/mpq8785 driver:
 *   VIN  m=4,  b=0, R=1  ->  V = raw / 40   (25 mV / LSB)
 *   IOUT m=16, b=0, R=0  ->  A = raw / 16   (62.5 mA / LSB)
 *   TEMP m=1,  b=0, R=0  ->  C = raw
 *
 * All scaled to mV / mA / C for display.
 */

/*
 * Return the LINEAR16 exponent from VOUT_MODE if the dump has it
 * set to LINEAR mode, or 0 (meaning "fall back to DIRECT/VID
 * scaling") if VOUT_MODE is absent or non-LINEAR. Exponent is
 * 5-bit signed (range -16 .. +15).
 */
static int
vout_mode_linear_exp(const struct dump_file *dump) {
	uint16_t v;

	if (!dump_get(dump, MPQ_REG_VOUT_MODE, &v))
		return 0;

	uint8_t b = (uint8_t)v;
	if (((b >> MPQ_VOUT_MODE_BITS_SHIFT) & MPQ_VOUT_MODE_BITS_MASK)
	    != MPQ_VOUT_MODE_LINEAR16)
		return 0;	/* not LINEAR, caller picks fallback */

	int exp = b & MPQ_VOUT_MODE_PARAM_MASK;
	if (exp & MPQ_VOUT_MODE_PARAM_SIGN_BIT)
		exp |= ~MPQ_VOUT_MODE_PARAM_MASK;	/* sign-extend */

	return exp;
}

void
dec_vout_mv(uint16_t v, char *out, size_t n, const struct dump_file *dump) {
	int exp = vout_mode_linear_exp(dump);
	unsigned int mv;

	if (exp < 0) {
		/* LINEAR16: V = raw * 2^exp, exp < 0 -> raw / 2^-exp.
		 * mv = raw * 1000 / (1 << -exp). */
		mv = (unsigned int)(((unsigned long)v * MPQ_MV_PER_V)
				    >> (unsigned)-exp);
	} else if (exp > 0) {
		mv = (unsigned int)(((unsigned long)v * MPQ_MV_PER_V)
				    << (unsigned)exp);
	} else {
		/* No / non-LINEAR VOUT_MODE: fall back to MPS VID
		 * scaling (raw / 640 V == raw * 25 / 16 mV). */
		mv = (unsigned int)v * MPQ_VID_VOUT_NUM / MPQ_VID_VOUT_DEN;
	}

	snprintf(out, n, "%u mV (%u.%03u V)",
		 mv, mv / MPQ_MV_PER_V, mv % MPQ_MV_PER_V);
}

void
dec_vin_mv(uint16_t v, char *out, size_t n, const struct dump_file *dump) {
	(void)dump;
	unsigned int mv = (unsigned int)v * MPQ_VIN_MV_PER_LSB;
	snprintf(out, n, "%u mV (%u.%03u V)",
		 mv, mv / MPQ_MV_PER_V, mv % MPQ_MV_PER_V);
}

void
dec_iout_ma(uint16_t v, char *out, size_t n, const struct dump_file *dump) {
	(void)dump;
	unsigned int ma = (unsigned int)v * MPQ_IOUT_NUM / MPQ_IOUT_DEN;
	snprintf(out, n, "%u mA (%u.%03u A)",
		 ma, ma / MPQ_MV_PER_V, ma % MPQ_MV_PER_V);
}

void
dec_temp_c(uint16_t v, char *out, size_t n, const struct dump_file *dump) {
	(void)dump;
	int16_t c = (int16_t)v;
	snprintf(out, n, "%d C", c);
}

/*
 * PMBus LINEAR11: 5-bit signed exponent (bits 15..11) +
 * 11-bit signed mantissa (bits 10..0). value = mant * 2^exp.
 */
void
dec_linear11_us(uint16_t v, char *out, size_t n, const struct dump_file *dump) {
	(void)dump;
	int e = (int)(v >> MPQ_LINEAR11_EXP_SHIFT);
	if (e & MPQ_LINEAR11_EXP_SIGN_BIT)
		e |= ~MPQ_LINEAR11_EXP_MASK;	/* sign-extend exponent */

	int m = (int)(v & MPQ_LINEAR11_MANT_MASK);
	if (m & MPQ_LINEAR11_MANT_SIGN_BIT)
		m |= ~MPQ_LINEAR11_MANT_MASK;	/* sign-extend mantissa */

	/* Use integer arithmetic, scaled to microseconds. */
	long long us;
	if (e >= 0)
		us = (long long)m << e;
	else
		us = (long long)m >> -e;

	snprintf(out, n, "%lld us (raw m=%d e=%d)", us, m, e);
}

/* ON_OFF_CONFIG (PMBus 1.3 Part II §11.3) bit layout */
#define MPQ_OOC_OPER_REQ        (1U << 4)
#define MPQ_OOC_CTRL_REQ        (1U << 2)
#define MPQ_OOC_CTRL_POL_AH     (1U << 1)
#define MPQ_OOC_CTRL_TURN_FAST  (1U << 0)

void
dec_on_off_config(uint16_t v, char *out, size_t n,
		  const struct dump_file *dump) {
	(void)dump;
	uint8_t b = v & 0xFF;
	snprintf(out, n,
		 "OPER=%s CTRL=%s%s %s-turn-off",
		 (b & MPQ_OOC_OPER_REQ)       ? "req"   : "ign",
		 (b & MPQ_OOC_CTRL_REQ)       ? "req-"  : "ign-",
		 (b & MPQ_OOC_CTRL_POL_AH)    ? "AH"    : "AL",
		 (b & MPQ_OOC_CTRL_TURN_FAST) ? "fast"  : "soft");
}

void
dec_write_protect(uint16_t v, char *out, size_t n,
		  const struct dump_file *dump) {
	(void)dump;
	switch (v & 0xFF) {
	case MPQ_WP_UNLOCKED:
		snprintf(out, n, "unlocked");
		break;
	case MPQ_WP_OPERATION_ONLY:
		snprintf(out, n, "OPERATION-only");
		break;
	case MPQ_WP_WP_ONLY:
		snprintf(out, n, "WP-only");
		break;
	default:
		snprintf(out, n, "0x%02x", v & 0xFF);
		break;
	}
}

void
dec_mfr_pmbus_lock(uint16_t v, char *out, size_t n,
		   const struct dump_file *dump) {
	(void)dump;
	switch (v & MPQ_PMBUS_LOCK_MASK) {
	case MPQ_PMBUS_LOCK_UNLOCKED:
		snprintf(out, n, "unlocked");
		break;
	case MPQ_PMBUS_LOCK_EXCEPT_VOUT_CMD:
		snprintf(out, n, "locked except VOUT_COMMAND");
		break;
	case MPQ_PMBUS_LOCK_FULL:
		snprintf(out, n, "fully locked");
		break;
	default:
		snprintf(out, n, "reserved 0b%c%c",
			 (v & 2) ? '1' : '0',
			 (v & 1) ? '1' : '0');
		break;
	}
}

void
dec_mfr_cfg_ext(uint16_t v, char *out, size_t n,
		const struct dump_file *dump) {
	(void)dump;
	snprintf(out, n,
		 "CLR_LAST_EN=%c RST_DEASS_CFG=%c (raw 0x%04x)",
		 (v & MPQ_MFR_CFG_EXT_CLR_LAST)      ? '1' : '0',
		 (v & MPQ_MFR_CFG_EXT_RST_DEASS_CFG) ? '1' : '0',
		 v);
}

void
dec_vout_mode(uint16_t v, char *out, size_t n,
	      const struct dump_file *dump) {
	(void)dump;
	const char *mode;
	int mode_bits = (v >> MPQ_VOUT_MODE_BITS_SHIFT)
			& MPQ_VOUT_MODE_BITS_MASK;
	int param = v & MPQ_VOUT_MODE_PARAM_MASK;

	switch (mode_bits) {
	case MPQ_VOUT_MODE_LINEAR16:
		mode = "LINEAR16";
		break;
	case MPQ_VOUT_MODE_VID:
		mode = "VID";
		break;
	case MPQ_VOUT_MODE_DIRECT:
		mode = "DIRECT";
		break;
	default:
		mode = "reserved";
		break;
	}

	if (mode_bits == MPQ_VOUT_MODE_LINEAR16) {
		/* LINEAR16: param is 5-bit two's complement exponent. */
		int exp = param;
		if (exp & MPQ_VOUT_MODE_PARAM_SIGN_BIT)
			exp |= ~MPQ_VOUT_MODE_PARAM_MASK;

		snprintf(out, n, "mode=%s exp=%d (1 LSB = 2^%d V)",
			 mode, exp, exp);
	} else {
		snprintf(out, n, "mode=%s param=0x%02x", mode, param);
	}
}

/*
 * MPS-specific PROTECTION_LAST (0xFB) bit names. NVM-backed
 * post-mortem: which fault tripped the chip on the previous
 * cycle. Cleared via CLEAR_LAST_FAULT (gated by MFR_CFG_EXT bit6).
 */
static const struct {
	uint16_t mask;
	const char *name;
} protection_last_bits[] = {
	{ 1u << 15, "INIT_FAULT"       },
	{ 1u << 14, "NVM_CRC_ERROR"    },
	{ 1u << 13, "NVM_FAULT"        },
	{ 1u << 12, "OC_PHASE_FAULT"   },
	{ 1u << 11, "OTP_SELF_FAULT"   },
	{ 1u <<  9, "SWITCH_PRD_FAULT" },
	{ 1u <<  8, "VIN_OV_FAULT"     },
	{ 1u <<  7, "VOUT_OV_FAULT"    },
	{ 1u <<  6, "VOUT_UV_FAULT"    },
	{ 1u <<  5, "OC_TOT_FAULT"     },
	{ 1u <<  4, "VIN_UVLO_FAULT"   },
	{ 1u <<  3, "DRMOS_OTP"        },
	{ 0, NULL }
};

void
dec_protection_last(uint16_t v, char *out, size_t n,
		    const struct dump_file *dump) {
	(void)dump;
	if (v == 0) {
		snprintf(out, n, "clean");
		return;
	}

	size_t off = 0;
	for (size_t i = 0; protection_last_bits[i].mask; i++) {
		if (!(v & protection_last_bits[i].mask))
			continue;
		int w = snprintf(out + off, n - off, "%s%s",
				 off > 0 ? " " : "",
				 protection_last_bits[i].name);
		if (w <= 0 || (size_t)w >= n - off)
			break;
		off += w;
	}
}

/*
 * STATUS_WORD (0x79) bit names. The MPQ8785/MPQ8646 reassigns
 * three of the PMBus 1.3 standard bits to MPS-specific meanings
 * (NVM_SUMMARY, WATCH_DOG, DRMOS_FAULT).
 */
static const struct {
	uint16_t mask;
	const char *name;
} status_word_bits[] = {
	{ 1u << 15, "VOUT"         },
	{ 1u << 14, "IOUT_POUT"    },
	{ 1u << 13, "INPUT"        },
	{ 1u << 12, "NVM_SUMMARY"  },	/* MPS extension */
	{ 1u << 11, "POWER_GOOD#"  },
	{ 1u << 10, "FANS"         },
	{ 1u <<  9, "OTHER"        },
	{ 1u <<  8, "WATCH_DOG"    },	/* MPS extension */
	{ 1u <<  7, "BUSY"         },
	{ 1u <<  6, "OFF"          },
	{ 1u <<  5, "VOUT_OV_FAULT" },
	{ 1u <<  4, "IOUT_OC_FAULT" },
	{ 1u <<  3, "VIN_UV_FAULT"  },
	{ 1u <<  2, "TEMP"          },
	{ 1u <<  1, "CML"           },
	{ 1u <<  0, "DRMOS_FAULT"   },	/* MPS extension */
	{ 0, NULL }
};

void
dec_status_word(uint16_t v, char *out, size_t n,
		const struct dump_file *dump) {
	(void)dump;
	if (v == 0) {
		snprintf(out, n, "clean");
		return;
	}

	size_t off = 0;
	for (size_t i = 0; status_word_bits[i].mask; i++) {
		if (!(v & status_word_bits[i].mask))
			continue;
		int w = snprintf(out + off, n - off, "%s%s",
				 off > 0 ? " " : "",
				 status_word_bits[i].name);
		if (w <= 0 || (size_t)w >= n - off)
			break;
		off += w;
	}
}
