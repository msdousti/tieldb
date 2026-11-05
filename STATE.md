# TielDB Development State

**Last updated:** 2026-11-05 **Phase 1 is complete. Phase 2 (persistence) is under way.** Milestone
A is done: data survives a restart. Milestone B has the page file with direct I/O, the heap tuple
header, the buffer pool (replacement policy, dirty pages, eviction, a lock), and the first part of
the free space map (a binary max-tree). Next: the rest of the free space map in three slices, then
the heap engine (see "What comes next").

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
checkpoint;                                    CHECKPOINT (saves the data directory)
select version();   select current_user;   select current_database();
;                                              (an empty statement is fine)
```

- `CREATE TABLE t (col type, ...)` with types `INTEGER`/`INT`/`INT4` and `TEXT`; zero columns is
  allowed (`create table t();`). Errors use Postgres SQLSTATEs: 42P07 table exists, 42701 repeated
  column, 42704 unknown type.
- `INSERT INTO t VALUES (...)`: integers and `'strings'` (with `''` as the escape); values are checked
  against the column types (22P02); wrong count gives 42601.
- `SELECT * FROM t`. Unknown table gives 42P01. Other statements get 0A000 ("unsupported").
- The server needs `--in-memory` or `--data-dir PATH`. With `--in-memory`, everything is lost on
  exit. `--data-dir` takes an existing directory. It is loaded at startup and saved when the server
  is stopped with SIGTERM or SIGINT, so the tables and rows survive a restart. `kill -9` loses what
  was inserted since the start. A table holds what fits in one 8 KB page (a few hundred small rows);
  a full table is error 53100, a row that could never fit 54000.
- One thread per connection. Tables and the catalog are shared and locked.
- Columns have a display order separate from the storage order (`table_set_display_order`); there
  is no SQL to change it yet (roadmap: `ALTER TABLE ... MODIFY ... AFTER`).

Integers are stored as 4-byte values, so `007` is shown as `7`, like Postgres.

Known deviations from Postgres. The server reports `server_version` `18.0 (TielDB 0.1.0)` so that
psql 18 does not print a version-mismatch warning. The table named `user` works only because
TielDB's matcher is simple (it is a reserved word in Postgres). SQL is recognized by stopgap
matchers (`src/sql/simple_sql.c`), not a parser. There are no comments and no multiple statements
per query.

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
- **A question gets an answer only.** However suggestive it sounds ("why not X?", "can't we Y?",
  "maybe in util.h?", "show me first"), do not edit, commit, push or touch a PR. Wait for an
  explicit instruction ("do it", "add it to #N", "drop X"). Give a recommendation when asked for an
  opinion, and offer the change in one line afterwards. If a reply mixes a correction with a hedge,
  restate the plan and ask. Read-only checks and scratch experiments are fine while answering. Do
  not start the next PR before being told to.
- **Estimates run low:** inflate PR sizes and PR counts by about 50%, and measure early.
- **Design rules from review** (details in `.claude/c-guidelines.md`). A module that uses an
  interface takes it from its caller and never creates a concrete implementation (section 6). Sizes
  and offsets that the module does not bound itself are computed with `ckd_add`/`ckd_mul`/`ckd_sub`
  (section 5, "Checked arithmetic"). Name things for what they are (`extra_flags`, `file_number`).
- **Throwaway experiments** (a kernel or CI experiment) run from a scratch branch whose workflow
  is triggered by a push; delete the branch afterwards and never merge it.
- **Body files for PRs:** use one file per PR (`body_<name>.md`); an old shared `body.md` once
  overwrote the wrong description.
- **Context window and compaction.** The user ends a merge message with the context percentage
  ("merged, 38%"). If it is missing and needed, ask. From about 55 to 60%, at a clean point (a PR
  merged, a clean tree, nothing half decided), offer to compact. When the user agrees, open a
  `STATE.md` update PR. After it merges, write the full state to `/tmp/compact.md` and show it. The
  user then types `/compact Keep only: at the start of the next window, read /tmp/compact.md and
  STATE.md before doing anything. Drop everything else.`
- **Check before committing:** run `git branch --show-current` (a `git checkout -b` onto an
  existing name fails without showing it, and a commit once landed on `main`). After a script
  edits Markdown, run `git diff -U0 | grep -c '^-$'` to see whether it ate blank lines.
- Stacked PRs: when a branch builds on an unmerged PR, push the branch and open its PR after the
  base merges, using `git rebase --onto origin/main <old-base> <branch>` (a plain rebase replays
  the squashed commits and conflicts).

## Toolchain (Clang 22 only)

- **Clang 22 is the only compiler**: `clang.ini` is a Meson native file, and `meson.build` rejects
  anything else. Always set up build directories with `--native-file clang.ini`. GCC is not
  installed on the machine. CI uses the composite action `.github/actions/setup-clang`
  (the clang-22 of the runner image, meson, cmocka, `postgresql-client`).
- Tools: `clang-format-22`, `clang-tidy-22`, `llvm-cov-22` (through gcovr), Valgrind, `psql` 18.
  CI and the local machine both have Meson 1.10.1 (checked in the CI log on 2026-11-05). An older
  Meson needed `psql.full_path()` in `test()`, not the program object, and that form still works.
- CI runs on `ubuntu-26.04` (glibc 2.43). It is a hosted runner, so `/dev/kvm` can be enabled for
  QEMU experiments (public repository).
- CI and the local machine both use clang 22.1.2, the Ubuntu archive build. CI takes it from the
  runner image. Locally, the 19 LLVM packages are on hold (`apt-mark hold`), so `apt upgrade` leaves
  them alone. To change the compiler version, unhold them first. `sudo` needs no password here.
  `/etc/apt/sources.list.d/llvm-22.list` still lists `apt.llvm.org`. Its duplicate files are in
  `~/apt-sources-backup-2026-11-05`.
- A prebuilt CI image (clang 22.1.8) was measured in a throwaway run and not adopted. It was 968 MB
  unpacked and 216 MB compressed, and a job took 10 to 14 s to start in it. The runner's own clang
  installs in 4 to 10 s. `--no-install-recommends` drops `libclang-rt-22-dev`, which the sanitizers
  need, so list it.
- `git.kernel.org`, `lore.kernel.org` and `phoronix.com` answer HTTP 403 to this machine. Read kernel
  sources with `gh api repos/torvalds/linux/contents/<path>?ref=<tag>` or `gh api
  repos/torvalds/linux/commits/<sha>`, and use WebSearch for mailing list threads.
- `/tmp` is tmpfs here. The user keeps loop-mounted `/mnt/ext4`, `/mnt/xfs` and `/mnt/ext2` for file
  system experiments; do not remove them.
- Environment variables fill in what a command line leaves out: if `PGDATABASE`, `PGUSER` etc. are
  set in the shell, a manual `psql` uses them.

## Daily commands

```bash
scripts/dev.sh                     # build build-asan, start tieldb --in-memory on 5432, open psql; stops on exit
scripts/dev.sh -c "select 1"       # psql arguments pass through; PORT=5433 and BUILD_DIR override
meson setup build-asan --native-file clang.ini -Db_sanitize=address,undefined   # once
ninja -C build-asan && meson test -C build-asan                   # build and test (ASan + UBSan)
meson setup build-tsan --native-file clang.ini -Db_sanitize=thread  # TSan; same test command
scripts/coverage.sh                # build-cov; fails below 100% lines/branches
                                   # on main, CI also publishes the README coverage badges (scripts/coverage-badges.sh)
scripts/valgrind.sh                # build-valgrind; occasional use
meson setup build-fuzz --native-file clang.ini -Dfuzz=true -Db_sanitize=address   # libFuzzer targets in tests/fuzz
ninja -C build-fuzz && build-fuzz/tests/fuzz/fuzz_page tests/fuzz/corpus/page -max_total_time=60
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
  main.c             the tieldb binary: --port N (default 5432), --once, and one of --data-dir PATH
                     (loaded at startup, saved on SIGTERM/SIGINT) or --in-memory,
                     stops cleanly on SIGTERM/SIGINT through a sigwait thread, prints "listening on port N"
  options.{h,c}      command-line parsing
  common/            byte_span.h (pointer + length), flags.h (flags_are_set / flags_are_clear);
                     types.h is an unused leftover
  io/                the I/O table (io->read/write/pread/pwrite/fsync/fdatasync/open/close/flock/fstat) and the
                     real operations; the fault injection lives in tests/io_fault, not here
  ds/                hash_map (chained, entries embed hash_node_t, no locking inside);
                     replacement_policy.h (a general vtable: record access, set evictable, pick
                     victim, over numbered slots) and clock_policy (its first implementation);
                     max_tree (a binary max-tree over bytes in caller-owned memory, padded leaves
                     not stored, max_tree_attach rebuilds the inner nodes of a page read back;
                     the free space map keeps one in each FSM page)
  buffer/            buffer_pool: frames over page files, page_id_t (file number + page number),
                     pin/unpin, mark_changed, flush_all, eviction through a replacement policy that
                     the caller passes to buffer_pool_create, one internal lock
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
    engine_registry    engines by name (`engine_find`, `engine_default`); tables, the catalog file and the data directory dispatch through it
    row_codec          null bitmap and values of a row as bytes, independent of the engine
    page               8 KB slotted page, slot states, CRC-32C checksum, page_validate; page_t is
                       4096-aligned (direct I/O), heap memory for pages from page_array_create
    page_file          read/write/count/allocate pages of a file through io->, checksum checked on
                       read; extra_flags: PAGE_FILE_FLAG_DIRECT (O_DIRECT, EINVAL if refused),
                       PAGE_FILE_FLAG_CREATE (create in the same step as the open)
    heap_tuple         the tuple header of the future heap engine (flags, optional xmin/xmax) and
                       heap_tid_t; nothing uses it yet
    one_page_file   save/load a file that holds exactly one page (one-page engine, catalog file)
    catalog_file    the table list as rows of one page file: a version row, then a row per table and per column; load checks it and calls a visitor per table
    data_dir        the data directory: TIELDB_VERSION file, `catalog`, one `<table>.tbl` per table,
                    `tieldb.lock`; init, lock, load and checkpoint (syncs the directory)
    crc32c             the page checksum
tests/               one test_*.c per module (cmocka), test_wire_buffer_oom.c (allocation
                     failures via renamed malloc/calloc/realloc/aligned_alloc), test_tieldb.c
                     (spawns the binary), psql_session.sh + .expected (real psql, suite "client"),
                     table_assert.h (assert_cell_text for tests that read tables),
                     io_fault.{h,c} (the fault injection: io_init(false), io_inject_fault),
                     direct_io.h (is O_DIRECT available here), fuzz/ (libFuzzer targets and
                     seed corpus), test_random.h (seeded generator for model tests)
docs/                TORN_WRITES.md: measurements and sources on what a kill and a power loss do
                     to a buffered write, by kernel version; FREE_SPACE_MAP.md: how several
                     systems find room for a row, and the design chosen for each TielDB engine;
                     FORMATTING.md; dev-setup.md
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

C23 (`nullptr`, `constexpr`, `[[nodiscard]]`, `static_assert`, `bool`). Functions are at most 20
lines, with nesting depth 2. The cmocka `assert_*` macros count as a nesting level inside loops and
`if` blocks: use `if (...) { fail(); }` or small helpers. A function has at most 4 parameters. There
are no magic numbers in `src/`: name them with `constexpr`. Stateful modules have opaque types.
Every failure sets errno, documented in the header and tested. The build uses `-Werror` with
`-Wconversion` and a strict `-std=c2x`. Some POSIX features need `#define _POSIX_C_SOURCE 200809L`,
as in `catalog.c`. Trust the client: no validation or tests whose only purpose is to survive
malformed or hostile client input. Branch coverage with Clang counts conditions: a loop condition
with `&&` needs a test for each operand being false. `assert_non_null` and `assert_ptr_*` count
heavily against the function-size limit: write `assert_true(x != nullptr)`. `alignas`, `alignof` and
`bool` are keywords in C23 (no `stdalign.h` or `stdbool.h`). Page and I/O tests that need an
allocation failure mock it in `test_wire_buffer_oom.c`; `aligned_alloc` has its own mock
(`oom_aligned_alloc`). Comments, docs and PR text use short sentences: one idea each, about 25
words at most. A statement mixes at most three kinds of operation (cast, ternary, shift or bitwise,
arithmetic, comparison, `&&` or `||`), and a ternary condition with `&&` or `||` is in parentheses.
Before drafting a design document or roadmap item, list the engines (heap, clustered index,
secondary indexes, LSM, one-page) and say what applies to each.

## What comes next

1. **Phase 2, in the order of the ROADMAP.md plan.** The decisions are written there. The catalog is
   a catalog file first, and system tables in milestone B. Flush on shutdown and checkpoint, since
   there is no WAL yet. Locking is a per-table mutex plus the buffer pool's own lock. Rows must fit
   one page until the heap engine. Overflow storage is Phase 8+. **Milestone A is done** (row codec,
   page file, engine interface and registry, one-page engine, catalog file, data directory,
   `--data-dir` / `--in-memory`, save on SIGTERM/SIGINT, `CHECKPOINT`, a lock so that one server
   uses a data directory, directory fsync). **Milestone B so far:** fuzzing build and page fuzz
   target, heap tuple header, page file with page count, allocation and direct I/O, fault injection
   moved to the tests, replacement policy interface and clock, buffer pool (pin/unpin, dirty pages,
   eviction, lock), page alignment and checked arithmetic, and the binary max-tree
   (`src/ds/max_tree`, with `max_tree_attach`). **Next, in order:** the free space map;
   the heap engine core; the engine's metadata in the catalog; the catalog in system tables; the
   crash test. The heap engine core covers insert through the free space map, cursors and TIDs,
   registered next to the one-page engine. The engine's metadata goes in the catalog as an opaque
   blob. Then the data directory creates heap tables, flushes the pool on shutdown and refuses or
   upgrades old one-page directories. This is the first step that psql shows: a table larger than 8
   KB survives a restart. The `--direct-io` server option comes with the heap engine, and the server
   must fail at startup if the file system refuses `O_DIRECT`. Phase 3 also lists memory contexts,
   BOOLEAN, NULL and DEFAULT. A small cleanup: `src/common/types.h` is unused. **Torn writes:** read
   `docs/TORN_WRITES.md` before writing or reviewing a crash test. A `kill -9` can tear a buffered
   write. This happens for 8 KB on tmpfs, on xfs before Linux 6.9 and on ext4 before 6.16, and for
   any write whose length is not a power of two or is above 2 MB on every kernel tried. So the Phase
   2 crash test kills the child only after its flush and sync have finished. The Phase 5 tests
   expect torn pages. A power loss is a separate problem (torn page protection, Phase 5).

   **The free space map, slice by slice.** Read `docs/FREE_SPACE_MAP.md` first. The map belongs to
   the heap engine only. It is a hint that may be wrong and is rebuilt from the heap: one byte per
   heap page (free bytes divided by 32, rounded down to record and up to search), kept in a separate
   file as a max-tree inside each page, through the buffer pool as a second file number. Two levels
   of pages, root at page 0 and leaf pages from 1, so a table can have about 4,070 squared pages
   (128 GiB at 8 KB). A third level would need sparse page files.
   - Slice 2: `PAGE_TYPE_FSM` and `page_body` in `page.{h,c}`, so a page holds the tree bytes after
     its header (a torn page fails its checksum and is rebuilt).
   - Slice 3: the map with one level: the category conversion, then `record` and `find` over the
     buffer pool, for up to about 4,000 heap pages. Write the addressing generically in the number
     of levels.
   - Slice 4: the second level, a format version and level count in the root page, growth of the
     file, and the rebuild when the file is missing or damaged.
   - Separate task in the roadmap: reuse of released pages (a freelist or a bitmap), shared by the
     heap and the indexes. The map does not do it.
2. Phase 3 (already in the roadmap): the real SQL parser and syntax tree first, then expressions,
   `WHERE`, projection, `DELETE`/`UPDATE`/`TRUNCATE`/`INSERT ... SELECT`, `generate_series`,
   `ALTER TABLE ... MODIFY/ADD COLUMN ... AFTER|FIRST`, ORDER BY, aggregates, joins.
3. Later: user-defined types on top of the registry (`CREATE TYPE ... AS ENUM` first), B+trees, WAL,
   MVCC. Some Phase 5 and 6 decisions are already in the roadmap. The WAL gets deltas with a
   checksum per record, and a resource manager ID in every record. Torn page protection is chosen
   when the WAL is designed. A control file is replaced atomically. A recording I/O layer builds
   crash states. Before planning Phase 5 (WAL) and Phase 6 (transactions), read the ROADMAP section
   "A Second Heap Engine: In-Place Updates with Undo Logs" (Phase 8+). It lists decisions (a
   resource manager ID in every WAL record, engine hooks, snapshot arguments) that are cheap now and
   expensive to add later.

## Troubleshooting

- `test_wire_buffer_oom` compiles the net, storage and executor sources with `malloc` renamed to a
  mock. A new `malloc`/`realloc`/`calloc` call in those files therefore needs a matching
  `will_return` in the tests that reach it. A missing one fails the test, and leftover values make
  later tests hang. Run it with `timeout 20` when debugging.

- A build directory made with GCC (or without the native file) fails at configure with a message
  pointing at `clang.ini`: delete it and set it up again.
- `coverage.sh` fails with stale `.gcno` errors after switching branches: `rm -rf build-cov`.
- gcov `NegativeHits` errors come from threaded tests; the coverage build uses
  `-fprofile-update=atomic` (already in `scripts/coverage.sh`).
- The TSan job fails in CI but the suite passed locally: the race depends on timing. Rebuild
  `build-tsan` and repeat the failing test (`meson test -C build-tsan <name> --repeat 20`); the
  report names the two threads and the lines.
- Do not use `pkill -f` with a pattern that also appears in your own command line (it kills the
  shell running it), and look for leftover `tieldb` processes after killing a test run.
- `git fetch` or `git push` fails with `git@github.com: Permission denied (publickey)`: the SSH agent
  that OrbStack forwards from macOS (`SSH_AUTH_SOCK=/opt/orbstack-guest/run/host-ssh-agent.sock`)
  holds no keys (`ssh-add -l` says "The agent has no identities"), for example after a Mac restart or
  sleep. `gh` keeps working because it uses a token over HTTPS. Add the key on the Mac (`ssh-add`, or
  unlock the keychain); do not change the remote URL without asking the user.
- clang-tidy rejects a commit for function size or nesting in tests too: split the helper, as in
  the existing tests. A hook failure means no commit was made, so check `git log` before pushing.
- The page file and buffer pool tests run twice, buffered and with `O_DIRECT`. The direct run is
  skipped (with a message) where the file system refuses `O_DIRECT`. tmpfs and btrfs accept it
  without enforcing alignment, ext2/ext4/xfs reject an unaligned buffer with EINVAL.
- A `page_t` on the heap must come from `page_array_create` (or an `aligned_alloc` of its alignment);
  UBSan reports a misaligned one.
- Valgrind reports "possibly lost" blocks from glibc's cache of exited detached threads: not a leak
  (`scripts/valgrind.sh` only fails on definite and indirect leaks).
- RLIMIT_NPROC test (`test_server`) is skipped when run as root.
- psql prints the version warning only on a terminal; the client test uses `script` for that and
  only when psql is version 18.
