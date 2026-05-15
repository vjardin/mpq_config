# SPDX-License-Identifier: BSD-4-Clause
# Copyright 2026 Vincent Jardin, Free Mobile <vjardin@free.fr>

project('mpq_config', 'c',
  version : '0.1.0',
  license : 'BSD-4-Clause',
  default_options : [
    'c_std=c11',
    'warning_level=3',
    'buildtype=release',
  ],
)

add_project_arguments(
  '-Wmissing-prototypes',
  '-D_POSIX_C_SOURCE=200809L',
  language : 'c',
)

link_args = []
if get_option('static')
  link_args += ['-static', '-static-libgcc']
endif

# src/meson.build defines mpq_config_sources
subdir('src')

mpq_config_exe = executable('mpq_config',
  mpq_config_sources,
  link_args : link_args,
  install : true,
)

# `meson test`
#
# host group is default,
#
# target group only when -Dwith_target_tests=true
# (needs MPQ_SERIAL_PORT in env, defaults to /dev/ttyUSB0 inside
# the tests).
#
# Both modes export MPQ_CONFIG_BIN pointing at the
# freshly-built binary so they exercise THIS tree, not whatever's
# in the $PATH.
pytest = find_program('pytest', 'pytest-3', required : false)
if pytest.found()
  test_env = environment()
  test_env.set('MPQ_CONFIG_BIN', mpq_config_exe.full_path())
  test_env.set('MPQ_TEST_DATA',  meson.current_source_dir() / 'test-data')

  test_dir = meson.current_source_dir() / 'test'

  test('host',
    pytest,
    args : ['-m', 'host', '-q', test_dir],
    env : test_env,
    suite : 'host',
    timeout : 60,
    depends : [mpq_config_exe],
  )

  if get_option('with_target_tests')
    test('target',
      pytest,
      args : ['-m', 'target', '-q', test_dir],
      env : test_env,
      suite : 'target',
      timeout : 120,
      depends : [mpq_config_exe],
    )
  endif
else
  warning('pytest not found; skipping `meson test` integration. ' +
          'apt install python3-pytest python3-pyserial')
endif
