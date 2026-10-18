# Clean Code Principles for Systems Programming

Adapted from Robert C. Martin's *Clean Code* for C systems programming, particularly database engines. These principles make code easier to read, understand, maintain, and extend.

---

## 1. Meaningful Names

Names appear everywhere: variables, functions, types, files. Good names eliminate confusion.

- **Intention-revealing:** A name should tell you *why* it exists, *what* it does, and *how* it's used. If it needs a comment to explain it, the name is wrong.
  ```c
  // Bad
  int d;  // elapsed time in days
  
  // Good
  int elapsed_days;
  ```

- **Pronounceable and searchable:** You can't discuss `DtaRcrd102` in a code review. Single letters are only acceptable as loop counters in short scopes. Use named constants so `grep` finds them.

- **Avoid disinformation:** Don't call something `page_list` when it's an array. Don't use names that differ by one character (`buffer_mgr` vs `buffer_mngr`).

- **Use domain terms:** Prefer `lsn` (log sequence number) over `log_pos`, `pin_count` over `ref_cnt` when that's what the literature calls it.

---

## 2. Functions

Functions are the building blocks. Clean functions make the whole system legible.

### Do one thing
If a function's name has "and" in it, it does too much. Extract the pieces.

```c
// Bad: does three things
int process_page_and_update_lru_and_log(page_t *p);

// Good: one thing each
int process_page(page_t *p);
void update_lru(page_t *p);
void log_page_access(page_t *p);
```

### Keep them small
**Rarely exceed 20 lines.** If you can see the whole function on one screen, you can hold its logic in your head. Long functions hide bugs.

### Avoid nesting (critical for readability)
**Nested structures—loops inside loops, ifs inside loops, ifs inside ifs—make code hard to follow and test.** Flatten with early returns, continue/break, or extraction.

```c
// Bad: triple nesting
for (int i = 0; i < n_buffers; i++) {
    if (buffers[i].pin_count == 0) {
        if (buffers[i].dirty) {
            if (flush_page(&buffers[i]) != 0) {
                return -1;
            }
        }
    }
}

// Good: flat, early continue
for (int i = 0; i < n_buffers; i++) {
    if (buffers[i].pin_count != 0) continue;
    if (!buffers[i].dirty) continue;
    
    if (flush_page(&buffers[i]) != 0)
        return -1;
}

// Even better: extract the loop body
for (int i = 0; i < n_buffers; i++) {
    if (try_evict_buffer(&buffers[i]) != 0)
        return -1;
}
```

For nested conditions, invert and return early:
```c
// Bad
int foo(page_t *p) {
    if (p != NULL) {
        if (p->lsn > checkpoint_lsn) {
            if (write_wal_record(p) == 0) {
                // actual work here, deeply nested
                return 0;
            }
        }
    }
    return -1;
}

// Good
int foo(page_t *p) {
    if (p == NULL) return -1;
    if (p->lsn <= checkpoint_lsn) return -1;
    if (write_wal_record(p) != 0) return -1;
    
    // actual work here, at the left margin
    return 0;
}
```

### Descriptive names
A long name beats a short enigmatic one or a comment. `validate_page_checksum` is clearer than `chk_pg`.

### Minimize arguments
**0 arguments (niladic)** is ideal. **1 (monadic)** or **2 (dyadic)** are acceptable. **3 (triadic)** needs justification. More than 3 → use an options struct (see `c-guidelines.md`).

### No side effects
If `buffer_pin(page_id)` secretly increments a global counter that `buffer_evict` must later decrement, that's a side effect. Make ownership and state changes explicit.

---

## 3. Comments

**Default to no comments.** Write code so clear that comments are unnecessary. When you must comment, explain *why*, never *what*.

### Good comments
- **Intent:** "We must flush before the checkpoint marker, not after, because recovery replays from the marker."
- **Amplification:** "This pause looks silly but prevents a rare starvation case in the clock sweep."
- **TODO/FIXME** with a ticket number or date.

### Bad comments
- **Mumbling:** "// increment i" next to `i++`.
- **Redundant:** `// Constructor` above a constructor.
- **Misleading:** comment says one thing, code does another.
- **Commented-out code:** delete it; git remembers.
- **Changelog comments** at the top of every file: that's what `git log` is for.

---

## 4. Formatting

Formatting is communication. Inconsistent style is noise.

- **Vertical density:** Related code stays close. Separate concepts with blank lines. A function and its helpers should be near each other in the file.
- **Indentation:** Consistent indentation makes structure visible. Use `clang-format` and never argue about style again (see `c-guidelines.md`).
- **Team rules:** Pick one style. Stick to it. Consistency beats personal preference.

---

## 5. Error Handling

Errors are facts of life. Handle them without obscuring the happy path.

### Pick one convention
- Return `0` for success, `-1` or `errno` for failure
- Return a pointer or `NULL`
- Return a result struct: `struct { int error; page_t *page; }`

Document it. Apply it everywhere.

### Don't ignore errors
Check every `malloc`, `read`, `write`, `fsync`. Ignoring errors turns a recoverable glitch into silent corruption.

### Don't return `NULL` casually
Returning `NULL` forces every caller to check. Prefer a result struct or an out-parameter, and reserve `NULL` for truly exceptional cases.

### Provide context
```c
// Bad
return -1;

// Good
log_error("Failed to write WAL record at LSN %lu: %s", lsn, strerror(errno));
return -1;
```

### Fail fast
If the buffer pool can't initialize, don't limp along. Return an error from `buffer_pool_create` and let the caller decide. Library code should never call `exit()`.

---

## 6. Testing (F.I.R.S.T.)

Clean code is testable code. Without tests, every change is a potential bug.

- **Fast:** Tests run in milliseconds. If they're slow, you won't run them.
- **Independent:** Test B must pass even if you skip test A. No shared state between tests.
- **Repeatable:** Same input, same output, every time, on any machine. No flaky tests. Log the RNG seed.
- **Self-Validating:** Pass or fail, no manual log inspection. Use `assert` or the test framework's macros.
- **Timely:** Write tests *before* the production code (TDD). It forces you to think about the interface and keeps the code modular.

---

## 7. The Boy Scout Rule

> *"Leave the campground cleaner than you found it."*

Every time you touch a file, leave it a little better: rename a confusing variable, extract a nested block, delete a dead `#if 0`. Small improvements compound.

---

## 8. Module Boundaries

- **Keep internals `static`:** Anything not in the header stays in the `.c` file. No visibility by accident.
- **One module, one responsibility:** The buffer pool doesn't know about SQL. The WAL doesn't parse pages. Keep concerns separate.
- **Avoid "train wrecks":** `buffer->pool->lru->head->next` couples you to three levels of internals. Instead, `buffer_pool_get_lru_victim(pool)` encapsulates the traversal.

---

## Quick Checklist

- [ ] Functions do one thing, stay under 20 lines
- [ ] Nesting depth rarely exceeds 2; flatten with early returns/extraction
- [ ] Names reveal intent; no abbreviations that save 2 characters
- [ ] Comments explain *why*, not *what*; or don't exist
- [ ] Error returns are checked everywhere; context is logged
- [ ] Tests are fast, independent, repeatable, self-validating, and written first
- [ ] Every file touched is left cleaner than you found it