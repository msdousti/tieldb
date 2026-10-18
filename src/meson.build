# TielDB source files

# I/O module
io_sources = files(
  'io/io.c',
)

# Build static library
tieldb_lib = static_library('tieldb',
  io_sources,
  include_directories: inc,
  install: false,
)

# Dependency for tests
tieldb_dep = declare_dependency(
  link_with: tieldb_lib,
  include_directories: inc,
)
