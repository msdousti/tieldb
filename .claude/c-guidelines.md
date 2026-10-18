# Modern C Guidelines

Based on Ben Klemens, *21st Century C: C Tips from the New School*, extended with
modern C23 (the latest standard) practice and a test-driven, sanitizer-first 
workflow for systems code (e.g. database engines like Postgres, SQLite, InnoDB).

**Project defaults:** C23 standard, Meson + Ninja for builds, cmocka for tests, 
ASan/UBSan as the default dev build, Valgrind as a cross-check. No hand-written 
Makefiles, no CMake.

---

## 1. Toolchain and Build (Meson + Ninja)

- Use **Meson + Ninja** for every project. Meson has a saner syntax than CMake, first-class sanitizer support, built-in test suites (`meson test`), native `pkg-config`/dependency lookup, and generates `compile_commands.json` for clangd. Postgres itself has moved to it.
- Don't write Makefiles or CMake files. If a task seems to need one, wrap it in a Meson `custom_target`/`run_target` instead.
- Find libraries with `dependency('glib-2.0')` (Meson uses `pkg-config` under the hood) rather than hardcoding paths or flags.
- Pick a standard explicitly in `project()`: `default_options: ['c_std=c2x', 'warning_level=3', 'werror=true']`. Use `c_std=c2x` for C23 (latest standard; some Meson versions may not support `c23` yet, use `c2x`). Use `gnu2x` when you want GNU extensions alongside C23. Never rely on the compiler default.
- Warnings from day one (retrofitting `-Wconversion` later is miserable). Add via `add_project_arguments`:
  `-Wshadow -Wconversion -Wstrict-prototypes -Wformat=2` on top of Meson's `warning_level=3` (`-Wall -Wextra -Wpedantic`). Use `werror=true` in dev and CI; consider relaxing for release tarballs.
- Use `-g` always; `-O2` for release, `-Og`/`-O0` for debugging (`buildtype=debugoptimized` / `debug`).
- Keep several build directories side by side:

| Dir | Setup | Use |
|---|---|---|
| `build-asan` | `meson setup build-asan --native-file clang.ini -Db_sanitize=address,undefined -Dc_args=-fno-omit-frame-pointer` | **default** for day-to-day work and tests |
| `build-tsan` | `meson setup build-tsan --native-file clang.ini -Db_sanitize=thread` | once concurrency exists (TSan can't combine with ASan) |
| `build-release` | `meson setup build-release --native-file clang.ini --buildtype=release` | benchmarks only |
| `build-fuzz` | `meson setup build-fuzz --native-file clang.ini` with `-fsanitize=fuzzer,address` in `c_args`/`c_link_args` | fuzz targets (Meson's `b_sanitize` has no `fuzzer` value, so pass the flag directly) |
| `build-cov` | `meson setup build-cov --native-file clang.ini -Db_coverage=true` | coverage reports (`ninja coverage`) |

- Minimal skeleton:

  ```meson
  project('tieldb', 'c',
    default_options: ['c_std=c2x', 'warning_level=3', 'werror=true'])
  add_project_arguments(['-Wshadow', '-Wconversion', '-Wstrict-prototypes', '-Wformat=2'],
                        language: 'c')
  # Dependencies (example)
  # glib = dependency('glib-2.0')
  
  # Library
  tieldb_lib = static_library('tieldb', files('src/buffer_pool.c', 'src/page.c'))
  
  # Tests
  cmocka = dependency('cmocka')
  test_buffer = executable('test_buffer_pool', 'tests/test_buffer_pool.c', 
                           link_with: tieldb_lib, dependencies: cmocka)
  test('buffer_pool', test_buffer)
  ```

- Use `gdb`/`lldb` for debugging, `gcov`/`llvm-cov` (via `b_coverage`) for coverage, `perf`/`gprof` for profiling.
- Keep the project in git. Document the public header with Doxygen-style comments.

## 2. Use the Ecosystem; Don't Reinvent

- Prefer well-tested libraries over custom code for:
  - containers and utilities: GLib
  - JSON: jansson, cJSON, json-c
  - XML: libxml2
  - SQL: SQLite
  - HTTP: libcurl
  - Unicode: ICU, or GLib's UTF-8 functions
  - Regex: PCRE2
  - Statistics and math: GSL
- Linking is cheap; maintaining your own hash map or parser is not. (Exception: when building the data structure *is* the project, e.g. a B+tree in a DBMS. Then test it against a reference model; see section 9.)
- POSIX is the practical baseline: `getopt`/`getopt_long`, `mmap`, `strdup`, `strndup`, `asprintf`, `popen`, `fork`/`exec`.
- Wrap external libraries in a thin layer of your own so you can swap them out.

## 3. Language Features (C99 and Later)

### Declarations and types
- Declare variables where first used and in the narrowest scope. Declare loop counters inline: `for (int i = 0; i < n; i++)`.
- Use `<stdint.h>` fixed-width types (`uint8_t`, `int64_t`) when width matters, `size_t` for sizes and indexes, `ptrdiff_t` for differences, `intptr_t`/`uintptr_t` for pointer-integers.
- Use `<stdbool.h>`. In C23, `bool`, `true`, and `false` are keywords.
- Use `const` aggressively, on pointer targets and on parameters you don't modify.
- Use `restrict` only where you can guarantee no aliasing.
- Use `static` on file-local functions and variables. Anything not in a header should be `static`.
- Use `enum` (or `static const`) instead of `#define` for integer constants.

### Initializers
- Use designated initializers: `struct cfg c = {.timeout = 30, .verbose = true};`. Unmentioned fields are zeroed.
- Zero-initialize with `= {0}` (or `= {}` in C23).
- Designated array initializers also work: `int a[] = {[3] = 1, [7] = 2};`.

### Compound literals
- Pass anonymous structs or arrays inline: `draw(&(point){.x = 1, .y = 2});`
- Pass inline arrays: `sum((int[]){1, 2, 3}, 3);`
- Lifetime: a compound literal at block scope lives until the end of the enclosing block. Never return a pointer to one.

### Variadic and named-argument tricks
- Use variadic macros (`__VA_ARGS__`) to implement optional and named arguments. The standard pattern is a struct of options, a function taking the struct, and a macro that builds it:

  ```c
  typedef struct { double a; double b; int n; } f_args;
  double f_base(f_args in);
  #define f(...) f_base((f_args){.a = 1, .b = 2, .n = 10, __VA_ARGS__})
  // f(.n = 5);  f();  f(.b = 7, .a = 3);
  ```

- Later designators override earlier ones, so defaults go first and user arguments come last.
- Count arguments with `sizeof((type[]){__VA_ARGS__}) / sizeof(type)` to get safe variadic arrays.
- Variadic C functions (`va_list`) are not type safe. Prefer the struct or array patterns above.

### C11 and C23 features
- `_Static_assert(cond, "msg")` (`static_assert` in C23) for compile-time checks, e.g. struct sizes and on-disk layouts.
- `_Generic` for type-based dispatch in macros.
- `_Alignas`/`_Alignof`, `_Noreturn`, `_Thread_local`, `_Atomic`, `<threads.h>`, `<stdatomic.h>`.
- `typeof` (GNU extension, standard in C23) for type-safe macros.
- C23 adds `nullptr`, `constexpr`, `#embed`, `[[nodiscard]]`, `[[maybe_unused]]`, `[[deprecated]]`, and `auto` type inference.
- Prefer `[[nodiscard]]` (or `__attribute__((warn_unused_result))`) on functions whose error returns must be checked.

### Strings
- Prefer `snprintf` to `sprintf`. Never use `gets`. Avoid `strcpy`, `strcat`, and `strncpy`; `strncpy` does not guarantee NUL termination.
- Use `asprintf`, `strdup`, and `strndup` (POSIX or GNU) for allocated strings. Check their return values.
- Use `strtol`/`strtod` instead of `atoi`/`atof`, and check `errno` and `endptr`.
- Treat strings as UTF-8 bytes. Use a library for anything beyond ASCII (case folding, normalization, width).
- Pass a length with every buffer: `(const char *s, size_t len)`.

## 4. Macros and Preprocessor

- Prefer `static inline` functions to macros. Use macros only for things functions can't do (type genericity, `__FILE__`/`__LINE__`, control flow, token pasting).
- Parenthesize macro arguments and the whole expression. Wrap multi-statement macros in `do { ... } while (0)`.
- Never evaluate a macro argument twice if it may have side effects. Use `typeof` temporaries to avoid this.
- Use `#pragma once` or include guards in headers. Headers must be self-contained and include only what they need.
- A common pattern is an error-checking macro that logs the location and jumps or returns:

  ```c
  #define Stopif(cond, action, ...) \
      do { if (cond) { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); action; } } while (0)
  // Stopif(!p, return -1, "malloc failed in %s", __func__);
  ```

- Use `#ifdef` conditionals sparingly; isolate platform code in separate files (select them in `meson.build`).

## 5. Memory and Ownership

- Every allocation has exactly one owner, who is responsible for freeing it. Document ownership in the function comment or the name (`take`, `borrow`, `new_`, `_free`).
- Pass borrowed pointers as `const T *`. Never transfer ownership implicitly.
- Encapsulate allocation in constructor and destructor pairs: `foo_create()` / `foo_destroy()`. The destructor should accept `NULL`.
- Minimize raw `malloc`/`free` in application code. Allocate in one place.
- **Add an arena / memory-context allocator early** (cf. Postgres `palloc`/`MemoryContext`). Per-query or per-operation arenas make cleanup trivial and remove most leak and double-free risk.
- Set the pointer to `NULL` after `free`. A `#define FREE(p) do { free(p); (p) = NULL; } while (0)` helps.
- Always check the return value of `malloc`/`calloc`/`realloc`.
  - `p = realloc(p, n)` leaks on failure; assign to a temporary first.
  - Use `calloc` or `reallocarray` for array allocations to avoid overflow in `count * size`.
- Prefer `p = malloc(n * sizeof *p)` to `sizeof(type)`. It stays correct when the type changes.
- Don't cast the result of `malloc` in C.
- Use `goto cleanup;` for single-exit resource release in functions with multiple resources, or GNU `__attribute__((cleanup(fn)))` if GNU C is acceptable.
- Avoid VLAs for anything large or user-sized (stack overflow risk). They are optional since C11.
- Never use uninitialized memory. Use `memset`/`calloc` or an initializer.
- Beware of undefined behavior: signed overflow, out-of-bounds access, null dereference, strict aliasing violations, use-after-free, bad shifts, unsequenced modifications. Use `memcpy` for type-punning rather than pointer casts, and unsigned arithmetic for wraparound. This matters most for byte-level page and tuple code.
- **Make leaks structural:** at teardown, assert that every arena, pool/pin count, and open file handle is back to zero. A buffer-pool pin leak is the DBMS version of a memory leak, and ASan will not see it.

## 6. Interface and Module Design

### Headers and encapsulation
- One header per module (`foo.h`) declaring the public API, with an implementation file `foo.c`. Keep internals `static` in the `.c` file.
- Use opaque types for encapsulation: `typedef struct foo foo;` in the header and the struct definition in the `.c` file. Callers can only hold `foo *`.
- Every stateful module type is opaque, including readers, cursors, and iterators: callers get a `foo_create` / `foo_destroy` pair, never the struct layout. Plain data structs (`tuple_id_t`), options structs, and vtables are public. Any other exception needs the user's approval and an entry in `design-choices.md`.
- Prefix every public symbol with the module name (`foo_open`, `foo_close`), since C has no namespaces.
- Extern globals should be rare. Pass state through a context struct instead.

### Object-style patterns
- Group data and operations: a struct plus `foo_*` functions taking the struct pointer as first argument.
- Vtables are for places with more than one implementation or a need for injection (I/O, storage engines, callbacks). A concrete type with a single implementation, like a byte buffer, is an opaque handle plus prefixed functions, with no vtable.
- Use a **vtable of function pointers** for swappable backends (e.g. a storage engine interface, as in Postgres's table access method API) and for callbacks. Always pair a callback with a `void *user_data` argument.
- Use `typedef` for function-pointer types to keep declarations readable.
- Use `struct` returns for multiple values (small structs are cheap to return), rather than a pile of out-pointers.

### Design for testability
- Route **all file I/O through a small interface** (`read_page`, `write_page`, `fsync`) so tests can inject faults: torn writes, dropped fsyncs, failure at the Nth call. This is how real databases test durability. Prefer it over linker-level mocking for fault injection.
- Inject other nondeterminism the same way (clock, RNG, allocator).
- Every module has a `foo_create` / `foo_destroy` pair so tests can build and tear down in isolation.

### Error handling
- Pick one convention per project and document it:
  - return `0` for success and a negative or `errno`-style code for failure
  - return a pointer or `NULL`
  - return a result struct, e.g. `struct { T value; int error; }`
  - set `errno` for POSIX-style APIs
- **TielDB convention:** return `0` on success; on failure return `-1` (or `nullptr` from `foo_create`) and set `errno` (`EINVAL`, `ENOMEM`, `EOVERFLOW`, ...). The header documents which `errno` values each module can produce, and tests assert them.
- Provide `foo_strerror()` or similar so callers can print errors.
- Don't silently ignore errors. Don't call `exit()` from library code; return an error.
- Validate arguments at public API boundaries with `assert` for programmer errors and error returns for runtime errors. Remember that `assert` disappears under `NDEBUG`.

### API ergonomics
- Sensible defaults. Callers shouldn't need to fill out ten parameters (use the options-struct pattern from section 3).
- Group related parameters into a struct once there are more than ~4.
- Make the correct use easy and misuse hard: distinct types for distinct concepts (wrap an `int` in a one-field struct when mixing them up would hurt), enums for modes instead of boolean flags.
- Keep functions short with one purpose. Avoid global mutable state.

## 7. Concurrency

- Use OpenMP (`#pragma omp parallel for`) for data-parallel loops with minimal code change.
- Use pthreads or C11 `<threads.h>` for general threading.
- Protect shared data with mutexes or `_Atomic`. Avoid `volatile` as a synchronization mechanism; it does not provide it.
- Keep thread-local state in `_Thread_local` variables or in per-thread context structs.
- Run `build-tsan` as soon as a second thread exists.

## 8. Testing (TDD in C)

C is a fine choice for TDD; it is less ceremonious than it sounds.

- **Framework: cmocka** (maintained, fixtures, group tests, mocking via the linker's `--wrap`). Wire every test binary into Meson with `test()` and run with `meson test -C build-asan`.
  - Alternatives: `utest.h`/`greatest` (single-header, zero dependencies), Check (forks each test, isolating segfaults), Unity (embedded-oriented, less pleasant here).
  - A plain `assert`-based test binary per module is acceptable for small projects.
  - The framework matters less than the techniques below. Most valuable tests are plain C programs.
- **Failure paths:** test allocation failure, bad input, and I/O errors, not just the happy path.
- **Reference-model testing (most valuable):** run random operations against the real structure (B+tree, slotted page, buffer pool, MVCC visibility) and against a trivial model (sorted array or hash map). Assert the two agree after every step. A few hundred lines of C give you property-based testing. Log the RNG seed so failures are reproducible.
- **Fault injection:** use the injectable I/O interface from section 6 to simulate torn writes, dropped fsyncs, and failure at the Nth call.
- **Crash testing:** `fork()`, run a workload in the child, `kill -9` it at a random point, then recover in the parent and check invariants. This is the main test for a write-ahead log (WAL).
- **Fuzzing:** libFuzzer or AFL++ on every parser or deserializer: page deserializer, wire protocol parser, WAL replay. Build in `build-fuzz`.
- **Coverage:** check it with `build-cov`.
- Run the whole suite under ASan + UBSan by default (`build-asan`).

## 9. Memory and Leak Checking: Sanitizers First, Valgrind Second

- **ASan** (`-Db_sanitize=address`) is the main tool: about 2x slowdown (Valgrind is 20-50x). Catches heap, stack, and global overflows, use-after-free, double-free. LeakSanitizer is included on Linux and reports at process exit.
- **UBSan** (`undefined`) catches signed overflow, misaligned access, bad shifts. The shift and alignment checks matter for page and tuple byte manipulation.
- **MSan** (`memory`, clang only) finds uninitialized reads but needs all code, including dependencies, instrumented. Valgrind memcheck covers that gap more easily.
- **TSan** (`thread`) for concurrency phases, in its own build dir.
- **Valgrind** (`--leak-check=full`) as a periodic cross-check, especially for uninitialized memory and `massif` (heap profiling). Run it on a non-sanitizer build occasionally, not daily.
- **LeakSanitizer is flaky outside Linux** (notably macOS on Apple Silicon). Run leak checks in a Linux container or VM when developing on a Mac.
- Use `-fno-omit-frame-pointer` in sanitizer builds for readable stack traces.

## 10. Static Analysis and Formatting

- Run `clang-tidy`, `cppcheck`, `scan-build`, or GCC's `-fanalyzer` in a periodic script or CI job (use Meson's `compile_commands.json`).
- Format with `clang-format` and a checked-in `.clang-format`, so style is never a discussion.
- Review compiler warnings as part of every change; with `werror=true` they block the build.

## 11. Legacy Habits to Drop

| Old habit | Modern replacement |
|---|---|
| All declarations at top of block | Declare at first use, in narrow scope |
| `int i; for (i = 0; ...)` | `for (int i = 0; ...)` |
| `char`/`int`/`long` for sized data | `<stdint.h>` fixed-width types |
| Positional struct init | Designated initializers |
| Temp variable to pass a struct | Compound literal |
| `#define N 10` | `enum` or `static const` |
| Function-like macro for everything | `static inline` function |
| `gets`, `strcpy`, `sprintf` | `fgets`, `snprintf`, explicit lengths |
| `atoi` | `strtol` with error checks |
| Hand-written list/hash/JSON | Library |
| Hand-written Makefile / CMake | Meson + Ninja + `dependency()` |
| Many positional parameters | Options struct plus variadic-macro wrapper |
| Casting `malloc` | No cast; `sizeof *p` |
| Early `return` that leaks resources | `goto cleanup` |
| Globals | Context structs |
| `void*` everywhere | `_Generic`, typed wrappers |
| Scattered `malloc`/`free` | Arena / memory-context allocator |
| Direct `read`/`write`/`fsync` calls | Injectable I/O interface |

## 12. Suggested Repo Layout

```
meson.build
src/storage/   src/buffer/   src/wal/   src/net/
tests/         (cmocka unit tests, reference-model tests, crash tests)
fuzz/          (libFuzzer targets)
.clang-format
```

Start with the I/O interface with fault injection, then a first cmocka test for the lowest-level structure (e.g. the slotted page).

## 13. Quick Checklist

**Build and quality**
- [ ] Meson + Ninja only; `c_std` set; warnings on and `werror=true`
- [ ] `build-asan` is the default; `build-tsan`, `build-release`, `build-fuzz`, `build-cov` available
- [ ] `clang-format` config committed; `clang-tidy`/`cppcheck` run periodically
- [ ] Leak checks run on Linux (container/VM on macOS)

**Code**
- [ ] Every allocation has one owner and a matching free; pointers nulled after free
- [ ] Arena allocator in place; teardown asserts zero outstanding arenas, pins, file handles
- [ ] All return values that can fail are checked
- [ ] Public API prefixed, documented, opaque where possible
- [ ] Internals `static`; headers self-contained with guards
- [ ] No unbounded string functions; every buffer carries a length
- [ ] Options-struct pattern for functions with optional parameters
- [ ] Stateful module types are opaque; vtables only for swappable implementations
- [ ] Every failure path sets `errno`, documented in the header and asserted in tests
- [ ] At most 4 parameters per function (clang-tidy enforces it); use an options struct beyond that

**Tests**
- [ ] All I/O behind an injectable interface
- [ ] Reference-model randomized test per data structure (seed logged)
- [ ] Fuzz targets for every parser/deserializer
- [ ] Crash test (`fork` + `kill -9` + recover + invariant check) for the WAL
- [ ] Failure paths tested, not just the happy path
