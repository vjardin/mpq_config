/* SPDX-License-Identifier: BSD-4-Clause */
/* Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr> */
/*
 * mpq_config: shared types, constants and public function prototypes
 * for the MPS MPQ8785 / MPQ8646 chip-config dump / restore / convert
 * tool. One translation unit per concern (i2c / dump / decoders /
 * regs / subcmd / main); each one includes this header.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Process exit codes
 * extend EXIT_SUCCESS and EXIT_FAILURE
 */
/* MPQ_EXIT_USAGE   bad argument vector / unknown subcommand */
/* MPQ_EXIT_REFUSE  precondition not met (e.g. alarm-poll active) */
#define MPQ_EXIT_USAGE          2
#define MPQ_EXIT_REFUSE         3

/*
 * Buffer sizing
 */
#define MPQ_DUMP_SOURCE_MAX     128
#define MPQ_DUMP_GENERATED_MAX  64
#define MPQ_DECODED_MAX         160
#define MPQ_PATH_MAX            256
#define MPQ_LINE_MAX            512
#define MPQ_DEBUGFS_POLL_PATH_MAX 128

/* Inventory grows infrequently; bound > NUM_REGS leaves head-room. */
#define MPQ_MAX_DUMP_ENTRIES    128

/*
 * Register widths in bytes
 */
enum {
	MPQ_W_BYTE = 1,
	MPQ_W_WORD = 2,
};

/*
 * PMBus / MPS register addresses (selected, by command code)
 *
 * Every numeric register address used in code below is defined
 * here; bare hex (0x88, 0xC0, ...) in the inventory table stays
 * because that table is the canonical map.
 */
#define MPQ_REG_OPERATION               0x01
#define MPQ_REG_WRITE_PROTECT           0x10
#define MPQ_REG_STORE_USER_ALL          0x15
#define MPQ_REG_VOUT_MODE               0x20
#define MPQ_REG_STATUS_WORD             0x79
#define MPQ_REG_READ_VIN                0x88
#define MPQ_REG_READ_VOUT               0x8B
#define MPQ_REG_READ_IOUT               0x8C
#define MPQ_REG_READ_TEMPERATURE_1      0x8D
#define MPQ_REG_PMBUS_REVISION          0x98
#define MPQ_REG_MFR_REVISION            0x9B
#define MPQ_REG_MFR_CONFIG_ID           0xC0
#define MPQ_REG_MFR_CFG_EXT             0xF5
#define MPQ_REG_CRC_USER                0xF8
#define MPQ_REG_PROTECTION_LAST         0xFB

/* MFR_CFG_EXT bits */
#define MPQ_MFR_CFG_EXT_CLR_LAST        (1U << 6)
#define MPQ_MFR_CFG_EXT_RST_DEASS_CFG   (1U << 12)

/* WRITE_PROTECT values (PMBus 1.3 Part II §10) */
#define MPQ_WP_UNLOCKED                 0x00
#define MPQ_WP_OPERATION_ONLY           0x40
#define MPQ_WP_WP_ONLY                  0x80

/* VOUT_MODE bit layout (PMBus 1.3 §8.3.1) */
#define MPQ_VOUT_MODE_BITS_SHIFT        5
#define MPQ_VOUT_MODE_BITS_MASK         0x07
#define MPQ_VOUT_MODE_PARAM_MASK        0x1F
#define MPQ_VOUT_MODE_PARAM_SIGN_BIT    0x10
#define MPQ_VOUT_MODE_LINEAR16          0
#define MPQ_VOUT_MODE_VID               1
#define MPQ_VOUT_MODE_DIRECT            2

/* PMBus LINEAR11 bit layout (5-bit signed exponent + 11-bit
 * signed mantissa). */
#define MPQ_LINEAR11_EXP_SHIFT          11
#define MPQ_LINEAR11_EXP_MASK           0x1F
#define MPQ_LINEAR11_EXP_SIGN_BIT       0x10
#define MPQ_LINEAR11_MANT_MASK          0x7FF
#define MPQ_LINEAR11_MANT_SIGN_BIT      0x400

/* MFR_PMBUS_LOCK values */
#define MPQ_PMBUS_LOCK_UNLOCKED         0x0
#define MPQ_PMBUS_LOCK_EXCEPT_VOUT_CMD  0x1
#define MPQ_PMBUS_LOCK_FULL             0x3
#define MPQ_PMBUS_LOCK_MASK             0x3

/* MPS-specific telemetry LSBs (DIRECT format used outside VOUT) */
#define MPQ_VIN_MV_PER_LSB              25U     /* raw * 25 mV */
#define MPQ_IOUT_NUM                    125U    /* raw * 125 / 2 = 62.5 mA */
#define MPQ_IOUT_DEN                    2U
#define MPQ_VID_VOUT_NUM                25U     /* raw * 25 / 16 = 1.5625 mV */
#define MPQ_VID_VOUT_DEN                16U
#define MPQ_MV_PER_V                    1000UL

/* NVM-busy retry parameters (after CLEAR_LAST_FAULT / STORE_USER_ALL) */
#define MPQ_NVM_BUSY_RETRIES            5
#define MPQ_NVM_BUSY_DELAY_US           3000L

/*
 * Types
 */
struct dump_file;

typedef void (*decode_fn)(uint16_t val, char *out, size_t n,
			  const struct dump_file *dump);

struct mpq_reg {
	uint8_t reg;
	uint8_t width;          /* MPQ_W_BYTE / MPQ_W_WORD */
	const char *name;
	bool clone_safe;
	bool skip_dump;         /* exclude from default read pass */
	decode_fn decode;       /* NULL -> "raw hex only" */
	const char *notes;
};

struct dump_entry {
	uint8_t reg;
	uint8_t width;
	uint16_t value;
};

struct dump_file {
	size_t count;
	struct dump_entry entries[MPQ_MAX_DUMP_ENTRIES];
	char source[MPQ_DUMP_SOURCE_MAX];
	char generated[MPQ_DUMP_GENERATED_MAX];
};

/*
 * mpq_regs.c -- register inventory
 */
extern const struct mpq_reg mpq_regs[];
extern const size_t mpq_num_regs;

const struct mpq_reg *lookup_reg(uint8_t reg);

/*
 * mpq_decoders.c -- per-register human-readable interpretation
 */
void dec_vout_mv(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_vin_mv(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_iout_ma(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_temp_c(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_linear11_us(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_on_off_config(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_write_protect(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_mfr_pmbus_lock(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_mfr_cfg_ext(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_vout_mode(uint16_t v, char *out, size_t n, const struct dump_file *dump);
void dec_protection_last(uint16_t v, char *out, size_t n, const struct dump_file *dump);

void dec_status_word(uint16_t v, char *out, size_t n, const struct dump_file *dump);

/*
 * mpq_dump.c -- file I/O (native / CSV / MPS), diff, accessors
 */
int dump_get(const struct dump_file *dump, uint8_t reg, uint16_t *out);

int dump_load_native(const char *path, struct dump_file *dump);

int dump_load_csv(const char *path, struct dump_file *dump);

int dump_load_mps(const char *path, struct dump_file *dump);

void dump_load_auto(const char *path, struct dump_file *dump);

void dump_emit_native(FILE *out, const struct dump_file *dump);

void dump_emit_csv(FILE *out, const struct dump_file *dump);

void dump_emit_mps(FILE *out, const struct dump_file *dump,
	      int i2c_addr, const char *source_path);

size_t dump_diff_run(const struct dump_file *a, const struct dump_file *b, const char *a_label, const char *b_label);

/*
 * mpq_i2c.c -- chip access via /dev/i2c-N, live-chip walks,
 *              kernel-driver coexistence pre-flight
 */
int i2c_open(int bus, int addr);
int chip_read_byte(int fd, uint8_t reg, uint8_t *out);
int chip_read_word(int fd, uint8_t reg, uint16_t *out);
int chip_write_byte(int fd, uint8_t reg, uint8_t val);
int chip_write_word(int fd, uint8_t reg, uint16_t val);
int chip_send_byte(int fd, uint8_t cmd);
void nvm_busy_wait(void);
int check_alarm_poll_quiescent(int bus, int addr, bool force);
int live_chip_to_dump(int bus, int addr, bool dump_all, struct dump_file *dump);
int live_chip_to_dump_filter(int bus, int addr, const struct dump_file *want, struct dump_file *out);

/*
 * mpq_subcmd.c -- one entry point per subcommand
 */
int cmd_read(int argc, char **argv);
int cmd_write(int argc, char **argv);
int cmd_to_csv(int argc, char **argv);
int cmd_from_csv(int argc, char **argv);
int cmd_to_mps(int argc, char **argv);
int cmd_from_mps(int argc, char **argv);
int cmd_diff(int argc, char **argv);
int cmd_explain(int argc, char **argv);
int cmd_live_diff(int argc, char **argv);
