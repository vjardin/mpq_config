# mpq_config pytest suite

The same tests run two ways:

* Host — against fixtures in `../test-data/`. No hardware. Pure file
  conversion / decoder / format-compliance / loader-guard coverage. Runs
  in ~0.1 s.
* Target — against the live chip on the board. Adds i2c,
  kernel hwmon, debugfs, and end-to-end `mpq_config` round-trip
  coverage. Adds ~30 s for the chip walk + factory.txt upload.

## Running

The suite is wired into `meson test` so the build system drives it.
See `../meson.build` for the `test()` declarations.

```sh
# Host only (default):
meson setup build && meson test -C build          # from ../

# Add the target-side tests (drives /dev/ttyUSB0 by default):
meson setup build-target -Dwith_target_tests=true
MPQ_SERIAL_PORT=/dev/ttyUSB0 meson test -C build-target

# Override the serial port / baud / chip address:
MPQ_SERIAL_PORT=/dev/ttyUSB1 \
MPQ_BUS=0 MPQ_ADDR=0x10 \
    meson test -C build-target
```

Direct pytest invocation also works (skips the meson rebuild step
and lets you pick individual tests / markers):

```sh
MPQ_CONFIG_BIN=$PWD/build/mpq_config \
    pytest -m host test/                          # host only
MPQ_CONFIG_BIN=$PWD/build/mpq_config \
MPQ_SERIAL_PORT=/dev/ttyUSB0 \
    pytest test/                                  # host + target
```

## Env vars

| Variable          | Default                          | Purpose                                  |
|-------------------|----------------------------------|------------------------------------------|
| `MPQ_CONFIG_BIN`  | `../mpq_config`                  | host build to exercise                   |
| `MPQ_TEST_DATA`   | `../test-data`                   | fixture directory                        |
| `MPQ_SERIAL_PORT` | unset (target tests skip)        | board console; presence enables target   |
| `MPQ_SERIAL_BAUD` | `115200`                         | serial baud                              |
| `MPQ_BUS`         | `0`                              | i2c bus index on the board               |
| `MPQ_ADDR`        | `0x10`                           | chip address                             |

## Test data

```
../test-data/
├── factory.txt            -- MPS Power Manager GUI export (factory NVM
│                             config that was burned into the board
│                             reference). Reference for the `diff`
│                             tests and for `from-mps` parser checks.
├── u2200-live.dmp         -- native dump captured from the live chip
│                             after the operator wrote 0xCAFE to
│                             MFR_PRODUCT_REV_USER and STORE_USER_ALL'd.
├── u2200-live.csv         -- to-csv of the above; round-trip fixture.
└── u2200-live.mps.txt     -- to-mps of the above; round-trip fixture.
```

Regenerate the fixtures after a chip-state change:

```sh
mpq_config read --bus 0 --addr 0x10 --all \
                --output test-data/u2200-live.dmp
mpq_config to-csv test-data/u2200-live.dmp test-data/u2200-live.csv
mpq_config to-mps test-data/u2200-live.dmp test-data/u2200-live.mps.txt
```

## Dependencies

Host: `python3-pytest`. Target also needs `python3-pyserial`.

## Markers

```sh
pytest -m host    test/   # host group; same set as `meson test -C build`
pytest -m target  test/   # only target tests (needs MPQ_SERIAL_PORT;
                          # same set as `meson test -C build-target`
                          # when configured with -Dwith_target_tests=true)
pytest test/              # both groups
```
