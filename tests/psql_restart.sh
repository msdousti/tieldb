#!/usr/bin/env bash
# Stops a real tieldb with SIGTERM, starts it again on the same data directory, and checks with a
# real psql that the rows are still there, in the display order they had.
#
# usage: psql_restart.sh <tieldb> <psql> <expected-output-file>
#
# Exit status 77 means the test is skipped (meson's convention).
set -euo pipefail

tieldb=$1
psql=$2
expected=$3

work=$(mktemp -d)
server_pid=""
port=""
trap 'rm -rf "$work"; if [ -n "$server_pid" ]; then kill "$server_pid" 2>/dev/null || true; fi' EXIT

start_server() {
    : >"$work/server.out"
    "$tieldb" --port 0 --data-dir "$work/data" >"$work/server.out" 2>&1 &
    server_pid=$!
    port=""
    for _ in $(seq 100); do
        port=$(sed -n 's/^listening on port //p' "$work/server.out")
        if [ -n "$port" ]; then
            return 0
        fi
        sleep 0.1
    done
    echo "tieldb did not start:" >&2
    cat "$work/server.out" >&2
    exit 1
}

# SIGTERM is a clean stop: the server saves the data directory and exits with status 0
stop_server() {
    kill -TERM "$server_pid"
    local status=0
    wait "$server_pid" || status=$?
    server_pid=""
    if [ "$status" -ne 0 ]; then
        echo "tieldb exited with status $status after SIGTERM:" >&2
        cat "$work/server.out" >&2
        exit 1
    fi
}

run_psql() {
    "$psql" -X -h 127.0.0.1 -p "$port" -U tester testdb
}

mkdir "$work/data"
start_server
run_psql >"$work/before" <<'SQL'
create table pet (id integer, name text);
insert into pet values (1, 'Rex');
insert into pet values (2, 'Tom');
SQL
stop_server

start_server
run_psql >"$work/after" <<'SQL'
select * from pet;
SQL
stop_server

diff -u "$expected" "$work/after"
