/* SPDX-License-Identifier: BSD-4-Clause */
/* Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr> */
/*
 * mpq_config: MPS MPQ8785 / MPQ8646 chip-config dump / restore /
 * convert tool. Companion to the WIP pmbus/mpq8785 kernel driver.
 *
 * Subcommands (each one's argv handling lives in mpq_subcmd.c):
 *
 *   read       --bus N --addr A [--output FILE] [--all]
 *   write      --bus N --addr A --input FILE [--force] [--store]
 *   to-csv     <input.dmp> [<output.csv>]
 *   from-csv   <input.csv> [<output.dmp>]
 *   to-mps     <input.dmp> [<output.txt>] [--addr 0x10]
 *   from-mps   <input.txt> [<output.dmp>]
 *   diff       <a> <b>                     (.dmp / .csv / .txt)
 *   explain    <input.dmp>
 *   live-diff  --bus N --addr A --input FILE
 *
 * Depends only on /dev/i2c-N; coexists with an in-tree
 * pmbus/mpq8785 driver via the i2c-adapter mutex. The `write`
 * command must NOT race with the kernel driver's alarm-poll
 * worker, set alarm_poll_interval_ms = 0 first.
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpq_config.h"

static void
usage(void) {
	fprintf(stderr,
"Usage: mpq_config <subcommand> [args]\n"
"\n"
"  read       --bus N --addr A [--output FILE] [--all]\n"
"  write      --bus N --addr A --input FILE [--force] [--store]\n"
"  to-csv     <input.dmp> [<output.csv>]\n"
"  from-csv   <input.csv> [<output.dmp>]\n"
"  to-mps     <input.dmp> [<output.txt>] [--addr 0x10]\n"
"  from-mps   <input.txt> [<output.dmp>]\n"
"  diff       <a> <b>           (accepts .dmp / .csv / .txt)\n"
"  explain    <input.dmp>\n"
"  live-diff  --bus N --addr A --input FILE\n"
"\n"
"See README.md for details.\n");
}

/*
 * One entry per subcommand. Order is informational only; matching
 * is by exact name. Kept in a table so future additions don't have
 * to grow another if-else chain.
 */
struct subcmd {
	const char *name;
	int (*fn)(int argc, char **argv);
};

static const struct subcmd SUBCMDS[] = {
	{ "read",      cmd_read      },
	{ "write",     cmd_write     },
	{ "to-csv",    cmd_to_csv    },
	{ "from-csv",  cmd_from_csv  },
	{ "to-mps",    cmd_to_mps    },
	{ "from-mps",  cmd_from_mps  },
	{ "diff",      cmd_diff      },
	{ "explain",   cmd_explain   },
	{ "live-diff", cmd_live_diff },
};

#define MPQ_NUM_SUBCMDS  (sizeof(SUBCMDS) / sizeof(SUBCMDS[0]))

int
main(int argc, char **argv) {
	if (argc < 2) {
		usage();
		return MPQ_EXIT_USAGE;
	}

	const char *sub = argv[1];
	argc -= 1;
	argv += 1;

	if (!strcmp(sub, "-h") || !strcmp(sub, "--help")
	    || !strcmp(sub, "help")) {
		usage();
		return EXIT_SUCCESS;
	}

	for (size_t i = 0; i < MPQ_NUM_SUBCMDS; i++) {
		if (strcmp(sub, SUBCMDS[i].name) == 0)
			return SUBCMDS[i].fn(argc, argv);
	}

	warnx("unknown subcommand: %s", sub);
	usage();

	return MPQ_EXIT_USAGE;
}
