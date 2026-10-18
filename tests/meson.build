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
)

test('wire_buffer', test_wire_buffer)

# Test: wire protocol input buffer
test_wire_reader = executable('test_wire_reader',
  'test_wire_reader.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('wire_reader', test_wire_reader)

# Test: backend message encoders
test_protocol = executable('test_protocol',
  'test_protocol.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('protocol', test_protocol)

# Test: wire protocol output buffer, out-of-memory paths. wire_buffer.c is compiled a second
# time with its allocation calls renamed to the mocks in the test, so only that file is affected.
wire_buffer_oom_lib = static_library('wire_buffer_oom',
  net_sources,
  include_directories: inc,
  c_args: ['-Dmalloc=oom_malloc', '-Dcalloc=oom_calloc', '-Drealloc=oom_realloc'],
)

test_wire_buffer_oom = executable('test_wire_buffer_oom',
  'test_wire_buffer_oom.c',
  dependencies: [cmocka_dep],
  include_directories: inc,
  link_with: wire_buffer_oom_lib,
)

test('wire_buffer_oom', test_wire_buffer_oom)
