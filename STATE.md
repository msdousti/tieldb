# TielDB Development State

**Last updated:** 2026-10-19
**Phase 1 is complete; Phase 2 (persistence) is under way.** The type registry is done and values
are stored in their own format. Next: the slotted page (see "What comes next").

This file is the hand-over for a new session. Read it first, then `CLAUDE.md` (the rules) and
`ROADMAP.md` (the plan). `ROADMAP.md` is the source of truth for what is done.

---

## What TielDB is

An educational DBMS in C23 that speaks the Postgres wire protocol, so `psql` connects to it. The
goal is to learn database internals by building it in small, tested steps. No TLS, no
authentication, no security features: the clients are `psql` and well-behaved drivers, and the code
does not defend against malformed or hostile client input.

## What works today

Start the server and connect with `psql` (one command: `scripts/dev.sh`, see below):

```
create table pet (id integer, name text);      CREATE TABLE
insert into pet values (1, 'Rex');             INSERT 0 1
select * from pet;                             the rows
select version();   select current_user;   select current_database();
;                                              (an empty statement is fine)
```

- `CREATE TABLE t (col type, ...)` with types `INTEGER`/`INT`/`INT4` and `TEXT`; zero columns is
  allowed (`create table t();`). Errors use Postgres SQLSTATEs: 42P07 table exists, 42701 repeated
  column, 42704 unknown type.
- `INSERT INTO t VALUES (...)`: integers and `'strings'` (with `''` as the escape); values are checked
  against the column types (22P02); wrong count gives 42601.
- `SELECT * FROM t`. Unknown table gives 42P01. Other statements get 0A000 ("unsupported").
- Everything is in memory and lost on restart (Phase 2 adds persistence). A table holds what fits in one 8 KB page (a few hundred small rows); a full table is error 53100, a row that could never fit 54000.
- One thread per connection. Tables and the catalog are shared and locked.
- Columns have a display order separate from the storage order (`table_set_display_order`); there
  is no SQL to change it yet (roadmap: `ALTER TABLE ... MODIFY ... AFTER`).

Integers are stored as 4-byte values, so `007` is shown as `7`, like Postgres.

Known deviations from Postgres: the server reports `server_version` `18.0 (TielDB 0.1.0)` so that
psql 18 does not print a version-mismatch warning; the table named `user` works only because
TielDB's matcher is simple (it is a reserved word in Postgres); SQL is recognized by stopgap
matchers (`src/sql/simple_sql.c`), not a parser; no comments, no multiple statements per query.

## How to work (the process the user wants)

The user (Sadeq Dousti) works in this Linux VM directly: never `ssh`. They merge PRs themselves
and say "merged and pulled"; then continue with the next PR. They run `/code-review` on PRs
themselves and expect a short triage (real / skipped, with reasons) before any fix is pushed.

- **Small PRs.** Feature PRs aim for 100 to 200 changed lines; **500 is a hard limit**: ask the
  user before exceeding it, or split. Measure with `git diff --shortstat main` before opening.
  Docs, CI and refactor PRs are exempt from the target but still kept small.
- **One concern per PR.** If a change grows, split it (parser, then catalog, then executor, then
  cleanup is how CREATE TABLE went).
- **Every PR updates `ROADMAP.md`**: tick a task only when all of it is done; add sub-items when a
  PR does part of a step.
- **PR description** follows `.github/pull_request_template.md` (Summary, Size, Checklist) and ends
  with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`. `gh pr edit` fails (a
  Projects-classic deprecation), so edit a body with
  `gh api -X PATCH repos/msdousti/tieldb/pulls/N -F body=@file`. Commits end with a
  `Co-Authored-By: Claude ...` line.
- **CI is required and strict.** Seven checks must pass, for admins too; never merge or bypass a
  red PR. Docs-only PRs (`*.md`, `docs/`, `.claude/`, and the local scripts `dev.sh`,
  `valgrind.sh`, `install-hooks.sh`, `pre-commit`) skip the build jobs but still report.
- **Coverage is 100% of lines and branches of `src/`** (`gcovr.cfg`). Do not contort code to
  reach it. If a path is impractical to cover, **stop and ask**; lowering the threshold or adding
  an exclusion needs the user's explicit approval each time. Mock allocation failures with the test
  library (the `-D` renames in `tests/meson.build`), never by touching production code.
- **Opinion questions** ("WDYT?", "should we...") get a direct answer with a recommendation, not
  code. "Just answer" means no changes. Do not start the next PR before being told to when the
  user said so.
- Stacked PRs: when a branch builds on an unmerged PR, push the branch and open its PR after the
  base merges, using `git rebase --onto origin/main <old-base> <branch>` (a plain rebase replays
  the squashed commits and conflicts).

## Toolchain (Clang 22 only)

- **Clang 22 is the only compiler**: `clang.ini` is a Meson native file, and `meson.build` rejects
  anything else. Always set up build directories with `--native-file clang.ini`. GCC is not
  installed on the machine. CI uses the composite action `.github/actions/setup-clang`
  (apt.llvm.org clang-22, meson, cmocka, `postgresql-client`).
- Tools: `clang-format-22`, `clang-tidy-22`, `llvm-cov-22` (through gcovr), Valgrind, `psql` 18.
  CI has Meson 1.3.2 (older than local 1.10): keep `meson.build` compatible (pass `psql.full_path()`,
  not the program object, to `test()`).
- Environment variables fill in what a command line leaves out: if `PGDATABASE`, `PGUSER` etc. are
  set in the shell, a manual `psql` uses them.

## Daily commands

```bash
scripts/dev.sh                     # build build-asan, start tieldb on 5432, open psql; stops on exit
scripts/dev.sh -c "select 1"       # psql arguments pass through; PORT=5433 and BUILD_DIR override
meson setup build-asan --native-file clang.ini -Db_sanitize=address,undefined   # once
ninja -C build-asan && meson test -C build-asan                   # build and test (ASan + UBSan)
meson setup build-tsan --native-file clang.ini -Db_sanitize=thread  # TSan; same test command
scripts/coverage.sh                # build-cov; fails below 100% lines/branches
                                   # on main, CI also publishes the README coverage badges (scripts/coverage-badges.sh)
scripts/valgrind.sh                # build-valgrind; occasional use
clang-format-22 -i src/*/*.[ch] tests/*.c
clang-tidy-22 -p build-asan $(find src tests -name '*.c')
```

The pre-commit hook (`scripts/install-hooks.sh`, copies `scripts/pre-commit`) runs the format check
and clang-tidy; tidy findings fail the commit. `build-release` (`--buildtype=release`) exists for
the CI release job. All `build-*` directories are git-ignored.

The git identity is already set: `3616518+msdousti@users.noreply.github.com` (never the gmail
address; GitHub rejects it).

## Code layout

```
src/
  main.c             the tieldb binary: --port N (default 5432), --once, prints "listening on port N"
  options.{h,c}      command-line parsing
  io/                injectable I/O (io->read/write/...) for fault injection
  ds/hash_map.{h,c}  reusable chained hash map; entries embed hash_node_t; no locking inside
  net/
    wire_buffer      big-endian writer (INT32_MAX cap, patch/truncate) and bounds-checked reader
    protocol         backend message encoders (R, S, Z, C, E, T, D, I)
    startup          SSLRequest/GSS decline, CancelRequest, StartupMessage; parses user/database
    session.h        session_info_t {user, database}
    query_handler    Query ('Q'): single-value queries, SELECT *, INSERT, CREATE TABLE, errors
    connection       per-connection loop (startup, then messages until Terminate)
    server           loopback TCP listener, a detached thread per connection
  types/             type registry: int4 and text (OID, storage size, accepts/input/output/compare)
  sql/simple_sql     stopgap recognizers (version/current_user/current_database, select *,
                     insert, create table, empty); the real parser is a Phase 3 task
  executor/          executor_insert, executor_create_table, executor_select_all (+ release)
  storage/
    table              catalog entry of a table: name, columns, display order, per-table mutex,
                       embedded hash node; it owns a storage engine instance that holds the rows
                       (insert converts text to the stored format of the column types)
    catalog            all tables by name in a hash map behind a read-write lock
    storage_engine.h   engine interface: rows and cursors (insert, open/next/close cursor, destroy)
    one_page_engine    the first engine: all rows of a table in one 8 KB page, in memory
    row_codec          null bitmap and values of a row as bytes, independent of the engine
    page               8 KB slotted page, slot states, CRC-32C checksum, page_validate
    page_file          read/write pages of a file through io->, checksum checked on read
    one_page_file   save/load a file that holds exactly one page (one-page engine, catalog file)
    catalog_file    the table list as rows of one page file: a version row, then a row per table and per column (save only so far)
    crc32c             the page checksum
tests/               one test_*.c per module (cmocka), test_wire_buffer_oom.c (allocation
                     failures via renamed malloc/calloc/realloc), test_tieldb.c (spawns the
                     binary), psql_session.sh + .expected (real psql, suite "client"),
                     table_assert.h (assert_cell_text for tests that read tables)
.claude/             design docs for the rules (style, clean code, C guidelines, design choices)
```

Design points worth knowing:
- **Executor vs handler.** The executor knows what a statement does and returns either success, a
  client-visible SQL error (`executor_error_t`: SQLSTATE + message, return 0), or a server failure
  (return -1 with `errno`, e.g. ENOMEM, which drops the connection). The query handler only encodes
  results as protocol messages.
- **Locking.** `executor_select_all` returns the table locked; the caller releases it after
  building the response. INSERT holds the table lock while it checks and inserts, because it reads
  the display order. `table_insert` locks itself; `table_insert_locked` is for lock holders. Readers
  currently block writers on a table (Postgres does not); the real fix is MVCC in Phase 6.
- **Atomic responses.** Every encoder and handler appends in full or not at all (rollback to the
  recorded writer size on failure).

## Coding rules that CI enforces (details in CLAUDE.md)

C23 (`nullptr`, `constexpr`, `[[nodiscard]]`, `static_assert`, `bool`). Functions at most 20 lines,
nesting depth 2 (cmocka `assert_*` macros count as a nesting level inside loops and `if` blocks:
use `if (...) { fail(); }` or small helpers), at most 4 parameters, no magic numbers in `src/`
(name them with `constexpr`), opaque types for stateful modules, errno on every failure documented
in the header and tested, `-Werror` with `-Wconversion`, strict `-std=c2x` (some POSIX features
need `#define _POSIX_C_SOURCE 200809L`, as in `catalog.c`). Trust the client: no validation or tests
whose only purpose is to survive malformed or hostile client input. Branch coverage with Clang
counts conditions: a loop condition with `&&` needs a test for each operand being false.

## What comes next

1. **Phase 2, in the order of the ROADMAP.md plan** (decisions are written there: the catalog in
   a catalog file first and system tables in milestone B, flush on shutdown and checkpoint since there is no WAL yet, per-table mutex
   plus a buffer pool lock, rows must fit one page, overflow storage is Phase 8+). Done: the type
   registry (PRs 1 to 3: registry, lookups through it, typed cells) and the slotted page with its
   reference-model test. Phase 2 has two milestones (see ROADMAP.md).
   **A, data survives a restart, with a table being one page:** row codec, page file (checksum and `page_validate`), storage engine interface with the
   one-page engine wired into the executor, catalog file, then `--data-dir`, the version file and
   clean shutdown (SIGTERM/SIGINT) with a restart test. **B, lift the one-page limit:** fuzzing
   build and page fuzz target, heap tuple header, heap file, buffer pool, free space map, heap
   engine, catalog in system tables, crash test. Phase 3 also lists memory contexts, BOOLEAN, NULL
   and DEFAULT.
2. Phase 3 (already in the roadmap): the real SQL parser and syntax tree first, then expressions,
   `WHERE`, projection, `DELETE`/`UPDATE`/`TRUNCATE`/`INSERT ... SELECT`, `generate_series`,
   `ALTER TABLE ... MODIFY/ADD COLUMN ... AFTER|FIRST`, ORDER BY, aggregates, joins.
3. Later: user-defined types on top of the registry (`CREATE TYPE ... AS ENUM` first), B+trees, WAL,
   MVCC.

## Troubleshooting

- `test_wire_buffer_oom` compiles the net, storage and executor sources with `malloc` renamed to a
  mock, so a new `malloc`/`realloc`/`calloc` call in those files needs a matching `will_return`
  in the tests that reach it (a missing one fails the test, and leftover values make later tests
  hang). Run it with `timeout 20` when debugging.

- A build directory made with GCC (or without the native file) fails at configure with a message
  pointing at `clang.ini`: delete it and set it up again.
- `coverage.sh` fails with stale `.gcno` errors after switching branches: `rm -rf build-cov`.
- gcov `NegativeHits` errors come from threaded tests; the coverage build uses
  `-fprofile-update=atomic` (already in `scripts/coverage.sh`).
- Valgrind reports "possibly lost" blocks from glibc's cache of exited detached threads: not a leak
  (`scripts/valgrind.sh` only fails on definite and indirect leaks).
- RLIMIT_NPROC test (`test_server`) is skipped when run as root.
- psql prints the version warning only on a terminal; the client test uses `script` for that and
  only when psql is version 18.
