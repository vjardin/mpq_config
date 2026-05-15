# mpq_config

MPS MPQ8785 / MPQ8646 chip-config dump / clone / restore tool.

Companion of the Linux `pmbus/mpq8785` kernel driver.

## Build

```sh
meson setup build
meson compile -C build

meson setup build-aarch64-static \
    --cross-file <path-to>/aarch64-cross.txt \
    -Dstatic=true
meson compile -C build-aarch64-static

meson install -C build --destdir=/usr/local
```

`<path-to>/aarch64-cross.txt` is a standard meson cross file for
the target's toolchain. Example for an aarch64 Linux board built
with the Debian/Ubuntu `crossbuild-essential-arm64` package:

```ini
[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'
pkg-config = '/bin/false'

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[built-in options]
prefer_static = true
default_library = 'static'
c_args = ['-O2']
c_link_args = ['-static']
```

Test the conversion paths without touching real hardware:

```sh
meson test -C build
```

Add target-side coverage with the option and exporting the serial port:

```sh
meson setup build-target -Dwith_target_tests=true
MPQ_SERIAL_PORT=/dev/ttyUSB0 meson test -C build-target
```

## Subcommands

```
mpq_config read       --bus N --addr A [--output FILE] [--all]
mpq_config write      --bus N --addr A --input FILE [--force] [--store]
mpq_config to-csv     <input.dmp> [<output.csv>]
mpq_config from-csv   <input.csv> [<output.dmp>]
mpq_config diff       <a.dmp> <b.dmp>
mpq_config explain    <input.dmp>
mpq_config live-diff  --bus N --addr A --input FILE [--all]
```

- `read` dumps the clone-safe register set (or all known registers
with `--all`) in native format to stdout or to `--output`.

- `write` takes a native file, writes each clone-safe entry back to the chip
in order, optionally followed by `STORE_USER_ALL (0x15)` to commit
to chip NVM.

- `to-csv` / `from-csv` round-trip lossless between the
two text formats.

- `diff` compares two native dumps and exits non-zero on mismatch

- `explain` decodes each register in a saved dump to human-readable
form (millivolts, mA, microseconds, bitfield names)

- `live-diff` reads the live chip and compares it to a saved dump
in one shot

## File formats

### Native (`.dmp`)

Minimal text, tab/space-separated, suitable for `awk` / `cut`:

```
# generated: 2026-05-15T01:00:00Z
# source: i2c@0x10 bus 0
# fields: <reg> <width-bytes> <value>
0x02 1 0x1E
0x21 2 0x01A8
0xC2 2 0xCAFE
 ...
```

### CSV (`.csv`)

Human-friendly with register names + clone-safety flags. Edit in
your favourite spreadsheet, convert back with `from-csv`:

```csv
reg,width,value,name,clone_safe,notes
0x02,1,0x1E,ON_OFF_CONFIG,yes,
0x21,2,0x01A8,VOUT_COMMAND,yes,VID/DIRECT-encoded
0xC0,2,0x0000,MFR_CONFIG_ID,no,board-specific NVM ID
0xC2,2,0xCAFE,MFR_PRODUCT_REV_USER,yes,operator-settable
```

## Workflow: clone a golden config to a fresh board

```sh
# On the golden board (already provisioned + STORE_USER_ALL'd):
mpq_config read --bus 0 --addr 0x10 --output golden.dmp
mpq_config to-csv golden.dmp golden.csv
# git add golden.csv && git commit -m "mpq8785 golden config v1"

# On a fresh board (pause the kernel alarm-poll first):
echo 0 > /sys/kernel/debug/mpq8785/0-0010/alarm_poll_interval_ms
mpq_config write --bus 0 --addr 0x10 --input golden.dmp --store

# Power-cycle the board, then verify:
mpq_config live-diff --bus 0 --addr 0x10 --input golden.dmp  # exit 0 = clean

# Inspect a saved dump in human terms:
mpq_config explain golden.dmp
```

## Safety

- `write` refuses to run if the kernel mpq8785 driver's alarm-poll
  worker is active (debugfs `alarm_poll_interval_ms != 0`). Its
  background `STATUS_WORD` reads would interleave with bulk writes
  and could land mid-NVM-busy window. Pause it first:
  `echo 0 > /sys/kernel/debug/mpq8785/0-0010/alarm_poll_interval_ms`,
  then re-run. `--force` overrides the check (not recommended).

- `write` clears `WRITE_PROTECT (0x10)` before bulk writes and
  restores it afterward.

- Entries with `clone_safe = no` are skipped on write unless
  `--force`. This excludes chip-identity (`MFR_CONFIG_ID`,
  `MFR_SILICON_REV`), strap-latched (`MFR_VBOOT_CFG`), addressing
  (`MFR_ADDR_PMBUS`), and live-state (`PROTECTION_LAST`,
  `MFR_RETRY_TIMES`) registers.

- `STORE_USER_ALL (0x15)` is opt-in via `--store`. Without it, the
  chip's RAM is updated but NVM is untouched: a power cycle
  reverts everything. It is safe for experimentation.

- The tool does not call `CLEAR_LAST_FAULT (0x08)`. The chip's
  NVM-backed `PROTECTION_LAST` is left untouched by config writes;
  use the kernel driver's `clear_protection_last[_force]` debugfs
  for that.

## Dependencies

- Linux kernel with `CONFIG_I2C_CHARDEV=y` (for `/dev/i2c-N`).

- Permissions: either run as root or join the `i2c` group on the
  target system (`udev` rule `KERNEL=="i2c-[0-9]*", GROUP="i2c",
  MODE="0660"` is standard).

- The `pmbus/mpq8785` kernel driver can stay bound: the tool coexists
  via the i2c-adapter mutex. For `write`, set the driver's
  `alarm_poll_interval_ms` to 0 first to avoid interleaving.

## Status

Work in progress.
