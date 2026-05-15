/* SPDX-License-Identifier: BSD-4-Clause */
/* Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr> */
/*
 * In-memory dump representation + load / emit for the three
 * file formats the tool understands (native .dmp, CSV, MPS Power
 * Manager GUI export .txt) plus the per-register diff core shared
 * by `diff` and `live-diff`.
 */

#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpq_config.h"

/* MPS GUI export markers */
#define MPS_END                "END"
#define MPS_CRC_START          "CRC_CHECK_START"
#define MPS_CRC_STOP           "CRC_CHECK_STOP"
#define MPS_PRODUCT_ID_LINE    "Product ID:"
#define MPS_I2C_ADDR_LINE      "I2C Address:"
#define MPS_4_DIGIT_CODE_LINE  "4-digit Code:"
#define MPS_NOTES_PREFIX       "****"
#define MPS_PRODUCT_ID         "MPQ8785"
#define MPS_FIELDS_PER_ROW     7
#define MPS_HEX_BYTE_CHARS     2     /* val-hex length <= 2 chars  -> 1 byte */

/* Native header comments */
#define NATIVE_HDR_SOURCE      "# source:"
#define NATIVE_HDR_GENERATED   "# generated:"

int
dump_get(const struct dump_file *dump, uint8_t reg, uint16_t *out) {
	if (!dump)
		return 0;

	for (size_t i = 0; i < dump->count; i++) {
		if (dump->entries[i].reg == reg) {
			*out = dump->entries[i].value;
			return 1;
		}
	}

	return 0;
}

static FILE *
open_for_read(const char *path) {
	FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "r");
	if (!f)
		err(EXIT_FAILURE, "open %s for read", path);

	return f;
}

/*
 * Strip trailing \n / \r in place. Returns the new (possibly
 * shorter) length.
 */
static size_t
chomp(char *line) {
	size_t n = strlen(line);
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		line[--n] = '\0';

	return n;
}

/*
 * Capture metadata from header comments. memcpy + manual
 * null-termination instead of strncpy() avoids GCC's
 * -Wstringop-truncation warning when the source is longer
 * than the destination buffer (intentional truncation here).
 */
static void
copy_metadata(char *dst, size_t dst_sz, const char *src) {
	size_t n = strnlen(src, dst_sz - 1);
	memcpy(dst, src, n);
	dst[n] = '\0';
}

int
dump_load_native(const char *path, struct dump_file *dump) {
	FILE *f = open_for_read(path);
	char line[MPQ_LINE_MAX];

	dump->count = 0;
	dump->source[0] = '\0';
	dump->generated[0] = '\0';

	while (fgets(line, sizeof(line), f)) {
		chomp(line);

		if (strncmp(line, NATIVE_HDR_SOURCE,
			    sizeof(NATIVE_HDR_SOURCE) - 1) == 0) {
			copy_metadata(dump->source, sizeof(dump->source),
				      line + sizeof(NATIVE_HDR_SOURCE));
			continue;
		}
		if (strncmp(line, NATIVE_HDR_GENERATED,
			    sizeof(NATIVE_HDR_GENERATED) - 1) == 0) {
			copy_metadata(dump->generated,
				      sizeof(dump->generated),
				      line + sizeof(NATIVE_HDR_GENERATED));
			continue;
		}

		/* Skip blanks + comments */
		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0' || *p == '#')
			continue;

		unsigned int reg, width, value;
		if (sscanf(p, "%x %u %x", &reg, &width, &value) != 3) {
			warnx("warning: malformed line: %s", line);
			continue;
		}
		if (reg > 0xFF) {
			warnx("warning: reg 0x%x > 0xFF, skipping", reg);
			continue;
		}
		if (width != MPQ_W_BYTE && width != MPQ_W_WORD) {
			warnx("warning: unsupported width %u for reg 0x%02x",
			      width, reg);
			continue;
		}
		if (dump->count >= MPQ_MAX_DUMP_ENTRIES) {
			warnx("warning: dump file has more than %zu entries, "
			      "truncating", (size_t)MPQ_MAX_DUMP_ENTRIES);
			break;
		}
		dump->entries[dump->count].reg = reg;
		dump->entries[dump->count].width = width;
		dump->entries[dump->count].value = value;
		dump->count++;
	}

	if (f != stdin)
		fclose(f);

	/*
	 * Refuse silent zero-entry loads. A `diff` or `live-diff` against
	 * an empty file would otherwise report "0 differences". The
	 * worst possible answer to "did the chip drift from this file?".
	 */
	if (dump->count == 0)
		errx(EXIT_FAILURE,
		     "read %s: parsed 0 entries (empty / wrong format?)",
		     path);

	return 0;
}

void
dump_emit_native(FILE *out, const struct dump_file *dump) {
	if (dump->generated[0])
		fprintf(out, "# generated: %s\n", dump->generated);
	if (dump->source[0])
		fprintf(out, "# source: %s\n", dump->source);
	fprintf(out, "# fields: <reg> <width-bytes> <value>\n");

	for (size_t i = 0; i < dump->count; i++) {
		const struct dump_entry *e = &dump->entries[i];
		if (e->width == MPQ_W_BYTE)
			fprintf(out, "0x%02X 1 0x%02X\n",
				e->reg, e->value & 0xFF);
		else
			fprintf(out, "0x%02X 2 0x%04X\n", e->reg, e->value);
	}
}

void
dump_emit_csv(FILE *out, const struct dump_file *dump) {
	fprintf(out, "reg,width,value,name,clone_safe,notes\n");

	for (size_t i = 0; i < dump->count; i++) {
		const struct dump_entry *e = &dump->entries[i];
		const struct mpq_reg *desc = lookup_reg(e->reg);
		const char *name  = desc ? desc->name : "(unknown)";
		const char *cs    = desc ? (desc->clone_safe ? "yes" : "no")
				         : "?";
		const char *notes = desc ? desc->notes : "";

		if (e->width == MPQ_W_BYTE)
			fprintf(out, "0x%02X,1,0x%02X,%s,%s,%s\n",
				e->reg, e->value & 0xFF, name, cs, notes);
		else
			fprintf(out, "0x%02X,2,0x%04X,%s,%s,%s\n",
				e->reg, e->value, name, cs, notes);
	}
}

int
dump_load_csv(const char *path, struct dump_file *dump) {
	FILE *f = open_for_read(path);
	char line[MPQ_LINE_MAX];

	dump->count = 0;
	dump->source[0] = '\0';
	dump->generated[0] = '\0';

	bool first = true;
	while (fgets(line, sizeof(line), f)) {
		chomp(line);
		if (line[0] == '\0' || line[0] == '#')
			continue;
		if (first) {
			first = false;
			if (strncmp(line, "reg,", 4) == 0)
				continue;
		}

		unsigned int reg, width, value;
		/*
		 * Format: reg,width,value,name,clone_safe,notes
		 *         (we only need the first 3)
		 */
		if (sscanf(line, "%x,%u,%x", &reg, &width, &value) != 3) {
			warnx("warning: malformed CSV line: %s", line);
			continue;
		}
		if (reg > 0xFF
		    || (width != MPQ_W_BYTE && width != MPQ_W_WORD))
			continue;
		if (dump->count >= MPQ_MAX_DUMP_ENTRIES)
			break;

		dump->entries[dump->count].reg = reg;
		dump->entries[dump->count].width = width;
		dump->entries[dump->count].value = value;
		dump->count++;
	}

	if (f != stdin)
		fclose(f);

	if (dump->count == 0)
		errx(EXIT_FAILURE,
		     "read CSV %s: parsed 0 entries "
		     "(header-only / wrong format?)", path);

	return 0;
}

/*
 * MPS Power Manager GUI -> native parser. Mirrors dump_load_csv.
 *
 * Recognises any line of the form:
 *
 *   0000<TAB>0<TAB><reg-hex><TAB><reg-dec><TAB><name><TAB><val-hex><TAB><val-dec>
 *
 * Width is inferred from the val-hex length (<=2 chars -> byte,
 * else word). The CRC_USER row inside CRC_CHECK_START /
 * CRC_CHECK_STOP is parsed like any other entry; the END /
 * CRC_CHECK_* / Product ID / Notes lines are ignored. Whitespace
 * tolerance is loose: a row is accepted when it splits into 7
 * tab-separated fields, the first two are `0000` and `0`, and
 * the value/register hex parses.
 */
int
dump_load_mps(const char *path, struct dump_file *dump) {
	FILE *f = open_for_read(path);
	char line[MPQ_LINE_MAX];

	dump->count = 0;
	dump->source[0] = '\0';
	dump->generated[0] = '\0';

	size_t parsed = 0;
	while (fgets(line, sizeof(line), f)) {
		size_t n = chomp(line);
		if (n == 0)
			continue;

		/* Skip section markers + footer + notes. */
		if (!strcmp(line, MPS_END) ||
		    !strcmp(line, MPS_CRC_START) ||
		    !strcmp(line, MPS_CRC_STOP) ||
		    !strncmp(line, MPS_PRODUCT_ID_LINE,
			     sizeof(MPS_PRODUCT_ID_LINE) - 1) ||
		    !strncmp(line, MPS_I2C_ADDR_LINE,
			     sizeof(MPS_I2C_ADDR_LINE) - 1) ||
		    !strncmp(line, MPS_4_DIGIT_CODE_LINE,
			     sizeof(MPS_4_DIGIT_CODE_LINE) - 1) ||
		    !strncmp(line, MPS_NOTES_PREFIX,
			     sizeof(MPS_NOTES_PREFIX) - 1))
			continue;

		/* Tab-split into up to MPS_FIELDS_PER_ROW fields. */
		char *fields[MPS_FIELDS_PER_ROW + 1] = { NULL };
		int nf = 0;
		char *p = line;
		fields[nf++] = p;
		while (*p && nf < (int)(sizeof(fields) / sizeof(fields[0]))) {
			if (*p == '\t') {
				*p = '\0';
				fields[nf++] = p + 1;
			}
			p++;
		}
		if (nf != MPS_FIELDS_PER_ROW) {
			warnx("warning: malformed MPS line (got %d fields, "
			      "expected %d): %s",
			      nf, MPS_FIELDS_PER_ROW,
			      fields[0] ? fields[0] : "");
			continue;
		}
		if (strcmp(fields[0], "0000") != 0
		    || strcmp(fields[1], "0") != 0)
			continue;

		char *endp = NULL;
		unsigned long reg = strtoul(fields[2], &endp, 16);
		if (endp == fields[2] || reg > 0xFF)
			continue;

		const char *hv = fields[5];
		size_t hv_len = strlen(hv);
		unsigned long val = strtoul(hv, &endp, 16);
		if (endp == hv || hv_len == 0)
			continue;

		uint8_t width = (hv_len <= MPS_HEX_BYTE_CHARS)
				? MPQ_W_BYTE : MPQ_W_WORD;

		if (dump->count >= MPQ_MAX_DUMP_ENTRIES) {
			warnx("warning: MPS file has more than %zu entries, "
			      "truncating", (size_t)MPQ_MAX_DUMP_ENTRIES);
			break;
		}
		dump->entries[dump->count].reg = (uint8_t)reg;
		dump->entries[dump->count].width = width;
		dump->entries[dump->count].value = (uint16_t)val;
		dump->count++;
		parsed++;
	}

	if (f != stdin)
		fclose(f);

	if (parsed == 0)
		errx(EXIT_FAILURE,
		     "read MPS %s: parsed 0 entries (not an MPS export?)",
		     path);

	snprintf(dump->source, sizeof(dump->source),
		 "MPS GUI export: %s", path);

	return (int)parsed;
}

/*
 * Load a dump from FILE. If the path ends with `.txt` (or starts
 * with the MPS-format signature on the first non-blank line), parse
 * as MPS GUI export. Otherwise treat as native (.dmp / .csv). This
 * lets `diff` and `live-diff` accept MPS files directly, so the
 * round-trip "edit in MPS GUI -> diff vs live chip" is one command.
 */
void
dump_load_auto(const char *path, struct dump_file *dump) {
	size_t plen = strlen(path);
	if (plen >= 4 && (!strcmp(path + plen - 4, ".txt") ||
			  !strcmp(path + plen - 4, ".TXT"))) {
		dump_load_mps(path, dump);
		return;
	}
	if (plen >= 4 && (!strcmp(path + plen - 4, ".csv") ||
			  !strcmp(path + plen - 4, ".CSV"))) {
		dump_load_csv(path, dump);
		return;
	}
	dump_load_native(path, dump);
}

/*
 * MPS Power Manager GUI emit helpers
 *
 * The export uses a couple of register names that differ from the
 * PMBus / pmbus/mpq8785 canonical ones; override here so the file
 * round-trips cleanly through MPS's tool.
 *
 * Telemetry / live-state registers (READ_*, STATUS_WORD,
 * PROTECTION_LAST, PMBUS_REVISION, MFR_REVISION) are filtered out
 * even if present in the input dump: they have no sense in an
 * MPS GUI config file.
 */
static bool
mps_skip_reg(uint8_t reg) {
	switch (reg) {
	case MPQ_REG_STATUS_WORD:
	case MPQ_REG_READ_VIN:
	case MPQ_REG_READ_VOUT:
	case MPQ_REG_READ_IOUT:
	case MPQ_REG_READ_TEMPERATURE_1:
	case MPQ_REG_PMBUS_REVISION:
	case MPQ_REG_MFR_REVISION:
	case MPQ_REG_PROTECTION_LAST:
		return true;
	}
	return false;
}

static const char *
mps_name_for(uint8_t reg, const char *fallback) {
	switch (reg) {
	case MPQ_REG_WRITE_PROTECT:
		return "WRITE_PROTECTION";
	}

	return fallback;
}

static void
mps_emit_row(FILE *out, uint8_t reg, const char *name, uint8_t width,
	     uint16_t value) {
	const char *mps_name = mps_name_for(reg, name);
	if (width == MPQ_W_BYTE)
		fprintf(out, "0000\t0\t%02X\t%u\t%s\t%02X\t%u\n",
			reg, reg, mps_name,
			value & 0xFF, value & 0xFF);
	else
		fprintf(out, "0000\t0\t%02X\t%u\t%s\t%04X\t%u\n",
			reg, reg, mps_name, value, value);
}

void
dump_emit_mps(FILE *out, const struct dump_file *dump, int i2c_addr,
	      const char *source_path) {
	/*
	 * Pull MFR_CONFIG_ID (hoisted to first row) and CRC_USER
	 * (separate section) out of the main walk.
	 */
	const struct dump_entry *cfg_id = NULL;
	const struct dump_entry *crc_user = NULL;
	for (size_t i = 0; i < dump->count; i++) {
		uint8_t r = dump->entries[i].reg;
		if (r == MPQ_REG_MFR_CONFIG_ID)
			cfg_id = &dump->entries[i];
		else if (r == MPQ_REG_CRC_USER)
			crc_user = &dump->entries[i];
	}

	/*
	 * Sort indices into the dump by reg ascending, skipping the
	 * two hoisted entries + telemetry.
	 */
	size_t order[MPQ_MAX_DUMP_ENTRIES];
	size_t n_order = 0;
	for (size_t i = 0; i < dump->count; i++) {
		uint8_t r = dump->entries[i].reg;
		if (r == MPQ_REG_MFR_CONFIG_ID || r == MPQ_REG_CRC_USER
		    || mps_skip_reg(r))
			continue;
		order[n_order++] = i;
	}
	/* bubble sort: count is small (< 100), simplest */
	for (size_t i = 0; i + 1 < n_order; i++) {
		for (size_t j = 0; j + 1 < n_order - i; j++) {
			if (dump->entries[order[j]].reg
			    > dump->entries[order[j + 1]].reg) {
				size_t t = order[j];
				order[j] = order[j + 1];
				order[j + 1] = t;
			}
		}
	}

	if (cfg_id)
		mps_emit_row(out, cfg_id->reg, "MFR_CONFIG_ID",
			     cfg_id->width, cfg_id->value);

	for (size_t k = 0; k < n_order; k++) {
		const struct dump_entry *e = &dump->entries[order[k]];
		const struct mpq_reg *desc = lookup_reg(e->reg);
		mps_emit_row(out, e->reg,
			     desc ? desc->name : "UNKNOWN",
			     e->width, e->value);
	}
	fprintf(out, "%s\n", MPS_END);
	fprintf(out, "%s\n", MPS_CRC_START);
	if (crc_user)
		mps_emit_row(out, crc_user->reg, "CRC_USER",
			     crc_user->width, crc_user->value);
	else
		fprintf(out, "0000\t0\t%02X\t%u\tCRC_USER\t0000\t0\n",
			MPQ_REG_CRC_USER, MPQ_REG_CRC_USER);
	fprintf(out, "%s\n", MPS_CRC_STOP);
	fprintf(out, "\n");

	/*
	 * Footer. Product ID + I2C address + 4-digit code. The
	 * 4-digit code is the low nibble of MFR_CONFIG_ID per the
	 * MPS GUI's naming convention (file "MPQ8785-NNNN").
	 */
	uint16_t code = cfg_id ? cfg_id->value : 0;
	fprintf(out, "%s\t%s\n", MPS_PRODUCT_ID_LINE, MPS_PRODUCT_ID);
	fprintf(out, "%s\t%02X\t%u\n", MPS_I2C_ADDR_LINE, i2c_addr, i2c_addr);
	fprintf(out, "%s\t%04X\t%u\n", MPS_4_DIGIT_CODE_LINE, code, code);
	fprintf(out, "\n");

	fprintf(out, "**************** Notes "
		     "*******************************\n");
	fprintf(out, "**** Configuration file: %s-%04X\n",
		MPS_PRODUCT_ID, code);
	fprintf(out, "**** Exported by mpq_config to-mps\n");
	if (source_path)
		fprintf(out, "**** Source: %s\n",
			dump->source[0] ? dump->source : source_path);
	if (dump->generated[0])
		fprintf(out, "**** Captured: %s\n", dump->generated);
}

/*
 * Compare two dumps; print human-readable delta lines; return
 * number of differences. Shared by `diff` and `live-diff`.
 */
size_t
dump_diff_run(const struct dump_file *a, const struct dump_file *b,
	      const char *a_label, const char *b_label) {
	size_t diffs = 0;

	for (size_t i = 0; i < a->count; i++) {
		const struct dump_entry *ea = &a->entries[i];
		const struct dump_entry *eb = NULL;
		for (size_t j = 0; j < b->count; j++) {
			if (b->entries[j].reg == ea->reg) {
				eb = &b->entries[j];
				break;
			}
		}
		const struct mpq_reg *desc = lookup_reg(ea->reg);
		const char *name = desc ? desc->name : "(unknown)";

		if (!eb) {
			printf("  0x%02X %-26s only in %s\n",
			       ea->reg, name, a_label);
			diffs++;
		} else if (eb->value != ea->value
			   || eb->width != ea->width) {
			printf("  0x%02X %-26s 0x%0*X -> 0x%0*X\n",
			       ea->reg, name,
			       ea->width == MPQ_W_WORD ? 4 : 2, ea->value,
			       eb->width == MPQ_W_WORD ? 4 : 2, eb->value);
			diffs++;
		}
	}

	for (size_t j = 0; j < b->count; j++) {
		const struct dump_entry *eb = &b->entries[j];
		bool seen = false;
		for (size_t i = 0; i < a->count; i++) {
			if (a->entries[i].reg == eb->reg) {
				seen = true;
				break;
			}
		}
		if (!seen) {
			const struct mpq_reg *desc = lookup_reg(eb->reg);
			const char *name = desc ? desc->name : "(unknown)";
			printf("  0x%02X %-26s only in %s\n",
			       eb->reg, name, b_label);
			diffs++;
		}
	}

	return diffs;
}
