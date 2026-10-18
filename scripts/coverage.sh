#!/bin/bash
# Run the tests with coverage and enforce the line-coverage threshold in gcovr.cfg.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [ ! -d build-cov ]; then
    meson setup build-cov -Db_coverage=true
fi

find build-cov -name '*.gcda' -delete
meson test -C build-cov

status=0
ninja -C build-cov coverage-text || status=$?
cat build-cov/meson-logs/coverage.txt
exit "$status"
