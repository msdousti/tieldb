# libFuzzer targets: meson setup build-fuzz --native-file clang.ini -Dfuzz=true -Db_sanitize=address
# Run one with: ./build-fuzz/tests/fuzz/fuzz_page tests/fuzz/corpus/page

fuzz_page = executable('fuzz_page',
  'fuzz_page.c',
  dependencies: [tieldb_dep],
  include_directories: inc,
  link_args: ['-fsanitize=fuzzer'],
)
