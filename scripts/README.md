# TielDB Scripts

Development and CI/CD scripts for TielDB.

## Git Hooks

### Installation

```bash
# From the repository root
./scripts/install-hooks.sh
```

### Pre-commit Hook

Automatically runs before each commit to ensure code quality:

- ✅ **Format check** - Verifies code is formatted with `clang-format`
- ✅ **Static analysis** - Runs `clang-tidy` on changed files (if `build-asan` exists)

**To bypass** (not recommended):
```bash
git commit --no-verify
```

**To uninstall:**
```bash
rm .git/hooks/pre-commit
```

## Manual Checks

### Format all code
```bash
clang-format -i src/**/*.{c,h} tests/**/*.{c,h}
```

### Check formatting without changes
```bash
find src tests -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror
```

### Run static analysis
```bash
clang-tidy -p build-asan src/io/*.c
```

### Run all tests
```bash
meson test -C build-asan -v
```

## CI/CD

See `.github/workflows/ci.yml` for the full CI pipeline.

**CI runs:**
- Format check
- clang-tidy static analysis
- Build + test with ASan + UBSan
- Build + test with TSan
- Build + test (release mode)

All on every push and pull request to `main`.
