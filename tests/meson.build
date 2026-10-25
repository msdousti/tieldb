# TielDB tests

if get_option('fuzz')
  subdir('fuzz')
endif

if not cmocka_dep.found()
  warning('cmocka not found, tests will be skipped')
  warning('Install with: sudo apt install libcmocka-dev (Ubuntu/Debian)')
  subdir_done()
endif

# Test support: fault injection for the I/O interface (the server does not contain it)
test_support = static_library('test_support',
  'io_fault.c',
  dependencies: [tieldb_dep],
  include_directories: inc,
)

test_support_dep = declare_dependency(
  link_with: test_support,
  dependencies: tieldb_dep,
)

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

# Test: hash map
test_hash_map = executable('test_hash_map',
  'test_hash_map.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('hash_map', test_hash_map)

# Test: type registry
test_type_registry = executable('test_type_registry',
  'test_type_registry.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('type_registry', test_type_registry)

# Test: slotted page
test_page = executable('test_page',
  'test_page.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('page', test_page)

# Test: one-page storage engine
test_one_page_engine = executable('test_one_page_engine',
  'test_one_page_engine.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('one_page_engine', test_one_page_engine)

# Test: saving the catalog file
test_catalog_file = executable('test_catalog_file',
  'test_catalog_file.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('catalog_file', test_catalog_file)

# Test: the engine registry
test_engine_registry = executable('test_engine_registry',
  'test_engine_registry.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('engine_registry', test_engine_registry)

# Test: tables and their engines
test_table_engines = executable('test_table_engines',
  'test_table_engines.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('table_engines', test_table_engines)

# Test: the data directory
test_data_dir = executable('test_data_dir',
  'test_data_dir.c',
  dependencies: [test_support_dep, cmocka_dep],
  include_directories: inc,
)

test('data_dir', test_data_dir)

# Test: loading the catalog file
test_catalog_file_load = executable('test_catalog_file_load',
  'test_catalog_file_load.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('catalog_file_load', test_catalog_file_load)

# Test: files that hold one page
test_one_page_file = executable('test_one_page_file',
  'test_one_page_file.c',
  dependencies: [test_support_dep, cmocka_dep],
  include_directories: inc,
)

test('one_page_file', test_one_page_file)

# Test: saving and loading the one-page engine
test_one_page_engine_files = executable('test_one_page_engine_files',
  'test_one_page_engine_files.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('one_page_engine_files', test_one_page_engine_files)

# Test: files of pages
test_page_file = executable('test_page_file',
  'test_page_file.c',
  dependencies: [test_support_dep, cmocka_dep],
  include_directories: inc,
)

test('page_file', test_page_file)

# Test: CRC-32C
test_crc32c = executable('test_crc32c',
  'test_crc32c.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('crc32c', test_crc32c)

# Test: page checksum and validation
test_page_check = executable('test_page_check',
  'test_page_check.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('page_check', test_page_check)

# Test: row codec
test_row_codec = executable('test_row_codec',
  'test_row_codec.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('row_codec', test_row_codec)

# Test: flag tests
test_flags = executable('test_flags',
  'test_flags.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('flags', test_flags)

# Test: buffer pool
test_buffer_pool = executable('test_buffer_pool',
  'test_buffer_pool.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('buffer_pool', test_buffer_pool)

# Test: heap tuple header
test_heap_tuple = executable('test_heap_tuple',
  'test_heap_tuple.c',
  dependencies: [tieldb_dep, cmocka_dep],
  include_directories: inc,
)

test('heap_tuple', test_heap_tuple)

# Test: catalog
test_catalog = executable('test_catalog',
  'test_catalog.c',
  dependencies: [tieldb_dep, cmocka_dep, thread_dep],
  include_directories: inc,
)

test('catalog', test_catalog)

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
  ds_sources,
  types_sources,
  net_sources,
  sql_sources,
  storage_sources,
  buffer_sources,
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

# Test: a real psql session against the tieldb binary. Skipped when psql is not installed.
psql = find_program('psql', required: false)
if psql.found()
  test('psql_session', find_program('bash'),
    args: [files('psql_session.sh'), tieldb_exe, psql.full_path(), files('psql_session.expected')],
    depends: tieldb_exe,
    suite: 'client',
    timeout: 60,
  )
  test('psql_restart', find_program('bash'),
    args: [files('psql_restart.sh'), tieldb_exe, psql.full_path(), files('psql_restart.expected')],
    depends: tieldb_exe,
    suite: 'client',
    timeout: 60,
  )
else
  warning('psql not found, the psql session test will be skipped')
endif
