# TielDB - Educational Database Management System
# Main Meson build configuration

project('tieldb', 'c',
  version: '0.1.0',
  license: 'Educational',
  default_options: [
    'c_std=c2x',           # C23 standard
    'warning_level=3',     # -Wall -Wextra -Wpedantic
    'werror=true',         # Warnings are errors
    'buildtype=debugoptimized',
  ]
)

cc = meson.get_compiler('c')
if cc.get_id() != 'clang' or not cc.version().version_compare('>=22')
  error('TielDB is built with clang 22 or newer. Set up a fresh build directory with: ' +
        'meson setup <builddir> --native-file clang.ini')
endif

# Add extra warnings
add_project_arguments([
  '-Wshadow',
  '-Wconversion',
  '-Wstrict-prototypes',
  '-Wformat=2',
], language: 'c')

if get_option('fuzz')
  if not get_option('b_sanitize').contains('address')
    error('The fuzz targets need AddressSanitizer: pass -Db_sanitize=address')
  endif
  add_project_arguments('-fsanitize=fuzzer-no-link', language: 'c')
endif

# Dependencies
cmocka_dep = dependency('cmocka', required: false)

# Include directories
inc = include_directories('include', 'src')

# Subdirectories
subdir('src')
subdir('tests')

# Summary
summary({
  'prefix': get_option('prefix'),
  'libdir': get_option('libdir'),
  'datadir': get_option('datadir'),
}, section: 'Directories')

summary({
  'c_std': get_option('c_std'),
  'buildtype': get_option('buildtype'),
  'werror': get_option('werror'),
}, section: 'Build Options')
