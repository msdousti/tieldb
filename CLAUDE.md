# TielDB - Educational Database Management System

An educational DBMS built to learn database internals, modeled after PostgreSQL and MariaDB. Written in modern C (C23, the latest standard) with emphasis on correctness, testability, and incremental development.

---

## Project Overview

**Purpose:** Learn database internals by building a working DBMS from scratch.

**Philosophy:**
- Not production-ready, but functionally complete
- Incremental development - working code at every step
- Test-driven with reference models, crash tests, and sanitizers
- Extensible architecture - multiple storage engines, concurrency models
- Uses Postgres wire protocol (connect with `psql`)

**Current Phase:** Phase 0 - Project Setup (see `ROADMAP.md`)

---

## How to Work on This Project

### 1. Read the Guidelines First

Before writing any code, familiarize yourself with:

- **[Project Summary](/.claude/summary.md)** - Goals, technical decisions, development environment
- **[C Coding Guidelines](/.claude/c-guidelines.md)** - Modern C practices, Meson build, sanitizers, testing
- **[Clean Code Principles](/.claude/clean-code.md)** - Functions, naming, avoiding nesting, testing
- **[Style Guide](/.claude/style-guide.md)** - Naming conventions, formatting rules, enforcement
- **[Design Choices](/.claude/design-choices.md)** - Architectural decisions and rationale
- **[Development Roadmap](/ROADMAP.md)** - Phased implementation plan

### 2. Development Environment

**Platform:** Linux (Ubuntu/Debian) via OrbStack VM on macOS
- macOS lacks O_DIRECT, io_uring, proper sanitizer support
- Edit source on Mac, build/run in Linux VM
- Data directory on VM disk (ext4/xfs), not shared folders

**Setup:**
```bash
# On macOS: Install OrbStack
# Create Linux VM, then inside the VM:

# Install dependencies
sudo apt update
sudo apt install -y build-essential meson ninja-build clang \
    clang-format clang-tidy libcmocka-dev git

# Clone and build
cd /path/to/tieldb
meson setup build-asan -Db_sanitize=address,undefined -Dc_std=c2x
ninja -C build-asan
meson test -C build-asan

# Note: c_std=c2x is C23 (c23 not yet supported by all Meson versions)
```

### 3. Code Generation Rules

**Always follow these rules when writing code:**

#### Naming Conventions (from `.claude/style-guide.md`)
- **Functions:** `module_verb_object` in `snake_case`
  ```c
  int buffer_pool_init(buffer_pool_t *pool, size_t n_buffers);
  page_t *buffer_pool_get_page(buffer_pool_t *pool, page_id_t page_id);
  ```
- **Variables:** `snake_case`
  ```c
  int pin_count;
  page_id_t victim_page_id;
  bool is_dirty;
  ```
- **Types:** `snake_case_t` suffix
  ```c
  typedef struct buffer_pool buffer_pool_t;
  typedef uint64_t page_id_t;
  typedef uint64_t lsn_t;
  ```
- **Constants/Macros:** `UPPER_SNAKE_CASE`
  ```c
  #define PAGE_SIZE 8192
  #define MAX_BUFFER_POOL_SIZE (1024 * 1024)
  ```

#### Formatting (enforced by `.clang-format`)
- **4 spaces** indentation, no tabs
- **100 characters** max line length
- **Braces:** Functions on new line, control structures on same line
  ```c
  // Function: brace on new line
  int buffer_pool_init(buffer_pool_t *pool)
  {
      // body
  }
  
  // Control: brace on same line
  if (condition) {
      // body
  }
  ```
- **Always use braces** for single statements
- **Pointer asterisk with name:** `int *ptr`, not `int* ptr`

#### Code Structure
- **Functions do one thing, stay small** (<20 lines ideal)
- **Avoid nesting** (max depth 2, use early returns)
  ```c
  // Good
  int foo(page_t *page)
  {
      if (page == NULL) {
          return -1;
      }
      if (page->pin_count == 0) {
          return -1;
      }
      
      return do_work(page);  // main logic at left margin
  }
  ```
- **No comments by default** - code should be self-explanatory
  - Only comment *why*, never *what*
  - Document public APIs with brief descriptions

#### Memory Management
- **Every allocation has one owner** with matching free
- **Use arena allocators** (Postgres-style `MemoryContext`) for per-query/per-transaction cleanup
- **Set pointers to NULL after free**
- **Always check malloc/calloc/realloc** return values
- **Make leaks structural:** assert zero outstanding arenas/pins/file handles at teardown

#### Error Handling
- **Consistent convention:** Return 0 for success; on failure return -1 (or `nullptr` from `foo_create`) and set `errno`
- **Check every return value** that can fail (malloc, read, write, fsync)
- **Don't return NULL casually** - use result structs when needed
- **Fail fast** - library code should never call `exit()`

#### C23 Features to Use
**Use the latest C23 features** (see `.claude/c-guidelines.md` for full list):

- **`nullptr`** instead of `NULL`:
  ```c
  if (page == nullptr) { return -1; }
  ```

- **`bool`, `true`, `false`** are keywords (no need for `<stdbool.h>`):
  ```c
  bool is_dirty = true;  // No #include <stdbool.h> needed
  ```

- **`typeof`** for type-safe macros (now standard, was GNU extension):
  ```c
  #define MIN(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })
  ```

- **`[[nodiscard]]`** for functions whose return value must be checked:
  ```c
  [[nodiscard]] int buffer_pool_pin_page(buffer_pool_t *pool, page_id_t id);
  ```

- **`[[maybe_unused]]`** for parameters in callbacks/interfaces:
  ```c
  static int callback([[maybe_unused]] void *arg)
  ```

- **`constexpr`** for compile-time constants:
  ```c
  constexpr size_t PAGE_SIZE = 8192;
  ```

- **`static_assert`** (no second argument needed):
  ```c
  static_assert(sizeof(page_header_t) == 64);  // Verify on-disk layout
  ```

- **`auto`** for type inference (use sparingly):
  ```c
  auto count = get_page_count();  // Type inferred from return value
  ```

- **Empty initializer `{}`** instead of `{0}`:
  ```c
  page_t page = {};  // Zero-initialize
  ```

### 4. Testing Requirements

**Every feature must have tests before it's considered done.**

#### Test Types (from `.claude/c-guidelines.md`)

1. **Reference-Model Tests** (most valuable)
   ```c
   // Test B+tree against a sorted array
   for (int i = 0; i < 1000; i++) {
       int key = rand();
       btree_insert(tree, key, value);
       array_insert(model, key, value);
       assert_trees_match(tree, model);
   }
   ```

2. **Crash Tests** (for WAL/durability)
   ```c
   pid_t pid = fork();
   if (pid == 0) {
       // Child: run workload
       insert_data();
       exit(0);
   }
   // Parent: kill child, recover, check invariants
   usleep(rand() % 1000);
   kill(pid, SIGKILL);
   recover();
   check_invariants();
   ```

3. **Fuzz Tests** (for parsers/deserializers)
   ```c
   // libFuzzer target for page deserializer
   int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
       page_t page;
       deserialize_page(&page, data, size);
       return 0;
   }
   ```

4. **Fault Injection** (via injectable I/O interface)
   ```c
   // Simulate torn write on 3rd write call
   io_inject_fault(IO_FAULT_TORN_WRITE, /* nth = */ 3);
   
   // Simulate fsync failure
   io_inject_fault(IO_FAULT_FSYNC_FAIL, 1);
   ```

#### Testing Workflow
```bash
# Run all tests (ASan + UBSan by default)
meson test -C build-asan

# Run with TSan (for concurrency)
meson test -C build-tsan

# Run with coverage (fails below the line-coverage threshold in gcovr.cfg)
./scripts/coverage.sh

# Run fuzz tests
./build-fuzz/fuzz_page_deserialize -max_total_time=60

# Run Valgrind (periodic, not daily)
valgrind --leak-check=full ./build-release/test_buffer_pool
```

### 5. Build System (Meson + Ninja)

**Directory structure:**
```
build-asan/          # Default dev build (ASan + UBSan)
build-tsan/          # Thread sanitizer (for Phase 6+)
build-release/       # Optimized build (benchmarks only)
build-fuzz/          # Fuzzing build (libFuzzer)
build-cov/           # Coverage build
```

**Common commands:**
```bash
# Setup (once)
meson setup build-asan -Db_sanitize=address,undefined

# Build
ninja -C build-asan

# Test
meson test -C build-asan

# Clean
rm -rf build-asan && meson setup build-asan -Db_sanitize=address,undefined

# Format code (before commit)
clang-format -i src/**/*.{c,h} tests/**/*.{c,h}

# Lint
clang-tidy -p build-asan src/*.c tests/*.c
```

### 6. Key Architecture Patterns

#### Injectable I/O Interface (Phase 0)
**All file I/O goes through this interface** for fault injection:

```c
// src/io/io.h
typedef struct io_ops {
    ssize_t (*read)(int fd, void *buf, size_t count);
    ssize_t (*write)(int fd, const void *buf, size_t count);
    int (*fsync)(int fd);
    // ... other ops
} io_ops_t;

// Production: real syscalls
extern const io_ops_t *io_real;

// Testing: injectable faults
extern const io_ops_t *io_fault_inject;

// Global pointer (set at startup)
extern const io_ops_t *io;
```

#### Storage Engine Abstraction (Phase 2+)
Multiple storage engines (heap, LSM) coexist via vtable:

```c
typedef struct storage_engine {
    int (*insert)(void *engine, const tuple_t *tuple);
    tuple_t *(*get)(void *engine, tuple_id_t tid);
    // ...
} storage_engine_t;

// Heap storage
extern const storage_engine_t heap_storage_engine;

// LSM storage (Phase 8+)
extern const storage_engine_t lsm_storage_engine;
```

#### Module Organization
- **One header per module:** `buffer_pool.h`, `page.h`, `wal.h`
- **Opaque types** for encapsulation:
  ```c
  // buffer_pool.h
  typedef struct buffer_pool buffer_pool_t;
  
  buffer_pool_t *buffer_pool_create(size_t n_buffers);
  void buffer_pool_destroy(buffer_pool_t *pool);
  
  // buffer_pool.c
  struct buffer_pool {
      page_t *pages;
      size_t n_pages;
      // ... internals hidden
  };
  ```
- **File-local functions are `static`:**
  ```c
  static int find_victim_page(buffer_pool_t *pool)
  {
      // ...
  }
  ```

### 7. Code Review Checklist

Before submitting/committing code:

- [ ] Follows naming conventions (snake_case, module prefixes)
- [ ] Formatted with `clang-format -i`
- [ ] Passes `clang-tidy` with no warnings
- [ ] Compiles with `-Werror` (all warnings are errors)
- [ ] All tests pass under ASan + UBSan
- [ ] New feature has tests (unit, reference-model, or crash test)
- [ ] Functions <20 lines, nesting depth ≤2
- [ ] No raw `malloc`/`free` in application code (use arenas)
- [ ] All error returns checked
- [ ] No comments unless explaining *why*
- [ ] Module internals are `static`
- [ ] Stateful module types are opaque (`foo_create`/`foo_destroy`); vtables only where implementations are swappable
- [ ] Every failure sets `errno`, documented in the header and asserted in tests
- [ ] At most 4 parameters per function; use an options struct beyond that
- [ ] Headers are self-contained with `#pragma once`

### 8. When Writing New Code

**Order of operations:**

1. **Write the test first** (TDD)
   - Reference-model test for data structures
   - Crash test for durability features
   - Fuzz test for parsers/deserializers

2. **Write the interface** (header file)
   - Opaque types
   - Function declarations
   - Brief comments for public API

3. **Write the implementation**
   - Follow style guide
   - Keep functions small
   - Avoid nesting
   - No comments unless necessary

4. **Run sanitizers**
   ```bash
   ninja -C build-asan
   meson test -C build-asan
   ```

5. **Format and lint**
   ```bash
   clang-format -i src/module.{c,h}
   clang-tidy -p build-asan src/module.c
   ```

6. **Commit**
   ```bash
   git add src/module.{c,h} tests/test_module.c
   git commit -m "Add module: brief description"
   ```

### 9. Development Model: Small Incremental PRs

Work proceeds as a sequence of small, reviewable pull requests, each leaving `main` working.

- **Target size:** feature PRs stay under 100-200 changed lines
- **Hard limit:** 500 changed lines for feature PRs; never exceed it without asking the user first
- **Exceptions:** ask the user for approval before opening a feature PR over the limit
- **Scope:** docs, CI, formatting, and other non-feature PRs are exempt from the limit
- **Splitting:** break roadmap items into the smallest slices that build, pass tests, and stand alone
  (e.g. header + tests, then implementation, then wiring)
- **Each PR includes** its own tests, and passes format, lint, and sanitizer checks
- **One concern per PR:** no unrelated refactors mixed into a feature PR
- **Roadmap upkeep:** each PR updates `ROADMAP.md` in the same PR. Check a task only when every part of it is done.
  If the PR does only part of a step, add sub-items under that step and check only the ones it completes
- **PR description:** fill in `.github/pull_request_template.md` (`gh pr create --body-file` does not apply it automatically); check only the boxes that are true
- **CI gate:** `main` requires every CI check (including `Detect Code Changes`), for admins too. Docs-only PRs skip the jobs but still report, so they merge normally. Never merge a red PR or ask to bypass; fix the cause
- **Coverage:** CI currently requires 100% line and branch coverage of `src/` (`gcovr.cfg`). Coverage is a tool, not a goal: do not write tests or contort code just to light up lines, and prefer reference-model, crash, and fuzz tests over coverage-driven ones. If a path is impractical or impossible to cover (crash and recovery code whose coverage data is lost on `kill -9`, concurrency races, I/O failure paths, compiler-generated branches), stop and ask the user to approve lowering the threshold or adding an exclusion in `gcovr.cfg` with a stated reason. Never lower or exclude on your own. Expect to relax the branch threshold (around 90-95%) once WAL and concurrency code arrive
- **Measure before opening:** check `git diff --stat main` and split if the size is over target

---

## Current Development Focus

**Phase 0: Project Setup** (see `ROADMAP.md`)

Tasks:
- [x] Define guidelines and design choices
- [ ] Set up OrbStack Linux VM
- [ ] Create Meson build configuration
- [ ] Scaffold directory structure
- [ ] Write injectable I/O interface
- [ ] First "hello world" test

**Next:** Phase 1 - In-Memory Skeleton + Postgres Wire Protocol

---

## Resources

**Documentation:**
- All guidelines: `.claude/` directory
- Roadmap: `ROADMAP.md`
- Formatting guide: `docs/FORMATTING.md`

**References:**
- CMU 15-445 Database Systems course
- *Database Internals* by Alex Petrov
- PostgreSQL source code (heapam, nbtree, xlog, bufmgr)
- SQLite source code (well-commented, single-file)

**When Stuck:**
1. Read the relevant `.claude/*.md` guideline
2. Check Postgres source for the corresponding component
3. Review `ROADMAP.md` for the current phase's tasks
4. Ask specific questions (reference the phase/component)

---

## Summary for Claude

**When working on this project:**

1. **Always read** `.claude/c-guidelines.md`, `.claude/clean-code.md`, and `.claude/style-guide.md` before writing code
2. **Follow naming:** `module_verb_object`, `snake_case`, `_t` suffix for types
3. **Format:** 4 spaces, 100 chars, braces on new line for functions
4. **Test-driven:** Write reference-model tests, crash tests, fuzz tests
5. **Incremental:** Each phase in `ROADMAP.md` produces working code
6. **Run sanitizers by default:** `build-asan` is the main dev build
7. **No nested structures:** Flatten with early returns, max depth 2
8. **Memory:** Use arenas, check all allocations, assert zero leaks
9. **Errors:** Check every return value, log context, fail fast
10. **Read Postgres code** when stuck on a component
11. **Small PRs:** feature PRs under 100-200 lines, 500 hard limit; ask the user for exceptions

**Code I write must:**
- Pass `clang-format` (auto-formatted)
- Pass `clang-tidy` (no warnings)
- Compile with `-Werror`
- Pass ASan + UBSan tests
- Have corresponding tests
- Follow all guidelines in `.claude/`

**This is a learning project** - prioritize correctness, testability, and clarity over performance.
