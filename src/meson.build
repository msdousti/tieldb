# TielDB source files

# I/O module
io_sources = files(
  'io/io.c',
)

# Network module
net_sources = files(
  'net/wire_buffer.c',
  'net/protocol.c',
  'net/startup.c',
  'net/query_handler.c',
  'net/connection.c',
  'net/server.c',
)

# Data structures
ds_sources = files(
  'ds/hash_map.c',
  'ds/clock_policy.c',
)

# Type registry
types_sources = files(
  'types/type_registry.c',
)

# SQL module
sql_sources = files(
  'sql/simple_sql.c',
)

# Executor module
executor_sources = files(
  'executor/executor.c',
)

# Storage module
storage_sources = files(
  'storage/table.c',
  'storage/catalog.c',
  'storage/catalog_file.c',
  'storage/data_dir.c',
  'storage/crc32c.c',
  'storage/page.c',
  'storage/engine_registry.c',
  'storage/one_page_engine.c',
  'storage/page_file.c',
  'storage/one_page_file.c',
  'storage/row_codec.c',
  'storage/heap_tuple.c',
)

# Buffer pool module
buffer_sources = files(
  'buffer/buffer_pool.c',
)

# Command-line options
cli_sources = files(
  'options.c',
)

thread_dep = dependency('threads')

# Build static library
tieldb_lib = static_library('tieldb',
  io_sources,
  ds_sources,
  types_sources,
  net_sources,
  sql_sources,
  storage_sources,
  buffer_sources,
  executor_sources,
  cli_sources,
  include_directories: inc,
  install: false,
)

# Dependency for tests
tieldb_dep = declare_dependency(
  link_with: tieldb_lib,
  dependencies: thread_dep,
  include_directories: inc,
)

# The server executable
tieldb_exe = executable('tieldb',
  'main.c',
  link_with: tieldb_lib,
  dependencies: thread_dep,
  include_directories: inc,
)
