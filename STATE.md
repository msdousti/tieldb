# TielDB Development State

**Last Updated:** 2026-10-18  
**Current Phase:** Phase 0 ✅ Complete → Ready for Phase 1

---

## Quick Start on OrbStack

```bash
# SSH into OrbStack
ssh sadeq@192.168.139.68

# Navigate to project
cd ~/tieldb

# Verify everything works
git status
meson test -C build-asan
```

---

## Project Status

### Phase 0: Complete ✅

All setup tasks finished:
- ✅ Meson build system configured
- ✅ Sanitizers (ASan, UBSan, TSan) working
- ✅ Injectable I/O interface with fault injection
- ✅ cmocka test framework with 5 passing tests
- ✅ clang-format-22 and clang-tidy configured
- ✅ CI/CD pipeline on GitHub Actions (all passing)
- ✅ Pre-commit hooks installed
- ✅ Development guidelines in `.claude/` directory

### Current Commit

```
2427e27 - docs: Mark Phase 0 as complete
```

### Test Status

```
1/1 test passing (test_io)
5/5 assertions passing:
- Real I/O operations work
- Fault injection: torn write
- Fault injection: fsync fail
- Fault injection: Nth call targeting
- Fault injection: reset
```

---

## Repository Structure

```
tieldb/
├── .claude/                    # Development guidelines
│   ├── c-guidelines.md        # Modern C23, Meson, sanitizers
│   ├── clean-code.md          # Functions, naming, avoiding nesting
│   ├── design-choices.md      # 20 architectural decisions
│   ├── style-guide.md         # Naming conventions, formatting
│   └── summary.md             # Project goals and tech decisions
│
├── .github/workflows/
│   └── ci.yml                 # CI pipeline (format, lint, build+test)
│
├── scripts/
│   ├── pre-commit             # Git pre-commit hook
│   └── install-hooks.sh       # Hook installation script
│
├── src/
│   ├── common/
│   │   └── types.h            # Common types (page_id_t, lsn_t, etc.)
│   └── io/
│       ├── io.h               # Injectable I/O interface
│       └── io.c               # Real and fault-injection implementations
│
├── tests/
│   └── test_io.c              # I/O interface tests (5 tests)
│
├── include/                   # Public headers (empty for now)
│
├── meson.build                # Build configuration
├── .clang-format              # Code formatting rules (sorting disabled)
├── .clang-tidy                # Static analysis configuration
├── .gitignore                 # Git ignore patterns
├── CLAUDE.md                  # Main development guide
├── ROADMAP.md                 # Phased development plan
├── README.md                  # Project overview
└── STATE.md                   # This file - current state snapshot
```

---

## Build Directories

```bash
build-asan/     # Default dev build (ASan + UBSan)
build-tsan/     # Thread sanitizer (not created yet, for Phase 6+)
build-release/  # Optimized build (not created yet, for benchmarks)
```

**Note:** Only `build-asan` exists currently. Others will be created as needed.

---

## Git Configuration

### Email Setup (IMPORTANT!)

Git is configured to use GitHub noreply email to protect privacy:

```bash
# Already configured on OrbStack:
git config user.name "Sadeq Dousti"
git config user.email "3616518+msdousti@users.noreply.github.com"
```

**Never use:** `msdousti@gmail.com` in commits (GitHub will reject pushes)

### Pre-commit Hooks

Already installed on OrbStack. To reinstall if needed:

```bash
./scripts/install-hooks.sh
```

Hook checks:
- ✅ clang-format-22 formatting
- ✅ clang-tidy static analysis (if build-asan exists)

---

## Development Workflow

### 1. Daily Workflow

```bash
# Pull latest
git pull

# Make changes to code
vim src/module/file.c

# Build (ASan + UBSan enabled)
ninja -C build-asan

# Run tests
meson test -C build-asan

# Format code (pre-commit does this, but can run manually)
find src tests -name '*.c' -o -name '*.h' | xargs clang-format-22 -i

# Commit (pre-commit hook runs automatically)
git add .
git commit -m "feat: description

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"

# Push
git push
```

### 2. Adding New Code

**Order of operations:**

1. **Write test first** (TDD)
   ```bash
   vim tests/test_new_feature.c
   # Add to meson.build tests section
   ```

2. **Write header** (public API)
   ```bash
   vim src/module/feature.h
   ```

3. **Write implementation**
   ```bash
   vim src/module/feature.c
   # Add to meson.build sources section
   ```

4. **Build and test**
   ```bash
   ninja -C build-asan
   meson test -C build-asan -v
   ```

5. **Format and commit**
   ```bash
   git add src/module/ tests/test_new_feature.c meson.build
   git commit -m "Add feature: description"
   ```

### 3. Build System Commands

```bash
# Initial setup (already done)
meson setup build-asan -Db_sanitize=address,undefined -Dc_std=c2x

# Clean rebuild
rm -rf build-asan
meson setup build-asan -Db_sanitize=address,undefined -Dc_std=c2x

# Build
ninja -C build-asan

# Test (all)
meson test -C build-asan

# Test (verbose)
meson test -C build-asan -v

# Test (specific)
meson test -C build-asan tieldb:io

# Check test log
cat build-asan/meson-logs/testlog.txt
```

### 4. Code Quality

```bash
# Format all code
find src tests -type f \( -name '*.c' -o -name '*.h' \) \
  -exec clang-format-22 -i {} \;

# Check formatting (without changes)
find src tests -type f \( -name '*.c' -o -name '*.h' \) \
  -exec clang-format-22 --dry-run --Werror {} \;

# Run static analysis
find src -name '*.c' -exec clang-tidy-22 -p build-asan {} \;
```

---

## Key Files and Configurations

### meson.build

```meson
project('tieldb', 'c', version: '0.1.0', default_options: [
  'c_std=c2x',              # C23 standard
  'warning_level=3',        # -Wall -Wextra -Wpedantic
  'werror=true',            # All warnings are errors
])

# Extra warnings for safety
add_project_arguments([
  '-Wshadow',
  '-Wconversion',
  '-Wstrict-prototypes',
  '-Wformat=2',
], language: 'c')

# Dependencies
cmocka_dep = dependency('cmocka')

# Includes
inc = include_directories('include', 'src')
```

### .clang-format

Key settings:
- `IndentWidth: 4` (spaces, no tabs)
- `ColumnLimit: 100`
- `BreakBeforeBraces: Custom` (functions on new line, control on same line)
- `PointerAlignment: Right` (`int *ptr`)
- **`SortIncludes: false`** (manual control due to dependencies)

### .clang-tidy

Enabled checks:
- `bugprone-*`, `cert-*`, `clang-analyzer-*`
- `concurrency-*`, `misc-*`, `performance-*`
- `portability-*`, `readability-*`

Disabled pedantic checks for systems programming.

### CI Pipeline (.github/workflows/ci.yml)

5 jobs on every push/PR:
1. **format-check** - clang-format-22
2. **lint** - clang-tidy-22
3. **build-asan** - ASan + UBSan
4. **build-tsan** - TSan
5. **build-release** - Release build

All use `ubuntu-24.04` with LLVM 22.

---

## Dependencies Installed

### System Packages (Ubuntu on OrbStack)

```bash
# Already installed:
sudo apt install -y \
  build-essential \
  meson \
  ninja-build \
  clang-22 \
  clang-format-22 \
  clang-tidy-22 \
  libcmocka-dev \
  pkg-config \
  git
```

### LLVM 22 Source

```bash
# LLVM apt repository already added:
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | \
  sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc
echo "deb http://apt.llvm.org/resolute/ llvm-toolchain-resolute-22 main" | \
  sudo tee /etc/apt/sources.list.d/llvm.list
```

---

## Coding Standards (Quick Reference)

### Naming Conventions

```c
// Functions: module_verb_object
int buffer_pool_init(buffer_pool_t *pool, size_t n_buffers);
page_t *buffer_pool_get_page(buffer_pool_t *pool, page_id_t page_id);

// Variables: snake_case
int pin_count;
page_id_t victim_page_id;
bool is_dirty;

// Types: snake_case_t suffix
typedef struct buffer_pool buffer_pool_t;
typedef uint64_t page_id_t;

// Constants/Macros: UPPER_SNAKE_CASE
#define PAGE_SIZE 8192
#define MAX_BUFFER_POOL_SIZE (1024 * 1024)
```

### C23 Features to Use

```c
// nullptr instead of NULL
if (page == nullptr) { return -1; }

// bool, true, false are keywords (no #include <stdbool.h>)
bool is_dirty = true;

// [[nodiscard]] for must-check returns
[[nodiscard]] int buffer_pool_pin_page(buffer_pool_t *pool, page_id_t id);

// [[maybe_unused]] for callback parameters
static int callback([[maybe_unused]] void *arg) { }

// constexpr for compile-time constants
constexpr size_t PAGE_SIZE = 8192;

// static_assert (no message needed)
static_assert(sizeof(page_header_t) == 64);

// Empty initializer {}
page_t page = {};
```

### Code Structure

```c
// Keep functions small (<20 lines ideal)
// Avoid nesting (max depth 2)
// Use early returns

// Good:
int foo(page_t *page)
{
    if (page == nullptr) {
        return -1;
    }
    if (page->pin_count == 0) {
        return -1;
    }
    
    return do_work(page);  // main logic at left margin
}

// Always use braces, even for single statements
if (condition) {
    do_something();
}
```

---

## Next Steps: Phase 1

**Goal:** Connect with `psql`, execute hardcoded queries in memory (no persistence yet).

### Phase 1 Components

1. **Postgres wire protocol** (`src/net/protocol.{h,c}`)
   - Startup (no authentication or TLS)
   - Simple query protocol
   - Response messages

2. **SQL parser** (`src/sql/parser.{h,c}`)
   - CREATE TABLE, INSERT, SELECT *
   - Consider using `libpg_query` (Postgres parser)

3. **In-memory catalog** (`src/catalog/catalog.{h,c}`)
   - Table metadata storage
   - Hash map: table name → definition

4. **In-memory tuple storage** (`src/storage/memory.{h,c}`)
   - Array of tuples per table
   - INTEGER and TEXT types only

5. **Hardcoded executor** (`src/executor/executor.{h,c}`)
   - CREATE TABLE → add to catalog
   - INSERT → append to array
   - SELECT * → iterate and return

6. **Network server** (`src/net/server.{h,c}`)
   - Listen on port 5433
   - Accept connections
   - Per-connection thread

### Phase 1 Success Criteria

```bash
# Start server
./build-asan/tieldb

# Connect with psql
psql -h localhost -p 5433 -U test testdb

# Run queries
CREATE TABLE users (id INTEGER, name TEXT);
INSERT INTO users VALUES (1, 'Alice');
SELECT * FROM users;
```

**Estimated time:** 1-2 weeks

### Recommended Reading Before Starting

1. **Postgres Wire Protocol**
   - https://www.postgresql.org/docs/current/protocol.html
   - Focus on: startup, simple query, row description, data row

2. **SQL Parsing**
   - Consider `libpg_query`: https://github.com/pganalyze/libpg_query
   - Or hand-write minimal parser for 3 statements

3. **CMU 15-445 Lecture 1-3**
   - Database storage, buffer pool, disk manager
   - https://15445.courses.cs.cmu.edu/

---

## Troubleshooting

### Build Issues

```bash
# Clean rebuild
rm -rf build-asan
meson setup build-asan -Db_sanitize=address,undefined -Dc_std=c2x
ninja -C build-asan

# Check Meson version (should be recent)
meson --version

# Check LLVM version
clang-22 --version
clang-format-22 --version
clang-tidy-22 --version
```

### Test Failures

```bash
# Run tests verbosely
meson test -C build-asan -v

# Check test log
cat build-asan/meson-logs/testlog.txt

# Run specific test under gdb
gdb --args build-asan/tests/test_io
```

### Git Issues

```bash
# Check email configuration
git config user.email
# Should be: 3616518+msdousti@users.noreply.github.com

# If wrong, fix it:
git config user.email "3616518+msdousti@users.noreply.github.com"

# Amend last commit if email was wrong
git commit --amend --reset-author --no-edit
```

### CI Failures

Check GitHub Actions: https://github.com/msdousti/tieldb/actions

Common issues:
- Formatting: run `clang-format-22 -i` on all files
- Lint warnings: fix or add to .clang-tidy disabled checks
- Test failures: fix locally first, then push

---

## Important Notes

### Memory Management

- Every allocation needs an owner with matching free
- Use arena allocators for per-query/per-transaction cleanup (later phases)
- Check all malloc/calloc/realloc return values
- Set pointers to nullptr after free

### Error Handling

- Return 0 for success, -1 or errno for failure
- Check every return value that can fail
- Fail fast - library code never calls `exit()`

### Testing Philosophy

1. **Reference-model tests** - Most valuable
2. **Crash tests** - For durability (Phase 5+)
3. **Fuzz tests** - For parsers/deserializers
4. **Sanitizers** - Run by default (ASan + UBSan)

### Include Order

**Important:** cmocka.h must come AFTER standard headers!

```c
// Standard C headers first
#include <stddef.h>
#include <setjmp.h>
// ... other standard headers

// Testing framework
#include <cmocka.h>

// Project headers
#include "io/io.h"
```

Reason: cmocka needs `size_t` and `jmp_buf` defined.

---

## Summary

**Phase 0 is complete!** All infrastructure is in place:
- ✅ Build system working
- ✅ Tests passing
- ✅ CI/CD pipeline green
- ✅ Code quality tooling configured
- ✅ Development environment ready

**You can now:**
1. SSH into OrbStack: `ssh sadeq@192.168.139.68`
2. Navigate to project: `cd ~/tieldb`
3. Start Phase 1: Begin implementing Postgres wire protocol

**All state is preserved in:**
- Git repository (commits + history)
- This STATE.md file
- ROADMAP.md (phase checklist)
- .claude/ directory (guidelines)

**Happy coding!** 🚀
