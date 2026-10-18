# TielDB Design Choices

Design decisions for the educational DBMS, ordered by implementation priority. Each choice lists the **first implementation** (what we build now) and **future extensions** (what we add later for completeness).

---

## 1. Process Model

**Options:** Multiprocess (fork model like Postgres) vs. Multithreading

**Choice:** **Multithreading**

**Rationale:**
- Simpler shared memory management (buffer pool, lock table)
- Easier to debug with TSan
- Modern approach (most new databases are multithreaded)

**Future:** None planned (multiprocess adds complexity without educational value)

---

## 2. Storage Engine Architecture

**Options:** Heap model vs. LSM trees vs. Other (B-tree storage, etc.)

**Choice:** **Flexible and extensible** - build both incrementally

**First:** Heap model (simpler, traditional SQL databases)
- Slotted pages with in-place updates
- B+tree indexes pointing to heap tuples

**Later:** LSM tree engine
- Write-optimized, append-only
- Compaction and merge process
- Both engines coexist via storage engine abstraction layer

---

## 3. Write-Ahead Logging (WAL)

**Options:** Undo logs vs. Redo logs vs. Undo/Redo (ARIES)

**Choice:** **Flexible and extensible** - build both incrementally

**First:** Redo-only logs (simpler recovery)
- Logical or physiological records
- Force-log-at-commit, no-steal buffer policy initially

**Later:** Full undo/redo (ARIES-style)
- Steal/no-force buffer policy
- Compensation log records (CLRs)
- More realistic recovery

---

## 4. Concurrency Control

**Options:** Two-Phase Locking (2PL) vs. MVCC (Multi-Version Concurrency Control) vs. Optimistic Concurrency Control (OCC)

**Choice:** **Start simple, extend later**

**First:** Strict 2PL with deadlock detection
- Row-level locks (shared/exclusive)
- Lock manager with wait-for graph
- Deadlock detection via cycle detection

**Later:** MVCC (Postgres-style)
- Tuple versioning with visibility rules
- Snapshot isolation
- No read locks, higher concurrency

---

## 5. Isolation Levels

**Options:** Read Uncommitted, Read Committed, Repeatable Read, Serializable

**Choice:** **Incremental support**

**First:** Read Committed only
- Simplest to implement with 2PL
- Locks held until transaction end

**Later:** Add Repeatable Read and Serializable
- Predicate locks for serializable
- Serialization graph testing (SSI)

---

## 6. Page Layout

**Options:** Slotted pages vs. Fixed-size records vs. PAX (Partition Attributes Across)

**Choice:** **Slotted pages** (standard for variable-length data)

**First:** Basic slotted pages
- Header with slot array
- Tuples grow from the end
- Free space in the middle

**Later:** Optimizations
- Compression (dictionary, RLE)
- TOAST-like external storage for large values

---

## 7. Buffer Replacement Policy

**Options:** LRU vs. Clock (Second Chance) vs. 2Q vs. ARC

**Choice:** **Start simple, experiment later**

**First:** Clock algorithm (Second Chance)
- Approximates LRU
- Single pass, low overhead
- Used by Postgres

**Later:** More sophisticated policies
- 2Q (two queues for hot/cold)
- ARC (adaptive replacement cache)
- Workload-aware policies

---

## 8. Index Structures

**Options:** B+tree vs. Hash indexes vs. LSM-tree indexes vs. Specialized (GiST, GIN, BRIN)

**Choice:** **B+tree first, extend later**

**First:** B+tree only
- Clustered or unclustered
- Unique and non-unique
- Range scans and point lookups

**Later:** Additional index types
- Hash indexes (equality only, faster)
- GiST-like (generalized search tree for custom types)

---

## 9. Query Execution Model

**Options:** Volcano (iterator/pull) vs. Vectorized (batch) vs. Push-based vs. Compiled

**Choice:** **Volcano model first**

**First:** Iterator model (Volcano)
- `open()`, `next()`, `close()` interface
- Simple, composable operators
- Easy to understand and debug

**Later:** Vectorization
- Process tuples in batches (e.g., 1000 at a time)
- Better CPU cache utilization
- More complex but faster

---

## 10. Query Optimizer

**Options:** Rule-based vs. Cost-based vs. Hybrid

**Choice:** **Rule-based first, cost-based later**

**First:** Simple rule-based optimizer
- Push down selections and projections
- Join order by heuristics (smaller table first)
- No cardinality estimation

**Later:** Cost-based optimizer
- Statistics collection (histograms, MCVs)
- Cardinality estimation
- Cost model for operators
- Dynamic programming for join ordering

---

## 11. Locking Granularity

**Options:** Database-level vs. Table-level vs. Page-level vs. Row-level (tuple-level)

**Choice:** **Row-level from the start**

**First:** Row-level locking (tuple-level)
- Shared (S) and Exclusive (X) locks
- Intention locks (IS, IX) on tables

**Later:** Possibly add page-level for bulk operations (optional)

---

## 12. WAL Record Format

**Options:** Logical (SQL operations) vs. Physical (byte-level) vs. Physiological (page-logical)

**Choice:** **Physiological first** (Postgres-style)

**First:** Physiological logging
- Page ID + logical operation within page
- "Insert tuple at slot 5 on page 42"
- Balances simplicity and efficiency

**Later:** Support logical replication (optional)
- Decode WAL into SQL-like operations
- For replication, not recovery

---

## 13. Checkpointing Strategy

**Options:** Sharp (quiesce system) vs. Fuzzy (online) vs. Incremental

**Choice:** **Fuzzy checkpointing**

**First:** Simple fuzzy checkpoint
- Record all dirty pages at checkpoint start
- Continue accepting writes during checkpoint
- Write checkpoint record with oldest active transaction

**Later:** Incremental checkpointing (optional)
- Track dirty pages continuously
- Smaller, more frequent checkpoints

---

## 14. Recovery Algorithm

**Options:** Simple redo-only vs. ARIES (full undo/redo) vs. Other (Shadow paging, etc.)

**Choice:** **Match the WAL choice**

**First:** Simple redo-only recovery
- Replay WAL from last checkpoint
- Force all pages at commit (no-steal policy)

**Later:** Full ARIES recovery
- Analysis, Redo, Undo phases
- Handle uncommitted transactions
- Compensation log records (CLRs)

---

## 15. Data Types

**Options:** Minimal (int, text) vs. Full SQL types vs. Extensible type system

**Choice:** **Start minimal, extend incrementally**

**First:** Core types only
- `INTEGER` (32-bit or 64-bit)
- `TEXT` (variable-length string)
- `BOOLEAN`
- NULL support

**Later:** Add more types
- `FLOAT`, `DOUBLE`
- `DECIMAL` (arbitrary precision)
- `DATE`, `TIMESTAMP`
- `BYTEA` (binary data)
- User-defined types (extensibility)

---

## 16. Storage Format

**Options:** Row-oriented (NSM) vs. Column-oriented (DSM) vs. Hybrid (PAX)

**Choice:** **Row-oriented first**

**First:** Row-oriented storage (NSM)
- Entire row stored together
- Good for OLTP (transaction processing)
- Simpler to implement

**Later:** Column-oriented storage engine (optional)
- Good for OLAP (analytics)
- Better compression
- Separate storage engine, coexists with row store

---

## 17. Network Protocol

**Options:** Custom binary vs. Text-based vs. Postgres wire protocol

**Choice:** **Postgres wire protocol** (already decided)

**First:** Subset of Postgres protocol
- Simple query protocol (text queries)
- Basic authentication (trust or md5)

**Later:** Extended protocol
- Prepared statements (parse/bind/execute)
- Copy protocol (bulk load)
- NOTIFY/LISTEN

---

## 18. Transaction Durability

**Options:** Full ACID vs. Relaxed durability vs. Configurable

**Choice:** **Full ACID first, then configurable**

**First:** Synchronous commit (full durability)
- fsync WAL at commit
- Guaranteed durability, slower

**Later:** Asynchronous commit option
- Return before fsync (trades durability for speed)
- Configurable per transaction

---

## 19. Vacuum/Garbage Collection

**Options:** Online (auto-vacuum) vs. Manual vs. Generational vs. None (overwrite in place)

**Choice:** **Depends on storage model**

**First (Heap):** Manual vacuum initially
- Reclaim dead tuple space
- Update free space map
- Simple, explicit

**Later (Heap):** Auto-vacuum
- Background process
- Triggered by thresholds

**First (LSM):** Compaction built-in
- Merge and rewrite SSTables
- Remove deleted/old versions

---

## 20. Query Language

**Options:** Subset of SQL vs. Custom query language vs. Full SQL

**Choice:** **Subset of SQL**

**First:** Core SQL DML
- `SELECT` (with `WHERE`, `ORDER BY`, `LIMIT`)
- `INSERT`, `UPDATE`, `DELETE`
- `CREATE TABLE`, `DROP TABLE`
- `CREATE INDEX`, `DROP INDEX`
- `BEGIN`, `COMMIT`, `ROLLBACK`

**Later:** Extended SQL
- `JOIN` (nested loop first, then hash/merge)
- Aggregates (`COUNT`, `SUM`, `AVG`, `GROUP BY`)
- Subqueries
- CTEs (Common Table Expressions)
- Window functions (much later)

---

## Summary Table

| Component | First Implementation | Later Extension |
|-----------|---------------------|-----------------|
| **Process Model** | Multithreading | None |
| **Storage Engine** | Heap model | + LSM trees |
| **WAL** | Redo-only | + Undo/redo (ARIES) |
| **Concurrency** | Strict 2PL | + MVCC |
| **Isolation** | Read Committed | + Repeatable Read, Serializable |
| **Page Layout** | Slotted pages | + Compression, TOAST |
| **Buffer Policy** | Clock algorithm | + 2Q, ARC |
| **Indexes** | B+tree | + Hash, GiST |
| **Execution** | Volcano (iterator) | + Vectorized |
| **Optimizer** | Rule-based | + Cost-based |
| **Locking** | Row-level | (already fine-grained) |
| **WAL Format** | Physiological | + Logical replication |
| **Checkpointing** | Fuzzy | + Incremental |
| **Recovery** | Redo-only | + ARIES |
| **Data Types** | int, text, bool | + float, decimal, date, UDT |
| **Storage Format** | Row-oriented | + Column-oriented |
| **Protocol** | Postgres wire (simple) | + Extended protocol |
| **Durability** | Sync commit | + Async commit option |
| **Vacuum** | Manual (heap), built-in (LSM) | + Auto-vacuum (heap) |
| **SQL** | Core DML + DDL | + Joins, aggregates, subqueries |

---

## Design Principles

1. **Start simple, extend incrementally:** Every component has a simple first version and a path to sophistication.
2. **Extensibility is key:** Use abstraction layers (storage engine API, lock manager API) so implementations can coexist.
3. **Test at every step:** Each feature gets tests (unit, reference-model, crash, fuzz) before moving on.
4. **Learn from the real world:** Postgres and SQLite are the reference models. Read their code when stuck.
