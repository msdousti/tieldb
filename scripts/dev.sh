#!/usr/bin/env bash
# Build tieldb, start it, and open psql against it. The server stops when psql exits.
#
#   scripts/dev.sh                        interactive psql session
#   scripts/dev.sh -c "select version()"  any psql arguments are passed through
#
# Environment: BUILD_DIR (default build-asan), PORT (default 5432)
set -euo pipefail

cd "$(dirname "$0")/.."
build_dir=${BUILD_DIR:-build-asan}
port=${PORT:-5432}

if [ ! -d "$build_dir" ]; then
    meson setup "$build_dir" --native-file clang.ini -Db_sanitize=address,undefined
fi
ninja -C "$build_dir"

server_log=$(mktemp)
"$build_dir/src/tieldb" --port "$port" --in-memory >"$server_log" 2>&1 &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || true; rm -f "$server_log"' EXIT

for _ in $(seq 50); do
    if grep -q '^listening on port' "$server_log"; then
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        cat "$server_log" >&2
        exit 1
    fi
    sleep 0.1
done

psql -h localhost -p "$port" "$@"
