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

executable('mpq_config',
  mpq_config_sources,
  link_args : link_args,
  install : true,
)
