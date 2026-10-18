# TielDB - Educational Database Management System

[![CI](https://github.com/msdousti/tieldb/actions/workflows/ci.yml/badge.svg)](https://github.com/msdousti/tieldb/actions/workflows/ci.yml)

A learning-focused DBMS implementation built from scratch in modern C, modeled after PostgreSQL and MariaDB.

## Project Status

**Current Phase:** Phase 0 - Project Setup

See [ROADMAP.md](ROADMAP.md) for the complete development plan.

## Goals

- Learn database internals by building a working DBMS
- Incremental, test-driven development
- Connect with `psql` using Postgres wire protocol
- Extensible architecture supporting multiple storage engines

## Quick Start

**Prerequisites:**
- Linux environment (OrbStack VM recommended for macOS users)
- Meson build system, Ninja, Clang
- cmocka testing framework

**Build** (TielDB builds with clang 22 only; `clang.ini` selects it):
```bash
meson setup build-asan --native-file clang.ini -Db_sanitize=address,undefined
ninja -C build-asan
meson test -C build-asan
```

**Install Git Hooks** (recommended):
```bash
./scripts/install-hooks.sh
```
Enables automatic format checking and linting before commits.

## Documentation

- **[CLAUDE.md](CLAUDE.md)** - Main development guide
- **[ROADMAP.md](ROADMAP.md)** - Phased implementation plan
- **[.claude/](.claude/)** - Design guidelines and coding standards
  - [Project Summary](.claude/summary.md)
  - [C Coding Guidelines](.claude/c-guidelines.md)
  - [Clean Code Principles](.claude/clean-code.md)
  - [Style Guide](.claude/style-guide.md)
  - [Design Choices](.claude/design-choices.md)
- **[docs/FORMATTING.md](docs/FORMATTING.md)** - Code formatting and linting

## Architecture

**First Implementation:**
- Multithreading (not multiprocess)
- Heap storage with slotted pages
- Redo-only WAL
- Strict 2PL concurrency control
- B+tree indexes
- Postgres wire protocol

**Future Extensions:**
- LSM tree storage engine
- MVCC (Multi-Version Concurrency Control)
- ARIES recovery
- Cost-based query optimizer

## Development Principles

1. **Working code at every step** - never broken
2. **Test-driven** - reference models, crash tests, sanitizers
3. **Incremental** - build features one at a time
4. **Extensible** - abstraction layers for swappable implementations
5. **Learn from the real world** - Postgres and SQLite as reference

## Testing

All code runs under AddressSanitizer and UndefinedBehaviorSanitizer by default:

```bash
# Unit tests
meson test -C build-asan

# Thread sanitizer (for concurrent code)
meson setup build-tsan --native-file clang.ini -Db_sanitize=thread
meson test -C build-tsan

# Coverage
meson setup build-cov --native-file clang.ini -Db_coverage=true
meson test -C build-cov
ninja coverage -C build-cov
```

## Coding Standards

- **Language:** Modern C (C23, latest standard)
- **Build:** Meson + Ninja
- **Style:** 4-space indent, 100-char lines, `snake_case`
- **Testing:** cmocka, reference models, crash tests, fuzzing
- **Enforcement:** clang-format, clang-tidy, -Werror

See [.claude/style-guide.md](.claude/style-guide.md) for complete style rules.

## Resources

- **Courses:** [CMU 15-445](https://15445.courses.cs.cmu.edu/) Database Systems
- **Books:**
  - *Database Internals* by Alex Petrov
  - *Database Management Systems* by Ramakrishnan & Gehrke
  - *Transaction Processing* by Gray & Reuter
- **Source Code:**
  - [PostgreSQL](https://github.com/postgres/postgres) (heapam, nbtree, xlog)
  - [SQLite](https://sqlite.org/src/doc/trunk/README.md) (well-commented)

## Timeline

**Estimated:** 4-6 months part-time (10-20 hours/week)

See [ROADMAP.md](ROADMAP.md) for detailed phase breakdown.

## License

Educational use only. Not intended for production.
