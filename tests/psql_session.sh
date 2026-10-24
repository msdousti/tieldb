#!/usr/bin/env bash
# Runs a real psql against a real tieldb and compares what psql prints with an expected file.
#
# usage: psql_session.sh <tieldb> <psql> <expected-output-file>
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
    "$tieldb" --port 0 --once --in-memory >"$work/server.out" 2>&1 &
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

run_session() {
    start_server
    "$psql" -X -h 127.0.0.1 -p "$port" -U tester testdb >"$work/actual" 2>&1 <<'SQL'
create table pet (id integer, name text);
insert into pet values (1, 'Rex');
insert into pet values (2, 'O''Neil');
insert into pet values (007, 'Spot');
select * from pet;
insert into pet values ('x', 'y');
select * from nope;
;
select version();
select current_user;
select current_database();
SQL
    wait "$server_pid" || true
    server_pid=""
    diff -u "$expected" "$work/actual"
}

# psql warns about a version mismatch when the server does not report server_version, but only
# on a terminal, so run it under script. The server claims to be version 18.
check_no_version_warning() {
    local major
    major=$("$psql" --version | sed -n 's/.* \([0-9][0-9]*\)\..*/\1/p')
    if [ "$major" != "18" ] || ! command -v script >/dev/null; then
        echo "skipping the version warning check (needs psql 18 and script)"
        return 0
    fi
    start_server
    printf '\\q\n' | script -qec "$psql -X -h 127.0.0.1 -p $port" /dev/null >"$work/banner" 2>&1 || true
    wait "$server_pid" || true
    server_pid=""
    if grep -q WARNING "$work/banner"; then
        echo "psql printed a warning:" >&2
        cat "$work/banner" >&2
        exit 1
    fi
}

run_session
check_no_version_warning
