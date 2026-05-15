/* SPDX-License-Identifier: BSD-4-Clause */
/* Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr> */
/*
 * One entry point per subcommand. The heavy lifting is delegated:
 *
 *   chip access  -> mpq_i2c.c
 *   file I/O     -> mpq_dump.c
 *   decoders     -> mpq_decoders.c (driven from mpq_regs[] table)
 */

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mpq_config.h"

/* Default I2C address for the MPS Power Manager footer */
#define MPQ_DEFAULT_I2C_ADDR    0x10

/* Argument-vector sanity */
#define MPQ_ARGV_BUS_UNSET      (-1)
#define MPQ_ARGV_ADDR_UNSET     (-1)

static FILE *
open_out(const char *path) {
	FILE *of = strcmp(path, "-") == 0 ? stdout : fopen(path, "w");
	if (!of)
		err(EXIT_FAILURE, "open %s for write", path);

	return of;
}

static void
close_out(FILE *of) {
	if (of != stdout)
		fclose(of);
}

int
cmd_read(int argc, char **argv) {
	int bus = MPQ_ARGV_BUS_UNSET;
	int addr = MPQ_ARGV_ADDR_UNSET;
	const char *output = "-";
	bool dump_all = false;

	static const struct option opts[] = {
		{ "bus",    required_argument, 0, 'b' },
		{ "addr",   required_argument, 0, 'a' },
		{ "output", required_argument, 0, 'o' },
		{ "all",    no_argument,       0, 'A' },
		{ 0, 0, 0, 0 }
	};
	int c;
	while ((c = getopt_long(argc, argv, "b:a:o:A", opts, NULL)) != -1) {
		switch (c) {
		case 'b':
			bus = (int)strtol(optarg, NULL, 0);
			break;
		case 'a':
			addr = (int)strtol(optarg, NULL, 0);
			break;
		case 'o':
			output = optarg;
  			break;
		case 'A':
			dump_all = true;
  			break;
		default:
			return MPQ_EXIT_USAGE;
		}
	}
	if (bus < 0 || addr < 0)
		errx(MPQ_EXIT_USAGE, "read: --bus N --addr A required");

	struct dump_file dump = {0};
	live_chip_to_dump(bus, addr, dump_all, &dump);

	FILE *of = open_out(output);
	dump_emit_native(of, &dump);
	close_out(of);

	warnx("dumped %zu entries from %s", dump.count, dump.source);

	return EXIT_SUCCESS;
}

/*
 * Try one write of the given entry, retrying NVM-busy NACK
 * windows up to MPQ_NVM_BUSY_RETRIES times. Returns
 *   >= 0   number of retries performed (0 = first try)
 *   < 0    final -errno after all retries failed
 */
static int
write_entry_with_retry(int fd, const struct dump_entry *e) {
	int wrc;
	if (e->width == MPQ_W_BYTE)
		wrc = chip_write_byte(fd, e->reg, e->value & 0xFF);
	else
		wrc = chip_write_word(fd, e->reg, e->value);

	int attempt = 0;
	while (wrc == -ENXIO && attempt < MPQ_NVM_BUSY_RETRIES) {
		nvm_busy_wait();
		if (e->width == MPQ_W_BYTE)
			wrc = chip_write_byte(fd, e->reg, e->value & 0xFF);
		else
			wrc = chip_write_word(fd, e->reg, e->value);
		attempt++;
	}

	return wrc < 0 ? wrc : attempt;
}

int
cmd_write(int argc, char **argv) {
	int bus = MPQ_ARGV_BUS_UNSET;
	int addr = MPQ_ARGV_ADDR_UNSET;
	const char *input = NULL;
	bool force = false;
	bool store = false;

	static const struct option opts[] = {
		{ "bus",   required_argument, 0, 'b' },
		{ "addr",  required_argument, 0, 'a' },
		{ "input", required_argument, 0, 'i' },
		{ "force", no_argument,       0, 'f' },
		{ "store", no_argument,       0, 's' },
		{ 0, 0, 0, 0 }
	};
	int c;
	while ((c = getopt_long(argc, argv, "b:a:i:fs", opts, NULL)) != -1) {
		switch (c) {
		case 'b':
			bus = (int)strtol(optarg, NULL, 0);
			break;
		case 'a':
			addr = (int)strtol(optarg, NULL, 0);
			break;
		case 'i':
			input = optarg;
  			break;
		case 'f':
			force = true;
			break;
		case 's':
			store = true;
			break;
		default:
			return MPQ_EXIT_USAGE;
		}
	}
	if (bus < 0 || addr < 0 || !input)
		errx(MPQ_EXIT_USAGE,
		     "write: --bus N --addr A --input FILE required");

	if (check_alarm_poll_quiescent(bus, addr, force) < 0)
		return MPQ_EXIT_REFUSE;

	struct dump_file dump = {0};
	dump_load_auto(input, &dump);

	int fd = i2c_open(bus, addr);

	/*
	 * Save + clear WRITE_PROTECT (0x10) so the bulk write isn't
	 * silently no-op'd.
	 */
	uint8_t wp_orig = 0;
	int rc = chip_read_byte(fd, MPQ_REG_WRITE_PROTECT, &wp_orig);
	if (rc < 0) {
		warnx("warning: WRITE_PROTECT read failed (%s), writes "
		      "may be rejected", strerror(-rc));
	} else if (wp_orig != MPQ_WP_UNLOCKED) {
		warnx("clearing WRITE_PROTECT (was 0x%02x)", wp_orig);
		if (chip_write_byte(fd, MPQ_REG_WRITE_PROTECT,
				    MPQ_WP_UNLOCKED) < 0)
			warnx("WP clear failed, writes may be rejected");
	}

	size_t written = 0, skipped = 0, errored = 0;
	for (size_t i = 0; i < dump.count; i++) {
		const struct dump_entry *e = &dump.entries[i];
		const struct mpq_reg *desc = lookup_reg(e->reg);
		const char *name = desc ? desc->name : "(unknown)";
		bool safe = desc ? desc->clone_safe : false;

		if (!safe && !force) {
			fprintf(stderr,
				"  skip   0x%02X %-26s (not clone-safe%s)\n",
				e->reg, name,
				desc ? "" : ", unknown reg");
			skipped++;
			continue;
		}

		int wrc = write_entry_with_retry(fd, e);
		if (wrc < 0) {
			fprintf(stderr, "  ERR    0x%02X %-26s (%s)\n",
				e->reg, name, strerror(-wrc));
			errored++;
		} else {
			fprintf(stderr,
				"  wrote  0x%02X %-26s = 0x%0*X%s\n",
				e->reg, name,
				e->width == MPQ_W_WORD ? 4 : 2,
				e->value
				 & (e->width == MPQ_W_WORD ? 0xFFFF : 0xFF),
				wrc > 0 ? " (retried)" : "");
			written++;
		}
	}

	/* Restore WRITE_PROTECT to its original value */
	if (rc == 0 && wp_orig != MPQ_WP_UNLOCKED) {
		if (chip_write_byte(fd, MPQ_REG_WRITE_PROTECT, wp_orig) < 0)
			warnx("WP restore (back to 0x%02x) failed", wp_orig);
	}

	if (store) {
		warnx("STORE_USER_ALL (0x%02x) -> chip NVM",
		      MPQ_REG_STORE_USER_ALL);
		int srrc = chip_send_byte(fd, MPQ_REG_STORE_USER_ALL);
		if (srrc < 0)
			warnx("STORE_USER_ALL failed (%s), "
			      "chip NVM not updated", strerror(-srrc));
	}

	close(fd);
	warnx("wrote=%zu skipped=%zu errored=%zu of %zu entries",
	      written, skipped, errored, dump.count);

	return errored > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

int
cmd_to_csv(int argc, char **argv) {
	if (argc < 2)
		errx(MPQ_EXIT_USAGE, "to-csv: input file required");

	const char *in = argv[1];
	const char *out = argc >= 3 ? argv[2] : "-";

	struct dump_file dump = {0};
	dump_load_native(in, &dump);

	FILE *of = open_out(out);
	dump_emit_csv(of, &dump);
	close_out(of);

	warnx("%zu entries native -> CSV", dump.count);

	return EXIT_SUCCESS;
}

int
cmd_from_csv(int argc, char **argv) {
	if (argc < 2)
		errx(MPQ_EXIT_USAGE, "from-csv: input file required");

	const char *in = argv[1];
	const char *out = argc >= 3 ? argv[2] : "-";

	struct dump_file dump = {0};
	dump_load_csv(in, &dump);

	FILE *of = open_out(out);
	dump_emit_native(of, &dump);
	close_out(of);

	warnx("%zu entries CSV -> native", dump.count);

	return EXIT_SUCCESS;
}

/*
 * Write the dump in the MPS Power Manager GUI export format so
 * the file can be opened by MPS's tooling.
 *
 *   <line#>  0000  0  <reg-hex>  <reg-dec>  <name>  <val-hex>  <val-dec>
 *
 * MFR_CONFIG_ID is hoisted to the first line; CRC_USER is moved
 * into a CRC_CHECK_START / ... / CRC_CHECK_STOP block; an "END"
 * marker separates the data block from the CRC block; a footer
 * carries Product ID, the chip's I2C address, and a 4-digit code
 * derived from MFR_CONFIG_ID.
 */
int
cmd_to_mps(int argc, char **argv) {
	int addr = MPQ_DEFAULT_I2C_ADDR;
	const char *in = NULL;
	const char *out_path = "-";

	/*
	 * getopt-light: scan for --addr separately so we don't have
	 * to mix positionals + getopt_long for one optional flag.
	 */
	int pos = 0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--addr") == 0 && i + 1 < argc) {
			addr = (int)strtol(argv[++i], NULL, 0);
			continue;
		}
		if (pos == 0)
			in = argv[i];
		else if (pos == 1)
			out_path = argv[i];
		pos++;
	}
	if (!in)
		errx(MPQ_EXIT_USAGE, "to-mps: <input.dmp> required");

	struct dump_file dump = {0};
	dump_load_native(in, &dump);

	FILE *of = open_out(out_path);
	dump_emit_mps(of, &dump, addr, in);
	close_out(of);

	/* dump.count includes the filtered-out telemetry; report a rough
	 * lower-bound row count (excludes the CRC + the hoisted CFG ID). */
	warnx("%zu entries native -> MPS GUI format", dump.count);

	return EXIT_SUCCESS;
}

int
cmd_from_mps(int argc, char **argv) {
	if (argc < 2)
		errx(MPQ_EXIT_USAGE, "from-mps: <input.txt> required");

	const char *in = argv[1];
	const char *out = argc >= 3 ? argv[2] : "-";

	struct dump_file dump = {0};
	dump_load_mps(in, &dump);

	FILE *of = open_out(out);
	dump_emit_native(of, &dump);
	close_out(of);

	warnx("%zu entries MPS -> native", dump.count);

	return EXIT_SUCCESS;
}

int
cmd_diff(int argc, char **argv) {
	if (argc < 3)
		errx(MPQ_EXIT_USAGE, "diff: <a.dmp> <b.dmp> required");

	struct dump_file a = {0}, b = {0};
	dump_load_auto(argv[1], &a);
	dump_load_auto(argv[2], &b);

	size_t diffs = dump_diff_run(&a, &b, argv[1], argv[2]);
	warnx("%zu differences", diffs);

	return diffs == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

/*
 * Loads a dump and prints each entry with its register name and a
 * decoded human-readable interpretation (millivolts, mA, us,
 * decoded bitfields) when a decoder is registered. Use to read a
 * dump file at a glance without consulting the datasheet.
 */
int
cmd_explain(int argc, char **argv) {
	if (argc < 2)
		errx(MPQ_EXIT_USAGE, "explain: <input.dmp> required");

	struct dump_file dump = {0};
	dump_load_auto(argv[1], &dump);

	printf("# explain: %s\n", argv[1]);
	if (dump.generated[0])
		printf("# generated: %s\n", dump.generated);
	if (dump.source[0])
		printf("# source: %s\n", dump.source);
	printf("# %-4s %-6s %-26s %s\n",
	       "reg", "value", "name", "decoded");

	for (size_t i = 0; i < dump.count; i++) {
		const struct dump_entry *e = &dump.entries[i];
		const struct mpq_reg *desc = lookup_reg(e->reg);
		const char *name = desc ? desc->name : "(unknown)";

		char val_hex[8];
		if (e->width == MPQ_W_BYTE)
			snprintf(val_hex, sizeof(val_hex), "0x%02X",
				 e->value & 0xFF);
		else
			snprintf(val_hex, sizeof(val_hex), "0x%04X",
				 e->value);

		char decoded[MPQ_DECODED_MAX];
		if (desc && desc->decode)
			desc->decode(e->value, decoded, sizeof(decoded),
				     &dump);
		else
			snprintf(decoded, sizeof(decoded), "raw %s", val_hex);

		printf("  0x%02X %-6s %-26s %s\n",
		       e->reg, val_hex, name, decoded);
	}

	return EXIT_SUCCESS;
}

/*
 * Reads the live chip, loads a saved dump file, prints the delta
 * between them, exits non-zero if they differ. Useful as a quick
 * "is this board mis-configured vs golden?" check.
 */
int
cmd_live_diff(int argc, char **argv) {
	int bus = MPQ_ARGV_BUS_UNSET;
	int addr = MPQ_ARGV_ADDR_UNSET;
	const char *input = NULL;

	static const struct option opts[] = {
		{ "bus",   required_argument, 0, 'b' },
		{ "addr",  required_argument, 0, 'a' },
		{ "input", required_argument, 0, 'i' },
		/*
		 * Backward-compat: --all was needed before we switched
		 * live-diff to read exactly the saved file's register
		 * set. Accept and silently ignore so existing scripts
		 * don't break.
		 */
		{ "all",   no_argument,       0, 'A' },
		{ 0, 0, 0, 0 }
	};
	int c;
	while ((c = getopt_long(argc, argv, "b:a:i:A", opts, NULL)) != -1) {
		switch (c) {
		case 'b':
			bus = (int)strtol(optarg, NULL, 0);
			break;
		case 'a':
			addr = (int)strtol(optarg, NULL, 0);
			break;
		case 'i':
			input = optarg;
  			break;
		case 'A':
			/* deprecated; live-diff reads saved set */
			break;
		default:
			return MPQ_EXIT_USAGE;
		}
	}
	if (bus < 0 || addr < 0 || !input)
		errx(MPQ_EXIT_USAGE,
		     "live-diff: --bus N --addr A --input FILE required");

	/*
	 * Load saved first so we know which regs to probe on the chip.
	 * Reading exactly the saved register set eliminates the
	 * skip_dump / --all asymmetry that used to produce false
	 * "only in saved" deltas.
	 */
	struct dump_file saved = {0}, live = {0};
	dump_load_auto(input, &saved);
	live_chip_to_dump_filter(bus, addr, &saved, &live);

	char live_label[64];
	snprintf(live_label, sizeof(live_label),
		 "live(bus=%d,addr=0x%02x)", bus, addr);

	size_t diffs = dump_diff_run(&saved, &live, input, live_label);
	warnx("%zu differences (live vs %s)", diffs, input);

	return diffs == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
