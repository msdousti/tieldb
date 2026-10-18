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
- ✅ `CLAUDE.md` - Development guide (replaces docs/dev-setup.md)
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

## Phase 1: In-Memory Skeleton + Postgres Wire Protocol

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
- `src/net/protocol.{h,c}` - Postgres wire protocol
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

testdb=> CREATE TABLE user (id INTEGER, name TEXT);
CREATE TABLE
testdb=> INSERT INTO user VALUES (1, 'Alice');
INSERT 0 1
testdb=> SELECT * FROM user;
 id | name
----+-------
  1 | Alice
(1 row)
```

**Estimated Time:** 1-2 weeks

---

## Phase 2: Persistence (Buffer Pool, Heap Files, Catalog on Disk)

**Goal:** Restart the server and your data survives.

### Tasks
- [ ] Page abstraction
  - [ ] 8 KB pages
  - [ ] Page header (LSN, checksum, page type, free space)
  - [ ] Slotted page layout (slot array + tuples)
  - [ ] Page-level operations (insert, delete, get tuple)
- [ ] Buffer pool
  - [ ] Fixed-size pool (e.g., 1000 pages)
  - [ ] Pin/unpin interface
  - [ ] Clock replacement policy
  - [ ] Dirty page tracking
  - [ ] Page ID (file number + page number)
- [ ] Heap file management
  - [ ] Create/open/close heap files
  - [ ] Allocate new pages
  - [ ] Free space map (bitmap or simple list)
- [ ] Persistent catalog
  - [ ] Special system tables (pg_class-like)
  - [ ] Bootstrap: create catalog files on first run
  - [ ] Load catalog at startup
  - [ ] Store each column's display position next to its definition, so a reordering survives a restart
- [ ] Type registry (like Postgres's `pg_type`), so types are data and not special cases in the code
  - [ ] Each type has a name, an OID, text input and output functions, a storage size (fixed or variable length) and a comparison function
  - [ ] `INTEGER` and `TEXT` become entries of the registry; the executor, the wire protocol layer and the tuple format look types up through it. This replaces the `column_type_t` enum, the type-name table in the executor and the type-to-OID mapping in the query handler
  - [ ] Values are stored in their type's own format (an integer as 4 bytes), converted from and to text only at the edges; this also fixes integers being stored as written (`007`)
  - [ ] The persistent catalog stores each column's type by id
- [ ] Tuple format
  - [ ] Fixed-length header (null bitmap, optional xmin/xmax)
  - [ ] Variable-length data (length prefix for TEXT)
  - [ ] Tuple ID (TID): page + slot
- [ ] Integrate with executor
  - [ ] INSERT → allocate page, add tuple, mark dirty
  - [ ] SELECT → pin pages, iterate tuples, unpin

### Tests
- [ ] **Reference-model test for slotted page**
  - [ ] Random insert/delete operations
  - [ ] Compare against simple array model
  - [ ] Log RNG seed for reproducibility
- [ ] Unit tests for buffer pool (pin/unpin, eviction)
- [ ] Crash test: write data, kill -9, restart, verify data survives
- [ ] Fuzz test for page deserialization

### Deliverables
- `src/storage/page.{h,c}` - Slotted page implementation
- `src/buffer/buffer_pool.{h,c}` - Buffer pool
- `src/storage/heap_file.{h,c}` - Heap file management
- `src/catalog/catalog.{h,c}` - Persistent catalog (updated)

### Completion Criteria
```bash
# Insert data
psql -h localhost -p 5433 -c "INSERT INTO user VALUES (2, 'Bob');"

# Restart server (Ctrl-C, then restart)
./build-asan/tieldb

# Data still there
psql -h localhost -p 5433 -c "SELECT * FROM user;"
 id | name
----+------
  1 | Alice
  2 | Bob
(2 rows)
```

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
- [ ] Expression evaluation
  - [ ] Arithmetic (`+`, `-`, `*`, `/`, `%`)
  - [ ] Comparison (`=`, `!=`, `<`, `>`, `<=`, `>=`)
  - [ ] Logical (`AND`, `OR`, `NOT`)
  - [ ] Type casting
- [ ] WHERE clause
  - [ ] Filter tuples during scan
  - [ ] Conjunctive normal form (CNF) for optimizations later
- [ ] Projection
  - [ ] Select specific columns: `SELECT id, name FROM user`
  - [ ] Computed columns: `SELECT id * 2 AS double_id FROM user`
- [ ] Changing data (after WHERE; with MVCC in Phase 6 these mark row versions instead of removing them)
  - [ ] `DELETE FROM t [WHERE ...]`
  - [ ] `UPDATE t SET col = expr [, ...] [WHERE ...]`
  - [ ] `TRUNCATE t`
  - [ ] `INSERT INTO t SELECT ...` (after projection)
- [ ] ORDER BY (external sort)
  - [ ] In-memory sort for small tables
  - [ ] External merge sort for large tables (spill to disk)
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
  - [ ] Existing rows need a value for the new column: needs NULL or `DEFAULT` support first
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
FROM user u JOIN orders o ON u.id = o.user_id
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
CREATE INDEX idx_user_id ON user(id);

-- Query uses index (verify with EXPLAIN later)
SELECT * FROM user WHERE id = 42;

-- Range scan works
SELECT * FROM user WHERE id >= 10 AND id < 100;
```

**Estimated Time:** 2-3 weeks

---

## Phase 5: Durability (WAL, Recovery, Crash Testing)

**Goal:** Survive crashes without data loss. **Most educational phase.**

### Tasks
- [ ] Write-Ahead Log (WAL)
  - [ ] WAL records (redo-only first: INSERT, UPDATE, DELETE)
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
psql -c "BEGIN; INSERT INTO user VALUES (100, 'Test'); COMMIT;"

# Kill server hard (kill -9)
kill -9 $(pgrep tieldb)

# Restart
./build-asan/tieldb

# Data is there
psql -c "SELECT * FROM user WHERE id = 100;"
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
UPDATE user SET name = 'Alice2' WHERE id = 1;
-- (pause)

-- Transaction 2 (blocks until T1 commits/aborts)
BEGIN;
UPDATE user SET name = 'Alice3' WHERE id = 1;  -- waits
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
ANALYZE user;

-- Show plan
EXPLAIN SELECT * FROM user WHERE id = 42;

QUERY PLAN
-------------------------------------------------------------
Index Scan using idx_user_id on user  (cost=0.00..8.27 rows=1)
  Index Cond: (id = 42)

-- Complex join plan
EXPLAIN SELECT * FROM user u JOIN orders o ON u.id = o.user_id;

QUERY PLAN
-------------------------------------------------------------
Hash Join  (cost=24.50..89.75 rows=1000)
  Hash Cond: (o.user_id = u.id)
  -> Seq Scan on orders o  (cost=0.00..20.00 rows=1000)
  -> Hash  (cost=15.00..15.00 rows=500)
        -> Seq Scan on user u  (cost=0.00..15.00 rows=500)
```

**Estimated Time:** 2-3 weeks

---

## Phase 8+: Extended Features (Optional, Pick What Interests You)

### Extended Query Protocol (Prepared Statements)
- [ ] Parse/Bind/Execute messages
- [ ] Parameter binding ($1, $2, ...)
- [ ] Plan caching

### More Data Types
- [ ] FLOAT, DOUBLE
- [ ] DECIMAL (arbitrary precision)
- [ ] DATE, TIMESTAMP, INTERVAL
- [ ] BYTEA (binary data)
- [ ] Arrays
- [ ] User-defined types (UDTs), on top of the type registry from Phase 2
  - [ ] `CREATE TYPE ... AS ENUM`, as the first one: simple, and visible in `psql`
  - [ ] Composite types: `CREATE TYPE ... AS (a int, b text)`
  - [ ] Domains: a base type plus a constraint
  - [ ] Extra base types registered at compile time. Loading user-written shared libraries (what Postgres extensions do) is out of scope

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
   - libFuzzer on page deserializer, wire protocol parser, WAL replay
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

**Don't write a full SQL parser initially.** It's a huge distraction.

Options:
- **libpg_query** (C library, extracts Postgres parser): best option for learning
- Hand-write a minimal parser for Phase 1 (just CREATE, INSERT, SELECT *)
- **sqlparser-rs** (Rust): if comfortable with FFI
- **JSqlParser** (Java) or **Calcite** (Java): if using JVM

Use a real parser by Phase 3 (when you need expressions, joins, etc.).

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
