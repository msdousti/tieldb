# TielDB tests

if not cmocka_dep.found()
  warning('cmocka not found, tests will be skipped')
  warning('Install with: sudo apt install libcmocka-dev (Ubuntu/Debian)')
  subdir_done()
endif

# Test: I/O interface
test_io = executable('test_io',
  'test_io.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('io', test_io)

# Test: wire protocol byte buffer
test_wire_buffer = executable('test_wire_buffer',
  'test_wire_buffer.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
  link_args: ['-Wl,--wrap=malloc', '-Wl,--wrap=calloc', '-Wl,--wrap=realloc'],
)

test('wire_buffer', test_wire_buffer)
