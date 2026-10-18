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

# Test: startup packet handling
test_startup = executable('test_startup',
  'test_startup.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('startup', test_startup)

# Test: simple SQL recognizers
test_simple_sql = executable('test_simple_sql',
  'test_simple_sql.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('simple_sql', test_simple_sql)

# Test: in-memory tables
test_table = executable('test_table',
  'test_table.c',
  dependencies: [tieldb_dep, cmocka_dep, thread_dep],
  include_directories: inc,
)

test('table', test_table)

# Test: statement execution
test_executor = executable('test_executor',
  'test_executor.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('executor', test_executor)

# Test: simple-query handler
test_query_handler = executable('test_query_handler',
  'test_query_handler.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('query_handler', test_query_handler)

# Test: per-connection message loop
test_connection = executable('test_connection',
  'test_connection.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('connection', test_connection)

# Test: command-line options
test_options = executable('test_options',
  'test_options.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('options', test_options)

# Test: the tieldb binary, started as a real process
test_tieldb = executable('test_tieldb',
  'test_tieldb.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('tieldb', test_tieldb, args: [tieldb_exe], depends: tieldb_exe)

# Test: TCP listener
test_server = executable('test_server',
  'test_server.c',
  dependencies: [tieldb_dep, cmocka_dep, thread_dep],
  include_directories: inc,
)

test('server', test_server)

# Test: wire protocol output buffer, out-of-memory paths. wire_buffer.c is compiled a second
# time with its allocation calls renamed to the mocks in the test, so only that file is affected.
wire_buffer_oom_lib = static_library('wire_buffer_oom',
  io_sources,
  net_sources,
  sql_sources,
  storage_sources,
  executor_sources,
  include_directories: inc,
  c_args: ['-Dmalloc=oom_malloc', '-Dcalloc=oom_calloc', '-Drealloc=oom_realloc'],
)

test_wire_buffer_oom = executable('test_wire_buffer_oom',
  'test_wire_buffer_oom.c',
  dependencies: [cmocka_dep, thread_dep],
  include_directories: inc,
  link_with: wire_buffer_oom_lib,
)

test('wire_buffer_oom', test_wire_buffer_oom)
