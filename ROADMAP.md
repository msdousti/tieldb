# TielDB Development Roadmap

Incremental, test-driven roadmap for building the educational DBMS. Each phase builds on the previous, with working code and tests at every step.

---

## Phase 0: Project Setup ✅

**Goal:** Scaffold the project, toolchain, and development environment.

### Tasks
- [x] Define coding guidelines (C, clean code, style)
- [x] Define design choices (storage, concurrency, WAL, etc.)
- [x] Set up development environment (Linux VM on macOS)
- [x] Initialize Meson build system
- [x] Configure sanitizers (build-asan, build-tsan, build-release)
- [x] Set up cmocka test framework
- [x] Create initial directory structure
- [x] Write injectable I/O interface (for fault injection)
- [x] First "hello world" test

### Deliverables
- ✅ `meson.build`, `.clang-format`, `.clang-tidy`
- ✅ `src/io/io.{h,c}` - Injectable I/O interface
- ✅ `tests/test_io.c` - First test (5 tests passing)
- ✅ `CLAUDE.md` - Development guide (with `docs/dev-setup.md` for the VM and tool setup)
- ✅ `.github/workflows/ci.yml` - CI/CD pipeline
- ✅ `scripts/pre-commit` - Git pre-commit hooks

### Completion Criteria
- ✅ `meson test -C build-asan` passes (1/1 tests, 5 assertions)
- ✅ `clang-format` and `clang-tidy` run clean
- ✅ Can build and run tests in a Linux VM
- ✅ CI pipeline passing on GitHub Actions

**Estimated Time:** 2-3 days  
**Actual Time:** ~1 day

---

## Phase 1: In-Memory Skeleton + Postgres Wire Protocol ✅

**Goal:** Connect with `psql`, execute hardcoded queries in memory. No persistence yet.

### Tasks
- [x] Implement Postgres wire protocol
  - [x] Output buffer (big-endian writer)
  - [x] Input buffer (bounds-checked reader)
  - [x] Writer length patching and size cap (support for variable-length messages)
  - [x] Startup message (accept every connection, database selection; no authentication)
    - [x] AuthenticationOk response
    - [x] ParameterStatus (`server_version`, `client_encoding`) so psql shows the server version
    - [x] Startup packet decoding
    - [x] Decline `SSLRequest` with `N` (no TLS)
    - [x] Parse startup parameters (user, database), kept in the session; `SELECT current_user` and `SELECT current_database()` report them
  - [x] Simple query protocol (text queries)
    - [x] Decode the Query message and answer `SELECT version()` (hardcoded)
    - [x] Per-connection message loop (Query, Terminate)
  - [x] Response messages: RowDescription, DataRow, CommandComplete, ReadyForQuery
    - [x] ReadyForQuery
    - [x] CommandComplete
    - [x] RowDescription
    - [x] DataRow
    - [x] EmptyQueryResponse, for a query string with no statement (`;`)
  - [x] Error handling (ErrorResponse)
- [x] Parse simple SQL (CREATE TABLE, INSERT, SELECT *)
  - [x] Stopgap hand-written matchers for `SELECT version()`, `SELECT * FROM <table>`, `INSERT INTO <table> VALUES (...)` and `CREATE TABLE <table> (<column> <type>, ...)`, until the real parser replaces them (see Phase 3: SQL parser and syntax tree)
- [x] In-memory catalog
  - [x] Table metadata (columns, types)
  - [x] Column display order, separate from the storage order (starts as the definition order; no SQL to change it yet)
  - [x] Hash map: table name → table definition (a reusable map in `src/ds/`; the catalog keeps it behind a read-write lock)
- [x] In-memory tuple storage
  - [x] Array of tuples per table (growable, one lock per table; rows are text for now)
  - [x] Support `INTEGER` and `TEXT` types only
- [x] Hardcoded executor
  - [x] `CREATE TABLE` → add to catalog
  - [x] `INSERT` → append to tuple array (values checked against the column types)
  - [x] `SELECT *` → iterate and return all rows
- [x] Network server
  - [x] Listen on port (default 5432, like Postgres)
    - [x] Listener on the loopback interface (`server_create`)
    - [x] `tieldb` binary with `--port` and `--once`
  - [x] Accept connections
  - [x] Per-connection thread (or event loop)

### Tests
- [x] Unit tests for wire protocol parsing
- [x] Unit tests for SQL parsing (CREATE, INSERT, SELECT)
- [x] Integration test: connect with `psql`, run queries
- Fuzz test for wire protocol messages: dropped, since TielDB trusts well-behaved clients and does not defend against malformed messages. The SQL parser is fuzzed in Phase 3

### Deliverables
- `src/net/wire_buffer.{h,c}` - Big-endian writer and bounds-checked reader
- `src/net/protocol.{h,c}` - Postgres wire protocol (backend message encoders)
- `src/net/startup.{h,c}`, `src/net/session.h` - Startup packet handling and session info
- `src/net/query_handler.{h,c}`, `src/net/connection.{h,c}` - Query messages and the per-connection loop
- `src/options.{h,c}` - Command-line options
- `src/ds/hash_map.{h,c}` - Reusable hash map
- `src/storage/table.{h,c}` - In-memory table
- `src/sql/simple_sql.{h,c}` - Stopgap SQL matchers (the real parser is a Phase 3 task: `src/sql/parser.{h,c}`)
- `src/storage/catalog.{h,c}` - In-memory catalog, on top of `src/ds/hash_map.{h,c}`
- `src/executor/executor.{h,c}` - Hardcoded executor
- `src/net/server.{h,c}` - Network server

### Completion Criteria
```bash
# Start the server
./build-asan/tieldb

# In another terminal
psql -h localhost -U test testdb

testdb=> CREATE TABLE users (id INTEGER, name TEXT);
CREATE TABLE
testdb=> INSERT INTO users VALUES (1, 'Alice');
INSERT 0 1
testdb=> SELECT * FROM users;
 id | name
----+-------
  1 | Alice
(1 row)
```

**Estimated Time:** 1-2 weeks

---

## Phase 2: Persistence (Buffer Pool, Heap Files, Catalog on Disk)

**Goal:** Restart the server and your data survives.

Two milestones, so that the first visible result comes early:
- **A. Data survives a restart, with a simplifying assumption:** a table is one 8 KB page, saved to a file at shutdown and loaded at startup. A table holds a few hundred small rows.
- **B. Lift the limit:** heap files with many pages, the buffer pool and the free space map replace the one-page engine behind the same storage engine interface, and the catalog moves into system tables.

**Decisions:**
- The storage engine interface (rows and scan cursors, no pages or TIDs) exists from milestone A. The one-page engine is its first implementation and the heap engine of milestone B replaces it, so the executor does not change. The row codec, the page format, the page file, the data directory and the shutdown handling carry over unchanged.
- Everything on disk is versioned: the data directory has a version file and the catalog has a format version, so a later format is refused or upgraded and never misread.
- Pages are checked when they are read: a CRC-32C checksum and a validity check of the header and slots.
- Durability before the WAL (Phase 5): dirty pages are flushed on clean shutdown and on an explicit checkpoint. After `kill -9` only data flushed before the kill is guaranteed.
- Locking: the per-table mutex stays; the buffer pool has its own internal lock. Page latches and MVCC wait for Phase 6.
- Milestone A keeps the catalog in a page file of its own, in the same row format. Milestone B moves it into real system tables (pg_class-like) with a bootstrap on first run.
- New modules: `src/types`, `src/storage` (page, page file, row codec, engines, catalog, and in B heap file, heap tuple and free space map) and `src/buffer`.
- Each step below is one small PR (about 100-300 lines), in the order of the lists.

### Tasks

#### Milestone A: data survives a restart
- [x] Type registry (like Postgres's `pg_type`), so types are data and not special cases in the code
  - [x] Registry module: name, OID, text input and output functions, storage size (fixed or variable length) and comparison function; `INTEGER` and `TEXT` are its first entries
  - [x] The executor and the wire protocol layer look types up through it; this replaces the type-name table in the executor and the type-to-OID mapping in the query handler
  - [x] Values are stored in their type's own format (an integer as 4 bytes), converted from and to text only at the edges; this replaces the `column_type_t` enum and fixes integers being stored as written (`007`)
- [ ] Page abstraction
  - [x] 8 KB pages
  - [ ] Page header (LSN, checksum, page type, free space): the fields and the page type are in place; the checksum (CRC-32C) is computed and the LSN set when pages are written (page file, WAL)
  - [x] Slotted page layout (slot array + tuples)
  - [x] Page-level operations (insert, delete, get tuple)
- [ ] Row codec, independent of the storage engine (a future LSM engine reuses it)
  - [x] Null bitmap (a NULL is a set bit and takes no space; SQL support for NULL comes in Phase 3), then the values of the columns in the stored format of their type, encoded and decoded through the type registry
  - [x] Variable-length data (length prefix for TEXT), with a spare bit in the prefix reserved for the overflow pointer of Phase 8+
  - [ ] A row that does not fit in one page is an error (errno `EFBIG`) that the SQL layer reports; no overflow storage yet (the codec already refuses a value over 32767 bytes with `EFBIG`; the page-sized limit comes with the engine)
- [x] Page file: read and write one page of a file, all I/O through `io->`
  - [x] CRC-32C, and `page_set_checksum` / `page_verify_checksum` for the page header
  - [x] `page_validate`: header and slot sanity checks on every page read from disk, so that later page code can trust a page
  - [x] Read and write pages of a file: the checksum is set on write, and verified and validated on read
- [ ] Storage engine interface (the vtable from `CLAUDE.md`), so that an LSM engine can be added in Phase 8+, and the one-page engine
  - [ ] Operations on rows and cursors, not on pages or TIDs: insert, and a scan cursor (open, next, close)
  - [ ] Separate the catalog entry (name, columns, engine, engine metadata) from the storage of the rows; `table_t` is both today
  - [ ] One-page engine: the rows of a table are in one page of encoded rows; an INSERT into a full page is an error ("table is full"); save and load the whole page through the page file
  - [ ] The executor uses the engine for INSERT and SELECT
- [ ] Catalog file
  - [ ] Save and load the tables: name, engine, each column's name, type id and display position (so a reordering survives a restart), in a page file of its own with a format version
  - [ ] Store the engine of each table and its engine-specific metadata (an opaque blob to the catalog)
- [ ] Data directory and shutdown
  - [ ] `--data-dir` option; load the catalog and the tables at startup, save them on clean shutdown and on an explicit checkpoint
  - [ ] Data directory format version: a file with a magic number and a format version (like Postgres's `PG_VERSION`), checked at startup so that an old or foreign directory is refused and not misread
  - [ ] One server per data directory: a lock on the directory (a pid file created with `O_EXCL`, as Postgres does, or an advisory lock that the kernel drops when the process dies), so that a second server on the same directory refuses to start and cannot overwrite the first one's pages; it needs a stale-lock check or a lock call in the injectable I/O layer
  - [ ] Directory durability: fsync the data directory after creating a file in it (the injectable I/O layer has no way to open or sync a directory yet)
  - [ ] Clean shutdown: SIGTERM and SIGINT stop the listener, close the connections, save the data and exit with status 0 (today only SIGPIPE is handled)

#### Milestone B: lift the one-page limit
- [ ] Fuzzing build (its own small PR, before the first fuzz target; `CLAUDE.md` documents `build-fuzz`, but nothing builds it yet)
  - [ ] Meson option that builds the `tests/fuzz/` targets with libFuzzer and ASan
  - [ ] CI job that builds them and runs each for a short time on a seed corpus
  - [ ] Page deserialization fuzz target
- [ ] Heap tuple header, specific to the heap engine
  - [ ] Header in front of the encoded row: flags, optional xmin/xmax
  - [ ] Tuple ID (TID): page + slot; it stays inside the heap engine and is not exposed to the executor
- [ ] Heap file management
  - [ ] Create/open/close heap files with many pages, all I/O through `io->`
  - [ ] Allocate new pages
- [ ] Buffer pool
  - [ ] Fixed-size pool (e.g., 1000 pages)
  - [ ] Pin/unpin interface
  - [ ] Clock replacement policy
  - [ ] Dirty page tracking
  - [ ] Page ID (file number + page number)
  - [ ] Generic over files and page numbers, with no heap assumptions: an LSM engine keeps its own file formats and block cache
  - [ ] Zero outstanding pins asserted at teardown
  - [ ] Optional direct I/O (`O_DIRECT`, off by default), so that a page is not cached twice: the page buffers are 4096-aligned (`alignas` on `page_t`); the tests run with it on and off, and fall back when the filesystem refuses it (`tmpfs` does); `fsync` is still needed for durability
- [ ] Free space map (bitmap or simple list)
- [ ] Heap engine
  - [ ] Implements the storage engine interface and replaces the one-page engine: INSERT finds a page with space, adds a tuple and marks the page dirty; SELECT pins pages, iterates tuples and unpins
  - [ ] An old data directory with one-page tables is upgraded or refused through the format version
- [ ] Catalog in system tables
  - [ ] Special system tables (pg_class-like), stored with the heap engine
  - [ ] Bootstrap: create the catalog tables on first run
  - [ ] Replaces the catalog file of milestone A

### Tests
- [x] **Reference-model test for slotted page**
  - [x] Random insert/delete operations
  - [x] Compare against simple array model
  - [x] Log RNG seed for reproducibility
- [x] Unit tests for the type registry and the row codec round trip
- [x] Page file: round trip, a flipped bit is caught by the checksum, a corrupt header or slot is caught by `page_validate`
- [ ] Restart test: create a table and insert rows, stop the server with SIGTERM, start it again, the rows are there
- [ ] Shutdown test: SIGTERM while connected saves the data and exits with status 0
- [ ] Fault injection for page file, heap file and buffer pool I/O (short write, fsync failure) through the injectable `io`
  - [x] Page file: write error, torn write, fsync failure, read error
  - [ ] Heap file and buffer pool (milestone B)
- [ ] Unit tests for buffer pool (pin/unpin, eviction)
- [ ] Crash test: write data, flush, kill -9, restart, verify the flushed data survives
- [ ] Fuzz test for page deserialization (milestone B)

### Deliverables
- `src/types/type_registry.{h,c}` - Type registry
- `src/storage/page.{h,c}` - Slotted page implementation
- `src/storage/row_codec.{h,c}` - Row codec (engine-independent)
- `src/storage/page_file.{h,c}` - Reading and writing pages of a file
- `src/storage/storage_engine.h` - Storage engine interface; the one-page engine and later the heap engine implement it
- `src/storage/catalog.{h,c}` - Catalog (updated: save and load)
- `src/storage/heap_tuple.{h,c}`, `src/storage/heap_file.{h,c}`, `src/storage/free_space_map.{h,c}` - Heap engine parts (milestone B)
- `src/buffer/buffer_pool.{h,c}` - Buffer pool (milestone B)
- `tests/fuzz/` - Fuzz targets (milestone B)

### Completion Criteria
```bash
# Insert data
psql -h localhost -p 5432 -c "INSERT INTO users VALUES (2, 'Bob');"

# Restart the server (Ctrl-C, then start it again with the same --data-dir)
./build-asan/tieldb --data-dir /tmp/tieldb-data

# Data still there
psql -h localhost -p 5432 -c "SELECT * FROM users;"
 id | name
----+------
  1 | Alice
  2 | Bob
(2 rows)
```

This works at the end of milestone A (a table is one page). At the end of milestone B it also works after inserting more rows than fit in one page.

**Estimated Time:** 2-3 weeks

---

## Phase 3: Real Queries (Expressions, WHERE, ORDER BY, Aggregates, Joins)

**Goal:** Move from `SELECT *` to real SQL query execution.

### Tasks
- [ ] SQL parser and syntax tree (first, because expressions and WHERE need a tree)
  - [ ] Tokenizer: keywords, quoted identifiers, numbers, strings, comments (`--`, `/* */`)
  - [ ] Recursive-descent parser building an AST; replaces the stopgap matchers in `sql/simple_sql`
  - [ ] Several statements in one query string (`select 1; select 2`)
  - [ ] Syntax errors with SQLSTATE `42601` and an error position, as in Postgres
  - [ ] Quick wins once it exists: `INSERT INTO t (a, b) VALUES ...` with a column list, multi-row `VALUES (1, 'a'), (2, 'b')`, `CREATE TABLE IF NOT EXISTS`, `DROP TABLE [IF EXISTS]`
  - [ ] Fuzz test for the parser (random text must never crash it)
- [ ] Memory contexts (arena allocator, the rule in `CLAUDE.md`; before the operators, which allocate a lot per query)
  - [ ] A per-query arena that is freed in one call at the end of the query; assert zero live arenas at teardown
  - [ ] Move the per-row and per-statement `malloc` calls of the query handler and the executor to it
- [ ] BOOLEAN type (a registry entry; the result type of comparisons and logical operators; `true` and `false` literals)
- [ ] NULL support
  - [ ] `NULL` literal, nullable columns (using the null bitmap of the row codec), `IS [NOT] NULL`, `NOT NULL` constraint
  - [ ] Three-valued logic in comparisons and `AND`, `OR`, `NOT`; a `WHERE` keeps a row only if the condition is true
  - [ ] Aggregates ignore NULL (`COUNT(*)` does not); `ORDER BY` puts NULL last
- [ ] `DEFAULT` values for columns
- [ ] Error codes: the SQLSTATEs the server uses, in one header and consistent with Postgres (`42601`, `42P01`, `42P07`, `42701`, `42704`, `22P02`, `0A000` so far; `23505` for unique violations in Phase 4; `40001` and `40P01` in Phase 6)
- [ ] Expression evaluation
  - [ ] Arithmetic (`+`, `-`, `*`, `/`, `%`)
  - [ ] Comparison (`=`, `!=`, `<`, `>`, `<=`, `>=`)
  - [ ] Logical (`AND`, `OR`, `NOT`)
  - [ ] `IN`, `BETWEEN`, `LIKE`
  - [ ] Type casting
- [ ] WHERE clause
  - [ ] Filter tuples during scan
  - [ ] Conjunctive normal form (CNF) for optimizations later
- [ ] Projection
  - [ ] Select specific columns: `SELECT id, name FROM users`
  - [ ] Computed columns: `SELECT id * 2 AS double_id FROM users`
- [ ] Changing data (after WHERE; with MVCC in Phase 6 these mark row versions instead of removing them)
  - [ ] `DELETE FROM t [WHERE ...]`
  - [ ] `UPDATE t SET col = expr [, ...] [WHERE ...]`
  - [ ] `TRUNCATE t`
  - [ ] `INSERT INTO t SELECT ...` (after projection)
- [ ] ORDER BY (external sort)
  - [ ] In-memory sort for small tables
  - [ ] External merge sort for large tables (spill to disk)
- [ ] `SELECT DISTINCT`
- [ ] LIMIT and OFFSET
- [ ] Aggregates
  - [ ] COUNT, SUM, AVG, MIN, MAX
  - [ ] GROUP BY (hash aggregation)
  - [ ] HAVING clause
- [ ] Joins
  - [ ] Nested-loop join (simple, correct, slow)
  - [ ] Hash join (faster for equi-joins)
  - [ ] Cartesian product (CROSS JOIN)
- [ ] Column reordering: `ALTER TABLE ... MODIFY ... AFTER ...` (MariaDB syntax; Postgres cannot do this)
  - [ ] Parse `ALTER TABLE <t> MODIFY <column> <type> AFTER <other column>` and `... FIRST`
  - [ ] Execute it by changing the column's display position (`table_set_display_order`); the stored rows are not touched. The display order already exists in the catalog; `SELECT *` and `INSERT ... VALUES` follow it
  - [ ] Only the column's current type is accepted at first; changing the type needs data conversion and is a separate step
- [ ] Adding columns: `ALTER TABLE <t> ADD COLUMN <column> <type> [AFTER <other column> | FIRST]` (MariaDB syntax)
  - [ ] Add the column at the end of the stored row, and give it the requested display position, so existing rows are not rewritten
  - [ ] Existing rows get the `DEFAULT` of the new column, or NULL (see the NULL and `DEFAULT` tasks above)
- [ ] `generate_series(start, stop [, step])` as a function in FROM, for test and benchmark data
  - [ ] `SELECT * FROM generate_series(1, 100000)` as a function-scan operator, no table needed
  - [ ] Then fill tables with `INSERT INTO t SELECT ... FROM generate_series(...)`; large inputs for ORDER BY, aggregate, join and (Phase 2) buffer pool tests
- [ ] Volcano iterator model
  - [ ] SeqScan, Filter, Project, Sort, Limit, HashAgg, NestedLoopJoin, HashJoin operators
  - [ ] `open()`, `next()`, `close()` interface

### Tests
- [ ] Unit tests for expression evaluation
- [ ] Integration tests for each query type
  - [ ] WHERE with complex predicates
  - [ ] ORDER BY with multiple columns, ASC/DESC
  - [ ] Aggregates with and without GROUP BY
  - [ ] Joins on multiple tables
- [ ] Reference-model test for join correctness (compare with naive nested loops)

### Deliverables
- `src/executor/expression.{h,c}` - Expression evaluator
- `src/executor/operators/` - Operator implementations (scan, filter, project, sort, join, agg)
- `src/executor/executor.{h,c}` - Updated executor with operator tree

### Completion Criteria
```sql
-- Complex query works
SELECT u.name, o.amount
FROM users u JOIN orders o ON u.id = o.user_id
WHERE o.amount > 100
ORDER BY o.amount DESC
LIMIT 10;

-- Aggregates work
SELECT status, COUNT(*), AVG(amount)
FROM orders
GROUP BY status
HAVING COUNT(*) > 5;
```

**Estimated Time:** 3-4 weeks

---

## Phase 4: B+Tree Indexes

**Goal:** Add indexes for fast lookups. Query planner uses them.

### Tasks
- [ ] B+tree implementation
  - [ ] Page-based (internal and leaf pages)
  - [ ] Insert with splits (recursive)
  - [ ] Point lookup
  - [ ] Range scan (iterator interface)
  - [ ] Delete with merges (optional, can defer)
- [ ] Index metadata
  - [ ] Catalog entry (pg_index-like)
  - [ ] Index-to-table mapping
  - [ ] Unique vs. non-unique
  - [ ] Composite keys (several columns, compared one after the other with the type registry's compare functions)
- [ ] Index maintenance
  - [ ] INSERT adds the key with the row's TID; a unique index rejects a duplicate (SQLSTATE `23505`)
  - [ ] DELETE and UPDATE remove or change the keys
  - [ ] CREATE INDEX builds the index in bulk from the rows that already exist
  - [ ] A dead heap slot is reclaimed (`page_reclaim_dead_slots`) only after the index entries that point to it are gone
- [ ] CREATE INDEX, DROP INDEX
- [ ] Index scan operator
  - [ ] Volcano interface
  - [ ] Push down predicates (index key conditions)
- [ ] Simple planner rule
  - [ ] If WHERE has indexed column, use index scan
  - [ ] Otherwise, use sequential scan
  - [ ] No cost-based yet (just heuristics)

### Tests
- [ ] **Reference-model test for B+tree**
  - [ ] Random insert/delete/lookup
  - [ ] Compare against sorted array or std::map
  - [ ] Log RNG seed
- [ ] Unit tests for splits, merges (if implemented)
- [ ] Integration test: create index, verify query uses it
- [ ] Fuzz test for B+tree node deserialization

### Deliverables
- `src/index/btree.{h,c}` - B+tree implementation
- `src/executor/operators/index_scan.{h,c}` - Index scan operator
- `src/planner/planner.{h,c}` - Simple rule-based planner

### Completion Criteria
```sql
-- Create index
CREATE INDEX idx_user_id ON users(id);

-- Query uses index (verify with EXPLAIN later)
SELECT * FROM users WHERE id = 42;

-- Range scan works
SELECT * FROM users WHERE id >= 10 AND id < 100;
```

**Estimated Time:** 2-3 weeks

---

## Phase 5: Durability (WAL, Recovery, Crash Testing)

**Goal:** Survive crashes without data loss. **Most educational phase.**

### Tasks
- [ ] Write-Ahead Log (WAL)
  - [ ] Statement-level autocommit: until Phase 6 every statement is its own transaction, and "commit" below means the end of the statement
  - [ ] WAL records (redo-only first: INSERT, UPDATE, DELETE), also for B+tree page splits and for catalog changes (CREATE/DROP TABLE and INDEX), so that recovery leaves the indexes and the catalog consistent
  - [ ] WAL segment files of a fixed size, recycled after a checkpoint
  - [ ] LSN (log sequence number) per page
  - [ ] WAL buffers + background writer
  - [ ] fsync on commit (force-log-at-commit)
- [ ] Logging discipline
  - [ ] Log record written before page modified
  - [ ] LSN stamped on page
  - [ ] No-steal policy (dirty pages not evicted before commit) initially
- [ ] Recovery (redo-only)
  - [ ] Read WAL from last checkpoint
  - [ ] Replay log records
  - [ ] Verify checksums
- [ ] Checkpointing
  - [ ] Fuzzy checkpoint (write dirty pages, record checkpoint LSN)
  - [ ] Checkpoint record in WAL
- [ ] fsync discipline
  - [ ] Treat fsync failure as PANIC (see c-guidelines.md)
  - [ ] Fault injection test for fsync failure
- [ ] (Later) ARIES-style undo
  - [ ] Undo records (CLRs)
  - [ ] Analysis, Redo, Undo phases
  - [ ] Steal/no-force buffer policy

### Tests
- [ ] **Crash test with fork() + kill -9**
  - [ ] Insert data in child process
  - [ ] Kill child at random point
  - [ ] Parent recovers, checks invariants
  - [ ] Repeat 1000 times with different seeds
- [ ] Fault injection for torn writes, dropped fsyncs
- [ ] Unit tests for WAL record serialization
- [ ] Recovery test: write WAL, simulate crash, verify replay

### Deliverables
- `src/wal/wal.{h,c}` - WAL writer and reader
- `src/recovery/recovery.{h,c}` - Recovery logic
- `src/storage/checkpoint.{h,c}` - Checkpointing

### Completion Criteria
```bash
# Insert data
psql -c "BEGIN; INSERT INTO users VALUES (100, 'Test'); COMMIT;"

# Kill server hard (kill -9)
kill -9 $(pgrep tieldb)

# Restart
./build-asan/tieldb

# Data is there
psql -c "SELECT * FROM users WHERE id = 100;"
 id | name
-----+------
 100 | Test
(1 row)
```

**Estimated Time:** 3-4 weeks (most complex phase)

---

## Phase 6: Transactions (Concurrency Control)

**Goal:** Multiple concurrent transactions with isolation guarantees.

### Tasks
- [ ] Transaction manager
  - [ ] BEGIN, COMMIT, ROLLBACK
  - [ ] ReadyForQuery reports the transaction state (`I` idle, `T` in a transaction, `E` failed; the encoder already supports the three); after an error inside a transaction block, commands are rejected until ROLLBACK
  - [ ] Transaction ID (XID) assignment
  - [ ] Active transaction list
- [ ] Strict 2PL (Two-Phase Locking)
  - [ ] Lock manager (hash table of locks)
  - [ ] Row-level locks (shared, exclusive)
  - [ ] Intention locks on tables (IS, IX)
  - [ ] Deadlock detection (wait-for graph, cycle detection)
  - [ ] Lock timeout
- [ ] Isolation level: Read Committed
  - [ ] Locks held until transaction end
  - [ ] No dirty reads
- [ ] (Later) MVCC (Multi-Version Concurrency Control)
  - [ ] xmin/xmax in tuple headers (PostgreSQL-style)
  - [ ] DELETE and UPDATE mark row versions dead and write new ones; vacuum removes the dead versions
  - [ ] Snapshot isolation
  - [ ] Visibility rules (tuple visible to transaction?)
  - [ ] No read locks (higher concurrency)
- [ ] Vacuum
  - [ ] Manual vacuum command
  - [ ] Mark dead tuples as reusable
  - [ ] Update free space map

### Tests
- [ ] Unit tests for lock manager
- [ ] Deadlock detection test (two transactions, cyclic wait)
- [ ] Concurrency test: multiple threads, random transactions
- [ ] TSan build: run all tests under TSan to catch data races
- [ ] Isolation test: verify Read Committed semantics

### Deliverables
- `src/txn/txn_manager.{h,c}` - Transaction manager
- `src/concurrency/lock_manager.{h,c}` - Lock manager
- `src/storage/vacuum.{h,c}` - Vacuum (for MVCC later)

### Completion Criteria
```sql
-- Transaction 1
BEGIN;
UPDATE users SET name = 'Alice2' WHERE id = 1;
-- (pause)

-- Transaction 2 (blocks until T1 commits/aborts)
BEGIN;
UPDATE users SET name = 'Alice3' WHERE id = 1;  -- waits
-- T1 commits
COMMIT;  -- now T2 proceeds
COMMIT;

-- Deadlock detection works
-- T1: locks A, tries B
-- T2: locks B, tries A
-- One transaction is aborted
```

**Estimated Time:** 2-3 weeks

---

## Phase 7: Query Planner and Optimizer

**Goal:** Cost-based query optimization with statistics.

### Tasks
- [ ] Statistics collection
  - [ ] ANALYZE command
  - [ ] Row count, distinct values, null count
  - [ ] Histograms (optional, can use simple approach first)
  - [ ] Most common values (MCVs)
- [ ] Cardinality estimation
  - [ ] Selectivity of predicates
  - [ ] Join cardinality
- [ ] Cost model
  - [ ] Cost of sequential scan, index scan, join
  - [ ] Disk I/O cost vs. CPU cost
- [ ] Join ordering
  - [ ] Dynamic programming for optimal order
  - [ ] Left-deep trees only (for simplicity)
- [ ] Plan representation
  - [ ] Physical plan tree
  - [ ] Annotated with costs
- [ ] EXPLAIN command
  - [ ] Print query plan
  - [ ] Show estimated costs and row counts

### Tests
- [ ] Unit tests for cardinality estimation
- [ ] Integration test: verify planner chooses correct plan
  - [ ] Index scan when selective
  - [ ] Sequential scan when not
  - [ ] Hash join vs. nested loop based on table sizes
- [ ] Compare estimated vs. actual row counts

### Deliverables
- `src/planner/optimizer.{h,c}` - Cost-based optimizer
- `src/statistics/stats.{h,c}` - Statistics collection and storage
- `src/executor/explain.{h,c}` - EXPLAIN command

### Completion Criteria
```sql
-- Collect stats
ANALYZE users;

-- Show plan
EXPLAIN SELECT * FROM users WHERE id = 42;

QUERY PLAN
-------------------------------------------------------------
Index Scan using idx_user_id on users  (cost=0.00..8.27 rows=1)
  Index Cond: (id = 42)

-- Complex join plan
EXPLAIN SELECT * FROM users u JOIN orders o ON u.id = o.user_id;

QUERY PLAN
-------------------------------------------------------------
Hash Join  (cost=24.50..89.75 rows=1000)
  Hash Cond: (o.user_id = u.id)
  -> Seq Scan on orders o  (cost=0.00..20.00 rows=1000)
  -> Hash  (cost=15.00..15.00 rows=500)
        -> Seq Scan on users u  (cost=0.00..15.00 rows=500)
```

**Estimated Time:** 2-3 weeks

---

## Phase 8+: Extended Features (Optional, Pick What Interests You)

### Extended Query Protocol (Prepared Statements)
psql needs only the simple protocol. JDBC, psycopg and most drivers use this one for parameterized queries, so move it earlier if you want to use such a driver.
- [ ] Parse/Bind/Execute messages
- [ ] Parameter binding ($1, $2, ...)
- [ ] Plan caching

### Copy Protocol and LISTEN/NOTIFY
- [ ] `COPY ... FROM STDIN` and `COPY ... TO STDOUT` (bulk load and dump)
- [ ] `LISTEN` and `NOTIFY`

### psql Meta-Commands
- [ ] Minimal `pg_catalog` views (`pg_class`, `pg_attribute`, `pg_namespace`, `pg_type`) over the persistent catalog, so that `\dt` and `\d` work

### Benchmarks
- [ ] A small benchmark program on `build-release`: bulk insert, scans, index lookups, using `generate_series` data
- [ ] Compare buffered and direct I/O with the buffer pool (the optional `O_DIRECT` mode of Phase 2)

### More Data Types
- [ ] FLOAT, DOUBLE
- [ ] DECIMAL (arbitrary precision)
- [ ] DATE, TIMESTAMP, INTERVAL
- [ ] BYTEA (binary data)
- [ ] Arrays
- [ ] User-defined types (UDTs), on top of the type registry from Phase 2
  - [ ] Once types can be created at runtime, make the type registry mutable and locked, and look types up through two hash maps (by name and by OID) instead of the linear scan; the `type_find_by_*` API stays the same
  - [ ] `CREATE TYPE ... AS ENUM`, as the first one: simple, and visible in `psql`
  - [ ] Composite types: `CREATE TYPE ... AS (a int, b text)`
  - [ ] Domains: a base type plus a constraint
  - [ ] Extra base types registered at compile time. Loading user-written shared libraries (what Postgres extensions do) is out of scope

### Overflow Storage for Large Values (like Postgres TOAST)
- [ ] A flag in the variable-length prefix marks a value stored out of line; the tuple keeps a pointer
- [ ] Side heap file per table holding the value in chunks (about 2 KB each), found by value id and chunk number
- [ ] Fetch the chunks when the value is read; delete them when the row is deleted
- [ ] Remove the one-page row limit of Phase 2
- [ ] Optional: compress large values before moving them out of line

### Checkpointing Improvements
- [ ] Incremental checkpointing
- [ ] Background writer for dirty pages

### B+tree Concurrency
- [ ] Latch crabbing (lock coupling)
- [ ] Concurrent inserts/deletes/lookups
- [ ] Test with TSan

### LSM Tree Storage Engine
- [ ] MemTable (in-memory write buffer)
- [ ] SSTables (sorted string tables on disk)
- [ ] Compaction (levels, merging)
- [ ] Bloom filters for fast negative lookups
- [ ] Coexists with heap storage via storage engine API

### More Index Types
- [ ] Hash indexes (equality only, faster)
- [ ] GiST (Generalized Search Tree) framework

### Vectorized Execution
- [ ] Process tuples in batches (e.g., 1024 at a time)
- [ ] Better CPU cache utilization
- [ ] SIMD optimizations (optional)

### Replication
- [ ] Logical replication (decode WAL to SQL-like ops)
- [ ] Streaming replication (ship WAL to standby)

### Better SQL Support
- [ ] Subqueries (correlated and uncorrelated)
- [ ] CTEs (Common Table Expressions)
- [ ] Window functions
- [ ] Triggers
- [ ] Stored procedures (PL/pgSQL-like)

### Monitoring and Introspection
- [ ] pg_stat tables (query stats, buffer pool stats)
- [ ] Query logging
- [ ] Slow query log

---

## Practical Advice

### Testing Strategy

1. **Reference-model tests are the most valuable**
   - Write them for every data structure: slotted page, B+tree, buffer pool
   - Run random operations against both real and trivial model
   - Compare results after every step
   - A few hundred lines of C give you property-based testing
   - Log the RNG seed so failures are reproducible

2. **Crash tests for durability**
   - `fork()`, run workload in child, `kill -9` at random point
   - Parent recovers and checks invariants
   - Run 1000 times with different timings
   - This is the main test for WAL

3. **Fuzz tests for parsers and deserializers**
   - libFuzzer on page deserializer, SQL parser, WAL replay
   - Run in `build-fuzz` with ASan
   - Let it run overnight to find edge cases

4. **Run everything under ASan + UBSan by default**
   - `build-asan` is the default dev build
   - Catches memory errors, undefined behavior
   - ~2x slowdown (Valgrind is 20-50x)

5. **TSan for concurrency phases**
   - Separate `build-tsan` directory
   - Run all tests to catch data races
   - Cannot combine with ASan, so use separately

### Parsing

TielDB writes its own parser: stopgap matchers in Phase 1 and 2, then the hand-written recursive-descent parser of Phase 3. Do not pull in a parser library. **libpg_query** (the Postgres parser as a C library) is useful only as an oracle in tests: parse the same text with both and compare.

### Resources

**Courses:**
- **CMU 15-445/645** (Intro to Database Systems)
  - BusTub project: phases 2-6 map directly to it
  - Lectures cover theory, projects teach practice
  - [https://15445.courses.cs.cmu.edu/](https://15445.courses.cs.cmu.edu/)

**Books:**
- **Database Internals** by Alex Petrov (O'Reilly)
  - Storage engines, B+trees, LSM trees, transaction processing
  - Modern, well-written, essential
- **Database Management Systems** by Ramakrishnan & Gehrke
  - Classic textbook, comprehensive
  - Query processing, optimization, concurrency, recovery
- **Transaction Processing** by Gray & Reuter
  - Deep dive on ACID, logging, recovery
  - Dense but authoritative

**Papers:**
- **ARIES** (Algorithm for Recovery and Isolation Exploiting Semantics)
  - The recovery algorithm (Phase 5)
  - [https://cs.stanford.edu/people/chrismre/cs345/rl/aries.pdf](https://cs.stanford.edu/people/chrismre/cs345/rl/aries.pdf)
- **Architecture of a Database System** by Hellerstein et al.
  - Big picture: how all components fit together
  - [https://dsf.berkeley.edu/papers/fntdb07-architecture.pdf](https://dsf.berkeley.edu/papers/fntdb07-architecture.pdf)

**Source Code:**
- **PostgreSQL** (C)
  - `src/backend/access/heap/heapam.c` - Heap access method
  - `src/backend/access/nbtree/nbtree.c` - B+tree index
  - `src/backend/access/transam/xlog.c` - WAL
  - `src/backend/storage/buffer/bufmgr.c` - Buffer manager
  - [https://github.com/postgres/postgres](https://github.com/postgres/postgres)
- **SQLite** (C)
  - Single-file, well-commented, excellent to read
  - B-tree, pager, VDBE (virtual machine)
  - [https://sqlite.org/src/doc/trunk/README.md](https://sqlite.org/src/doc/trunk/README.md)

### Debugging Tips

1. **Print LSNs everywhere** during Phase 5 (WAL). Understanding log sequence is critical.
2. **Use `gdb` to inspect page layouts.** Print pages as hex dumps and decode by hand at first.
3. **Log buffer pool state** (pin counts, dirty bits) to catch leaks.
4. **Assertions are your friend.** Add them liberally. They're free in dev builds.
5. **Test single-threaded first,** then add concurrency (Phase 6). It's easier to debug.

### When You Get Stuck

1. **Read Postgres code** for the corresponding component.
2. **Ask specific questions** in database forums (Reddit r/databasedevelopment, Stack Overflow).
3. **Simplify.** If B+tree splits are confusing, make the tree order smaller (M=4) and draw it on paper.
4. **Take a break.** Step away, come back fresh. Database systems are complex; progress is incremental.

---

## Summary Timeline (Rough Estimates)

| Phase | Topic | Time |
|-------|-------|------|
| 0 | Project setup | 2-3 days |
| 1 | In-memory + wire protocol | 1-2 weeks |
| 2 | Persistence (buffer pool, heap files) | 2-3 weeks |
| 3 | Real queries (expressions, joins, aggregates) | 3-4 weeks |
| 4 | B+tree indexes | 2-3 weeks |
| 5 | Durability (WAL, recovery) | 3-4 weeks |
| 6 | Transactions (2PL, MVCC) | 2-3 weeks |
| 7 | Query optimizer | 2-3 weeks |
| **Total** | **~4-6 months part-time** | |

**Note:** These are estimates for working part-time (10-20 hours/week). Full-time work could compress this significantly. The key is **incremental progress**: working code and tests at each step.

---

## Principles

1. **Work incrementally.** Each phase produces a working system.
2. **Test everything.** Reference models, crash tests, fuzzing, sanitizers.
3. **Connect with `psql` early** (Phase 1). Instant feedback keeps you motivated.
4. **Read real code** (Postgres, SQLite) when stuck. Don't guess.
5. **Durability is the hardest part** (Phase 5). Give it time.
6. **Extensibility from the start.** Use abstraction layers (storage engine API, lock manager API) so you can swap implementations later.
