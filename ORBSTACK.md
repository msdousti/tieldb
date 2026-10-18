# OrbStack Development Setup

Quick reference for running Claude Code and developing on OrbStack.

---

## SSH Connection

```bash
# From macOS terminal
ssh sadeq@192.168.139.68

# Direct access to project
cd ~/tieldb
```

---

## Claude Code on OrbStack

### Option 1: SSH from macOS Terminal

```bash
# SSH into OrbStack
ssh sadeq@192.168.139.68

# Navigate to project
cd ~/tieldb

# Run Claude Code
# (Assumes Claude Code CLI is available on OrbStack)
```

### Option 2: Remote Development

If Claude Code is running on macOS but you want to build/test on OrbStack:

```bash
# On macOS (this repo directory)
cd /Users/sadeq/tieldb

# Edit files on macOS
# Then sync and test on OrbStack:
ssh sadeq@192.168.139.68 'cd ~/tieldb && git pull && ninja -C build-asan && meson test -C build-asan'
```

---

## Quick Commands

### Build and Test

```bash
# Build
ninja -C build-asan

# Test (all)
meson test -C build-asan

# Test (verbose)
meson test -C build-asan -v

# Clean rebuild
rm -rf build-asan
meson setup build-asan -Db_sanitize=address,undefined -Dc_std=c2x
ninja -C build-asan
```

### Code Quality

```bash
# Format
find src tests -name '*.c' -o -name '*.h' | xargs clang-format-22 -i

# Lint
find src -name '*.c' -exec clang-tidy-22 -p build-asan {} \;
```

### Git Workflow

```bash
# Check status
git status

# Pull latest
git pull

# Add and commit
git add .
git commit -m "feat: description

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"

# Push
git push
```

---

## Environment Details

- **OS:** Ubuntu 26.04 (OrbStack VM)
- **Compiler:** clang-22
- **Build System:** Meson + Ninja
- **Sanitizers:** ASan + UBSan (default)
- **Test Framework:** cmocka

---

## Installed Tools

All development tools are already installed:

```bash
clang-22 --version              # LLVM 22.1.8
clang-format-22 --version       # LLVM 22.1.8
clang-tidy-22 --version         # LLVM 22.1.8
meson --version                 # Build system
ninja --version                 # Build tool
```

---

## Pre-commit Hooks

Already installed on OrbStack:

```bash
# Verify hook exists
ls -la .git/hooks/pre-commit

# Reinstall if needed
./scripts/install-hooks.sh
```

Hook automatically runs on `git commit`:
- ✅ Format check (clang-format-22)
- ✅ Static analysis (clang-tidy-22)

---

## File Sync

The OrbStack VM has its own copy of the repo:

```
macOS:     /Users/sadeq/tieldb/
OrbStack:  /home/sadeq/tieldb/
```

**To sync:**
```bash
# On OrbStack
cd ~/tieldb
git pull
```

**Important:** Don't use shared folders for the repo - OrbStack's local disk (ext4) is required for proper O_DIRECT and file system features.

---

## Current State

- **Phase:** 0 ✅ Complete
- **Tests:** 1/1 passing (5 assertions)
- **Build:** Clean (no warnings)
- **CI:** All jobs passing

**Next:** Phase 1 - Postgres Wire Protocol

See `STATE.md` for complete project state and `ROADMAP.md` for next steps.

---

## Troubleshooting

### Can't SSH?

```bash
# Check OrbStack is running
orb list

# Get VM IP
orb list | grep ubuntu
```

### Build fails?

```bash
# Clean rebuild
rm -rf build-asan
meson setup build-asan -Db_sanitize=address,undefined -Dc_std=c2x
```

### Git email wrong?

```bash
# Check
git config user.email

# Should be: 3616518+msdousti@users.noreply.github.com
# If wrong:
git config user.email "3616518+msdousti@users.noreply.github.com"
```

### Pre-commit hook not working?

```bash
# Reinstall
./scripts/install-hooks.sh

# Make executable
chmod +x .git/hooks/pre-commit
```

---

## Quick Start Verification

Run this to verify everything works:

```bash
ssh sadeq@192.168.139.68 << 'EOF'
cd ~/tieldb
echo "=== Git Status ==="
git status
echo ""
echo "=== Build ==="
ninja -C build-asan
echo ""
echo "=== Test ==="
meson test -C build-asan
echo ""
echo "=== Format Check ==="
find src tests -name '*.c' -o -name '*.h' | \
  xargs clang-format-22 --dry-run --Werror 2>&1 | \
  head -5 || echo "✅ All files formatted correctly"
echo ""
echo "=== Ready for development! ==="
EOF
```

If all steps succeed, you're ready to start Phase 1! 🚀
