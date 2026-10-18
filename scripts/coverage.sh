#!/bin/bash
# Run the tests with coverage and enforce the line-coverage threshold in gcovr.cfg.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Unoptimized: inlining and _FORTIFY_SOURCE add compiler-generated branches that no test can reach.
# Atomic counters: threads running the same code would otherwise corrupt the coverage counts
options=(-Db_coverage=true -Dbuildtype=debug -Dc_args=-fprofile-update=atomic)
if [ ! -d build-cov ]; then
    meson setup build-cov --native-file clang.ini "${options[@]}"
else
    meson configure build-cov "${options[@]}"
fi

find build-cov -name '*.gcda' -delete
meson test -C build-cov

status=0
ninja -C build-cov coverage-text || status=$?
cat build-cov/meson-logs/coverage.txt
exit "$status"
