# TielDB Development Environment Setup

This guide helps you set up the development environment for TielDB.

## Platform

**Primary:** Linux (Ubuntu/Debian recommended)  
**macOS Users:** Run a Linux VM

### Why Linux?

TielDB targets Linux for several critical features:
- `O_DIRECT` for buffer pool I/O control
- `io_uring` for async I/O (Phase 5+)
- Proper sanitizer support (LeakSanitizer, TSan, ASan)
- Accurate `fsync` semantics (critical for durability testing)

macOS lacks these features or has different semantics, which would teach the wrong lessons.

---

## Setup

### 1. Linux VM (macOS users)

Create an Ubuntu VM with the tool of your choice and work inside it. Keep the repository on the VM's
own disk (ext4/xfs), not on a shared folder, so that `O_DIRECT` and `fsync` behave correctly.

### 2. Install Dependencies (Inside VM)

```bash
# Update package list
sudo apt update

# Install build tools
sudo apt install -y \
    build-essential \
    meson \
    ninja-build \
    clang-22 \
    clang-format-22 \
    clang-tidy-22 \
    llvm-22 \
    gcovr \
    git \
    libcmocka-dev \
    pkg-config

# Verify installations
clang-22 --version     # TielDB is built with clang 22 or newer only; pass --native-file clang.ini to meson setup
meson --version        # Should be 0.60+
```

---

## Project Setup

### 1. Clone Repository (in VM)

```bash
# Clone to VM filesystem (not shared folder)
cd ~
git clone <your-repo-url> tieldb
cd tieldb
```

**Important:** Keep the project on the VM's own disk (ext4/xfs), not on a macOS shared folder. Shared folders are slow and O_DIRECT fails on them.

### 2. Build

```bash
# Setup build directory (ASan + UBSan by default)
meson setup build-asan --native-file clang.ini -Db_sanitize=address,undefined

# Build
ninja -C build-asan

# Run tests
meson test -C build-asan

# Verbose test output
meson test -C build-asan -v
```

### 3. Additional Build Types

```bash
# Thread sanitizer (for concurrency testing, Phase 6+)
meson setup build-tsan --native-file clang.ini -Db_sanitize=thread

# Release build (for benchmarks only)
meson setup build-release --native-file clang.ini --buildtype=release

# Coverage build
meson setup build-cov --native-file clang.ini -Db_coverage=true
```

---

## Workflow

### Editing Code

**Option 1: Edit on macOS, build in VM**
- Use your favorite editor on macOS (VS Code, Vim, etc.)
- Code is synced to the VM through a shared folder (for editing only)
- Clone project separately in VM for building

**Option 2: Edit in VM via SSH**
- Use VS Code Remote-SSH to edit directly in VM
- Best performance, native experience

**Option 3: Edit in VM terminal**
- Use `vim`, `emacs`, or `nano` directly in the VM

### Building and Testing

**Always build and run tests inside the Linux VM:**

```bash
# Format code (before commit)
clang-format -i src/**/*.{c,h} tests/**/*.{c,h}

# Build
ninja -C build-asan

# Run tests
meson test -C build-asan

# Run specific test
meson test -C build-asan io

# Run tests with verbose output
meson test -C build-asan -v

# Run with Valgrind (periodic check)
valgrind --leak-check=full ./build-asan/tests/test_io
```

---

## Editor Setup

### VS Code

Install extensions:
- C/C++ (Microsoft) - IntelliSense, debugging
- Meson - Syntax highlighting for `meson.build`
- Remote-SSH (optional) - Edit files directly in VM

Add to `.vscode/settings.json`:
```json
{
  "C_Cpp.default.configurationProvider": "mesonbuild.mesonbuild",
  "C_Cpp.default.compileCommands": "${workspaceFolder}/build-asan/compile_commands.json",
  "editor.formatOnSave": true,
  "C_Cpp.clang_format_path": "/usr/bin/clang-format"
}
```

### Vim/Neovim

```vim
" Using vim-plug
Plug 'rhysd/vim-clang-format'

" Format on save
autocmd FileType c,h ClangFormatAutoEnable

" Use clangd for LSP
Plug 'neoclide/coc.nvim'
```

---

## Troubleshooting

### "cmocka not found"

```bash
sudo apt install libcmocka-dev
```

### "meson: command not found"

```bash
sudo apt install meson ninja-build
```

### Build fails with sanitizer errors

```bash
# Clean and rebuild
rm -rf build-asan
meson setup build-asan --native-file clang.ini -Db_sanitize=address,undefined
ninja -C build-asan
```

### Tests fail on macOS

Tests must run in the Linux VM, not on macOS. The I/O semantics are different.

---

## Quick Reference

```bash
# Setup (once)
meson setup build-asan --native-file clang.ini -Db_sanitize=address,undefined

# Build
ninja -C build-asan

# Test
meson test -C build-asan

# Format
clang-format -i src/**/*.{c,h}

# Lint
clang-tidy -p build-asan src/*.c

# Clean
rm -rf build-asan

# Rebuild
meson setup build-asan --native-file clang.ini -Db_sanitize=address,undefined && ninja -C build-asan
```

---

## Next Steps

1. Build the project: `ninja -C build-asan`
2. Run tests: `meson test -C build-asan`
3. Read `ROADMAP.md` for Phase 1 tasks
4. Read `CLAUDE.md` for coding guidelines

---

## Resources

- [Meson Documentation](https://mesonbuild.com/Tutorial.html)
- [cmocka Documentation](https://cmocka.org/)
- [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer)
