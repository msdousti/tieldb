# TielDB Style Guide

Enforced via `.clang-format` and `.clang-tidy`. All code must pass `clang-format` and compile with `-Werror`.

---

## Naming Conventions

### Functions
**Pattern:** `module_verb_object` in `snake_case`, prefixed by module name.

```c
// Good
int buffer_pool_init(buffer_pool_t *pool, size_t n_buffers);
page_t *buffer_pool_get_page(buffer_pool_t *pool, page_id_t page_id);
void wal_write_record(wal_t *wal, const wal_record_t *record);

// Bad
int InitBufferPool(buffer_pool_t *pool, size_t n_buffers);  // wrong case
int get_page(buffer_pool_t *pool, page_id_t page_id);       // no module prefix
```

**Static (file-local) functions:** Same convention, marked `static`.

```c
static int find_victim_page(buffer_pool_t *pool);
```

### Variables
**Pattern:** `snake_case`, descriptive.

```c
// Good
int pin_count;
page_id_t victim_page_id;
bool is_dirty;

// Bad
int pinCnt;      // wrong case
int pc;          // too short
int n;           // meaningless (except loop counters)
```

**Loop counters:** Single letter (`i`, `j`, `k`) only in short scopes (<10 lines).

### Types
**Pattern:** `snake_case_t` suffix.

```c
typedef struct buffer_pool buffer_pool_t;
typedef struct page page_t;
typedef uint64_t page_id_t;
typedef uint64_t lsn_t;  // log sequence number

typedef enum {
    PAGE_STATUS_CLEAN,
    PAGE_STATUS_DIRTY,
    PAGE_STATUS_WRITING
} page_status_t;
```

**Struct definitions:** Name the struct tag the same as the typedef (minus `_t`).

```c
typedef struct buffer_pool {
    page_t *pages;
    size_t n_pages;
    // ...
} buffer_pool_t;
```

### Constants and Macros
**Pattern:** `UPPER_SNAKE_CASE`.

```c
#define PAGE_SIZE 8192
#define MAX_BUFFER_POOL_SIZE (1024 * 1024)

enum {
    DEFAULT_N_BUFFERS = 1000,
    MIN_PIN_COUNT = 0
};

static const size_t WAL_SEGMENT_SIZE = 16 * 1024 * 1024;
```

### Enumerators
**Pattern:** `MODULE_ENUM_VALUE` in `UPPER_SNAKE_CASE`, prefixed.

```c
typedef enum {
    BUFFER_POOL_STATUS_OK,
    BUFFER_POOL_STATUS_FULL,
    BUFFER_POOL_STATUS_ERROR
} buffer_pool_status_t;

typedef enum {
    PAGE_STATUS_CLEAN,
    PAGE_STATUS_DIRTY,
    PAGE_STATUS_WRITING
} page_status_t;
```

### Files
**Pattern:** `module.{c,h}` in lowercase with underscores.

```
src/
  buffer_pool.h
  buffer_pool.c
  page.h
  page.c
  wal.h
  wal.c
tests/
  test_buffer_pool.c
  test_page.c
  test_wal.c
```

---

## Indentation and Formatting

### Indentation
**4 spaces, no tabs.** Rationale: readable without being too wide, consistent across editors.

```c
int buffer_pool_init(buffer_pool_t *pool, size_t n_buffers) {
    pool->pages = calloc(n_buffers, sizeof(page_t));
    if (pool->pages == NULL) {
        return -1;
    }
    pool->n_pages = n_buffers;
    return 0;
}
```

### Line Length
**100 characters maximum.** Rationale: modern screens can handle it; 80 is too restrictive for descriptive names.

Break long lines at logical points:
```c
// Good
int result = buffer_pool_evict_page(pool, victim_page_id, 
                                     /* force */ true);

// Also good (break before operator)
if (page->pin_count == 0 
    && page->status == PAGE_STATUS_CLEAN 
    && page->lsn < checkpoint_lsn) {
    evict_page(page);
}
```

### Braces
**K&R style** with modifications:

1. **Functions:** Opening brace on **new line** (Allman-style for functions only).
```c
int buffer_pool_init(buffer_pool_t *pool, size_t n_buffers)
{
    // body
}
```

2. **Control structures:** Opening brace on **same line** (K&R).
```c
if (condition) {
    // body
}

for (int i = 0; i < n; i++) {
    // body
}

while (running) {
    // body
}
```

3. **Always use braces,** even for single statements (safety, clarity).
```c
// Good
if (page == NULL) {
    return -1;
}

// Bad
if (page == NULL)
    return -1;
```

4. **else/else if** on same line as closing brace:
```c
if (condition) {
    // ...
} else if (other) {
    // ...
} else {
    // ...
}
```

### Pointer Declarations
**Asterisk with the variable name,** not the type.

```c
// Good
page_t *page;
const char *name;
void *data;

// Bad
page_t* page;    // misleading: looks like page_t* is the type
page_t * page;   // extra space
```

Multiple pointers on one line (discouraged, but if needed):
```c
char *p, *q;  // not char* p, q; which makes q an int
```

### Spacing
- **After keywords:** `if (`, `while (`, `for (`, `switch (`
- **No space after function names:** `foo(args)`
- **No space inside parentheses:** `(a + b)`, not `( a + b )`
- **Space around binary operators:** `a + b`, `x == y`, `ptr->field`
- **No space around unary operators:** `!flag`, `*ptr`, `&var`, `++i`
- **Comma followed by space:** `foo(a, b, c)`

### Blank Lines
- **One blank line** between function definitions.
- **No blank line** between a comment and the thing it describes.
- **One blank line** between conceptual sections within a function (sparingly).

---

## Structure and Organization

### Header Guards
Use `#pragma once` (simpler, faster, supported by all modern compilers).

```c
// buffer_pool.h
#pragma once

#include <stddef.h>
#include <stdint.h>

// declarations
```

### Include Order
1. System headers (`<stdio.h>`, `<stdlib.h>`, ...)
2. Blank line
3. Project headers (`"buffer_pool.h"`, ...)

Within each group, alphabetically:
```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "buffer_pool.h"
#include "page.h"
#include "wal.h"
```

### Declaration Order in Headers
1. `#pragma once`
2. Includes
3. Forward declarations (if needed)
4. Constants and macros
5. Type definitions (`typedef`, `enum`, `struct`)
6. Function declarations

```c
#pragma once

#include <stddef.h>

// Forward declarations
typedef struct buffer_pool buffer_pool_t;

// Constants
#define DEFAULT_POOL_SIZE 1000

// Types
typedef struct page {
    // ...
} page_t;

// Functions
buffer_pool_t *buffer_pool_create(size_t n_buffers);
void buffer_pool_destroy(buffer_pool_t *pool);
```

### Function Order in Implementation Files
1. File-level static variables (if any, should be rare)
2. Forward declarations of static functions
3. Static helper functions
4. Public API functions

Keep related functions together. Place helpers near their callers.

---

## Comments

**Default to no comments.** Code should be self-explanatory.

### When to Comment

**Do:**
- Explain *why* (intent), not *what* (code already shows that)
- Warn about non-obvious side effects or requirements
- Document public API in headers (Doxygen-style for functions, brief for others)

```c
/**
 * Pin a page in the buffer pool, preventing eviction.
 * Returns NULL if the page cannot be loaded.
 */
page_t *buffer_pool_pin_page(buffer_pool_t *pool, page_id_t page_id);
```

**Don't:**
- Restate the code: `i++; // increment i`
- Leave commented-out code (delete it; git remembers)
- Write changelog comments (use git log)

---

## Control Flow

### Early Returns
Prefer early returns over deep nesting:
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
    
    // main logic at left margin
    return do_work(page);
}

// Bad
int foo(page_t *page)
{
    if (page != NULL) {
        if (page->pin_count > 0) {
            // main logic deeply nested
            return do_work(page);
        }
    }
    return -1;
}
```

### Loop Continue
Use `continue` to flatten nested loops:
```c
// Good
for (int i = 0; i < n; i++) {
    if (!is_candidate(&pages[i])) {
        continue;
    }
    
    process_page(&pages[i]);
}

// Bad
for (int i = 0; i < n; i++) {
    if (is_candidate(&pages[i])) {
        process_page(&pages[i]);
    }
}
```

### Avoid goto Except for Cleanup
`goto` is acceptable **only** for single-exit cleanup:
```c
int foo(void)
{
    char *buf = malloc(SIZE);
    FILE *fp = fopen("data", "r");
    int result = -1;
    
    if (buf == NULL) {
        goto cleanup;
    }
    if (fp == NULL) {
        goto cleanup;
    }
    
    // work
    result = 0;
    
cleanup:
    free(buf);
    if (fp) {
        fclose(fp);
    }
    return result;
}
```

---

## Enforcement

### clang-format
Run on every file before commit:
```bash
clang-format -i src/**/*.c src/**/*.h tests/**/*.c
```

Configured in `.clang-format` (see below).

### clang-tidy
Run periodically (in CI):
```bash
clang-tidy src/*.c -- -Isrc
```

Configured in `.clang-tidy` (see below).

### Build with -Werror
All warnings are errors. Fix them, don't suppress them.

---

## Quick Checklist

- [ ] All names are `snake_case` (functions, variables, types with `_t`)
- [ ] Constants and macros are `UPPER_SNAKE_CASE`
- [ ] Functions prefixed with module name: `module_verb_object`
- [ ] 4-space indentation, no tabs
- [ ] Lines ≤ 100 characters
- [ ] Opening brace on new line for functions, same line for control structures
- [ ] Always use braces, even for single statements
- [ ] Pointer asterisk with name: `type *name`
- [ ] `#pragma once` in all headers
- [ ] Includes: system, then blank line, then project (alphabetical in each)
- [ ] Comments explain *why*, not *what*
- [ ] Early returns, avoid deep nesting
- [ ] `clang-format -i` before commit
