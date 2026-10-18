#!/bin/bash
# Run the tests with coverage and enforce the line-coverage threshold in gcovr.cfg.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Unoptimized: inlining and _FORTIFY_SOURCE add compiler-generated branches that no test can reach
if [ ! -d build-cov ]; then
    meson setup build-cov -Db_coverage=true -Dbuildtype=debug
else
    meson configure build-cov -Db_coverage=true -Dbuildtype=debug
fi

find build-cov -name '*.gcda' -delete
meson test -C build-cov

status=0
ninja -C build-cov coverage-text || status=$?
cat build-cov/meson-logs/coverage.txt
exit "$status"
