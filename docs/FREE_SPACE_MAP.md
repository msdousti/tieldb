# Free space maps: how database systems find room for a row, and what TielDB does

A heap file is an unordered pile of pages, so an `INSERT` has to find a page with enough room for
the new row. Scanning the file for it is far too slow, so every system that stores rows in a heap
keeps a summary of the free space. This document compares what several systems do, says which of
the designs apply to which TielDB storage engine, and records the design chosen for the heap
engine (`ROADMAP.md`, Phase 2, milestone B).

## How to read this document

The comparison was made from the documents linked at the end. Each statement is one of:

- **Documented**: stated in a document that I read (linked in "Sources").
- **Secondary**: taken from a blog post or slides that describe the source code, not from the
  vendor's documentation. Details such as bit meanings may be off.
- **General knowledge**: not checked against a source for this document.

Rows in the table are marked with these words. Nothing here was measured.

## Two problems that are easy to mix up

1. **Page allocation**: which pages of the file belong to nobody (freshly extended, or released
   when a table is dropped or a page is emptied) and can be handed to any structure that needs a
   page. The unit is a whole page, and the answer is exact.
2. **Free space inside pages**: which pages that already hold rows have room for a row of N bytes.
   The unit is bytes, and the answer may be approximate, because a wrong guess costs one extra
   page visit (the inserter looks at the real page and moves on).

A heap needs both. A structure that decides the position of a row by its key, such as a B+tree
(a clustered index), needs only the first: the row goes to the leaf that the key selects, and if
that leaf is full the leaf is split and one new page is allocated. SQL Server says exactly this:
free space is tracked only for heap and LOB pages, because "the point at which to insert a new
row is always determined by the index key values".

## The comparison

| System | Tracks | Granularity | Where it lives | Used for | Status |
|---|---|---|---|---|---|
| **PostgreSQL** | Free bytes per page | 1 byte per page: free space in units of 1/256 of a page (32 bytes for 8 KB), rounded down | A separate fork file `<filenode>_fsm`; a tree of FSM pages of fixed height 3, each page a binary max-tree (every inner node holds the maximum of its children) | Heaps and also indexes (only hash indexes have none) | Documented |
| **SQL Server** | Fullness band per page, plus whether the page is allocated | 1 byte per page: empty, 1-50%, 51-80%, 81-95%, 96-100% full. One PFS page covers 8,088 pages. Extents (8 pages) are tracked apart in GAM/SGAM bitmaps (1 bit per extent) and per object in IAM pages | Special pages (PFS, GAM, SGAM, IAM) inside the data file, at fixed intervals | Heap and LOB pages only; B-tree pages need no free space tracking | Documented |
| **InnoDB** (MySQL, MariaDB) | Free pages and extents, not bytes | Extent of 64 pages; the extent descriptor holds 2 bits per page (a free bit and a clean bit; the clean bit appears unused). Each segment keeps FREE, NOT_FULL and FULL extent lists | Extent descriptor pages and inode pages inside the tablespace file | Allocation for the clustered index and secondary indexes; rows are placed by key | Secondary |
| **Oracle** | Fullness state per block (automatic segment space management, ASSM) | A few states per block: unformatted, full, and ranges of free space (0-25%, 25-50%, 50-75%, 75-100%) in a 3-level hierarchy of bitmap blocks. The exact state codes were not verified. The older manual method uses freelists with `PCTFREE`/`PCTUSED` | Bitmap blocks inside the segment | Heap tables | Documented (concept), Secondary (states) |
| **Db2 LUW** | Free space per page | Free space control records (FSCRs): one on every 500th data page, covering the 500 pages that follow. An insert searches only a few FSCRs (default 5, `DB2MAXFSCRSEARCH`), then appends | Inside the table's own pages | Row tables. `APPEND ON` tables skip the tracking and always append | Documented |
| **SQLite** | Whole free pages only | A freelist of trunk pages; a trunk page lists many leaf pages (at least 120 entries; the count depends on the page size). Within a B-tree page, a chain of freeblocks plus a count of fragmented bytes | In the database file. With `auto_vacuum`, pointer-map pages (5 bytes per page) record each page's parent so pages can be moved | All tables are B-trees (rowid or key), so there is no cross-page free space search | Documented |
| **DuckDB** | Free blocks of the file | A free list of 256 KB blocks, reached from a database header. Deleted rows are marked in per-row-group version data and reclaimed at a checkpoint by merging row groups that have many deletes (about 25% in adjacent groups) | Meta blocks in the single database file | Column storage; there is no slot reuse inside a page | Secondary, Documented (checkpoint) |
| **LSM stores** (RocksDB, LevelDB) | Nothing | None: files are immutable, a delete writes a tombstone, and compaction rewrites files and drops dead data | None | Not applicable | General knowledge |

## Notes on the designs

**PostgreSQL** treats the map as a hint. The README says it is not explicitly WAL-logged: it
corrects itself, since after an update the root is compared with the new value, searches detect
parents set too high, and corrupted upper nodes are rebuilt on the spot. Pages are written with
`MarkBufferDirtyHint` and read with `RBM_ZERO_ON_ERROR`, so a damaged FSM page is zeroed instead of
failing the read. Relation extension is not logged either, so after replay a slot can point past
the end of the file; it is detected by comparing with the file size and marked full. A search
starts at the root and descends to a child with enough room, takes one shared lock at a time, and
uses a per-page "next slot" hint so that concurrent inserters spread over different pages while
still filling pages in order. `VACUUM` records the real free space of each page it scans and then
propagates the values up the tree.

**SQL Server** and **Db2** keep the map inside the data file, in special pages at fixed
intervals, which makes the file's page numbers non-uniform (page 1, 8088, 16176 are PFS pages in
SQL Server). **Oracle** also keeps it inside the segment, as bitmaps. **PostgreSQL** keeps it in a
second file, so that heap page N is always block N.

**InnoDB** and **SQLite** have no byte-level map at all, because their rows live in key-ordered
B-trees. What they track is whole pages (InnoDB groups them in extents so that a segment gets
sequential pages; SQLite uses one freelist).

**DuckDB** and the **LSM** stores avoid the problem by never updating in place: space is
reclaimed in bulk, by rewriting row groups or files, not page by page.

## What this means for TielDB

TielDB will have several storage engines behind one interface (`ROADMAP.md`), and they need
different things. Two layers follow:

| Engine | Needs a free space map? | Needs page allocation? |
|---|---|---|
| Heap (milestone B) | Yes | Yes |
| Clustered index (B+tree, planned) | No (rows go to the leaf chosen by the key) | Yes: leaf and inner pages, reuse of emptied pages |
| LSM (Phase 8+) | No | No, it works with immutable files; space comes back by compaction |
| One-page engine (milestone A) | No | No |
| Secondary indexes | No | Yes |

- **The free space map belongs to the heap engine**, not to the server. It is created, used and
  repaired by the heap engine, and nobody else depends on it. This is what the SQL Server and
  PostgreSQL documents also say about their own B-trees (PostgreSQL does keep a map for index
  files, but only to find completely empty pages for reuse, which is problem 1, not problem 2).
- **Reuse of whole free pages is a separate, smaller piece**, needed by the heap and by every
  page-based index. It is not part of the free space map, and the choice here does not decide it.
  The page file already has `page_file_allocate_page`, which extends the file; reuse of released
  pages is a later task and gets its own decision (an in-file freelist like SQLite, or a bitmap).
- **LSM does not use pages**, so neither piece applies. Its equivalent is compaction.

## Decision: a PostgreSQL-style map for the heap engine

The heap engine gets a free space map in a separate file next to the heap file: one byte per heap
page holding the free space in units of 1/256 of the page size, organized as a tree of max-nodes,
treated as a hint that is allowed to be wrong, and repaired from the heap itself.

Why:

1. **It is approximate by design.** A byte per page in 32-byte units is enough to choose a
   candidate page; the inserter checks the real page (`page_free_space`) and, if it does not fit,
   corrects the map and tries the next page. No exact bookkeeping is needed.
2. **It needs no log.** Because the map is rebuilt from the heap pages (each page header states
   its free space), a lost or damaged map costs a scan and never data. This matters now: there
   is no WAL before Phase 5, and a map that had to be recovered would need one. It also fits the
   torn write findings (`docs/TORN_WRITES.md`): a torn map page is only a wrong hint.
3. **The heap file stays uniform.** Page N of the heap file is heap page N. In-file maps (SQL
   Server, Db2, Oracle) put special pages among the data pages, which complicates the page file,
   the buffer pool's page numbering and every sequential scan, for a gain TielDB does not need.
4. **The search is logarithmic and simple.** The max-tree answers "is there a page with at
   least N bytes" with one lookup at the root and finds one in a descent of fixed height.

Alternatives considered:

- **Exact bitmap in the data file (SQL Server, Oracle):** exact, but the exactness is not needed
  (point 1), it must be logged to be crash safe (point 2), and it breaks the uniform file
  (point 3).
- **Chained free-space records inside the table pages (Db2):** same objections, plus the bounded
  search depth is a tuning parameter we do not want to carry.
- **Extent lists (InnoDB):** a good answer to a different question (sequential allocation for a
  B-tree), not to "which page has room for 120 bytes".
- **One freelist of whole pages (SQLite):** it works for key-ordered storage only.
- **No map, append only:** simple and what the one-page engine effectively does, but a table
  would grow without bound under deletes and updates.

## Open points for the implementation PRs

These are not decided here:

- The page size is a parameter, so the category width is `page_size / 256`; the map must state
  which size it was built for, or be rebuilt when the sizes differ.
- Who refreshes the map before `VACUUM` exists (Phase 6 or later)? Candidates: the inserter that
  finds a page full, the delete path, and a scan when the map is missing or fails its checksum.
- Locking: PostgreSQL holds one lock at a time and tolerates stale answers. The buffer pool has
  one lock today; whether the map needs its own is for the concurrency phases.
- How the map file is opened by the engine (through the buffer pool as a second file, which the
  pool's `page_id_t` already supports with a different `file_number`) and what a missing map file
  means at startup.
- Whether the first slice is a flat array of bytes scanned linearly, with the tree added next.
  The scan is correct and the tree is an optimization; that is a question of PR size.

## Sources

- PostgreSQL documentation, [Free Space Map](https://www.postgresql.org/docs/current/storage-fsm.html)
  (documented: separate fork, tree of pages, max-tree per page, which relations have one).
- PostgreSQL source, [`src/backend/storage/freespace/README`](https://github.com/postgres/postgres/blob/master/src/backend/storage/freespace/README)
  (documented: 1/256 categories, fixed height 3, the search, the logging and recovery notes, the
  role of `VACUUM`).
- Microsoft Learn, [Pages and extents architecture guide](https://learn.microsoft.com/en-us/sql/relational-databases/pages-and-extents-architecture-guide)
  (documented: PFS bands, intervals of 8,088 pages, GAM/SGAM/IAM, no tracking for B-trees).
- SQLite, [Database file format](https://www.sqlite.org/fileformat2.html) (documented: freelist
  trunk and leaf pages, pointer-map pages, freeblocks and fragmented bytes).
- IBM, [Improving insert performance](https://www.ibm.com/support/knowledgecenter/en/SSEPGG_11.1.0/com.ibm.db2.luw.admin.perf.doc/doc/c0009677.html)
  and the IBM support note
  [When is space from a deleted row reused in Db2](https://www.ibm.com/support/pages/when-space-deleted-row-reused-db2)
  (FSCR every 500th page, `DB2MAXFSCRSEARCH`, `APPEND ON`).
- Oracle, [Logical storage structures](https://docs.oracle.com/en/database/oracle/oracle-database/19/cncpt/logical-storage-structures.html)
  (ASSM uses bitmaps; documented) and Tanel Poder,
  [Freelists vs ASSM](https://tanelpoder.com/files/Poder_Freelists_vs_ASSM.pdf) (the bitmap block
  levels and states; secondary).
- MySQL blog archive, [Extent descriptor page of InnoDB](https://dev.mysql.com/blog-archive/extent-descriptor-page-of-innodb/)
  and [InnoDB tablespace space management](https://dev.mysql.com/blog-archive/innodb-tablespace-space-management/)
  (secondary: extents of 64 pages, 2 bits per page, segment lists). The page in the MySQL
  developer documentation that I tried to read did not exist.
- DuckDB, [`CHECKPOINT`](https://www.duckdb.org/docs/current/sql/statements/checkpoint.html)
  (documented: partial reclaiming of deleted rows) and the Alibaba Cloud articles
  [file format](https://www.alibabacloud.com/blog/duckdb-internals---part-1-file-format-overview_602511)
  and [table storage](https://www.alibabacloud.com/blog/duckdb-internals---part-2-table-storage-format_602657)
  (secondary: free list, 256 KB blocks, row groups).
