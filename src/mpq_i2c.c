/* SPDX-License-Identifier: BSD-4-Clause */
/* Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr> */
/*
 * /dev/i2c-N access for the MPS MPQ8785 / MPQ8646. Covers:
 *
 *   - one-shot byte / word SMBus transfers through ioctl(I2C_SMBUS),
 *   - live-chip dump walks (full inventory and saved-set filtered),
 *   - the NVM-busy delay used after STORE_USER_ALL / CLEAR_LAST_FAULT,
 *   - the pre-flight check that refuses to write while the kernel
 *     mpq8785 driver's alarm-poll worker is active.
 *
 * Coexists with the in-tree pmbus/mpq8785 driver: I2C_SLAVE_FORCE
 * rather than I2C_SLAVE so we don't EBUSY on the kernel claim. Bus
 * serialization is still done by the i2c-adapter mutex inside
 * every SMBus xfer.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "mpq_config.h"

int
i2c_open(int bus, int addr) {
	char path[MPQ_PATH_MAX];
	int fd;

	snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
	fd = open(path, O_RDWR);
	if (fd < 0)
		err(EXIT_FAILURE, "open %s", path);

	/*
	 * I2C_SLAVE_FORCE, not I2C_SLAVE: the kernel mpq8785 driver
	 * holds 0x10 reserved in its normal-bind state and the plain
	 * advisory ioctl would fail with EBUSY. Bus serialization is
	 * still provided by the i2c-adapter mutex inside every SMBus
	 * xfer, so coexistence is safe. We just need to
	 * pause the kernel alarm-poll worker before bulk writes (the
	 * `write` subcommand's pre-flight enforces that).
	 */
	if (ioctl(fd, I2C_SLAVE_FORCE, addr) < 0)
		err(EXIT_FAILURE, "I2C_SLAVE_FORCE %s addr 0x%02x",
		    path, addr);

	return fd;
}

static int
i2c_smbus_xfer(int fd, char read_write, uint8_t cmd, int size,
	       union i2c_smbus_data *data) {
	struct i2c_smbus_ioctl_data args = {
		.read_write = read_write,
		.command = cmd,
		.size = size,
		.data = data,
	};

	return ioctl(fd, I2C_SMBUS, &args);
}

int
chip_read_byte(int fd, uint8_t reg, uint8_t *out) {
	union i2c_smbus_data data = {0};
	int rc = i2c_smbus_xfer(fd, I2C_SMBUS_READ, reg,
				I2C_SMBUS_BYTE_DATA, &data);
	if (rc < 0)
		return -errno;

	*out = data.byte;

	return 0;
}

int
chip_read_word(int fd, uint8_t reg, uint16_t *out) {
	union i2c_smbus_data data = {0};
	int rc = i2c_smbus_xfer(fd, I2C_SMBUS_READ, reg,
				I2C_SMBUS_WORD_DATA, &data);
	if (rc < 0)
		return -errno;

	*out = data.word;

	return 0;
}

int
chip_write_byte(int fd, uint8_t reg, uint8_t val) {
	union i2c_smbus_data data = { .byte = val };
	int rc = i2c_smbus_xfer(fd, I2C_SMBUS_WRITE, reg,
				I2C_SMBUS_BYTE_DATA, &data);

	return rc < 0 ? -errno : 0;
}

int
chip_write_word(int fd, uint8_t reg, uint16_t val) {
	union i2c_smbus_data data = { .word = val };
	int rc = i2c_smbus_xfer(fd, I2C_SMBUS_WRITE, reg,
				I2C_SMBUS_WORD_DATA, &data);

	return rc < 0 ? -errno : 0;
}

int
chip_send_byte(int fd, uint8_t cmd) {
	int rc = i2c_smbus_xfer(fd, I2C_SMBUS_WRITE, cmd,
				I2C_SMBUS_BYTE, NULL);

	return rc < 0 ? -errno : 0;
}

void
nvm_busy_wait(void) {
	struct timespec ts = { 0, MPQ_NVM_BUSY_DELAY_US * 1000L };
	nanosleep(&ts, NULL);
}

/*
 * Pre-flight: refuse to write if the kernel mpq8785 driver's
 * alarm-poll worker is active. Its 1 Hz STATUS_WORD reads would
 * interleave with our bulk writes and could land mid-NVM-busy
 * window, corrupting the cloned config.
 *
 *   echo 0 > /sys/kernel/debug/mpq8785/<bus>-<addr>/alarm_poll_interval_ms
 *
 * before running `write` (or pass --force to bypass the check).
 *
 * Returns 0 if it is safe to proceed, -1 if the caller should
 * abort.
 */
int
check_alarm_poll_quiescent(int bus, int addr, bool force) {
	char path[MPQ_DEBUGFS_POLL_PATH_MAX];
	snprintf(path, sizeof(path),
		 "/sys/kernel/debug/mpq8785/%d-%04x/alarm_poll_interval_ms",
		 bus, addr);

	FILE *f = fopen(path, "r");
	if (!f) {
		/*
		 * No kernel driver bound / debugfs disabled / not mpq8785.
		 * Nothing to race with.
		 */
		return 0;
	}

	unsigned int ms = 0;
	int ok = (fscanf(f, "%u", &ms) == 1);
	fclose(f);
	if (!ok)
		return 0;	/* couldn't parse; assume safe */

	if (ms == 0)
		return 0;	/* already paused */

	if (force) {
		warnx("warning: alarm_poll_interval_ms = %u "
		      "(kernel driver actively polling); "
		      "--force given, proceeding anyway", ms);
		return 0;
	}

	warnx("REFUSING to write: kernel mpq8785 driver's\n"
	      "            alarm-poll worker is active (%s = %u ms).\n"
	      "            Its STATUS_WORD reads would race with this\n"
	      "            bulk write.\n"
	      "\n"
	      "            Pause it first:\n"
	      "              echo 0 > %s\n"
	      "\n"
	      "            Then re-run this command. (Or pass --force to\n"
	      "            override, it is not recommended.)",
	      path, ms, path);

	return -1;
}

/*
 * Read one register from the chip into a dump_entry already
 * populated with reg + width. Returns 0 on success, -errno on
 * I/O error.
 */
static int
chip_read_reg(int fd, struct dump_entry *e) {
	if (e->width == MPQ_W_BYTE) {
		uint8_t v = 0;
		int rc = chip_read_byte(fd, e->reg, &v);
		if (rc < 0)
			return rc;
		e->value = v;
		return 0;
	}

	uint16_t v = 0;
	int rc = chip_read_word(fd, e->reg, &v);
	if (rc < 0)
		return rc;
	e->value = v;

	return 0;
}

/*
 * Fill dump->source / dump->generated with the current chip
 * identity + UTC timestamp.
 */
static void
dump_stamp(struct dump_file *dump, int bus, int addr,
	   const char *src_fmt) {
	time_t now = time(NULL);
	struct tm tm;
	gmtime_r(&now, &tm);
	strftime(dump->generated, sizeof(dump->generated),
		 "%Y-%m-%dT%H:%M:%SZ", &tm);
	snprintf(dump->source, sizeof(dump->source),
		 src_fmt, addr, bus);
}

/*
 * Live chip -> in-memory dump (shared by `read` and the rest).
 * Opens the i2c-dev fd, walks the register table, fills `dump`.
 * Skips entries flagged `skip_dump` unless `dump_all` is true.
 * Closes the fd before returning.
 */
int
live_chip_to_dump(int bus, int addr, bool dump_all,
		  struct dump_file *dump) {
	int fd = i2c_open(bus, addr);

	dump->count = 0;
	dump_stamp(dump, bus, addr, "i2c@0x%02x bus %d");

	for (size_t i = 0; i < mpq_num_regs; i++) {
		const struct mpq_reg *r = &mpq_regs[i];
		if (r->skip_dump && !dump_all)
			continue;

		struct dump_entry *e = &dump->entries[dump->count];
		e->reg = r->reg;
		e->width = r->width;

		int rc = chip_read_reg(fd, e);
		if (rc < 0) {
			warnx("reg 0x%02x %s: read failed (%s); skipping",
			      r->reg, r->name, strerror(-rc));
			continue;
		}
		dump->count++;
	}

	close(fd);

	return 0;
}

/*
 * Read from the chip exactly the registers present in `want`, using
 * each entry's recorded width. Used by `live-diff` so the live read
 * matches the saved dump's register set 1:1 Otherwise a default
 * `live-diff` against a `read --all` dump would produce false
 * "only in saved" deltas for every skip_dump register.
 */
int
live_chip_to_dump_filter(int bus, int addr,
			 const struct dump_file *want,
			 struct dump_file *out) {
	int fd = i2c_open(bus, addr);

	out->count = 0;
	dump_stamp(out, bus, addr, "i2c@0x%02x bus %d (filtered)");

	for (size_t i = 0; i < want->count; i++) {
		const struct dump_entry *w = &want->entries[i];
		struct dump_entry *e = &out->entries[out->count];
		e->reg = w->reg;
		e->width = w->width;

		int rc = chip_read_reg(fd, e);
		if (rc < 0) {
			warnx("reg 0x%02x: read failed (%s); skipping",
			      w->reg, strerror(-rc));
			continue;
		}
		out->count++;
	}

	close(fd);

	return 0;
}
