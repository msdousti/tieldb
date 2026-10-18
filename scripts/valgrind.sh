#!/usr/bin/env bash
# Run the test suite under Valgrind (Memcheck). Meant for occasional use, for example at the end
# of a phase: ASan in CI covers the same ground, but Valgrind also finds uninitialized reads.
#
# Only definite and indirect leaks fail the run. glibc keeps the stack and TLS block of an exited
# detached thread cached, which Valgrind reports as "possibly lost"; it is not a leak.
set -euo pipefail

cd "$(dirname "$0")/.."
build_dir=${BUILD_DIR:-build-valgrind}

if [ ! -d "$build_dir" ]; then
    meson setup "$build_dir" --native-file clang.ini
fi
ninja -C "$build_dir"

meson test -C "$build_dir" --timeout-multiplier 10 \
    --wrapper "valgrind --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite,indirect --trace-children=yes --quiet" "$@"
