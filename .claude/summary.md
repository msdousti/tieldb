# Educational DBMS Project Summary

## Project Goals
- Build a DBMS to learn database internals (for yourself and others)
- Not production-ready, just bare minimum requirements
- New features shall be built incrementally. Extensibility in this sense is a key
- Use open-source databases, such as postgresql and mariadb, as a role models (Postgres wire protocol will be used, so we can connect using psql)

## Key Technical Decisions Made

### 1. Buffer Pool & I/O Strategy
- **Build your own buffer pool** instead of relying on OS page cache
- Avoid double buffering (Postgres's approach with shared_buffers + OS cache)
- Use **O_DIRECT** for true control over I/O, eviction, and flushing
- Start with **buffered I/O initially**, switch to O_DIRECT later
- Use `aligned_alloc(4096, ...)` from day one for page frames
- **Avoid mmap** (breaks WAL guarantees, invisible page faults, TLB shootdown issues)

### 2. Durability & Error Handling
- **Treat fsync errors as fatal** from the start (lesson from Postgres's 2018 fsyncgate)
- Build a **fault-injection layer** to simulate torn writes and dropped fsyncs
- Never mix buffered and direct I/O on the same file

### 3. Future Experiments
- Try **io_uring** for async page reads and WAL writes (modern async I/O)
- Compare buffered vs O_DIRECT performance with metrics

### 4. Development Environment
- **Target Linux** (not macOS) because:
  - macOS lacks O_DIRECT, io_uring, fallocate, sync_file_range
  - fsync semantics differ (needs F_FULLFSYNC)
  - Sanitizers (LeakSanitizer, Valgrind) don't work properly on Apple Silicon
- **Recommended setup**: Use OrbStack for lightweight Linux VM on Mac
- Keep data directory on VM's disk (ext4/xfs), not shared folders
- Edit source on Mac, build/run in VM
- Use modern C (C23, latest standard). Incorporate guidelines from `c-guidelines.md`

### 5. Testing Strategy
- Use fork() + kill -9 for crash testing
- Fault-injection layer for power-loss simulation (torn writes, lost syncs)

## Where You Left Off
The discussion ended with the question of whether to scaffold the repo or go deeper on buffer pool and I/O interface design.

## Next Steps
- [ ] Decide: scaffold repo structure vs. dive deeper into buffer pool design
- [ ] Set up Linux development environment (OrbStack)
- [ ] Define bare minimum feature set
- [ ] Create initial project structure
